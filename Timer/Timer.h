//设计一个计时器,专门用于处理HTTP请求中的超时情况。为了统一事件源,通过SIGALRM信号来实现定时器

#ifndef TIMER_H
#define TIMER_H

#include<time.h>
#include<queue>
#include<vector>
#include<sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include<errno.h>
#include<sys/epoll.h>
#include<assert.h>
#include<unistd.h>
#include"../http/HTTP.h"
#include<stdio.h>
/**
 * client_data结构体,保存client的相关数据:client地址、负责与client连接的sockfd、该连接的计时器
 */
class Timer;
struct client_data
{
public:
    sockaddr_in address;
    int sockfd;
    Timer *timer;
};

/**
 * 一个回调函数,当计时器超时时,调用这个回调函数
 */
void cb_function(client_data *user_data);

/**
 * 首先实现一个计时器类
 */
class Timer
{
public:
    Timer(int delay)
    {
        expire=time(NULL)+3*delay;
        cb_func=cb_function;
        user=nullptr;
    }
    ~Timer(){
        //printf("释放Timer %d\n",user->sockfd);
        }
    time_t get_expire(){return expire;}
    void set_expire(int delay){expire=time(NULL)+3*delay;}
    client_data* user;//该计时器对应地连接的数据
    void (* cb_func)(client_data *);    //回调函数
private:
    time_t expire;  //到期时间
    
};

/**
 * 实现一个比较器
 */
struct TimerCompare {
    bool operator()(Timer* t1, Timer* t2) {
        return t1->get_expire() > t2->get_expire();
    }
};

/**
 * 然后实现一个基于时间堆的计时器容器类
 */
class heap_timer
{
public:
    heap_timer(){}
    void init(int time_slot){timesolt=time_slot;}
    bool add_timer(Timer*timer);
    bool del_timer(Timer* timer);
    Timer* get_top()const{
        if(minheap.empty()){
            return nullptr;
        }
        return minheap.top();//top()方法,在容器为空时导致不确定行为,必须先检查容器是否为空
    }
    void tick();
    
    static void sig_handler(int sig);//还需要补充信号处理函数
    void addsig(int sig,void(handler)(int),bool restart=true);
    static int*sig_pipefd;  //用于信号处理的管道
    static int epollfd;
private:
    std::priority_queue<Timer*,std::vector<Timer*>,TimerCompare> minheap;
    int timesolt;
};

#endif