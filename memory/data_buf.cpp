#include <sys/ioctl.h>
#include <unistd.h>
#include <memory.h>
#include <assert.h>

#include "data_buf.h"
#include "pr.h"

BufferBase::BufferBase() 
{
}

BufferBase::~BufferBase()
{
    clear();
}

const int BufferBase::length() const 
{
    return data_buf != nullptr ? data_buf->length : 0;
}

void BufferBase::pop(int len) 
{
    assert(data_buf != nullptr && len <= data_buf->length);

    data_buf->pop(len);
    if(data_buf->length == 0) {
        Mempool::get_instance().retrieve(data_buf);
        data_buf = nullptr;
    }
}

void BufferBase::clear()
{
    if (data_buf != nullptr)  {
        Mempool::get_instance().retrieve(data_buf);
        data_buf = nullptr;
    }
}

/// 读取数据到缓冲区 ，返回读取的字节数
int InputBuffer::read_from_fd(int fd)
{
    int need_read;
    // FIONREAD: get readable bytes num in kernel buffer
    /// 读取内核缓冲区中的可读字节数    
    if (ioctl(fd, FIONREAD, &need_read) == -1) {
        PR_ERROR("ioctl FIONREAD error\n");
        return -1;
    }
    
    if (data_buf == nullptr) {
        data_buf = Mempool::get_instance().alloc_chunk(need_read);
        if (data_buf == nullptr) {
            PR_INFO("no free buf for alloc\n");
            return -1;
        }
    }
    else {
        assert(data_buf->head == 0);
        /// 如果缓冲区剩余空间不足，重新分配
        if (data_buf->capacity - data_buf->length < (int)need_read) {   
            Chunk *new_buf = Mempool::get_instance().alloc_chunk(need_read + data_buf->length);
            if (new_buf == nullptr) {
                PR_INFO("no free buf for alloc\n");
                return -1;
            }
            new_buf->copy(data_buf);
            Mempool::get_instance().retrieve(data_buf);
            data_buf = new_buf;
        }
    }

    int already_read = 0;
    do { 
        if(need_read == 0) {  ///need_read == 0，表示 ioctl 未能确定可读的字节数，或者没有数据可读
            already_read = read(fd, data_buf->data + data_buf->length, m4K);
        } else {
            already_read = read(fd, data_buf->data + data_buf->length, need_read);
        }
    } while (already_read == -1 && errno == EINTR);/// 读取数据时，被信号中断，重新读取
    if (already_read > 0)  {    /// 读取成功，更新缓冲区长度
        if (need_read != 0) {/// 需要读取的字节数不为0，已读取的字节数应该等于需要读取的字节数
            assert(already_read == need_read);
        }
        data_buf->length += already_read;
    }

    return already_read;
}
/// 获取缓冲区中的数据
const char *InputBuffer::get_from_buf() const 
{
    return data_buf != nullptr ? data_buf->data + data_buf->head : nullptr;
}

void InputBuffer::adjust()
{
    if (data_buf != nullptr) {
        data_buf->adjust();
    }
}

/// 将数据写入缓冲区，返回写入的字节数
int OutputBuffer::write2buf(const char *data, int len)
{
    if (data_buf == nullptr) {
        data_buf = Mempool::get_instance().alloc_chunk(len);
        if (data_buf == nullptr) {
            PR_INFO("no free buf for alloc\n");
            return -1;
        }
    }
    else {
        assert(data_buf->head == 0);
        if (data_buf->capacity - data_buf->length < len) {
            Chunk *new_buf = Mempool::get_instance().alloc_chunk(len + data_buf->length);
            if (new_buf == nullptr) {
                PR_INFO("no free buf for alloc\n");
                return -1;
            }
            new_buf->copy(data_buf);
            Mempool::get_instance().retrieve(data_buf);
            data_buf = new_buf;
        }
    }

    memcpy(data_buf->data + data_buf->length, data, len);
    data_buf->length += len;

    return 0;
}
/// 将缓冲区中的数据写入文件描述符，返回写入的字节数
int OutputBuffer::write2fd(int fd)
{
    assert(data_buf != nullptr && data_buf->head == 0);

    int already_write = 0;

    do { 
        already_write = write(fd, data_buf->data, data_buf->length);
    } while (already_write == -1 && errno == EINTR);

    if (already_write > 0) {
        data_buf->pop(already_write);
        data_buf->adjust();
    }

    if (already_write == -1 && errno == EAGAIN) {
        already_write = 0;
    }

    return already_write;
}