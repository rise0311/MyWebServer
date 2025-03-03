//本文件实现一个HTTP请求类,能够解析HTTP请求

#ifndef HTTP_H
#define HTTP_H
/**
 * 设计思路：
 * 一个HTTP请求由3部分组成,请求行(1行)+头部(n行)+空行(1行)+请求内容()
 */

#include<sys/socket.h>
#include<netinet/in.h>
#include<cstring>
#include<sys/epoll.h>
#include<unistd.h>
#include<errno.h>
#include<fcntl.h>
#include<cstdlib>
#include<sys/mman.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<stdarg.h>
#include<cstdio>
#include<sys/uio.h>
#include<map>
#include<string>
#include<utility>
#include"../MySql/MySql.h"
#include"../log/Log.h"

using std::map;
using std::string;

#define READ_BUFFER_SIZE 2048
#define WRITE_BUFFER_SIZE 1024
#define FILE_NAME_LENGTH 200

int setnonblocking(int fd);
void removefd(int epollfd, int fd);
void modfd(int epollfd, int fd, int ev, int TRIGMode);
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

//HTTP请求的method
enum METHOD{GET,POST,HEAD,PUT,DELETE,TRACE,OPTIONS,CONNECT,PATCH};
//HTTP请求的返回码
enum HTTP_CODE{NO_REQUEST,GET_REQUEST,BAD_REQUEST,NO_RESOURCE,FORBIDDEN_REQUEST,FILE_REQUEST,INTERNAL_ERROR,CLOSED_CONNECTION,UNKNOWN};
//主状态机的三种状态,解析:请求行、请求头、请求内容
enum CHECK_STATE{CHECK_STATE_REQUESTLINE,CHECK_STATE_HEADERS,CHECK_STATE_CONTENT};
//每一行的三种状态:LINE_OPEN表示该行不完整,LINE_OK表示该行没有问题,LINE_BAD表示该行出错(格式等存在错误)
enum LINE_STATUS{LINE_OPEN,LINE_OK,LINE_BAD};

class http_conn
{
public: //共有成员函数
    http_conn(){}   //构造函数
    ~http_conn(){}  //析构函数
    void init(int client_socketfd,const sockaddr_in& addr,int trig_mode,const char*root);    //初始化连接
    void close_fd();
    void process(); //处理连接，内部会先解析连接，然后生成response
    bool read();    //非阻塞读,把请求从client_socket读到read_buffer中
    bool write();   //非阻塞写,把响应从write_buffer写到client_socket中
    void init_users(MySqlPool*pool);
private://私有成员函数
    void init();    //私有的init函数,用于初始化部分私有的成员变量
    HTTP_CODE process_read();    //解析read_buffer中的请求,返回的是解析结果
    bool process_write(HTTP_CODE,HTTP_CODE);   //生成response

    //下面是process_read()中要使用到的函数
    HTTP_CODE parse_request_line(char*line);
    HTTP_CODE parse_headers(char*line);
    HTTP_CODE parse_content(char*line);
    char*get_line(){return read_buffer+start_of_this_line;}    //获取当前要解析的行的起始地址
    LINE_STATUS parse_line();   //当前行是否完整
    HTTP_CODE do_request();     //请求合法时,执行请求,获取对应地资源

    //下面是process_write()中要使用到的函数
    void unmmap();  //解映射
    bool add_response(const char*format,...);//
    bool add_content(const char*content);
    bool add_content_length(int content_len);
    bool add_status_line(int status,const char*title);
    bool add_headers(int content_len);
    bool add_linger();
    bool add_blank_line();
    bool add_content_type();
public:
    static int epollfd;
    static int user_count;
    MYSQL*mysql_conn;   //当前http请求要使用的数据库连接
    char root_path[200];    //html资源目录
    static MySqlPool*pool;  //所有连接共享数据库连接池
    
    int sockfd; //负责该连接的socket
    sockaddr_in address;    //该连接的client的socket地址(包括IP地址与端口)

private:
    char read_buffer[READ_BUFFER_SIZE];
    char write_buffer[WRITE_BUFFER_SIZE];

    int start_of_this_line; //当前要处理的行的首字符的索引
    int checked_idx;    //解析时,要逐个字符解析。checked_idx是当前正在解析的字符的索引
    int read_idx;   //read buffer中从read_idx及以后，是可以继续读入的部分
    int write_idx;  //write_buffer中待发送的字节数

    // 解析结果
    METHOD method;
    char*url;
    char*version;
    char*host;
    char*connection;
    char*content;
    int content_length;
    bool linger;

    //状态机的状态
    CHECK_STATE current_state;  //主状态机当前状态

    //资源
    char file_path[FILE_NAME_LENGTH];//要申请的文件的完整目录
    char*file_address;  //文件映射到内存中的地址
    struct stat file_stat;//文件状态
    int fd;

    struct iovec iv[2];
    int iv_count;
    int bytes_to_send;
    int bytes_have_send;

    //水平触发or边沿触发
    int trig_mode;

    int cgi;    //是否为登录注册

    //同步量
    Locker lock;
};

#endif