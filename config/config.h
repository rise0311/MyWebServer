#ifndef CONFIG_H
#define CONFIG_H

#include "../WebServer/WebServer.h"

using namespace std;

class Config
{
public:
    Config();
    ~Config(){};

    void parse_arg(int argc, char*argv[]);

    //端口号
    int PORT;

    //数据库连接池数量
    int sql_num;

    //线程池内的线程数量
    int thread_num;

    //请求数
    int request_num;
};

#endif