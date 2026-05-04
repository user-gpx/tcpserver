#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "../common.h"
#include "../utils/Buffer.h"

struct HttpRequest { // http请求协议的内容格式
    std::string method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    bool keepAlive() const { // 是否保持长连接由客户端决定
        auto it = headers.find("connection");
        if (version == "HTTP/1.1") { return it == headers.end() || it->second != "close"; }
        return it != headers.end() && it->second == "keep-alive";
    }
};

class HttpParser { // 一个链接对应一个http解析器，保存解析的结果
  public:
    enum class Result { // 解析结果
        Complete,       // 正确解析完一个请求
        Incomplete,     // 此次数据不完整
        Error           // 解析请求发生错误
    };

    // State  = 当前解析进度（内部）
    // Result = 本次解析结果（输出）
  private:
    enum class State { // 解析状态
        RequestLine,   // 正在解析请求行
        Headers,       // 请求头
        Body           // 请求体
    };

    State state = State::RequestLine;
    HttpRequest req; // 将http请求内容解析后成一个结构体
    size_t contentLength = 0;

  public:
    Result parse(Buffer& buf, HttpRequest& out) {
        while (true) {
            if (state == State::RequestLine) {
                std::string line;
                if (!readLine(buf, line)) return Result::Incomplete;
                if (!parseRequestLine(line)) return Result::Error;
                state = State::Headers;
            }

            if (state == State::Headers) {
                while (true) {
                    std::string line;
                    if (!readLine(buf, line)) return Result::Incomplete;

                    if (line.empty()) { // 空行表示请求头解析结束了
                        auto it = req.headers.find("content-length");
                        contentLength = it == req.headers.end() ? 0 : std::stoul(it->second);

                        if (contentLength == 0) { // 没有请求体
                            out = std::move(req);
                            reset();
                            return Result::Complete;
                        }

                        state = State::Body;
                        break;
                    }

                    if (!parseHeader(line)) return Result::Error;
                }
            }

            if (state == State::Body) {
                if (buf.size() < contentLength) return Result::Incomplete;

                req.body.assign(reinterpret_cast<const char*>(buf.data()), contentLength);
                buf.consume(contentLength);
                out = std::move(req);
                reset();
                return Result::Complete;
            }
        }
    }

  private:
    void reset() {
        state = State::RequestLine;
        req = HttpRequest{};
        contentLength = 0;
    }

    static std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    static void trim(std::string& s) { // 去掉空白字符
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
            s.erase(s.begin());
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    }

    bool readLine(Buffer& buf, std::string& line) { // 根据\r\n分割出一行
        for (size_t i = 1; i < buf.size(); i++) {
            if (buf[i - 1] == '\r' && buf[i] == '\n') {
                line.assign(reinterpret_cast<const char*>(buf.data()), i - 1);
                buf.consume(i + 1);
                return true;
            }
        }
        return false;
    }

    bool parseRequestLine(const std::string& line) { // 解析请求行
        size_t p1 = line.find(' ');
        if (p1 == std::string::npos) return false;

        size_t p2 = line.find(' ', p1 + 1);
        if (p2 == std::string::npos) return false;

        req.method = line.substr(0, p1);
        req.path = line.substr(p1 + 1, p2 - p1 - 1);
        req.version = line.substr(p2 + 1);

        return req.method == "GET" || req.method == "POST";
    }

    bool parseHeader(const std::string& line) {
        size_t pos = line.find(':');
        if (pos == std::string::npos) return false;

        std::string key = lower(line.substr(0, pos));
        std::string value = line.substr(pos + 1);
        trim(value);

        req.headers[key] = value;
        return true;
    }
};
