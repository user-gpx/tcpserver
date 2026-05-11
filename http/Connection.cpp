#include "Connection.h"

#include "../db/MySQLStore.h"
#include "../db/PasswordHash.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <dirent.h>
#include <random>
#include <sstream>
#include <sys/stat.h>
#include <unordered_map>
#include <utility>

namespace {
std::mutex g_authMutex;
struct SessionInfo {
    long long userId{0};
    std::string username;
};

std::unordered_map<std::string, SessionInfo> g_sessions;
// 在内存中保存session：
// 键是session id，值是SessionInfo结构体，包含用户id和用户名。

int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') { return ch - '0'; }
    if (ch >= 'a' && ch <= 'f') { return ch - 'a' + 10; }
    if (ch >= 'A' && ch <= 'F') { return ch - 'A' + 10; }
    return -1;
}

std::string url_decode(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            out.push_back(' ');
        } else if (value[i] == '%' && i + 2 < value.size()) {
            int hi = hex_value(value[i + 1]);
            int lo = hex_value(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            } else {
                out.push_back(value[i]);
            }
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

std::string url_encode(const std::string& value) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.') {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('%');
            out.push_back(hex[ch >> 4]);
            out.push_back(hex[ch & 0x0f]);
        }
    }
    return out;
}

std::unordered_map<std::string, std::string> parse_form(const std::string& body) {
    std::unordered_map<std::string, std::string> fields;
    size_t start = 0;
    while (start <= body.size()) {
        size_t end = body.find('&', start);
        std::string pair =
            body.substr(start, end == std::string::npos ? std::string::npos : end - start);
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            fields[url_decode(pair.substr(0, eq))] = url_decode(pair.substr(eq + 1));
        }
        if (end == std::string::npos) { break; }
        start = end + 1;
    }
    return fields;
}

std::unordered_map<std::string, std::string> parse_query(const std::string& target) {
    size_t pos = target.find('?');
    if (pos == std::string::npos || pos + 1 >= target.size()) { return {}; }
    return parse_form(target.substr(pos + 1));
}

std::string request_path(const std::string& target) {
    size_t pos = target.find('?');
    return pos == std::string::npos ? target : target.substr(0, pos);
}

std::string get_cookie_value(const HttpRequest& req, const std::string& name) {
    // 从请求头中的cookie中获取指定name的值
    auto it = req.headers.find("cookie");
    if (it == req.headers.end()) { return ""; }
    std::string cookie = it->second;
    size_t start = 0;
    while (start < cookie.size()) {
        while (start < cookie.size() && (cookie[start] == ' ' || cookie[start] == ';')) { ++start; }
        size_t end = cookie.find(';', start);
        std::string item =
            cookie.substr(start, end == std::string::npos ? std::string::npos : end - start);
        size_t eq = item.find('=');
        if (eq != std::string::npos && item.substr(0, eq) == name) { return item.substr(eq + 1); }
        if (end == std::string::npos) { break; }
        start = end + 1;
    }
    return "";
}

SessionInfo current_session(const HttpRequest& req) { // 获取当前请求（当前连接）的session信息
    std::string sid = get_cookie_value(
        req,
        "SID"); // 从请求头中的cookie中获取SID，找到对应的session信息，返回用户id和用户名。如果没有SID或者SID无效，就返回空的SessionInfo。
    if (sid.empty()) { return {}; }
    std::lock_guard<std::mutex> lock(g_authMutex);
    auto it = g_sessions.find(
        sid); // 从全局session表中查找SID对应的session信息，使用互斥锁保护访问，防止多线程同时修改session表导致数据不一致。
    return it == g_sessions.end() ? SessionInfo{} : it->second;
}

std::string current_user(const HttpRequest& req) {
    return current_session(req)
        .username; // 获取当前请求的用户名，如果没有登录就返回空字符串。通过调用current_session函数获取session信息，再从session信息中获取用户名。
}

std::string make_session_id() {
    // 生成session
    // id，使用随机数生成器生成一个随机的64位整数，并转换成16进制字符串。使用thread_local保证每个线程有自己的随机数生成器，避免多线程竞争导致性能下降。
    static thread_local std::mt19937_64 rng(std::random_device{}());
    std::ostringstream oss;
    oss << std::hex << rng() << rng();
    return oss.str();
}

std::string json_escape(const std::string& value) {
    std::string out;
    for (char ch : value) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += ch; break;
        }
    }
    return out;
}

std::string base_name(std::string filename) {
    size_t pos = filename.find_last_of("/\\");
    if (pos != std::string::npos) { filename = filename.substr(pos + 1); }
    return filename;
}

std::string sanitize_filename(const std::string& filename) {
    std::string clean;
    for (unsigned char ch : base_name(filename)) {
        if (std::isalnum(ch) || ch == '.' || ch == '-' || ch == '_') {
            clean.push_back(static_cast<char>(ch));
        } else {
            clean.push_back('_');
        }
    }

    while (!clean.empty() && clean.front() == '.') { clean.erase(clean.begin()); }
    if (clean.empty()) { clean = "upload"; }
    return clean;
}

std::string timestamp_suffix() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() %
        1000;
    std::tm tm{};
    localtime_r(&t, &tm);

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    char full[40];
    snprintf(full, sizeof(full), "%s_%03lld", buf, static_cast<long long>(ms));
    return full;
}

std::string make_saved_filename(const std::string& original) {
    std::string clean = sanitize_filename(original);
    size_t dot = clean.find_last_of('.');

    std::string stem = dot == std::string::npos ? clean : clean.substr(0, dot);
    std::string ext = dot == std::string::npos ? "" : clean.substr(dot);
    if (stem.empty()) { stem = "upload"; }

    return stem + "_" + timestamp_suffix() + ext;
}

std::string multipart_filename(const std::string& part_header) {
    std::string key = "filename=\"";
    size_t start = part_header.find(key);
    if (start == std::string::npos) { return "upload"; }

    start += key.size();
    size_t end = part_header.find('"', start);
    if (end == std::string::npos) { return "upload"; }

    return part_header.substr(start, end - start);
}

bool is_regular_file(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::vector<std::string> list_uploaded_images() {
    std::vector<std::string> files;
    DIR* dir = opendir("uploads");
    if (!dir) { return files; }

    while (dirent* ent = readdir(dir)) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") { continue; }

        std::string clean = sanitize_filename(name);
        if (clean != name) { continue; }

        std::string path = "uploads/" + name;
        if (is_regular_file(path)) { files.push_back(name); }
    }

    closedir(dir);
    std::sort(files.begin(), files.end());
    return files;
}

std::string image_list_json(long long userId) {
    std::string body = "[";
    auto files = MySQLStore::instance().listFiles(userId);
    for (size_t i = 0; i < files.size(); ++i) {
        if (i > 0) { body += ","; }
        body += "{\"name\":\"" + json_escape(files[i].storedName) + "\",\"original_name\":\"" +
                json_escape(files[i].originalName) +
                "\",\"url\":\"/download?file=" + url_encode(files[i].storedName) + "\"}";
    }
    body += "]";
    return body;
}

std::string image_content_type(const std::string& filename) {
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".png") { return "image/png"; }
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".gif") { return "image/gif"; }
    if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".webp") { return "image/webp"; }
    return "image/jpeg";
}

std::string response_with_extra_headers(int code, const std::string& reason,
                                        const std::string& content_type, const std::string& body,
                                        bool keep_alive, const std::string& extra_headers) {
    return "HTTP/1.1 " + std::to_string(code) + " " + reason + "\r\n" +
           "Content-Type: " + content_type + "\r\n" +
           "Content-Length: " + std::to_string(body.size()) + "\r\n" +
           "Connection: " + std::string(keep_alive ? "keep-alive" : "close") + "\r\n" +
           extra_headers + "\r\n" + body;
}

std::string redirect_response(const std::string& location, bool keep_alive,
                              const std::string& extra_headers = "") {
    return response_with_extra_headers(302, "Found", "text/plain", "", keep_alive,
                                       "Location: " + location + "\r\n" + extra_headers);
}
} // namespace

Connection::Connection(int fd, Epoller& epoller, ThreadPool& pool, IoQueueContext& ioque)
    : connfd(fd), _epoller(epoller), pool(pool), ioque(ioque), _isprocessing(false),
      _lastActive(std::chrono::steady_clock::now()), _closeAfterWrite(false) {
    baseevent = (_epoller.triggermode == TriggerMode::EdgeTrigger) ? EPOLLET : 0;
}

int Connection::handleread() {
    while (true) {
        uint8_t buf[1024];
        ssize_t n = read(connfd, buf, sizeof(buf));
        if (n > 0) {
            touch();
            inputbuffer.append(buf, n);
        } else if (n == 0) {
            return -1;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else if (errno == EINTR) {
                continue;
            } else {
                LOG_ERR("read(connfd)");
                return -1;
            }
        }
        if (!(_epoller.triggermode == TriggerMode::EdgeTrigger)) { break; }
    }
    while (true) {
        int rv = try_one_request();
        if (rv < 0) { return -1; }
        if (rv == 0) { break; }
    }
    return 0;
}

int Connection::handlewrite() {
    while (!outputbuffer.empty()) {
        ssize_t n = write(connfd, outputbuffer.data(), outputbuffer.size());
        if (n > 0) {
            touch(); // 更新活跃时间
            outputbuffer.consume(n);
        } else if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { break; }
            if (errno == EINTR) { continue; }
            LOG_ERR("write(connfd)");
            return -1;
        } else {
            break;
        }
        if (!(_epoller.triggermode == TriggerMode::EdgeTrigger)) { break; }
    }
    if (outputbuffer.empty() && _epoller.modfd(connfd, EPOLLIN | baseevent) < 0) { return -1; }
    if (outputbuffer.empty() && _closeAfterWrite) return -1;
    return 0;
}

int Connection::try_one_request() {
    HttpRequest req;
    auto ret = parser.parse(inputbuffer, req);
    if (ret == HttpParser::Result::Incomplete) { return 0; }
    if (ret == HttpParser::Result::Error) {
        std::string resp = "HTTP/1.1 400 Bad Request\r\n"
                           "Content-Length: 11\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "bad request";
        bool fl = outputbuffer.empty();
        touch();
        outputbuffer.append(reinterpret_cast<const uint8_t*>(resp.data()), resp.size());
        if (fl && _epoller.modfd(connfd, EPOLLIN | EPOLLOUT | baseevent) < 0) { return -1; }
        return 0;
    }
    _closeAfterWrite = !req.keepAlive();
    if (isfastresponse(req) || pool.getSize() == 0) {
        // 请请求或者是线程池大小为0，就直接在reactor线程处理，避免线程切换的开销。
        std::string resp = URL(req); // 获取响应
        bool fl = outputbuffer.empty();
        touch();
        outputbuffer.append(reinterpret_cast<const uint8_t*>(resp.data()), resp.size());
        if (fl && _epoller.modfd(connfd, EPOLLIN | EPOLLOUT | baseevent) < 0) { return -1; }
        // 先检查flag再修改事件，避免重复修改事件导致性能下降。
    } else {
        _pendingReqs.push(std::move(req));
        if (!_isprocessing) { processNextSlowRequest(); }
        // 只有在没有线程池处理时才调用，防止多个线程池处理一个连接。
    }
    return 1;
}
void Connection::processNextSlowRequest() {
    if (_pendingReqs.empty()) {
        _isprocessing = false; // 处理完了，设置为false，下次有请求了再设置为true并提交到线程池。
        return;
    }
    _isprocessing = true;
    HttpRequest req = std::move(_pendingReqs.front());
    _pendingReqs.pop();
    std::weak_ptr<Connection> weak_self = shared_from_this();
    // 这前面的一部分都是reactor线程处理的，真正耗时的请求处理放在线程池里执行
    // 处理完了再通过ioqueue回到reactor线程更新socket状态和发送响应。
    pool.submit([weak_self, req = std::move(req)]() mutable {
        auto self = weak_self.lock(); // 弱指针加锁获取shared_ptr，获取失败说明连接已经关闭了。
        if (!self) { return; }
        std::string resp = self->URL(req); // 线程池真正处理的请求是这个。
        auto func = [weak_self,
                     resp = std::move(resp)]() mutable { // 并不会执行，只是创建一个函数对象。
            auto self = weak_self.lock();
            if (!self) { return; }
            bool fl = self->outputbuffer.empty();
            self->touch();
            self->outputbuffer.append(reinterpret_cast<const uint8_t*>(resp.data()), resp.size());
            bool arm_ok = true;
            if (fl) { // 只有在写入前是空的才会监听可写事件，避免重复修改事件导致性能下降。
                arm_ok =
                    self->_epoller.modfd(self->connfd, EPOLLIN | EPOLLOUT | self->baseevent) == 0;
            }
            self->_isprocessing = false;
            if (!arm_ok) { return; }
            self->processNextSlowRequest();
            // 可能还有请求，继续处理下一个请求。递归调用
        };
        self->ioque.enqueue(func); // 将要执行的函数加入回调队列，由reactor线程执行
    });
}

void Connection::send_response(const std::string& resp) { // 现在并没有使用
    auto iofunc = [this, resp = std::string(resp)]() mutable {
        bool fl = outputbuffer.empty();
        touch();
        outputbuffer.append(reinterpret_cast<const uint8_t*>(resp.data()), resp.size());
        if (fl && _epoller.modfd(connfd, EPOLLIN | EPOLLOUT | baseevent) < 0) { return; }
    };
    ioque.enqueue(std::move(iofunc));
}

std::string Connection::URL(const HttpRequest& req) {
    std::string path = request_path(req.path);
    if (req.method == "GET" && (path == "/" || path == "/index.html")) {//主页，必须先登录
        std::string username = current_user(req);
        if (username.empty()) { return redirect_response("/login", req.keepAlive()); }
        //如果没有登录就重定向到登录页
        return make_home_page(req.keepAlive());
    }
    if (req.method == "GET" && path == "/login") {
        std::string body = read_file("static/login.html");
        if (body.empty()) {
            return make_response(404, "Not Found", "text/plain", "login.html not found",
                                 req.keepAlive());
        }
        return make_response(200, "OK", "text/html; charset=utf-8", body, req.keepAlive());
    }
    if (req.method == "GET" && path == "/register") {
        std::string body = read_file("static/register.html");
        if (body.empty()) {
            return make_response(404, "Not Found", "text/plain", "register.html not found",
                                 req.keepAlive());
        }
        return make_response(200, "OK", "text/html; charset=utf-8", body, req.keepAlive());
    }
    if (req.method == "POST" && path == "/register") { // 注册请求
        auto fields = parse_form(req.body);            // 解析表单数据，获取用户名和密码
        std::string username = fields["username"];
        std::string password = fields["password"];
        if (username.empty() || password.empty()) {
            return make_response(400, "Bad Request", "text/plain", "username or password empty",
                                 req.keepAlive());
        }
        UserRecord existing;
        MySQLStore& db = MySQLStore::instance(); // 获取数据库实例
        if (!db.ensureReady()) {                 // 确保数据库连接可用
            return make_response(500, "Internal Server Error", "text/plain", "database unavailable",
                                 req.keepAlive());
        }
        if (db.findUser(username, existing)) {
            return make_response(409, "Conflict", "text/plain", "username exists", req.keepAlive());
        }
        if (!db.createUser(username, makePasswordHash(password))) {
            return make_response(500, "Internal Server Error", "text/plain", "create user failed",
                                 req.keepAlive());
        }
        return redirect_response("/login", req.keepAlive()); // 注册完了，重定向到登录页
    }
    if (req.method == "POST" && path == "/login") { // 登录请求
        auto fields = parse_form(req.body);
        std::string username = fields["username"];
        std::string password = fields["password"];
        UserRecord user;
        MySQLStore& db = MySQLStore::instance();//
        if (!db.ensureReady()) {
            return make_response(500, "Internal Server Error", "text/plain", "database unavailable",
                                 req.keepAlive());
        }
        if (!db.findUser(username, user) || !verifyPassword(password, user.passwordHash)) {
            return make_response(401, "Unauthorized", "text/plain", "bad username or password",
                                 req.keepAlive());//简单回复，没有使用静态文件
        }
        std::string sid = make_session_id(); // 生成
        {
            std::lock_guard<std::mutex> lock(g_authMutex);
            g_sessions[sid] = SessionInfo{user.id, user.username};//填入
        }
        return redirect_response("/", req.keepAlive(),
                                 "Set-Cookie: SID=" + sid + "; Path=/; HttpOnly\r\n");
    }

    if (req.method == "GET" && path == "/logout") { // 登出请求
        std::string sid = get_cookie_value(req, "SID");
        if (!sid.empty()) {
            std::lock_guard<std::mutex> lock(g_authMutex);
            g_sessions.erase(sid);
        }
        return redirect_response("/login", req.keepAlive(),
                                 "Set-Cookie: SID=; Path=/; Max-Age=0; HttpOnly\r\n");
    }

    if (req.method == "POST" && path == "/upload") { return handle_upload(req); }

    if (req.method == "GET" && path == "/api/images") {
        SessionInfo session = current_session(req);
        if (session.username.empty()) { return redirect_response("/login", req.keepAlive()); }

        return make_response(200, "OK", "application/json; charset=utf-8",
                             image_list_json(session.userId), req.keepAlive());
    }

    if (req.method == "GET" && path == "/download") {
        SessionInfo session = current_session(req);
        if (session.username.empty()) { return redirect_response("/login", req.keepAlive()); }

        auto query = parse_query(req.path);
        std::string filename = sanitize_filename(query["file"]);
        if (filename.empty() || filename != query["file"]) {
            return make_response(400, "Bad Request", "text/plain", "bad filename", req.keepAlive());
        }

        FileRecord file;
        if (!MySQLStore::instance().findFile(session.userId, filename, file)) {
            return make_response(404, "Not Found", "text/plain", "image not found",
                                 req.keepAlive());
        }
        std::string body = read_file(file.storedPath);
        if (body.empty()) {
            return make_response(404, "Not Found", "text/plain", "image not found",
                                 req.keepAlive());
        }

        return response_with_extra_headers(
            200, "OK", image_content_type(file.storedName), body, req.keepAlive(),
            "Content-Disposition: attachment; filename=\"" + file.originalName + "\"\r\n");
    }
    return make_response(404, "Not Found", "text/plain", "404 not found", req.keepAlive());
}

std::string Connection::make_response(int code, const std::string& reason,
                                      const std::string& content_type, const std::string& body,
                                      bool keep_alive) {
    return "HTTP/1.1 " + std::to_string(code) + " " + reason + "\r\n" +
           "Content-Type: " + content_type + "\r\n" +
           "Content-Length: " + std::to_string(body.size()) + "\r\n" +
           "Connection: " + std::string(keep_alive ? "keep-alive" : "close") + "\r\n" + "\r\n" +
           body;
}

std::string Connection::read_file(const std::string& path) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) {
        LOG_ERR("fopen(read_file)");
        return "";
    }

    std::string data;
    char buf[4096];

    while (true) {
        size_t n = fread(buf, 1, sizeof(buf), fp);
        if (n > 0) { data.append(buf, n); }
        if (n < sizeof(buf)) {
            if (ferror(fp)) {
                LOG_ERR("fread(read_file)");
                data.clear();
            }
            break;
        }
    }

    if (fclose(fp) != 0) { LOG_ERR("fclose(read_file)"); }
    return data;
}

std::string Connection::make_home_page(bool keep_alive) {
    std::string body = read_file("static/index.html");

    if (body.empty()) {
        return make_response(404, "Not Found", "text/plain", "index.html not found", keep_alive);
    }

    return make_response(200, "OK", "text/html; charset=utf-8", body, keep_alive);
}

std::string Connection::handle_upload(const HttpRequest& req) {
    SessionInfo session = current_session(req);
    if (session.username.empty()) { return redirect_response("/login", req.keepAlive()); }

    auto it = req.headers.find("content-type");
    if (it == req.headers.end()) {
        return make_response(400, "Bad Request", "text/plain", "missing content-type",
                             req.keepAlive());
    }

    std::string content_type = it->second;

    size_t pos = content_type.find("boundary=");
    if (pos == std::string::npos) {
        return make_response(400, "Bad Request", "text/plain", "no boundary", req.keepAlive());
    }

    std::string boundary = "--" + content_type.substr(pos + 9);
    const std::string& body = req.body;

    size_t start = body.find("\r\n\r\n");
    if (start == std::string::npos) {
        return make_response(400, "Bad Request", "text/plain", "bad format", req.keepAlive());
    }
    std::string part_header = body.substr(0, start);
    start += 4;

    size_t end = body.find(boundary, start);
    if (end == std::string::npos) {
        return make_response(400, "Bad Request", "text/plain", "bad format", req.keepAlive());
    }

    if (end >= 2 && body[end - 2] == '\r' && body[end - 1] == '\n') { end -= 2; }

    std::string filedata = body.substr(start, end - start);
    std::string originalName = sanitize_filename(multipart_filename(part_header));
    std::string storedName = make_saved_filename(originalName);
    std::string filename = "uploads/" + storedName;

    FILE* fp = fopen(filename.c_str(), "wb");
    if (!fp) {
        LOG_ERR("fopen(upload_file)");
        return make_response(500, "Internal Server Error", "text/plain", "open file failed",
                             req.keepAlive());
    }

    size_t written = fwrite(filedata.data(), 1, filedata.size(), fp);
    if (written != filedata.size()) {
        LOG_ERR("fwrite(upload_file)");
        fclose(fp);
        return make_response(500, "Internal Server Error", "text/plain", "write file failed",
                             req.keepAlive());
    }

    if (fclose(fp) != 0) {
        LOG_ERR("fclose(upload_file)");
        return make_response(500, "Internal Server Error", "text/plain", "close file failed",
                             req.keepAlive());
    }

    FileRecord file;
    file.originalName = originalName;
    file.storedName = storedName;
    file.storedPath = filename;
    file.size = filedata.size();
    file.contentType = image_content_type(storedName);
    if (!MySQLStore::instance().addFile(session.userId, file)) {
        return make_response(500, "Internal Server Error", "text/plain", "save file record failed",
                             req.keepAlive());
    }

    return redirect_response("/", req.keepAlive());
}
