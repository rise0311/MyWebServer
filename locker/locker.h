#ifndef LOCKER_H
#define LOCKER_H

#include<iostream>
#include<exception>
#include<semaphore.h>

//信号量类
class Sem{
public:
    Sem(){
        if(sem_init(&sem,0,0)!=0){
            throw std::exception();
        }
    }

    Sem(int num){
        if(sem_init(&sem,0,num)!=0){
            throw std::exception();
        }
    }
    ~Sem(){
        sem_destroy(&sem);
    }
    bool wait(){
        return sem_wait(&sem)==0;
    }
    bool post(){
        return sem_post(&sem)==0;
    }
private:
    sem_t sem;
};

// 互斥锁类:这里利用一个信号量来实现
class Locker{
public:
    Locker(){
        if(sem_init(&sem,0,1)!=0){
            throw std::exception();
        }
    }
    ~Locker(){
        sem_destroy(&sem);
    }
    bool lock(){
        return sem_wait(&sem)==0;
    }
    bool unlock(){
        return sem_post(&sem)==0;
    }
private:
    sem_t sem;
};
#endif
