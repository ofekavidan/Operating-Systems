//
// Created by liora_ves on 4/3/23.
//

#include "Thread.h"

address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
                 "rol    $0x9,%0\n"
            : "=g" (ret)
            : "0" (addr));
    return ret;
}


Thread::Thread(int tid,states state, thread_entry_point entry_point): tid(tid), state(state), stack(new int[STACK_SIZE]) {
    address_t sp = (address_t) stack + STACK_SIZE - sizeof(address_t);
    address_t pc = (address_t) entry_point;
    (env[tid]->__jmpbuf)[JB_SP] = translate_address(sp);
    (env[tid]->__jmpbuf)[JB_PC] = translate_address(pc);
    sigemptyset(&env[tid]->__saved_mask);
}

int Thread::get_tid() {
    return tid;
}

states Thread::get_state() {
    return state;
}
void Thread::set_state(states state) {
    this->state=state;
}

int *Thread::get_stack() {
    return stack;
}
thread_entry_point Thread::get_entry_point() {
    return entry_point;
}