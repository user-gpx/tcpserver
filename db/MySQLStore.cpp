#include "MySQLStore.h"

#include "../utils/SocketUtil.h"

#include <cctype>
#include <cstring>
#include <utility>

namespace {

bool is_safe_identifier(const std::string& value) {
    if (value.empty()) { return false; }
    for (unsigned char ch : value) {
        if (!std::isalnum(ch) && ch != '_') { return false; }
    }
    return true;
}

MYSQL_BIND string_param(const std::string& value, unsigned long& length) {
    length = static_cast<unsigned long>(value.size());
    MYSQL_BIND bind{};
    bind.buffer_type = MYSQL_TYPE_STRING;
    bind.buffer = const_cast<char*>(value.data());
    bind.buffer_length = length;
    bind.length = &length;
    return bind;
}

MYSQL_BIND int64_param(long long& value) {
    MYSQL_BIND bind{};
    bind.buffer_type = MYSQL_TYPE_LONGLONG;
    bind.buffer = &value;
    bind.is_unsigned = 0;
    return bind;
}

MYSQL_BIND uint64_param(std::uint64_t& value) {
    MYSQL_BIND bind{};
    bind.buffer_type = MYSQL_TYPE_LONGLONG;
    bind.buffer = &value;
    bind.is_unsigned = 1;
    return bind;
}

} // namespace

MySQLStore& MySQLStore::instance() {
    static MySQLStore store;
    return store;
}

void MySQLStore::configure(const Config& config) {
    instance().applyConfig(config);
}

MySQLStore::~MySQLStore() {
    std::lock_guard<std::mutex> lock(mutex);
    while (!idleConnections.empty()) { idleConnections.pop(); }
    for (MYSQL* conn : connections) {
        if (conn != nullptr) { mysql_close(conn); }
    }
    connections.clear();
    ready = false;
}

MySQLStore::Lease::Lease(MySQLStore& store) : store(store), conn(store.acquire()) {}

MySQLStore::Lease::~Lease() {
    if (conn != nullptr) { store.release(conn); }
}

void MySQLStore::applyConfig(const Config& config) {
    std::lock_guard<std::mutex> lock(mutex);
    if (ready) { return; }
    host = config.dbHost.empty() ? "127.0.0.1" : config.dbHost;
    port = config.dbPort <= 0 ? 3306 : static_cast<unsigned int>(config.dbPort);
    user = config.dbUser.empty() ? "tcpserver" : config.dbUser;
    password = config.dbPassword;
    database = is_safe_identifier(config.dbName) ? config.dbName : "tcpserver";
}

bool MySQLStore::ensureReady() {
    std::lock_guard<std::mutex> lock(mutex);
    if (ready) { return true; }
    return initPoolLocked();
}

bool MySQLStore::initPoolLocked() {
    MYSQL* setupConn = openConnection(nullptr);
    if (setupConn == nullptr) { return false; }

    bool schemaOk = initializeSchema(setupConn);
    mysql_close(setupConn);
    if (!schemaOk) { return false; }

    for (unsigned int i = 0; i < poolSize; ++i) {
        MYSQL* conn = openConnection(database.c_str());
        if (conn == nullptr) {
            for (MYSQL* existing : connections) { mysql_close(existing); }
            connections.clear();
            while (!idleConnections.empty()) { idleConnections.pop(); }
            return false;
        }
        connections.push_back(conn);
        idleConnections.push(conn);
    }

    ready = true;
    cv.notify_all();
    LOG_INFO("mysql pool ready host=%s port=%u database=%s size=%u", host.c_str(), port,
             database.c_str(), poolSize);
    return true;
}

MYSQL* MySQLStore::openConnection(const char* selectedDatabase) {
    MYSQL* conn = mysql_init(nullptr);
    if (conn == nullptr) {
        LOG_ERROR("mysql_init failed");
        return nullptr;
    }

    const char* db = selectedDatabase == nullptr ? nullptr : selectedDatabase;
    if (mysql_real_connect(conn, host.c_str(), user.c_str(), password.c_str(), db, port, nullptr,
                           CLIENT_MULTI_STATEMENTS) == nullptr) {
        LOG_ERROR("mysql connect failed: %s", mysql_error(conn));
        mysql_close(conn);
        return nullptr;
    }
    return conn;
}

bool MySQLStore::initializeSchema(MYSQL* setupConn) {
    if (!exec(setupConn, "CREATE DATABASE IF NOT EXISTS `" + database +
                         "` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci")) {
        return false;
    }

    if (mysql_select_db(setupConn, database.c_str()) != 0) {
        LOG_ERROR("mysql_select_db failed: %s", mysql_error(setupConn));
        return false;
    }

    if (!exec(setupConn, "CREATE TABLE IF NOT EXISTS users ("
                         "id BIGINT PRIMARY KEY AUTO_INCREMENT,"
                         "username VARCHAR(64) NOT NULL UNIQUE,"
                         "password_hash VARCHAR(255) NOT NULL,"
                         "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
                         ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4")) {
        return false;
    }

    return exec(setupConn, "CREATE TABLE IF NOT EXISTS user_files ("
                          "id BIGINT PRIMARY KEY AUTO_INCREMENT,"
                          "user_id BIGINT NOT NULL,"
                          "original_name VARCHAR(255) NOT NULL,"
                          "stored_name VARCHAR(255) NOT NULL,"
                          "stored_path VARCHAR(512) NOT NULL,"
                          "size BIGINT UNSIGNED NOT NULL DEFAULT 0,"
                          "content_type VARCHAR(128) NOT NULL DEFAULT '',"
                          "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
                          "FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,"
                          "UNIQUE KEY uk_user_file (user_id, stored_name),"
                          "INDEX idx_user_files_user_id (user_id)"
                          ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
}

MYSQL* MySQLStore::acquire() {
    if (!ensureReady()) { return nullptr; }

    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [this] { return !idleConnections.empty(); });
    MYSQL* conn = idleConnections.front();
    idleConnections.pop();
    return conn;
}

void MySQLStore::release(MYSQL* conn) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        idleConnections.push(conn);
    }
    cv.notify_one();
}

bool MySQLStore::exec(MYSQL* conn, const std::string& sql) {
    if (mysql_query(conn, sql.c_str()) != 0) {
        LOG_ERROR("mysql query failed: %s", mysql_error(conn));
        return false;
    }

    do {
        MYSQL_RES* result = mysql_store_result(conn);
        if (result != nullptr) { mysql_free_result(result); }
    } while (mysql_next_result(conn) == 0);
    return true;
}

bool MySQLStore::prepareAndExecute(MYSQL* conn, MYSQL_STMT*& stmt, const char* sql,
                                   MYSQL_BIND* params, unsigned long paramCount) {
    stmt = mysql_stmt_init(conn);
    if (stmt == nullptr) {
        LOG_ERROR("mysql_stmt_init failed");
        return false;
    }
    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
        LOG_ERROR("mysql_stmt_prepare failed: %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        stmt = nullptr;
        return false;
    }
    if (paramCount > 0 && mysql_stmt_bind_param(stmt, params) != 0) {
        LOG_ERROR("mysql_stmt_bind_param failed: %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        stmt = nullptr;
        return false;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        LOG_ERROR("mysql_stmt_execute failed: %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        stmt = nullptr;
        return false;
    }
    return true;
}

bool MySQLStore::findUser(const std::string& username, UserRecord& user) {
    Lease lease(*this);
    if (lease.conn == nullptr) { return false; }

    unsigned long usernameLength = 0;
    MYSQL_BIND params[1]{};
    params[0] = string_param(username, usernameLength);

    MYSQL_STMT* stmt = nullptr;
    if (!prepareAndExecute(lease.conn, stmt,
                           "SELECT id, username, password_hash FROM users WHERE username=?",
                           params, 1)) {
        return false;
    }

    long long id = 0;
    char usernameBuf[65]{};
    char hashBuf[256]{};
    unsigned long usernameOutLength = 0;
    unsigned long hashOutLength = 0;
    bool isNull[3]{};

    MYSQL_BIND result[3]{};
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &id;
    result[0].is_null = &isNull[0];
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = usernameBuf;
    result[1].buffer_length = sizeof(usernameBuf);
    result[1].length = &usernameOutLength;
    result[1].is_null = &isNull[1];
    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = hashBuf;
    result[2].buffer_length = sizeof(hashBuf);
    result[2].length = &hashOutLength;
    result[2].is_null = &isNull[2];

    bool found = false;
    if (mysql_stmt_bind_result(stmt, result) == 0 && mysql_stmt_fetch(stmt) == 0) {
        user.id = id;
        user.username.assign(usernameBuf, usernameOutLength);
        user.passwordHash.assign(hashBuf, hashOutLength);
        found = true;
    }

    mysql_stmt_close(stmt);
    return found;
}

bool MySQLStore::createUser(const std::string& username, const std::string& passwordHash) {
    Lease lease(*this);
    if (lease.conn == nullptr) { return false; }

    unsigned long usernameLength = 0;
    unsigned long hashLength = 0;
    MYSQL_BIND params[2]{};
    params[0] = string_param(username, usernameLength);
    params[1] = string_param(passwordHash, hashLength);

    MYSQL_STMT* stmt = nullptr;
    bool ok = prepareAndExecute(lease.conn, stmt,
                                "INSERT INTO users (username, password_hash) VALUES (?, ?)",
                                params, 2);
    if (stmt != nullptr) { mysql_stmt_close(stmt); }
    return ok;
}

bool MySQLStore::addFile(long long userId, const FileRecord& file) {
    Lease lease(*this);
    if (lease.conn == nullptr) { return false; }

    long long uid = userId;
    std::uint64_t size = file.size;
    unsigned long originalLength = 0;
    unsigned long storedNameLength = 0;
    unsigned long storedPathLength = 0;
    unsigned long contentTypeLength = 0;
    MYSQL_BIND params[6]{};
    params[0] = int64_param(uid);
    params[1] = string_param(file.originalName, originalLength);
    params[2] = string_param(file.storedName, storedNameLength);
    params[3] = string_param(file.storedPath, storedPathLength);
    params[4] = uint64_param(size);
    params[5] = string_param(file.contentType, contentTypeLength);

    MYSQL_STMT* stmt = nullptr;
    bool ok = prepareAndExecute(
        lease.conn, stmt,
        "INSERT INTO user_files (user_id, original_name, stored_name, stored_path, size, "
        "content_type) VALUES (?, ?, ?, ?, ?, ?)",
        params, 6);
    if (stmt != nullptr) { mysql_stmt_close(stmt); }
    return ok;
}

std::vector<FileRecord> MySQLStore::listFiles(long long userId) {
    std::vector<FileRecord> files;
    Lease lease(*this);
    if (lease.conn == nullptr) { return files; }

    long long uid = userId;
    MYSQL_BIND params[1]{};
    params[0] = int64_param(uid);

    MYSQL_STMT* stmt = nullptr;
    if (!prepareAndExecute(
            lease.conn, stmt,
            "SELECT original_name, stored_name, stored_path, size, content_type FROM user_files "
            "WHERE user_id=? ORDER BY created_at DESC, id DESC",
            params, 1)) {
        return files;
    }

    char originalBuf[256]{};
    char storedNameBuf[256]{};
    char storedPathBuf[513]{};
    char contentTypeBuf[129]{};
    std::uint64_t size = 0;
    unsigned long lengths[5]{};
    bool isNull[5]{};

    MYSQL_BIND result[5]{};
    result[0].buffer_type = MYSQL_TYPE_STRING;
    result[0].buffer = originalBuf;
    result[0].buffer_length = sizeof(originalBuf);
    result[0].length = &lengths[0];
    result[0].is_null = &isNull[0];
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = storedNameBuf;
    result[1].buffer_length = sizeof(storedNameBuf);
    result[1].length = &lengths[1];
    result[1].is_null = &isNull[1];
    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = storedPathBuf;
    result[2].buffer_length = sizeof(storedPathBuf);
    result[2].length = &lengths[2];
    result[2].is_null = &isNull[2];
    result[3].buffer_type = MYSQL_TYPE_LONGLONG;
    result[3].buffer = &size;
    result[3].is_unsigned = 1;
    result[3].is_null = &isNull[3];
    result[4].buffer_type = MYSQL_TYPE_STRING;
    result[4].buffer = contentTypeBuf;
    result[4].buffer_length = sizeof(contentTypeBuf);
    result[4].length = &lengths[4];
    result[4].is_null = &isNull[4];

    if (mysql_stmt_bind_result(stmt, result) == 0) {
        while (mysql_stmt_fetch(stmt) == 0) {
            FileRecord file;
            file.originalName.assign(originalBuf, lengths[0]);
            file.storedName.assign(storedNameBuf, lengths[1]);
            file.storedPath.assign(storedPathBuf, lengths[2]);
            file.size = size;
            file.contentType.assign(contentTypeBuf, lengths[4]);
            files.push_back(std::move(file));
        }
    }

    mysql_stmt_close(stmt);
    return files;
}

bool MySQLStore::findFile(long long userId, const std::string& storedName, FileRecord& file) {
    Lease lease(*this);
    if (lease.conn == nullptr) { return false; }

    long long uid = userId;
    unsigned long storedNameLength = 0;
    MYSQL_BIND params[2]{};
    params[0] = int64_param(uid);
    params[1] = string_param(storedName, storedNameLength);

    MYSQL_STMT* stmt = nullptr;
    if (!prepareAndExecute(
            lease.conn, stmt,
            "SELECT original_name, stored_name, stored_path, size, content_type FROM user_files "
            "WHERE user_id=? AND stored_name=?",
            params, 2)) {
        return false;
    }

    char originalBuf[256]{};
    char storedNameBuf[256]{};
    char storedPathBuf[513]{};
    char contentTypeBuf[129]{};
    std::uint64_t size = 0;
    unsigned long lengths[5]{};
    bool isNull[5]{};

    MYSQL_BIND result[5]{};
    result[0].buffer_type = MYSQL_TYPE_STRING;
    result[0].buffer = originalBuf;
    result[0].buffer_length = sizeof(originalBuf);
    result[0].length = &lengths[0];
    result[0].is_null = &isNull[0];
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = storedNameBuf;
    result[1].buffer_length = sizeof(storedNameBuf);
    result[1].length = &lengths[1];
    result[1].is_null = &isNull[1];
    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = storedPathBuf;
    result[2].buffer_length = sizeof(storedPathBuf);
    result[2].length = &lengths[2];
    result[2].is_null = &isNull[2];
    result[3].buffer_type = MYSQL_TYPE_LONGLONG;
    result[3].buffer = &size;
    result[3].is_unsigned = 1;
    result[3].is_null = &isNull[3];
    result[4].buffer_type = MYSQL_TYPE_STRING;
    result[4].buffer = contentTypeBuf;
    result[4].buffer_length = sizeof(contentTypeBuf);
    result[4].length = &lengths[4];
    result[4].is_null = &isNull[4];

    bool found = false;
    if (mysql_stmt_bind_result(stmt, result) == 0 && mysql_stmt_fetch(stmt) == 0) {
        file.originalName.assign(originalBuf, lengths[0]);
        file.storedName.assign(storedNameBuf, lengths[1]);
        file.storedPath.assign(storedPathBuf, lengths[2]);
        file.size = size;
        file.contentType.assign(contentTypeBuf, lengths[4]);
        found = true;
    }

    mysql_stmt_close(stmt);
    return found;
}
