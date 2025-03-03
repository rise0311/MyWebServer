#ifndef WEBSERVER_H
#define WEBSERVER_H

#include<sys/socket.h>
#include<netinet/in.h>
#include<sys/epoll.h>
#include"../ThreadPool/ThreadPool.h"
#include"../Timer/Timer.h"
#include"../MySql/MySql.h"
#include"../locker/locker.h"
#include"../http/HTTP.h"
#include"../log/Log.h"

#define MAX_EVENTS 10000
#define MAX_FD  65536
#define TIMESLOT 5

class WebServer{
public:
    WebServer();
    ~WebServer();
    void init(int port,int trig_mode, string user,string passwd,string database,int sql_num,int thread_num,int request_num); //初始化服务器:IP地址,端口号,线程池,数据库连接池,
    void init_database();   //初始化数据库连接
    void init_threadpool(); //初始化线程池
    void init_log();
    void eventListen(); //创建listenfd,创建监听队列
    void eventLoop();   //监听事务的loop
private:
    //服务器基础
    int port;   //WebServer的端口,默认为9006
    char*root_path; //root文件夹的位置,包含了所有会申请到的html资源
    int trig_mode;  //ET or LT   1为ET,0为LT
    int pipefd[2];  //信号管道
    int epollfd;    //epoll实例
    http_conn* users;//所有可能的连接实例
    int listenfd;   //监听socket
    epoll_event events[MAX_EVENTS];//存储监听到的事件

    //数据库相关
    string db_user;
    string db_passwrod;
    string db_name;
    MySqlPool*db_pool;
    int mysql_conn_num;
    string url; //数据库地址

    //线程池相关
    ThreadPoll<http_conn>*thread_pool;
    int thread_number;  //线程池的线程数
    int request_number; //请求数

    //计时器相关
    heap_timer  minheap_timer;
    client_data* users_timer;   //以连接对应的sockfd为索引,client_data包含客户的timer指针、sockfd、客户socket地址

    //日志相关
    int m_close_log;
};

#endif