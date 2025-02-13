#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "tcp_conn.h"
#include "tcp_server.h"
#include "event_loop.h"
#include "../log/pr.h"
#include "../log/log.h"

using namespace std;

TcpConnection::TcpConnection(TcpServer *server, EventLoop* loop, int sockfd, struct sockaddr_in& addr, socklen_t& len) {
    tc_server = server;
    tc_peer_addr = addr;
    tc_peer_addrlen = len;
    tc_loop = loop;
    tc_fd = sockfd;
    set_sockfd(tc_fd);//设置为非阻塞
}
//添加任务，将连接任务添加到poller中 
void TcpConnection::add_task() {
    LOG_INFO("tcp connection add connected task to poller, conn fd is %d\n", tc_fd);
    tc_loop->add_task([shared_this=shared_from_this()](){ shared_this->connected(); });
    LOG_INFO("tcp connection add do read to poller, conn fd is %d\n", tc_fd);
    tc_loop->add_to_poller(tc_fd, EPOLLIN, [shared_this=shared_from_this()](){ shared_this->do_read(); });
}

TcpConnection::~TcpConnection() {
    LOG_INFO("TcpConnection descontructed, fd is %d\n", tc_fd);
}
//设置socket，设置非阻塞，关闭nagle算法，提高网络传输效率
void TcpConnection::set_sockfd(int& fd) {
    int flag = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, O_NONBLOCK|flag);
    int op = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &op, sizeof(op));
}
//读取数据
void TcpConnection::do_read() {
    if (int ret = tc_ibuf.read_from_fd(tc_fd); ret == -1) {
        PR_ERROR("read data from socket error\n");
        this->do_close();
        return;
    }
    else if (ret == 0) {
        LOG_INFO("connection closed by peer\n");
        this->do_close();
        return;
    }
    // 执行消息回调 
    tc_message_cb(shared_from_this(), &tc_ibuf);

    return;
}
//发送数据
bool TcpConnection::send(const char *data, int len) {
    bool should_activate_epollout = false; 
    //如果输出缓冲区为空，说明之前没有数据，现在有数据了，需要激活epoll_out事件
    if(tc_obuf.length() == 0) {
        should_activate_epollout = true;
    }

    if (int ret = tc_obuf.write2buf(data, len); ret != 0) {
        PR_ERROR("send data to output buf error\n");
        return false;
    }

    if (should_activate_epollout == true) {
        //激活epoll_out事件 ,因为输出缓冲区不为空了
        tc_loop->add_to_poller(tc_fd,EPOLLOUT, [this](){ this->do_write(); });
    }

    return true;
}
//写数据，将输出缓冲区的数据写入到fd中
void TcpConnection::do_write() {
    while (tc_obuf.length()) {
        int ret;
        if (ret = tc_obuf.write2fd(tc_fd); ret == -1) {
            PR_ERROR("write2fd error, close conn!\n");
            this->do_close();
            return ;
        }
        if (ret == 0) {
            break;
        }
    }

    if (tc_obuf.length() == 0) {
        tc_loop->del_from_poller(tc_fd, EPOLLOUT);
    }

    return;    
}
//关闭连接
void TcpConnection::do_close() {
    if (tc_close_cb) {
        tc_close_cb();
    }

    tc_loop->del_from_poller(tc_fd);
    tc_ibuf.clear(); 
    tc_obuf.clear();

    int fd = tc_fd;
    tc_fd = -1;
    close(fd);

    tc_server->do_clean(shared_from_this());
}
//连接，执行连接回调
void TcpConnection::connected() {
    if(tc_connected_cb) {
        LOG_INFO("execute connected callback, conn fd is %d\n", tc_fd);
        tc_connected_cb(shared_from_this());
    }
    else {
        LOG_INFO("tcp connected callback is null\n");
    }
}
