#ifndef LOG_H
#define LOG_H

#include<cstdio>
#include"../locker/locker.h"
#include<cstring>
#include<stdarg.h>
#include<sys/time.h>
#include<fcntl.h>
#include <unistd.h>

using std::string;

//日志级别
enum LOGLEVEL{
    NONE,
    ERROR,
    WARNING,
    DEBUG,
    INFO
};
//日志记录位置
enum LOGTARGET{
    NOWHERE=0x00,
    INCONSOLE=0x01,
    INFILE=0x10,
};
class Log
{
public:
    bool init(const char*file,int buf_size,int split_lines,bool close);
    static Log* get_instance()//静态函数,用于获取唯一的LOG实例
    {
        lock.lock();
        static Log instance;//这是静态局部变量,是全局唯一的。 这个变量在第一次调用get_instance函数时被初始化。如果不加锁,可能多个线程同时进入get_instance,都发现instance没有初始化，这样导致多次初始化。
        lock.unlock();
        return &instance;
    }
    bool write_log(LOGLEVEL level,string file_name,string function_name,int line_number,string format,...);   //写日志到buffer
    // void output_log();  //把buffer中的日志写到文件

private:
    Log(); //私有构造函数,防止外界利用Log instance;创建Log类的对象
    ~Log();

    static Locker lock; //一个锁,用于保护静态变量唯一初始化
    Locker lock_;    //一个锁,用于保护关键区域

    char* buffer;//日志文件的buffer
    int buffer_size;

    char log_dir[128];
    char log_file[128];
    int fd;

    LOGLEVEL log_level; //日志级别
    LOGTARGET log_target;//输出日志的位置:Console/File/None
    int split_line_number;  //超行数,超过这个值就要分文件
    int number_of_line; //当前目录的行数
    bool close_log; //当前程序执行是否禁用目录

    int day;//当前时间
    
};

#endif