#include <string.h>
#include <time.h>
#include <chrono>
#include <stdarg.h>
#include <stdexcept>
#include "log.h"

using namespace std;

Logger::log_level g_log_level = Logger::LOG_LEVEL_INFO;

const char* LogLevelName[Logger::NUM_LOG_LEVELS] =
{
  "[ERROR]",
  "[WARN ]",
  "[INFO ]",
  "[DEBUG]",
};

struct my_time
{
    int year;
    char month;
    char day;
    char hour;
    char minute;
    char second; 
};
/// 获取当前系统时间，格式为年月日时分秒
static my_time get_current_sys_time()
{
    auto tt = chrono::system_clock::to_time_t(chrono::system_clock::now());
    struct tm* ptm = localtime(&tt);
    my_time t = { ptm->tm_year + 1900, static_cast<char>(ptm->tm_mon + 1), static_cast<char>(ptm->tm_mday),
            static_cast<char>(ptm->tm_hour), static_cast<char>(ptm->tm_min), static_cast<char>(ptm->tm_sec)};
    return t;
}

Logger::Logger()
{
    PR_DEBUG("log class constructed\n");
    l_count = 0;
}

Logger::~Logger()
{
    if(l_asyncw_thread)
    {
        is_thread_stop = true;
        if(l_asyncw_thread->joinable())
        {
            l_buffer_queue->notify();
            l_asyncw_thread->join();
        }
        delete l_asyncw_thread;
    }

    lock_guard<mutex> lck (l_mutex);
    if (l_fp != NULL)
    {
        fclose(l_fp);
    }

    if(l_buf)
    {
        delete [] l_buf;
    }

    if(l_buffer_queue)
    {
        delete l_buffer_queue;
    }
}
///
/// \param file_name  dir/ dir / file, 作为 log output ,默认为stdout
/// \param buffer_queue_size 默认为0，表示同步输出，大于0表示异步输出
/// \param level 默认为INFO
/// \param buffer_size 默认为 1024*8
/// \param split_lines  默认为5000
/// \return
 bool Logger::init(const char *file_name, int buffer_queue_size, 
                    Logger::log_level level, int buffer_size, int split_lines)
{
    if(l_inited)
    {
        PR_WARN("Logger has been initialized, do not try again!\n");
        return false;
    }

    if(!file_name)
    {
        l_is_stdout = true;
    }

    if( !l_is_stdout && strlen(file_name)>=128 )
    {
        PR_ERROR("file name must be less than 128 bytes!\n");
        // exit(-1);
        throw invalid_argument("file name must be less than 128 bytes!");;
    }

    set_log_level(level);
    ///如果buffer_queue_size大于0，则表示异步输出
    if (buffer_queue_size >= 1)
    {
        l_is_async = true;
         ///创建一个buffer_queue对象，用于存储日志信息
        l_buffer_queue = new buffer_queue<string>(buffer_queue_size);
        ///创建一个线程，用于异步输出日志
        l_asyncw_thread = new thread(&Logger::async_flush);
    }
    
    l_buf_size = buffer_size;
    l_buf = new char[l_buf_size];
    memset(l_buf, '\0', l_buf_size);
    l_split_lines = split_lines;

    my_time tm = get_current_sys_time();
    l_today = tm.day;
    /// 如果file_name为空，则使用stdout作为输出 
    if(l_is_stdout)
    {
        l_inited = true;
        l_fp = stdout;
        PR_DEBUG("succeed in using stdout as log output\n");
        PR_DEBUG("log init finished!\n");
        return true;
    }
    /// 如果file_name不为空，则使用file_name作为输出
    const char *p = strrchr(file_name, '/');
    char log_file_fullname[268] = {0};

    if (p == NULL)
    {
        PR_ERROR("log file name should behind '/'\n");
        return false;
    }
    else
    {
        strcpy(l_file_name, p + 1);
        strncpy(l_dir_name, file_name, p - file_name + 1);
        snprintf(log_file_fullname, 267, "%s%04d_%02d_%02d_%s", l_dir_name, 
            tm.year, tm.month, tm.day, l_file_name);
        ///假设file_name为 C:/logs/error.log, tm.year为2023，tm.month为6，tm.day为29，
        /// 则 l_dir_name为"C:/logs/"，
        /// l_file_name为"error.log"，则结果字符串将会是：
        /// C:/logs/2023_06_29_error.log
        l_fp = fopen(log_file_fullname, "a");
    }

    if (l_fp == NULL)
    {
        PR_ERROR("open %s failed!\n", log_file_fullname);
        return false;
    }

    l_inited = true;
    PR_DEBUG("succeed in using file %s as log output\n", log_file_fullname);
    PR_DEBUG("log init finished!\n");

    return true;
}

void Logger::write_log(const char* file_name, const char* tn_callbackname, int line_no, log_level level, const char *format, ...)
{
    my_time my_tm = get_current_sys_time();

    {
        lock_guard<mutex> lck (l_mutex);
        l_count++;
        ///如果当前时间不是当天或者写入的行数达到了split_lines，则创建一个新的日志文件
        if (l_today != my_tm.day || l_count % l_split_lines == 0)
        {
            PR_DEBUG("start to create a new log file\n");
            char new_file_name[301] = {0};
            fflush(l_fp);
            fclose(l_fp);
            char prefix[24] = {0};
        
            snprintf(prefix, 23, "%04d_%02d_%02d_", my_tm.year, my_tm.month, my_tm.day);
        
            if (l_today != my_tm.day)
            {
                snprintf(new_file_name, 300, "%s%s%s", l_dir_name, prefix, l_file_name);
                l_today = my_tm.day;
                l_count = 0;
            }
            else
            {
                snprintf(new_file_name, 300, "%s%s%s.%lld", l_dir_name, prefix, l_file_name, l_count / l_split_lines);
            }
            l_fp = fopen(new_file_name, "a");
        }
    }

    va_list valst;///允许函数接收和处理数量不定的参数
    va_start(valst, format);

    string log_str;
    {
        lock_guard<mutex> lck (l_mutex);
        /// 将日志信息写入缓冲区  ，格式为：年-月-日 时:分:秒 日志等级 [文件名:函数名:行号] 日志信息
        int n = snprintf(l_buf, 300, "%04d-%02d-%02d %02d:%02d:%02d %s [%s:%s:%d] ",
                        my_tm.year, my_tm.month, my_tm.day,
                            my_tm.hour, my_tm.minute, my_tm.second, LogLevelName[level],
                            file_name, tn_callbackname, line_no);
        
        int m = vsnprintf(l_buf + n, l_buf_size - 1, format, valst);
        l_buf[n + m] = '\n';
        l_buf[n + m + 1] = '\0';
        log_str = l_buf;
    }
    va_end(valst);
    
    /// 如果是异步输出，则将日志信息写入buffer_queue中
    if (l_is_async)
    {
        while (!l_buffer_queue->push(log_str) && !is_thread_stop)   //FIXME: use tm_condvar replacing busy loop
        {               
             ///busy loop
        }
        
    }
    /// 如果是同步输出，则直接将日志信息写入文件 
    else
    {
        lock_guard<mutex> lck (l_mutex);
        fputs(log_str.c_str(), l_fp);   
    }
}
/// flush函数用于将缓冲区的内容写入文件
void Logger::flush(void)
{
    lock_guard<mutex> lck (l_mutex);
    fflush(l_fp);
}
