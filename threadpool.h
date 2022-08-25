//线程池
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include <exception>
#include <cstdio>

#include "locker.h"

//线程池类，定义成模板类是为了代码的复用，模板参数T是任务类
template<typename T>
class threadpool{
public:
    threadpool(int thread_number = 8, int max_request = 10000);

    ~threadpool();

    //向工作队列中添加任务
    bool append(T * request);

private:
    static void *worker(void * arg);

    void run();

private:
    //线程的数量
    int m_thread_number;

    //线程池数组，大小为m_thread_number
    pthread_t * m_threads;

    //请求队列中最多允许的，等待处理的请求数量
    int m_max_requests;

    //请求队列
    std::list<T * > m_workqueue;

    //互斥锁
    locker m_queuelocker;

    //信号量，用来判断是否有任务需要处理
    sem m_queuestat;

    //是否结束线程
    bool m_stop;
};

//冒号后面对成员进行初始化
template<typename T>
threadpool<T>::threadpool(int thread_number, int max_request) : m_thread_number(thread_number), m_max_requests(max_request), m_stop(false), m_threads(NULL){
    if((thread_number <= 0) || (max_request <= 0)){
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];
    if(!m_threads){
        throw std::exception();
    }

    //创建thread_number个线程，并将它们设置为线程脱离
    //让子线程自动释放资源
    for(int i = 0; i < thread_number; ++i){
        printf("create the %dth thread\n", i);

        if(pthread_create(m_threads + i, NULL, worker, this) != 0){
            delete[] m_threads;
            throw std::exception();
        }

        if(pthread_detach(m_threads[i])){
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool(){
    delete[] m_threads;
    m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T * request){
    m_queuelocker.lock();
    //防止工作队列超过最大数量
    if(m_workqueue.size() > m_max_requests){
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    m_queuelocker.unlock();

    //通过信号量判断是否堵塞还是执行
    m_queuestat.post();
    return true;
}

template<typename T>
//静态类型无法访问类中的成员变量，需要通过指针传递
void *threadpool<T>::worker(void * arg){
    threadpool * pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run(){
    while(!m_stop){
        //信号量有值就不阻塞，无值就阻塞
        m_queuestat.wait();
        m_queuelocker.lock();
        //判断工作队列是否有数据
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if(!request){
            continue;
        }
        
        request->process();
    }
}

#endif