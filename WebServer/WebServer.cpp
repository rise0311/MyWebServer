#include"WebServer.h"
#include<assert.h>
#include<signal.h>

/**
 * 类的构造函数,主要作用是动态创建users数组,设置项目的根目录以及网页资源的目录,创建users_timer数组
 */
WebServer::WebServer(){
    //初始化root_path,users
    users=new http_conn[MAX_FD];

    char server_path[200]="";
    getcwd(server_path,200);
    root_path=new char[200];
    strcpy(root_path,server_path);
    strcat(root_path,"/root");
    // printf("root path:%s ",root_path);

    users_timer=new client_data[MAX_FD];
}

/**
 * 类的析构函数
 */
WebServer::~WebServer(){
    delete []users_timer;
    delete []root_path;
    delete []users;
}

/**
 * 类的public初始化函数
 */
void WebServer::init(int port_,int mode, string user,string passwd,string database,int sql_num,int thread_num,int request_num)//初始化部分变量
{
    port=port_;
    // trig_mode=mode;
    trig_mode=1;
    
    db_user=user;
    db_passwrod=passwd;
    db_name=database;
    mysql_conn_num=sql_num;

    thread_number=thread_num;
    request_number=request_num;

    m_close_log=0;//不关闭log
}

/**
 * 利用init()函数初始化的量来初始化线程池
 */
void WebServer::init_threadpool()
{
    thread_pool=new ThreadPoll<http_conn>(thread_number,request_number);
}

/**
 * 利用init()函数初始化的量来初始化数据库连接池
 */
void WebServer::init_database()
{
    db_pool=MySqlPool::GetInstance();//获取实例
    db_pool->init("localhost",db_user,db_passwrod,db_name,mysql_conn_num,3306);//初始化
    users->init_users(db_pool);//初始化用户数据表
    http_conn::pool=db_pool;
}

/**
 * 利用init()函数初始化的量来初始化日志
 */
void WebServer::init_log()
{
    Log::get_instance()->init("Serverlog",2048,50000,false);
}

/**
 * 创建listenfd,epollfd;注册listenfd;初始化时间堆
 */
void WebServer::eventListen()
{
    //创建listenfd
    listenfd=socket(PF_INET,SOCK_STREAM,0);
    if(listenfd<0){//出错
        return;
    }

    //设置选项,关闭socket时,如果还有数据每发送完,那么仍等待发送完数据
    struct linger temp={1,10};
    setsockopt(listenfd,SOL_SOCKET,SO_LINGER,&temp,sizeof(temp));

    //构造listenfd的socket地址
    int ret=0;
    struct sockaddr_in address;
    bzero(&address,sizeof(address));
    
    
    address.sin_port=htons(port);

    //绑定listenfd的socket地址
    int flag = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret=bind(listenfd,(struct sockaddr*)&address,sizeof(address));
    assert(ret>=0);

    //创建监听队列
    ret=listen(listenfd,5);
    assert(ret>=0);

    //创建epoll
    epollfd=epoll_create(5);
    assert(epollfd!=-1);
    http_conn::epollfd=epollfd; //http_conn类的epollfd也用同样的epollfd(需要先注册写读就绪,然后注册写就绪)
    addfd(epollfd,listenfd,false,1);    //注册监听listenfd上的事件,采trigmode是1则用ET模式,必须一次性处理完当前全部的连接请求

    //下面还有关于定时机制的一些内容
    minheap_timer.init(TIMESLOT);   //初始化计时器的触发时间间隔。
    heap_timer::sig_pipefd=pipefd;  //使用统一的管道(这里是指针,所以可以在创建管道之前赋值)
    heap_timer::epollfd=epollfd;    //方便timer的回调函数处理

    //设置信号管道
    ret=socketpair(PF_UNIX,SOCK_STREAM,0,pipefd);//创建管道
    setnonblocking(pipefd[1]);//管道的写口只在信号处理程序中使用,要设置为非阻塞。
    addfd(epollfd,pipefd[0],false,0);//监听信号管道上的读就绪事件

    //设置信号处理函数
    minheap_timer.addsig(SIGPIPE,SIG_IGN);  //忽略sigpipe
    minheap_timer.addsig(SIGALRM,minheap_timer.sig_handler,false);  //sig_handler必须是静态成员函数
    minheap_timer.addsig(SIGTERM,minheap_timer.sig_handler,false);

}

void WebServer::eventLoop()
{
    bool timeout=false;
    bool server_stop=false;
    while(!server_stop){
        // printf("loop\n");
        int number=epoll_wait(epollfd,events,MAX_EVENTS,-1);
        int error=errno;
        if(number<0&&error!=EINTR)//出错
        {
            //记录日志
            // perror("epoll_wait failed");
            Log::get_instance()->write_log(ERROR,"WebServer.cpp","eventloop",127,"Error at function<epoll_wait()>");
            break;
        }

        for(int i=0;i<number;++i){
            int cur_sockfd=events[i].data.fd;
            
            if(cur_sockfd==listenfd)//发生事件的fd是listenfd,说明有新的连接到来
            {
                struct sockaddr_in address;
                socklen_t addrlength = sizeof(address);
                while(1){//listenfd采用了ET模式,需要一次性把所有连接都处理完
                    int connfd=accept(listenfd,(sockaddr*)&address,&addrlength);//接收连接
                    if(connfd==-1)//出错,则跳过本连接,处理后面的连接
                    {
                        if(errno==EAGAIN||errno==EWOULDBLOCK){
                            break;
                        }
                        else{
                            Log::get_instance()->write_log(ERROR,"WebServer.cpp","eventloop",145,"%s:errno is %d","accept error",errno);
                            break;
                        }
                       
                    }
                    if(http_conn::user_count>=MAX_FD)//当前连接数已经达到上限
                    {
                        send(connfd,"Internet server busy!",strlen("Internet server busy!"),0);//告知client繁忙的信息
                        close(connfd);
                        //日志记录
                        Log::get_instance()->write_log(INFO,"WebServer.cpp","eventloop",153,"Internet Server Busy at new connection %d",connfd);
                        break;//后面的都不必处理了
                    }

                    //一切正常时,初始化对应地http_conn数据
                    users[connfd].init(connfd,address,trig_mode,root_path);//该函数内部注册了connfd上的读就绪事件 (更新了user_count).设置为了非阻塞模式

                    //再创建timer,设置对应的client_data
                    users_timer[connfd].address=address;
                    users_timer[connfd].sockfd=connfd;
                    Timer*timer=new Timer(TIMESLOT);
                    timer->user=users_timer+connfd;
                    users_timer[connfd].timer=timer;
                    // printf("初次设置时,client %d的timer pointer:%p\nuser pointer:%p\n\n",connfd,timer,timer->user);
                    //timer加入到时间堆中并定时
                    minheap_timer.add_timer(timer);
                    alarm(3*TIMESLOT);

                    //下面需要补上日志
                    Log::get_instance()->write_log(INFO,"WebServer.cpp","eventloop",173,"New Timer and New Connection %d Ready",connfd);

                }
            }
            else if(events[i].events&(EPOLLRDHUP|EPOLLHUP|EPOLLERR))//出现错误或者是连接挂断
            {
                // printf("client%d出现错误或挂断\n\n",cur_sockfd);
                Timer*timer=users_timer[cur_sockfd].timer;//获取该sockfd对应地计时器
                timer->cb_func(&users_timer[cur_sockfd]);//调用回调函数,取消该sockfd注册的事件,关闭sockfd,http_conn的静态变量users_count减1
                if(!timer){
                    int ret=minheap_timer.del_timer(timer);//从时间堆中删除这个timer
                }
                //后面补充上日志记录
                Log::get_instance()->write_log(INFO,"WebServer.cpp","eventloop",187,"Error or Hub.Close connfd %d",cur_sockfd);
            }
            else if((cur_sockfd==pipefd[0])&&(events[i].events&EPOLLIN))//处理信号
            {
                char signals[1024];
                int ret=0;
                ret=recv(cur_sockfd,signals,1024,0);//读取数据,是一个信号值
                if(ret==-1)//出错
                {
                    //记录日志
                    Log::get_instance()->write_log(ERROR,"WebServer.cpp","eventloop",195,"Error at fucntion <recv>");
                    return;
                }
                else if(ret==0)
                {
                    //记录日志,没有收到信号值
                    Log::get_instance()->write_log(ERROR,"WebServer.cpp","eventloop",201,"Error at function <recv>:connection has already been closed!");
                    return;
                }
                else
                {
                    //逐个分析信号
                    for(int j=0;j<ret;++j){
                        switch (signals[j])
                        {
                        case SIGALRM:{
                            timeout=true;
                            break;
                        }
                        case SIGINT:{
                            server_stop=true;
                            break;
                        }
                        case SIGTERM:{
                            server_stop=true;
                            break;
                        }
                        default:
                            break;
                        }
                    }
                }
            }
            else if(events[i].events&EPOLLIN)//读就绪事件,说明有新的请求到来
            {
                Timer*timer=users_timer[cur_sockfd].timer;//获取计时器
                // printf("client %d获取请求之前 ,timer pointer:%p\nuser pointer:%p\n\n",timer->user->sockfd,timer,timer->user);
                if(users[cur_sockfd].read())//读数据到buffer成功。把数据读到buffer
                {
                    // printf("client %d获取请求\n",cur_sockfd);
                    Log::get_instance()->write_log(INFO,"WebServer.cpp","eventloop",233,"Read data of client %d",cur_sockfd);
                    thread_pool->append_request(&users[cur_sockfd]);//加入到任务队列中
                    //调整计时器时间
                    if(timer){
                        // printf("client %d调整计时器\n",cur_sockfd);
                        int ret=minheap_timer.del_timer(timer);//从堆里删除该计时器
                        // printf("删除timer的结果:%d\n",ret);
                        timer->set_expire(TIMESLOT);//重新计时
                        minheap_timer.add_timer(timer);//修改后的重新加入
                        alarm(3*TIMESLOT);
                        //下面补上日志记录
                        Log::get_instance()->write_log(INFO,"WebServer.cpp","eventloop",240,"Adjust timer of client %d",cur_sockfd);
                    }
                    
                }
                else{//失败
                    // printf("client %d获取请求失败\n",cur_sockfd);
                    Log::get_instance()->write_log(ERROR,"WebServer.cpp","eventloop",245,"client:%d Error at function <read()>",cur_sockfd);
                }
                // printf("读就绪事件处理完毕后,client %d的timer pointer:%p\nuser pointer:%p\n\n",cur_sockfd,timer,timer->user);
            }
            else if(events[i].events&EPOLLOUT)//写就绪事件
            {
                Log::get_instance()->write_log(INFO,"WebServer.cpp","eventloop",258,"Begin to write data of client %d",cur_sockfd);
                Timer*timer=users_timer[cur_sockfd].timer;//获取计时器
                // printf("client %d处理写就绪事件之前 ,timer pointer:%p\nuser pointer:%p\n\n",timer->user->sockfd,timer,timer->user);
                if(!users[cur_sockfd].write())//出错了(实际是指连接不持续),要关闭连接,删去计时器
                {
                    // printf("不保持连接client%d\n",cur_sockfd);
                    Log::get_instance()->write_log(ERROR,"WebServer.cpp","eventloop",245,"client:%d Conncetion doesn't keep alive, close this connection",cur_sockfd);
                    timer->cb_func(&users_timer[cur_sockfd]);//调用回调函数,取消该sockfd注册的事件,关闭sockfd
                    if(timer)
                    {
                        minheap_timer.del_timer(timer);//从时间堆中删除这个timer
                        delete timer;
                    }
                }
                else//没有出错,要调整计时器(连接活跃)
                {
                    // printf("保持连接client %d\n",cur_sockfd);
                    Log::get_instance()->write_log(INFO,"WebServer.cpp","eventloop",261,"Write data to client %d",cur_sockfd);
                    if(timer){
                        minheap_timer.del_timer(timer);//从堆里删除该计时器
                        timer->set_expire(TIMESLOT);//重新计时
                        minheap_timer.add_timer(timer);//修改后的重新加入
                        alarm(3*TIMESLOT);
                        //下面补上日志记录
                        Log::get_instance()->write_log(INFO,"WebServer.cpp","eventloop",267,"Adjust timer of client %d",cur_sockfd);
                    }
                }
                // printf("写就绪后,client %d的timer pointer:%p\nuser pointer:%p\n",cur_sockfd,timer,timer->user);
            }
        }

        if(timeout)
        {
            //统一处理超时的事件,从时间堆中删除这些计时器
            minheap_timer.tick();//处理超时事件
            timeout=false;
        }

    }
}