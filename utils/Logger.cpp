#include "Logger.h"

#include <chrono>
#include <cstring>
#include <thread>
#include <utility>

#include <errno.h>
#include <sys/stat.h>

namespace {

constexpr size_t kLogBufferSize = 2048;

} // namespace

Logger& Logger::instance() {
    return server();
}

Logger& Logger::server() {
    static Logger logger;
    return logger;
}

Logger& Logger::client() {
    static Logger logger;
    return logger;
}

bool Logger::init(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(stateMutex);
    ensureWorkerStarted();
    closeFilesLocked();
    this->filePath.clear();
    levelFilePaths.fill("");
    if (filePath.empty()) { return true; }
    if (!openFile(fileHandle, filePath)) { return false; }
    this->filePath = filePath;
    return true;
}

bool Logger::initByLevel(const std::string& debugPath, const std::string& infoPath,
                         const std::string& warnPath, const std::string& errorPath) {
    std::lock_guard<std::mutex> lock(stateMutex);
    ensureWorkerStarted();
    closeFilesLocked();
    filePath.clear();
    levelFilePaths = {debugPath, infoPath, warnPath, errorPath};

    for (size_t i = 0; i < levelFilePaths.size(); ++i) {
        const std::string& path = levelFilePaths[i];
        if (path.empty()) { continue; }
        if (!openFile(levelFileHandles[i], path)) {
            closeFilesLocked();
            levelFilePaths.fill("");
            return false;
        }
    }
    return true;
}

void Logger::setLevel(LogLevel level) {
    minLevel.store(level, std::memory_order_relaxed);
}

void Logger::setConsoleLevel(LogLevel level) {
    consoleMinLevel.store(level, std::memory_order_relaxed);
}

void Logger::logf(LogLevel level, const char* file, int line, const char* func, const char* fmt,
                  ...) {
    if (!shouldLog(level)) { return; }

    va_list args;
    va_start(args, fmt);
    logv(level, file, line, func, fmt, args);
    va_end(args);
}

void Logger::logSystemError(const char* msg, int errnum, const char* file, int line,
                            const char* func) {
    if (!shouldLog(LogLevel::Error)) { return; }
    logf(LogLevel::Error, file, line, func, "%s: %s", msg, std::strerror(errnum));
}

Logger::~Logger() {
    stopWorker();
    std::lock_guard<std::mutex> lock(stateMutex);
    closeFilesLocked();
}

void Logger::logv(LogLevel level, const char* file, int line, const char* func, const char* fmt,
                  va_list args) {
    char message[kLogBufferSize];
    vsnprintf(message, sizeof(message), fmt, args);

    auto tid = static_cast<unsigned long long>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
    std::string prefix = "[" + makeTimestamp() + "] [" + levelName(level) + "] [tid=" +
                         std::to_string(tid) + "] " + file + ":" + std::to_string(line) + " " +
                         func + "(): ";
    PendingLog entry{level,
                     static_cast<int>(level) >=
                         static_cast<int>(consoleMinLevel.load(std::memory_order_relaxed)),
                     prefix + message + "\n"};

    ensureWorkerStarted();
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        pendingLogs.push(std::move(entry));
    }
    queueCv.notify_one();
}

bool Logger::shouldLog(LogLevel level) const {
    return static_cast<int>(level) >=
           static_cast<int>(minLevel.load(std::memory_order_relaxed));
}

std::string Logger::makeTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t tt = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &tt);
#else
    localtime_r(&tt, &localTime);
#endif

    char timePart[32];
    strftime(timePart, sizeof(timePart), "%Y-%m-%d %H:%M:%S", &localTime);

    char full[40];
    snprintf(full, sizeof(full), "%s.%03lld", timePart, static_cast<long long>(ms.count()));
    return full;
}

const char* Logger::levelName(LogLevel level) const {
    switch (level) {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}

bool Logger::ensureParentDir(const std::string& filePath) {
    size_t pos = filePath.rfind('/');
    if (pos == std::string::npos) { return true; }

    std::string path;
    std::string dir = filePath.substr(0, pos);
    for (size_t i = 0; i < dir.size(); ++i) {
        path.push_back(dir[i]);
        if (dir[i] != '/' && i + 1 != dir.size()) { continue; }
        if (path.empty() || path == "/") { continue; }
        if (mkdir(path.c_str(), 0755) < 0 && errno != EEXIST) { return false; }
    }
    if (!dir.empty() && mkdir(dir.c_str(), 0755) < 0 && errno != EEXIST) { return false; }
    return true;
}

bool Logger::openFile(FILE*& handle, const std::string& filePath) {
    if (!ensureParentDir(filePath)) { return false; }
    handle = fopen(filePath.c_str(), "a");
    return handle != nullptr;
}

void Logger::closeFilesLocked() {
    if (fileHandle != nullptr) {
        fclose(fileHandle);
        fileHandle = nullptr;
    }
    for (FILE*& handle : levelFileHandles) {
        if (handle != nullptr) {
            fclose(handle);
            handle = nullptr;
        }
    }
}

void Logger::ensureWorkerStarted() {
    std::lock_guard<std::mutex> lock(queueMutex);
    if (workerThread.joinable()) { return; }
    stopRequested = false;
    workerThread = std::thread(&Logger::workerLoop, this);
}

void Logger::stopWorker() {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        stopRequested = true;
    }
    queueCv.notify_all();
    if (workerThread.joinable()) { workerThread.join(); }
}

void Logger::workerLoop() {
    std::vector<PendingLog> batch;
    while (true) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCv.wait(lock, [this] { return stopRequested || !pendingLogs.empty(); });
            if (stopRequested && pendingLogs.empty()) { break; }

            batch.clear();
            batch.reserve(pendingLogs.size());
            while (!pendingLogs.empty()) {
                batch.push_back(std::move(pendingLogs.front()));
                pendingLogs.pop();
            }
        }

        writeBatch(batch);
    }
}

void Logger::writeBatch(const std::vector<PendingLog>& batch) {
    std::lock_guard<std::mutex> lock(stateMutex);

    bool wroteStderr = false;
    bool wroteSharedFile = false;
    std::array<bool, 4> touchedLevelFiles{{false, false, false, false}};

    for (const PendingLog& entry : batch) {
        if (entry.writeConsole) {
            fwrite(entry.line.data(), 1, entry.line.size(), stderr);
            wroteStderr = true;
        }

        size_t index = levelIndex(entry.level);
        FILE* target = levelFileHandles[index];
        if (target != nullptr) {
            fwrite(entry.line.data(), 1, entry.line.size(), target);
            touchedLevelFiles[index] = true;
        } else if (fileHandle != nullptr) {
            fwrite(entry.line.data(), 1, entry.line.size(), fileHandle);
            wroteSharedFile = true;
        }
    }

    if (wroteStderr) { fflush(stderr); }
    if (wroteSharedFile) { fflush(fileHandle); }
    for (size_t i = 0; i < levelFileHandles.size(); ++i) {
        if (touchedLevelFiles[i]) { fflush(levelFileHandles[i]); }
    }
}

size_t Logger::levelIndex(LogLevel level) const { return static_cast<size_t>(level); }
