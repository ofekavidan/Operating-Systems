//
// Created by liora_ves on 4/3/23.
//
#include "uthreads.h"
#include <iostream>
#include <algorithm>
#include <queue>
#include "Thread.h"
#include <map>

using std::map;
using std::queue;

#define SYSTEM_ERROR_MSG "system error:"
#define NON_POSITIVE_QUANTUM_ERR " non-positive quantum_usecs"
#define NULL_ENTRY_INPUT " null entry point input"
#define TID_NOT_FOUND " tid not found"

queue<Thread*> ready_queue;
static int quantum=0;

queue<int> next_tid({0});
map<int, Thread*> threads_map;

void remove_thread_from_ready_queue(int tid)
{
    queue<Thread*> temp_ready_queue;

    while(!ready_queue.empty())
    {
        if(ready_queue.front()->get_tid() == tid)
        {
            delete(ready_queue.front());
            ready_queue.pop();
        }
        else {
            temp_ready_queue.push(ready_queue.front());
            ready_queue.pop();
        }
    }
    while(!temp_ready_queue.empty())
    {
        ready_queue.push(temp_ready_queue.front());
        ready_queue.pop();
    }
}

int uthread_init(int quantum_usecs){
    if (quantum_usecs <= 0){
        std::cerr<< SYSTEM_ERROR_MSG << NON_POSITIVE_QUANTUM_ERR << std::endl;
        return -1;
    }
    quantum = quantum_usecs;
    auto* main_thread= new Thread(next_tid.front(), RUNNING, nullptr);
    threads_map[next_tid.front()]=main_thread;
    //todo: make this a function?
    int prev= next_tid.front();
    next_tid.pop();
    next_tid.push(prev+1);
    return 0;
}

int uthread_spawn(thread_entry_point entry_point) {
    if(entry_point == nullptr){
        std::cerr<< SYSTEM_ERROR_MSG << NULL_ENTRY_INPUT<< std::endl;
        return -1;
    }
    if(ready_queue.size() >= MAX_THREAD_NUM){
        return -1; // todo: check if error is needed
    }

    auto* new_thread= new Thread(next_tid.front(),READY,entry_point);

    threads_map[next_tid.front()]=new_thread;
    //todo: make this a function?
    int prev= next_tid.front();
    next_tid.pop();
    next_tid.push(prev+1);

    ready_queue.push(new_thread);

    return 0;
}

int uthread_terminate(int tid){
    if(threads_map.count(tid)==0){
        std::cerr<< SYSTEM_ERROR_MSG << TID_NOT_FOUND << std::endl;
        return -1;
    }
    if (tid ==0){
        //todo:check(how to delete all the threads before we exit the program)
        for(auto cur_pair_int_thread: threads_map){
            delete(cur_pair_int_thread.second);
        }
        exit(0);
    }

    //if tid not 0

    // if tid is running
    if(threads_map.at(tid)->get_state()==RUNNING){
        delete(threads_map.at(tid));
        threads_map.erase(tid);
        Thread* turn_ready_to_running = ready_queue.front();
        ready_queue.pop();
        turn_ready_to_running->set_state(RUNNING);
    }
    // if tid is ready
    if(threads_map.at(tid)->get_state()==READY){
        delete(threads_map.at(tid));
        remove_thread_from_ready_queue(tid);
    }
    // if tid is blocked
    if(threads_map.at(tid)->get_state()==BLOCKED){
        delete(threads_map.at(tid));
        threads_map.erase(tid);
    }

    return 0;
}

/**
 * @brief Blocks the thread with ID tid. The thread may be resumed later using uthread_resume.
 *
 * If no thread with ID tid exists it is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision should be made. Blocking a thread in
 * BLOCKED state has no effect and is not considered an error.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_block(int tid)
{
    //todo: consider QUANTUM !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    if(threads_map.count(tid)==0){
        std::cerr<< SYSTEM_ERROR_MSG << TID_NOT_FOUND << std::endl;
        return -1;
    }
    if (tid ==0){
        //todo:check(how to delete all the threads before we exit the program)
        for(auto cur_pair_int_thread: threads_map){
            delete(cur_pair_int_thread.second);
        }
        exit(0);
    }
    if(threads_map.at(tid)->get_state()==BLOCKED){
        return 0;
    }
    if(threads_map.at(tid)->get_state()==RUNNING){
        threads_map.at(tid)->set_state(BLOCKED);

        // remove the new running thread from ready queue
        int ready_to_running_tid = ready_queue.front()->get_tid();

        remove_thread_from_ready_queue(ready_to_running_tid);
        threads_map[ready_to_running_tid]->set_state(RUNNING);



    }
    if(threads_map.at(tid)->get_state()==READY) {
        threads_map.at(tid)->set_state(BLOCKED);
        remove_thread_from_ready_queue(tid);
    }

}
