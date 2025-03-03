#include "config.h"

Config::Config(){
    //端口号,默认9006
    PORT = 9006;

    //数据库连接池数量,默认8
    sql_num = 8;

    //线程池内的线程数量,默认8
    thread_num = 8;

    //请求队列中最大请求数,默认为10000
    request_num=10000;
}

void Config::parse_arg(int argc, char*argv[]){
    int opt;
    const char *str = "p:s:t:r";
    while ((opt = getopt(argc, argv, str)) != -1)
    {
        switch (opt)
        {
        case 'p':
        {
            PORT = atoi(optarg);
            break;
        }
        case 's':
        {
            sql_num = atoi(optarg);
            break;
        }
        case 't':
        {
            thread_num = atoi(optarg);
            break;
        }
        case 'r':
        {
            request_num=atoi(optarg);
            break;
        }
        default:
            break;
        }
    }
}