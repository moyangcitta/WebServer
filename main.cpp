#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <signal.h>

#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"

#define MAX_FD 65535    //最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000   //监听的最大的事件数量

//添加信号捕捉，handler（函数指针），信号处理函数
void addsig(int sig, void(handler)(int)){
    //注册信号的参数
    struct sigaction sa;
    //所有数据置空
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    //设置临时阻塞信号集
    sigfillset(&sa.sa_mask);
    //注册信号
    sigaction(sig, &sa, NULL);
}

//添加文件描述符到epoll中
extern void addfd(int epollfd, int fd, bool one_shot);
//从epoll中删除文件描述符
extern int removefd(int epollfd, int fd);
//修改文件描述符
extern void modfd(int epollfd, int fd, int ev);

int main(int argc, char *argv[]){
    if(argc <= 1){
        //获取路径
        printf("按照如下格式运行 : %s port_number\n", basename(argv[0]));
        exit(-1);
    }

    //获取端口号
    int port = atoi(argv[1]);

    //对SIGPIE信号进行处理
    //如果你把函数的指针（地址）作为参数传递给另一个函数，当这个指针被用来调用其所指向的函数时，我们就说这是回调函数。
    addsig(SIGPIPE, SIG_IGN);

    //创建线程池，初始化线程池
    threadpool<http_conn> *pool = NULL;
    try{
        pool = new threadpool<http_conn>;
    }
    catch(...){
        exit(-1);
    }

    //创建一个数组用于保存所有的客户端信息
    http_conn *users = new http_conn[ MAX_FD ];

    //创建监听的套接字
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    //设置端口复用，在绑定之前设置
    //端口复用最常用的用途应该是防止服务器重启时之前绑定的端口还未释放或者程序突然退出而系统没有释放端口。这种情况下如果设定了端口复用，则新启动的服务器进程可以直接绑定端口。
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    //绑定
    bind(listenfd, (struct sockaddr*)&address, sizeof(address));

    //监听
    listen(listenfd, 5);

    //创建epoll对象，事件数组，添加
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);

    //将监听的文件描述符添加到epoll对象中
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    while(true){
        //检测到了几个事件
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        //不是被突然中断
        if((num < 0) && (errno != EINTR)){
            printf("epoll failure\n");
            break;
        }

        //循环遍历
        for(int i = 0; i < num; i++){
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd){
                //有客户端链接进来
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlen);

                if(http_conn::m_user_count >= MAX_FD){
                    //目前的连接数满了
                    //给客户端写一个信息：服务器内部正忙
                    close(connfd);
                    continue;
                }
                //将新的客户的数据初始化，放到数组当中
                users[connfd].init(connfd, client_address);
            }
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                //对方异常断开或者错误等事件
                users[sockfd].close_conn();
            }
            else if(events[i].events & EPOLLIN){
                if(users[sockfd].read()){
                    //一次性把所有数据都读完
                    //调用线程池，将数据添加到线程当中
                    pool->append(users + sockfd);
                }
                else{
                    users[sockfd].close_conn();
                }
            }
            else if(events[i].events & EPOLLOUT){
                if(!users[sockfd].write()){ //一次性写完所有的数据
                    users[sockfd].close_conn();
                }
            }
        }
    }

    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;

    return 0;
}