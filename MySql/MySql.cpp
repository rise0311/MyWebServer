#include"MySql.h"

Locker MySqlPool::lock=Locker();

MySqlPool::MySqlPool(){
    FreeConnectionNumber=0;
    ConnectionNumber=0;
}

MySqlPool::~MySqlPool(){
    DestroyMySqlPool();
}

bool MySqlPool::init(string url,string username,string pass_word,string DBName, int MaxNumber,int port){
    m_url=url;
    user_name=username;
    password=pass_word;
    DataBaseName=DBName;
    MaxConnectionNumber=MaxNumber;
    m_port=port;

    if(MaxConnectionNumber<=0) return false;

    for(int i=0;i<MaxConnectionNumber;++i){
        MYSQL*conn=nullptr;
        // printf("%d\n",i);
        conn = mysql_init(conn);
        if(!conn){
            //记录日志
            Log::get_instance()->write_log(ERROR,"MySql.cpp","init",29,"Error at function<mysql_init>");
            exit(1);
        }
        conn=mysql_real_connect(conn,m_url.c_str(),user_name.c_str(),password.c_str(),DataBaseName.c_str(),m_port,NULL,0);
        if(!conn){
            //记录日志
            const char* mysql_error_msg = mysql_error(nullptr);
            printf("url:%s user:%s password:%s database:%s port:%d\n",url.c_str(),user_name.c_str(),pass_word.c_str(),DBName.c_str(),port);
            printf("MySQL error: %s\n", mysql_error_msg);
            Log::get_instance()->write_log(ERROR,"MySql.cpp","init",35,"Error at function<mysql_real_connect>");
            exit(1);

        }
        pool.push_back(conn);
        ++FreeConnectionNumber;
    }

    sem=Sem(FreeConnectionNumber);

    return true;
}

MYSQL* MySqlPool::GetConnection(){
    if(pool.size()==0) 
        return nullptr;
    
    MYSQL*conn=nullptr;

    sem.wait(); //有资源(空闲连接)才会往下执行

    lock_.lock();

    FreeConnectionNumber--;
    ConnectionNumber++;

    conn=pool.front();
    pool.pop_front();

    lock_.unlock();

    return conn;
}

bool MySqlPool::ReleaseConnection(MYSQL*conn){
    if(!conn){
        return false;
    }

    lock_.lock();

    FreeConnectionNumber++;
    ConnectionNumber--;

    pool.push_back(conn);

    lock_.unlock();

    sem.post();
    return true;
}

bool MySqlPool::DestroyMySqlPool(){
    lock_.lock();
    if(pool.size()>0){
        for(auto it=pool.begin();it!=pool.end();++it){
            MYSQL*conn=*it;
            mysql_close(conn);
        }
        ConnectionNumber=0;
        FreeConnectionNumber=0;
        pool.clear();
    }
    lock_.unlock();
    return true;
}

int MySqlPool::GetFreeNumber(){
    return this->FreeConnectionNumber;
}


connectionRAII::connectionRAII(MYSQL **conn, MySqlPool *Pool){
    *conn=Pool->GetConnection();
    conRAII=*conn;
    poolRAII=Pool;
}

connectionRAII::~connectionRAII(){
    poolRAII->ReleaseConnection(conRAII);
}