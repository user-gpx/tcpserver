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

void Logger::setAsync(bool enabled) {
    asyncMode.store(enabled, std::memory_order_relaxed);
}

void Logger::setEnabled(bool enabled) {
    this->enabled.store(enabled, std::memory_order_relaxed);
}

void Logger::logf(LogLevel level, const char* file, int line, const char* func, const char* fmt,
                  ...) {
    if (!shouldLog(level)) { return; }

    va_list args;                             // 可变参数列表
    va_start(args, fmt);                      // 初始化可变参数，从fmt后面开始访问。
    logv(level, file, line, func, fmt, args); // 解析可变参数并记录日志
    va_end(args);                             // 清理可变参数列表
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
    vsnprintf(message, sizeof(message), fmt,
              args); // 将可变参数格式化为字符串，存储在message缓冲区中。

    auto tid = static_cast<unsigned long long>(std::hash<std::thread::id>{}(
        std::this_thread::get_id())); // 获取当前线程ID并转换为无符号长长整数，以便在日志中使用。
    std::string prefix = "[" + makeTimestamp() + "] [" + levelName(level) +
                         "] [tid=" + std::to_string(tid) + "] " + file + ":" +
                         std::to_string(line) + " " + func + "(): ";
    PendingLog entry{level,
                     static_cast<int>(level) >=
                         static_cast<int>(consoleMinLevel.load(std::memory_order_relaxed)),
                     prefix + message + "\n"};

    if (asyncMode.load(
            std::
                memory_order_relaxed)) { // 是异步日志模式，将日志条目添加到待处理队列并通知工作线程。
        ensureWorkerStarted();
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            pendingLogs.push(std::move(entry)); // 压入待处理日志队列
        }
        queueCv.notify_one(); // 通知工作线程有新的日志条目需要处理
    } else {
        std::vector<PendingLog> single; // 同步日志模式，直接写入日志，不使用工作线程。
        single.push_back(std::move(
            entry)); // 虽然只有一个日志条目，但仍然使用writeBatch函数来写入日志，以保持代码的一致性。
        writeBatch(single); // 直接写入日志
    }
}

bool Logger::shouldLog(LogLevel level) const { // 检查是否应该记录给定级别的日志。
    if (!enabled.load(std::memory_order_relaxed)) { return false; }
    return static_cast<int>(level) >= static_cast<int>(minLevel.load(std::memory_order_relaxed));
}

std::string Logger::makeTimestamp()
    const { // 生成当前时间的时间戳字符串，格式为"YYYY-MM-DD HH:MM:SS.mmm"，其中mmm是毫秒部分。
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t tt = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
#ifdef _WIN32 // Windows平台使用localtime_s函数将时间转换为本地时间。
    localtime_s(&localTime, &tt);
#else
    localtime_r(&tt, &localTime); // 类Unix平台使用localtime_r函数将时间转换为本地时间。
#endif

    char timePart[32];
    strftime(timePart, sizeof(timePart), "%Y-%m-%d %H:%M:%S", &localTime);

    char full[40];
    snprintf(full, sizeof(full), "%s.%03lld", timePart, static_cast<long long>(ms.count()));
    return full;
}

const char* Logger::levelName(LogLevel level) const {
    switch (level) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Error: return "ERROR";
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
    workerThread = std::thread(&Logger::workerLoop, this); // 启动日志
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
    // 日志写入线程的主循环，负责从待处理日志队列中取出日志条目并写入日志文件或控制台。
    while (true) {
        std::queue<PendingLog> logsToProcess;
        { // 锁范围
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCv.wait(lock, [this] { return stopRequested || !pendingLogs.empty(); });
            if (stopRequested && pendingLogs.empty()) break;
            logsToProcess.swap(pendingLogs); // O(1) 交换，锁内操作最短
        } // 释放锁
        // 锁外生成 vector 批量写
        std::vector<PendingLog> batch;
        batch.reserve(logsToProcess.size());
        while (!logsToProcess.empty()) {
            batch.push_back(std::move(logsToProcess.front()));
            logsToProcess.pop();
        }
        writeBatch(batch); // 批量写日志
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

size_t Logger::levelIndex(LogLevel level) const {
    return static_cast<size_t>(level);
}
