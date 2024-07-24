
## Build
&emsp; ./build.sh
## Description
&emsp;&emsp;基于C++11、部分C++14/17特性的一个高性能并发网络服务器，包括目前已实现日志、线程池、内存池、定时器、网络io等模块。模块间低耦合高内聚，可作为整体也可单独提供服务。对各模块提供了单元测试。  
&emsp;&emsp;网络io使用epoll LT触发模式，采用主从reactor设计。提供同步和异步日志，内存池使用哈希表、链表结合的管理，线程池支持任意任务参数和任务结果返回，定时器使用最小堆管理、支持多执行线程、支持在指定时间后执行任务、支持周期性执行任务、支持指定时间间隔重复执行指定次数任务、支持取消定时器等。  
&emsp;&emsp;*具体设计参考各目录下readme。*







## TODO List

### 成功：
zhangshiping@zhangshiping:~/Documents/Coding/HttpServer/webbench-1.5$ ./webbench -c 300 -t 10 http://127.0.0.1:8880/
Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.

Benchmarking: GET http://127.0.0.1:8880/
300 clients, running 10 sec.

Speed=1800 pages/min, 5580 bytes/sec.
Requests: 300 susceed, 0 failed.

### 偶尔存在报错：

#### 
free(): corrupted unsorted chunks
Aborted (core dumped)
####
double free or corruption (fasttop) Aborted (core dumped)
#### others
