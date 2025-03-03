#include<sys/socket.h>
#include<iostream>
#include<cstdlib>
#include <arpa/inet.h>
#include"./WebServer/WebServer.h"
#include"./config/config.h"

using std::cout;
using std::endl;

//首先实现一个最简单的echo server
int main(int argc,char* argv[])
{
    string user = "root";
    string passwd = "Tcr220730.";
    string databasename = "WebServerUsers";

    Config con;
    con.parse_arg(argc,argv);

    WebServer server;
    // printf("开始初始化server\n");
    server.init(con.PORT,1,user,passwd,databasename,con.sql_num,con.thread_num,con.request_num);
    // printf("初始化log\n");
    server.init_log();
    // printf("初始化数据库\n");
    server.init_database();
    // printf("初始化线程池\n");
    server.init_threadpool();
    // printf("开始监听\n");
    server.eventListen();
    // printf("初始化事件循环\n");
    server.eventLoop();

    return 0;
}