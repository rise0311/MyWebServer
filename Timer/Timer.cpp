#include"Timer.h"
#include<vector>
#include<signal.h>
using std::vector;

int* heap_timer::sig_pipefd=nullptr;
int heap_timer::epollfd=-1;

/**
 * 向时间堆中添加计时器Timer
 */
bool heap_timer::add_timer(Timer* timer)
{
    if(!timer){
        return false;
    }
    // printf("add_timer时,client:%d\nuser pointer:%p\ntimer pointer:%p\n\n",timer->user->sockfd,timer->user,timer);
    minheap.push(timer);
    return true;
}

/**
 * 从时间堆中删除计时器timer
 */
bool heap_timer::del_timer(Timer*timer){
    vector<Timer*>temp;
    Timer*elem;
    while(!minheap.empty())
    {
        elem=minheap.top();
        minheap.pop();
        if(elem == timer){//找到目标元素
            for(Timer*t:temp) minheap.push(t);//恢复
            return true;
        }
        else{
            temp.push_back(elem);
        }
    }
    //出了循环,timer不存在于minheap中  -->  false
    for(Timer*t:temp) add_timer(t);//恢复
    return false;
}

/**
 * 信号处理函数:把信号值写到管道
 */
void heap_timer::sig_handler(int sig){
     //为保证函数的可重入性，保留原来的errno
     int save_errno = errno;
     int msg = sig;
     send(sig_pipefd[1], (char *)&msg, 1, 0);
     errno = save_errno;
}

/**
 * 设置信号sig的处理函数为handler
 */
void heap_timer::addsig(int sig,void(handler)(int),bool restart){
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

/**
 * tick函数,负责检查超时,并让超时计时器调用cb_function(),并从时间堆中删除超时的计时器
 * @bug:超时函数执行时,timer->user出现Segmentation fault(core dumped)
 */
void heap_timer::tick(){
    if(minheap.empty()){
        return;
    }
    time_t cur_time=time(NULL);
    Timer*timer=nullptr;
    while(true)
    {
        timer=get_top();
        if(timer==nullptr||timer->user==nullptr||cur_time<timer->get_expire()) //目前堆里的最小到期时间>当前时间
        {
            // printf("退出tick循环");
            break;
        }
        //否则,这个计时器就已经超时
        if (timer->user == nullptr) {
            // printf("Invalid user pointer! Skipping...\n");
            return;
        }
        // printf("client %d ,timer pointer:%p\nuser pointer:%p\n\n",timer->user->sockfd,timer,timer->user);//这里的user存在问题
        // fflush(stdout);
        // Log::get_instance()->write_log(INFO,"Timer.cpp","tick",83,"Connection %d time out!",timer->user->sockfd);
        Log::get_instance()->write_log(INFO,"Timer.cpp","tick()",95,"client:%d Time out! Close the connection\n",timer->user->sockfd);
        timer->cb_func(timer->user);    //调用回调函数处理
        minheap.pop();//删去队头
        // timer->user->timer=nullptr;
        delete timer;
    }
    
}

/**
 * 计时器超时时调用的回调函数。
 * 注销该连接注册的事件,关闭连接,连接总数减1.
 */
void cb_function(client_data*user_data){//超时后关闭连接,需要删除该连接在epollfd上注册的事件
    epoll_ctl(heap_timer::epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);//取消注册的事件
    assert(user_data);
    close(user_data->sockfd);   //关闭连接
    http_conn::user_count--;    //更新http链接数
    // delete user_data->timer;
    // user_data->timer=nullptr;
}