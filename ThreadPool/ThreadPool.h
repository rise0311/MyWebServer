//本文件要实现一个高效的、半同步/半反应堆线程池

#ifndef THREADPOLL_H
#define THREADPOLL_H

#include<pthread.h>
#include <list>
#include"../locker/locker.h"

using std::list;
using std::exception;

/**
 * 首先实现一个线程池模板类
 */
template <typename T>   // T是模版参数,这里是request的类型 
class ThreadPoll{
public:
    ThreadPoll(int max_thread_number,int max_requests=10000);
    ~ThreadPoll();
    bool append_request(T*request);
    static int pipefd[2];   //管道
private:
    static void*worker(void*arg);   // 设计为静态成员函数,而且返回值类型为void*类型,且参数是一个void*指针,是因为pthread_create()的参数要求。
    void run();
private:
    pthread_t* threads;  //线程池容器,用于保存线程池中的线程信息(主要是Tid)
    int max_requests;   //请求队列中允许的最大请求数
    int max_threads;    //线程池中允许的最多线程数
    Locker lock;        //一个锁，用于保证请求队列在任意时刻最多只有一个线程访问
    Sem sem_requests;   // 一个信号量，值是请求队列中的请求数
    list<T*> request_queue;   // 一个请求队列,成员是指向请求的指针(使用指针,可以节省空间)
};

/**
 * 线程池类的构造函数。创建max_thread_number个线程.
 * 线程属性设置为脱离线程,无需父线程调用pthread_join()回收,线程在退出时自动回收
 */
template<typename T>
ThreadPoll<T>::ThreadPoll(int max_thread_number,int max_requests): max_threads(max_thread_number),
        max_requests(max_requests),threads(NULL)
{   
    // 参数范围不正确,则抛出异常
    if(max_threads<=0 || max_requests<=0)
    {
        throw exception();
    }
    
    // 创建线程Tid数组,如果创建失败则抛出异常
    threads=new pthread_t[max_threads];
    if(!threads)
    {   
        throw exception();
    }

    // 逐个创建线程
    for(int i=0;i<max_threads;++i)
    {   
        // 创建线程返回值不为0,则创建失败,抛出异常
        if(pthread_create(threads+i,NULL,worker,this)!=0)
        {
            delete []threads;   //构造函数执行过程中异常,不会调用析构函数。需要手动释放
            throw exception();
        }

        //设置为脱离线程,成功时返回0
        if(pthread_detach(threads[i]))
        {
            delete []threads;
            throw exception();
        }
    }
}

//实现析构函数
template<typename T>
ThreadPoll<T>::~ThreadPoll()
{
    delete []threads;
}

/**
 * 线程要执行的worker函数
 */
template<typename T>
void* ThreadPoll<T>:: worker(void*arg)  
{
    ThreadPoll*poll=(ThreadPoll*)arg;
    poll->run();
    return poll;
}

/**
 * 实现append成员函数,把请求添加到请求队列里
 */
template<typename T>
bool ThreadPoll<T>::append_request(T*request)
{
    lock.lock();//上锁
    // 如果请求队列满了
    if(request_queue.size()>=max_requests){
        lock.unlock();
        return false;
    }
    // 加入队列
    request_queue.push_back(request);
    lock.unlock();  //解锁
    sem_requests.post();//通知，请求队列里请求数增加+1
    return true;
}

/**
 * 实现run成员函数  采用的事件处理模式是以同步I/O模拟Proactor模式,得到的request就是request本身,而不是所在的socket
 */
template<typename T>
void ThreadPoll<T>::run()
{
    while(true)
    {
        sem_requests.wait();    //先等待请求
        lock.lock();    //加锁
        T*request=request_queue.front();
        request_queue.pop_front();
        lock.unlock();
        if(!request)
        {
            continue;
        }
        //接下来,处理这个请求   
        request->process();   //调process的前提是已经把数据读到了buffer中。我用的是同步I/O模拟proactor,在把请求加入队列之前就已经读入到buffer
    }
}
/**
 * 半同步/半反应堆模式
 * 只有一个异步线程————即主线程。主线程监听listenfd,收到连接请求之后建立新的连接;主线程监听connfd,connfd上监听到事件说明有读写事件。
 * 所有的工作线程都是同步线程，负责处理读写事件(首先解析读取到的请求，然后生成response)
 */

#endif