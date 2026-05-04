#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class Buffer {
  public:
    void append(const uint8_t* data, size_t len);
    void consume(size_t n);
    uint8_t* data();

    void append(const char* data, size_t len) {
        append(reinterpret_cast<const uint8_t*>(data), len);
    }

    void append(const std::string& data) { append(data.data(), data.size()); }

    const uint8_t* data() const { return const_cast<Buffer*>(this)->data(); }

    size_t size() const { return _size; }

    bool empty() const { return _size == 0; }

    void clear() {
        // 清空逻辑数据，但保留底层容量，避免高频请求时反复申请内存。
        _head = 0;
        _tail = 0;
        _size = 0;
    }

    uint8_t operator[](size_t index) const {
        return _buffer[(_head + index) % _buffer.size()];
    }

  private:
    void ensureCapacity(size_t required);
    void linearize();

    bool isWrapped() const {
        if (_size == 0 || _buffer.empty()) { return false; }
        return _head + _size > _buffer.size();
    }

    std::vector<uint8_t> _buffer;
    size_t _head{0};
    size_t _tail{0};
    size_t _size{0};
};
