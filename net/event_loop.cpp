#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include "event_loop.h"
#include "../log/pr.h"
#include "../log/log.h"

using namespace std;

EventLoop::EventLoop() : el_epoller(new Epoll()) {
    //创建一个event_fd,用于唤醒epoll_wait,并且设置为非阻塞 
    if(el_evfd = { eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC) }; el_evfd < 0)
    {
        PR_ERROR("fail to create event_fd\n");
        exit(1);
    }
    LOG_INFO("create one eventloop, event fd is %d\n", el_evfd);
    el_epoller->epoll_add(el_evfd, EPOLLIN , [this](){ this->evfd_read(); });
}

EventLoop::~EventLoop() {
    close(el_evfd);
}
//向一个事件文件描述符(event_fd)写入一个64位的整数值（通常是1），以此来唤醒或激活事件循环。
//这种机制通常用于跨线程或进程通信，尤其是在基于事件的编程中，用于通知事件循环有新事件需要处理。
//这种唤醒机制在多线程或异步编程中非常有用，特别是在需要从一个线程向另一个正在等待事件的线程发送信号时。
void EventLoop::evfd_wakeup() {
    uint64_t one = 1;
    if(auto n = write(el_evfd, &one, sizeof one); n != sizeof one)
    {
        PR_ERROR("write %ld bytes to event_fd instead of 8\n", n);
    }
}

void EventLoop::evfd_read() {
    uint64_t one = 1;
    if(auto n = read(el_evfd, &one, sizeof one); n != sizeof one)
    {
        PR_ERROR("read %ld bytes from event_fd instead of 8\n", n);
    }
}
//添加一个任务到任务队列中
void EventLoop::add_task(Task&& cb) {
    LOG_INFO("eventloop, add one task\n");
    if (is_in_loop_thread())
    {
        cb();
    }
    else
    {
        lock_guard<mutex> lock(el_mutex);
        el_task_funcs.emplace_back(move(cb));       
    }

    if (!is_in_loop_thread() || el_dealing_task_funcs) { evfd_wakeup(); }
}
// 事件循环主体，不断调用epoll_wait等待事件发生，然后处理事件
void EventLoop::loop() {
    el_quit = false;
    while (!el_quit) {
        auto cnt = el_epoller->poll();
        LOG_INFO("eventloop, tid %lld, loop once, epoll event cnt %d\n", tid_to_ll(this_thread::get_id()), cnt);
        execute_task_funcs();
    }
}
//执行任务队列中的任务
void EventLoop::execute_task_funcs() {
    std::vector<Task> functors;
    el_dealing_task_funcs = true;

    {
        lock_guard<mutex> lock(el_mutex);
        //swap函数交换两个vector的元素，这样可以减少锁的持有时间
        functors.swap(el_task_funcs);
    }
    //执行任务
    for (size_t i = 0; i < functors.size(); ++i) functors[i]();
    el_dealing_task_funcs = false;
}

void EventLoop::quit() {
    el_quit = true;
    if (!is_in_loop_thread()) {
        evfd_wakeup();
    }
}