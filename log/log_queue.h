#ifndef __Logger_BUFFER_H__
#define __Logger_BUFFER_H__

#include <stdlib.h>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <assert.h>
#include <stdexcept>

#include "pr.h"

using namespace std;

template <class T>
class buffer_queue
{
public:
    buffer_queue() : b_array(new T[capacity])
    {
    }

    explicit buffer_queue(int max_size, bool debug=false)
    {
        if (max_size <= 0)
        {
            PR_ERROR("max_size is illegal!\n");
           // exit(-1);
           throw invalid_argument("max_size is illegal!");
        }

        b_debug = debug;
        capacity = max_size;
        b_array = new T[capacity];
    }
    //清空队列
    void clear()
    {
        lock_guard<mutex> lck (b_mutex);
        if(b_debug)
        {
            b_rcnt = 0;
            b_wcnt = 0;
        }
        b_first = -1;
        b_last = -1;
        b_size = 0;
    }

    ~buffer_queue()
    {
        lock_guard<mutex> lck (b_mutex);
        if (b_array != NULL)
            delete [] b_array;
    }
    //获取读写次数
    int get_rcnt() const
    {
        lock_guard<mutex> lck (b_mutex);
        if(b_debug)
        {
            return b_rcnt;    
        }
        PR_WARN("this method is invalid when q_debug is false!\n");
        return -1;
    }
    //获取写次数    
    int get_wcnt() const
    {
        lock_guard<mutex> lck (b_mutex);
        if(b_debug)
        {
            return b_wcnt;    
        }
        PR_WARN("this method is invalid when q_debug is false!\n");
        return -1;
    }
    //获取队列大小  
    int get_size() const
    {
        lock_guard<mutex> lck (b_mutex);       
        assert(b_size >= 0 && b_size <= capacity);
        return b_size;
    }
    //判断队列是否满
    bool is_full() const
    {
        lock_guard<mutex> lck (b_mutex);       
        return b_size == capacity ? true : false;
    }
    //判断队列是否为空
    bool is_empty() const
    {
        lock_guard<mutex> lck (b_mutex);       
        return b_size == 0 ? true :false;
    }
    //获取队列容量
    int get_capacity() const
    {
        return capacity;
    }
    //获取队列容量
    bool front(T &value) 
    {
        lock_guard<mutex> lck (b_mutex);
        if (0 == b_size)
        {
            return false;
        }
        value = b_array[b_first+1];
        return true;
    }
    //获取队列容量
    bool back(T &value) 
    {
        lock_guard<mutex> lck (b_mutex);
        if (0 == b_size)
        {
            return false;
        }
        value = b_array[b_last];
        return true;
    }
    //入队
    bool push(const T &item)
    {
        unique_lock<mutex> lck (b_mutex);;
        if (b_size >= capacity)
        {
            b_cond.notify_all();            
            return false;
        }

        b_last = (b_last + 1) % capacity;
        b_array[b_last] = item;

        b_size++;
        if(b_debug)
        {
            b_wcnt++;
        }
        b_cond.notify_all();
        
        return true;
    }
    
    void notify()
    {
        b_cond.notify_all();
    }
    //出队
    bool pop(T &item)
    {

        unique_lock<mutex> lck (b_mutex);
        if (b_size <= 0)
        {           
            b_cond.wait(lck);
            if(b_size <= 0) 
            {
                return false;
            }
        }

        b_first = (b_first + 1) % capacity;
        item = b_array[b_first];
        b_size--;
        if(b_debug)
        {
            b_rcnt++;
        }

        return true;       
    }

    bool pop(T &item, int ms_timeout)
    {
        unique_lock<mutex> lck (b_mutex);
        if (b_size <= 0)
        {           
            if(b_cond.wait_for(lck, chrono::milliseconds(ms_timeout)) == cv_status::timeout)
                return false;
        }

        if (b_size <= 0)
        {            
            return false;
        }

        b_first = (b_first + 1) % capacity;
        item = b_array[b_first];
        b_size--;
        if(b_debug)
        {
            b_rcnt++;
        }
        
        return true;
    }

private:
    mutable mutex b_mutex;
    condition_variable b_cond;  
    T *b_array;
    long long b_rcnt = 0;
    long long b_wcnt = 0;
    bool b_debug = false;
    int b_first = -1;
    int b_last = -1;
    int b_size = 0;
    int capacity = 1000;
};

#endif
