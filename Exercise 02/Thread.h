//
// Created by liora_ves on 4/3/23.
//

#ifndef UTHREADS_H_THREAD_H
#define UTHREADS_H_THREAD_H

#include <csetjmp>
#include "uthreads.h"
#include <signal.h>

/** enum for the possible states */
typedef enum states {
    READY, RUNNING, BLOCKED
} states;

typedef unsigned long address_t;

// todo : ask why 4 5.
#define JB_SP 4
#define JB_PC 5
sigjmp_buf env[2];
typedef void (*thread_entry_point)(void);

/** A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr);


class Thread {
private:
    int tid;
    states state;
    int *stack;
    thread_entry_point entry_point;

public:
    Thread(int tid,states state,thread_entry_point entry_point);

    int get_tid();
    states get_state();
    void set_state(states state);
    int* get_stack();
    thread_entry_point get_entry_point();

};


#endif //UTHREADS_H_THREAD_H
