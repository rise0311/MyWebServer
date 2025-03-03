#ifndef MYSQL_H
#define MYSQL_H

#include<string>
#include<mysql/mysql.h>
#include<list>
#include"../locker/locker.h"
#include"../log/Log.h"

using std::string;
using std::list;

class MySqlPool{
public:
    static MySqlPool*GetInstance(){
        lock.lock();
        static MySqlPool instance=MySqlPool();
        lock.unlock();
        return &instance;
    }
    bool init(string url,string username,string pass_word,string DBName, int MaxNumber,int port);    //初始化函数
    MYSQL* GetConnection(); //从数据库连接池中获取一个链接
    bool ReleaseConnection(MYSQL*conn);//释放连接conn
    bool DestroyMySqlPool();    //销毁连接池,用于析构函数中
    int GetFreeNumber();    //获取可用连接的数量
private:
    MySqlPool();
    ~MySqlPool();

    int MaxConnectionNumber;
    int FreeConnectionNumber;
    int ConnectionNumber;   //池中连接总数

    static Locker lock;//保护静态局部变量初始化
    Locker lock_;   //用于保护关键区域

    Sem sem;    //负责控制对连接池资源的访问

    string m_url;     //数据库地址
    int m_port;    //数据库端口号
    string user_name;
    string password;
    string DataBaseName;    //数据库名字

    list<MYSQL*> pool;
};

class connectionRAII{
public:
    connectionRAII(MYSQL **con, MySqlPool *connPool);
    ~connectionRAII();
private:
    MYSQL *conRAII;
    MySqlPool *poolRAII;
};

#endif