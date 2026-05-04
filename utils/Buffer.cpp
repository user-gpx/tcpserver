#include "Buffer.h"
#include <algorithm>
#include <cstring>

namespace {
// 初始容量大小
constexpr size_t kInitialCapacity = 4096;
} // namespace

// 向缓冲区追加数据
void Buffer::append(const uint8_t* data, size_t len) {
    // 如果要追加的数据长度为 0，直接返回
    if (len == 0) { return; }
    // 确保缓冲区容量足够，若不够则扩容
    ensureCapacity(_size + len);
    size_t capacity = _buffer.size();
    // 计算首部分拷贝的数据长度
    size_t first = std::min(len, capacity - _tail);
    // 拷贝数据到缓冲区尾部
    std::memcpy(_buffer.data() + _tail, data, first);
    size_t remaining = len - first;
    // 如果还有剩余数据，拷贝到缓冲区的开始部分
    if (remaining > 0) { std::memcpy(_buffer.data(), data + first, remaining); }
    // 更新尾指针，进行环形计算
    _tail = (_tail + len) % capacity;
    // 增加缓冲区的大小
    _size += len;
}
// 消费缓冲区的前 n 个字节
void Buffer::consume(size_t n) {
    // 如果消费的字节数大于等于当前缓冲区大小，直接清空缓冲区
    if (n >= _size) {
        clear();
        return;
    }
    // 否则，更新头指针，进行环形计算
    _head = (_head + n) % _buffer.size();
    // 减少缓冲区的大小
    _size -= n;
}
// 获取缓冲区中的数据
uint8_t* Buffer::data() {
    // 如果缓冲区有数据，则进行线性化处理
    linearize();
    // 如果缓冲区为空，返回 nullptr
    if (_size == 0) { return nullptr; }
    // 返回缓冲区中有效数据的起始位置
    return _buffer.data() + _head;
}
// 确保缓冲区有足够的容量，若不够则扩容
void Buffer::ensureCapacity(size_t required) {
    // 如果当前缓冲区容量大于或等于所需容量，直接返回
    if (_buffer.size() >= required) { return; }
    // 新的容量大小是当前容量的两倍，或者所需容量与初始容量之间的最大值
    size_t newCapacity = std::max(required, std::max(kInitialCapacity, _buffer.size() * 2));
    // 创建一个新的缓冲区
    std::vector<uint8_t> newBuffer(newCapacity);
    // 如果缓冲区已有数据，则将数据拷贝到新缓冲区中
    if (_size > 0) {
        if (isWrapped()) { // 如果缓冲区内容已环绕
            // 将头部到缓冲区末尾的数据拷贝到新缓冲区
            size_t first = _buffer.size() - _head;
            std::memcpy(newBuffer.data(), _buffer.data() + _head, first);

            // 将缓冲区开始的数据拷贝到新缓冲区的尾部
            std::memcpy(newBuffer.data() + first, _buffer.data(), _tail);
        } else {
            // 如果没有环绕，直接拷贝所有数据
            std::memcpy(newBuffer.data(), _buffer.data() + _head, _size);
        }
    }
    // 交换原缓冲区和新缓冲区
    _buffer.swap(newBuffer);
    // 重置头指针和尾指针
    _head = 0;
    _tail = (_size == _buffer.size()) ? 0 : _size;
}

// 线性化缓冲区，确保所有数据都在缓冲区的开始部分
void Buffer::linearize() {
    // 如果缓冲区为空，直接重置头指针和尾指针
    if (_size == 0) {
        _head = 0;
        _tail = 0;
        return;
    }
    // 如果缓冲区没有环绕，直接返回
    if (!isWrapped()) { return; }
    // 缓冲区内容已环绕，进行线性化操作
    // 使用 std::rotate 将缓冲区内容移动，使得数据从头部开始
    std::rotate(_buffer.begin(), _buffer.begin() + static_cast<std::ptrdiff_t>(_head),
                _buffer.end());
    // 重置头指针和尾指针
    _head = 0;
    _tail = (_size == _buffer.size()) ? 0 : _size;
}