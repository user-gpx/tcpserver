#pragma once
#include "../config.h"
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <mysql/mysql.h>
#include <queue>
#include <string>
#include <vector>
struct UserRecord {
    long long id{0};          // 用户id
    std::string username;     // 用户名
    std::string passwordHash; // 密码哈希
};
struct FileRecord {
    std::string originalName; // 原始文件名
    std::string storedName;   // 实际保存名
    std::string storedPath;   // 保存路径
    std::string contentType;  // 文件类型
    std::uint64_t size{0};    // 文件大小
};
class MySQLStore {
  public:
    static MySQLStore& instance();                                // 获取单例对象
    static void configure(const Config& config);                  // 设置数据库配置
    bool ensureReady();                                           // 确保连接池已初始化
    bool findUser(const std::string& username, UserRecord& user); // 按用户名查找用户
    bool createUser(const std::string& username, const std::string& passwordHash); // 创建用户
    bool addFile(long long userId, const FileRecord& file);                        // 添加文件记录
    std::vector<FileRecord> listFiles(long long userId); // 查询用户文件列表
    bool findFile(long long userId, const std::string& storedName,
                  FileRecord& file); // 查找指定文件
  private:
    struct Lease { // 数据库连接的RAII包装类
        MySQLStore& store;
        MYSQL* conn{nullptr};
        explicit Lease(MySQLStore& store); // 构造时获取连接
        ~Lease();                          // 析构时归还连接
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
    };
    MySQLStore() = default; // 私有构造，配合单例
    ~MySQLStore();          // 析构时关闭连接
    MySQLStore(const MySQLStore&) = delete;
    MySQLStore& operator=(const MySQLStore&) = delete;
    void applyConfig(const Config& config);              // 应用配置
    bool initPoolLocked();                               // 初始化连接池
    MYSQL* openConnection(const char* selectedDatabase); // 创建数据库连接
    bool initializeSchema(MYSQL* setupConn);             // 初始化表结构
    MYSQL* acquire();                                    // 获取空闲连接
    void release(MYSQL* conn);                           // 归还连接
    bool exec(MYSQL* conn, const std::string& sql);      // 执行普通SQL
    bool prepareAndExecute(MYSQL* conn, MYSQL_STMT*& stmt, const char* sql, MYSQL_BIND* params,
                           unsigned long paramCount); // 执行预处理SQL
    std::mutex mutex;                                 // 保护连接池
    std::condition_variable cv;                       // 无空闲连接时等待
    std::vector<MYSQL*> connections;                  // 所有连接
    std::queue<MYSQL*> idleConnections;               // 空闲连接
    bool ready{false};                                // 是否初始化完成
    std::string host{"127.0.0.1"};
    unsigned int port{3306};
    std::string user{"tcpserver"};
    std::string password{"123456"};
    std::string database{"tcpserver"};
    unsigned int poolSize{8}; // 数据库连接池大小
};