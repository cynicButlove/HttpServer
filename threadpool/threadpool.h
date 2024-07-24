#ifndef __THREADPOOL_H__
#define __THREADPOOL_H__


#include <vector>
#include <queue>
#include <atomic>
#include <future>
#include <condition_variable>
#include <thread>
#include <functional>
#include <stdexcept>
#include <assert.h>

#define  THREADPOOL_MAX_NUM 64
//#define  THREADPOOL_AUTO_GROW

using namespace std;

class Threadpool
{
public:
	typedef function<void()> Task;

	inline Threadpool(unsigned short size = 4) { 
        assert(size <= THREADPOOL_MAX_NUM);
        add_thread(size); 
    }
	inline ~Threadpool()
	{
		tp_run=false;
		tp_task_cv.notify_all();
		for (thread& thread : tp_pool) {
			if(thread.joinable())
				thread.join(); 
		}
	}
    /// 提交任务,返回future,可以获取任务执行的结果
	template<class F, class... Args>
	decltype(auto) post_task(F&& f, Args&&... args)
	{
		if (!tp_run)
			throw runtime_error("post_task on Threadpool has been stopped.");

		using return_type = typename std::result_of_t<F(Args...)>;/// result_of_t 推断函数类型F的调用返回类型
		auto task = make_shared<packaged_task<return_type()>>(///异步操作  packaged_task
			bind(forward<F>(f), forward<Args>(args)...)
		);
		future<return_type> res = task->get_future();///获取异步结果
		{
			lock_guard<mutex> lock{ tp_lock };
            /// 将任务添加到队列中
			tp_tasks.emplace([task](){
				(*task)();
			});
		}
#ifdef THREADPOOL_AUTO_GROW
		if (tp_idl_tnum < 1 && tp_pool.size() < THREADPOOL_MAX_NUM)
			add_thread(1);
#endif
        /// 通知一个线程执行任务
		tp_task_cv.notify_one(); 

		return res;
	}


	int idl_thread_cnt() { return tp_idl_tnum; }

	int thread_cnt() { return tp_pool.size(); }

#ifndef THREADPOOL_AUTO_GROW
private:
#endif
    /// 添加size个线程,线程池中线程数量不超过THREADPOOL_MAX_NUM
	void add_thread(unsigned short size)
	{
		for (; tp_pool.size() < THREADPOOL_MAX_NUM && size > 0; --size)
		{
            /// 线程池中线程执行函数
			tp_pool.emplace_back( [this]{
                /// 线程不断从任务队列中取任务执行
				while (tp_run)
				{
					Task task;
					{
						unique_lock<mutex> lock{ tp_lock };
                        /// wait直到有任务到来或者线程池销毁
						tp_task_cv.wait(lock, [this]{
								return !tp_run || !tp_tasks.empty();
						});
                        /// 如果线程池销毁，且任务队列为空，则线程退出
						if (!tp_run && tp_tasks.empty())
							return;
						task = move(tp_tasks.front());
						tp_tasks.pop();
					}
					tp_idl_tnum--;
					task();
					tp_idl_tnum++;
				}
			});
			tp_idl_tnum++;
		}
	}

private:
    Threadpool(const Threadpool &) = delete;
    Threadpool(Threadpool &&) = delete;
    Threadpool & operator=(const Threadpool &) = delete;
    Threadpool & operator=(Threadpool &&) = delete;

	vector<thread> tp_pool; 
	queue<Task> tp_tasks;
	mutex tp_lock;
	condition_variable tp_task_cv;
	atomic<bool> tp_run{ true };
	atomic<int>  tp_idl_tnum{ 0 };
};


#endif