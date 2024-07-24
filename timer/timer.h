#ifndef __TIMER_H__
#define __TIMER_H__

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <queue>

#include "../threadpool/threadpool.h"
#include "hash_map.h"

using namespace std;

#define DEFAULT_TIMER_THREAD_POOL_SIZE 2

class Timer {
public:
    struct TimerNode {
        chrono::time_point<chrono::high_resolution_clock> tn_tm_point; /// 定时器超时时间
        function<void()> tn_callback;/// 定时器回调函数
        int repeated_id;/// 重复定时器的id
        int tn_id;/// 定时器的id
        int tn_repeated_num;/// 重复定时器的次数
        bool tn_is_period=false;/// 是否是周期性定时器
        bool tn_is_repeated=false;/// 是否是重复定时器
        chrono::milliseconds tn_period;/// 定时器的周期
        bool operator<(const TimerNode& other) const {return tn_tm_point>other.tn_tm_point;};/// 重载小于号,用于优先队列
    };

    enum class IdState{ Running = 0 };

    Timer() : tm_thread_pool(DEFAULT_TIMER_THREAD_POOL_SIZE) {
// cout <<"create timer" <<endl;
        tm_id.store(0);
        tm_running.store(true);
    }

    ~Timer()
    {
        tm_running.store(false);
        tm_cond.notify_all();
        if(tm_tick_thread.joinable())
        {
            tm_tick_thread.join();
        }
    }

    void run()
    {
        tm_tick_thread = thread([this]() { run_local(); });
    }
    
    bool is_available() { return tm_thread_pool.idl_thread_cnt()>=0; }

    int size() { return tm_queue.size(); }

    //F是回调函数，Args是回调函数的参数
    //run_after函数用于在ms_time时间后执行回调函数F
    template <typename F, typename... Args>
    int run_after(int ms_time, bool is_period, F&& f, Args&&... args) {
        TimerNode s;
        s.tn_id = tm_id.fetch_add(1);
        tm_id_state_map.emplace(s.tn_id, IdState::Running);
        s.tn_is_period = is_period;
        s.tn_period = chrono::milliseconds(ms_time);
        s.tn_tm_point = chrono::high_resolution_clock::now() + s.tn_period;
        //bind()函数用于将函数对象f和参数args绑定在一起，返回一个可调用对象
        s.tn_callback = bind(forward<F>(f), forward<Args>(args)...);
        unique_lock<mutex> lock(tm_mutex);
        tm_queue.push(s);
        tm_cond.notify_all();
        // cout <<"push to time queue and notify all"<<endl;
        return s.tn_id;
    }

    //run_at函数用于在time_point时间点执行回调函数F
    template <typename F, typename... Args>
    int run_at(const chrono::time_point<chrono::high_resolution_clock>& time_point, F&& f,
                            Args&&... args) {
        TimerNode s;
        s.tn_id = tm_id.fetch_add(1);
        tm_id_state_map.emplace(s.tn_id, IdState::Running);
        s.tn_is_period = false;
        s.tn_tm_point = time_point;
        // TODO: use lamda, tuple and apply of C++17 replacing bind
        s.tn_callback = bind(forward<F>(f), forward<Args>(args)...);
        unique_lock<mutex> lock(tm_mutex);
        tm_queue.push(s);
        tm_cond.notify_all();

        return s.tn_id;
    }

    //run_repeated函数用于在ms_time时间后执行回调函数F，重复repeated_num次
    template <typename F, typename... Args>
    int run_repeated(int ms_time, int repeated_num, F&& f, Args&&... args)
    {
        TimerNode s;
        s.tn_id = tm_id.fetch_add(1);
        tm_id_state_map.emplace(s.tn_id, IdState::Running);
        s.tn_is_repeated = true;
        s.tn_repeated_num = repeated_num;
        s.tn_period = chrono::milliseconds(ms_time);
        s.tn_tm_point = chrono::high_resolution_clock::now() + s.tn_period;
        // TODO: use lamda, tuple and apply of C++17 replacing bind
        s.tn_callback = bind(forward<F>(f), forward<Args>(args)...);
        unique_lock<mutex> lock(tm_mutex);
        tm_queue.push(s);
        tm_cond.notify_all();   

        return s.tn_id; 
    }

    void cancel(int id)
    {
        if(tm_id_state_map.is_key_exist(id))
        {
            tm_id_state_map.erase(id);
        }
    }

private:
    /// 定时器线程函数 从队列中取出时间最小的定时器，等待时间到达，执行定时器的回调函数
    void run_local()
    {
        while (tm_running.load()) {
            unique_lock<mutex> lock(tm_mutex);
            if (tm_queue.empty()) {
                tm_cond.wait(lock);
                continue;
            }
            /// 取出时间最小的定时器
            auto s = tm_queue.top();
            auto diff = s.tn_tm_point - chrono::high_resolution_clock::now();
            /// 如果时间差大于0，说明还没到时间，等待
            if (chrono::duration_cast<chrono::milliseconds>(diff).count() > 0) {
                tm_cond.wait_for(lock, diff);
                continue;
            } else {
                tm_queue.pop();
                /// 如果定时器已经被取消，直接跳过
                if(!tm_id_state_map.is_key_exist(s.tn_id))
                {
                    continue;
                }
                /// 如果定时器是周期性的，重新加入队列
                if(s.tn_is_period)
                {
                    s.tn_tm_point = chrono::high_resolution_clock::now() + s.tn_period;
                    tm_queue.push(s);
                }
                /// 如果定时器是重复的，重复次数大于0，重新加入队列，重复次数减一
                else if(s.tn_is_repeated && s.tn_repeated_num>0)
                {
                    s.tn_tm_point = chrono::high_resolution_clock::now() + s.tn_period;
                    s.tn_repeated_num--;
                    tm_queue.push(s);
                }
                lock.unlock();
                tm_thread_pool.post_task(move(s.tn_callback));
            }
        }
    }

    priority_queue<TimerNode> tm_queue;/// 用来存储定时器，按照时间排序，时间最小的在队列头部
    atomic<bool> tm_running;
    mutex tm_mutex;
    condition_variable tm_cond;
    thread tm_tick_thread;

    Threadpool tm_thread_pool;
    atomic<int> tm_id;
    hash_map<int, IdState> tm_id_state_map;
};

#endif