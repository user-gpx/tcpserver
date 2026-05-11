#pragma once

#include <array>
#include <atomic>
#include <cstdarg>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

enum class LogLevel {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
};

class Logger {
  public:
    static Logger& instance();
    static Logger& server();
    static Logger& client();

    bool init(const std::string& filePath);
    bool initByLevel(const std::string& debugPath, const std::string& infoPath,
                     const std::string& warnPath, const std::string& errorPath);
    void setLevel(LogLevel level);
    void setConsoleLevel(LogLevel level);
    void setAsync(bool enabled);
    void setEnabled(bool enabled);
    void logf(LogLevel level, const char* file, int line, const char* func, const char* fmt, ...);
    void logSystemError(const char* msg, int errnum, const char* file, int line, const char* func);

  private:
    struct PendingLog {
        LogLevel level;
        bool writeConsole;
        std::string line;
    };

    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void logv(LogLevel level, const char* file, int line, const char* func, const char* fmt,
              va_list args);
    bool shouldLog(LogLevel level) const;
    std::string makeTimestamp() const;
    const char* levelName(LogLevel level) const;
    bool ensureParentDir(const std::string& filePath);
    bool openFile(FILE*& handle, const std::string& filePath);
    void closeFilesLocked();
    void ensureWorkerStarted();
    void stopWorker();
    void workerLoop();
    void writeBatch(const std::vector<PendingLog>& batch);
    size_t levelIndex(LogLevel level) const;

    mutable std::mutex stateMutex;
    std::mutex queueMutex;
    std::condition_variable queueCv;
    FILE* fileHandle{nullptr};
    std::array<FILE*, 4> levelFileHandles{{nullptr, nullptr, nullptr, nullptr}};
    std::atomic<LogLevel> minLevel{LogLevel::Info};
    std::atomic<LogLevel> consoleMinLevel{LogLevel::Info};
    std::atomic<bool> asyncMode{true};//默认异步日志
    std::atomic<bool> enabled{true};//是否启用日志
    std::string filePath;//日志文件路径
    std::array<std::string, 4> levelFilePaths;
    std::queue<PendingLog> pendingLogs;//待写入的日志队列
    std::thread workerThread;//日志写入线程，只有一个，负责异步写日志
    bool stopRequested{false};//
};
