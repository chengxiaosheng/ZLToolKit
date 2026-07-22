/**
 *
 *  @file ParseCursor.h
 *
 *  连续只读 span 视图 + 游标偏移。HTTP/WebSocket 解析器据此增量消费入站字节。
 *
 *  不拥有内存：基底由 TcpConnection::handleRecv 设定--
 *    - fresh 模式：覆盖 toolkit::Buffer::Ptr（零拷贝，主路径）
 *    - leftover 模式：覆盖 TcpConnection::readBuffer_（不完整 token 碎片拼接）
 *
 *  方法名与 MsgBuffer 的只读探查子集一一对应（peek/beginWrite/readableBytes/
 *  findCRLF/retrieve/retrieveUntil/retrieveAll/operator[]），故解析器从
 *  `MsgBuffer *` 改为 `ParseCursor *` 时函数体无需改动。
 *
 *  解析器在单次 parse 调用内持 findCRLF/peek 返回的指针，基底 span 在该次调用
 *  期间稳定（fresh：Buffer::Ptr 不动；leftover：parse 期间不 append）；
 *  解析器跨 recv 不持裸指针，故无悬垂。
 */

#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>

namespace trantor
{
class ParseCursor
{
  public:
    ParseCursor(const char *data, size_t size)
        : data_(data), size_(size), cur_(0)
    {
    }

    const char *peek() const { return data_ + cur_; }

    // 可读区末尾（对齐 MsgBuffer::beginWrite：readable 区间为 [peek, beginWrite)）
    const char *beginWrite() const { return data_ + size_; }

    size_t readableBytes() const { return size_ - cur_; }

    const char *findCRLF() const
    {
        static constexpr char CRLF[2] = {'\r', '\n'};
        const char *found = std::search(peek(), beginWrite(), CRLF, CRLF + 2);
        return found == beginWrite() ? nullptr : found;
    }

    void retrieve(size_t len)
    {
        if (len >= readableBytes())
        {
            cur_ = size_;  // 等价 retrieveAll（逻辑清空；物理收缩由 owner 处理）
            return;
        }
        cur_ += len;
    }

    void retrieveUntil(const char *end)
    {
        assert(peek() <= end);
        assert(end <= beginWrite());
        retrieve(static_cast<size_t>(end - peek()));
    }

    void retrieveAll() { cur_ = size_; }

    char operator[](size_t offset) const
    {
        assert(readableBytes() > offset);
        return data_[cur_ + offset];
    }

    /// handleRecv 据此判断是否有未消费尾部需落地 leftover
    size_t consumed() const { return cur_; }

  private:
    const char *data_;
    size_t size_;
    size_t cur_;
};

}  // namespace trantor
