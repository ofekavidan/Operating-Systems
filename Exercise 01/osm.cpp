#include "osm.h"
#include <sys/time.h>
#include <cmath>
#include "iostream"

/** this function does nothing. */
void empty_function() {

}

double osm_operation_time(unsigned int iterations) {
    if (iterations == 0) {
        return -1;
    }
    struct timeval start_time;
    struct timeval end_time;

    int res = 0;
    int a = 1;
    int b = 1;
    if (res > 4) { res += 1; }
    gettimeofday(&start_time, nullptr);
    for (unsigned int i = 0; i < iterations; i += 5) {
        res = a + b;
        res = a + b;
        res = a + b;
        res = a + b;
        res = a + b;
    }

    gettimeofday(&end_time, nullptr);

    double sec_diff = 0, micro_sec_diff = 0, final_res;
    sec_diff += (end_time.tv_sec - start_time.tv_sec);
    sec_diff = sec_diff * 1000000;
    micro_sec_diff += (end_time.tv_usec - start_time.tv_usec);
    final_res = (sec_diff + micro_sec_diff) * 1000 / (iterations);
    return final_res;
}

double osm_function_time(unsigned int iterations) {
    if (iterations == 0) {
        return -1;
    }
    struct timeval start_time;
    struct timeval end_time;

    gettimeofday(&start_time, nullptr);
    for (unsigned int i = 0; i < iterations; i += 5) {
        empty_function();
        empty_function();
        empty_function();
        empty_function();
        empty_function();
    }

    gettimeofday(&end_time, nullptr);

    double sec_diff = 0, micro_sec_diff = 0, final_res;
    sec_diff += (end_time.tv_sec - start_time.tv_sec);
    sec_diff = sec_diff * 1000000;
    micro_sec_diff += (end_time.tv_usec - start_time.tv_usec);
    final_res = (sec_diff + micro_sec_diff) * 1000 / (iterations);
    return final_res;

}

double osm_syscall_time(unsigned int iterations) {
    if (iterations == 0) {
        return -1;
    }
    struct timeval start_time;
    struct timeval end_time;
    gettimeofday(&start_time, nullptr);

    for (unsigned int i = 0; i < iterations; i += 5) {
        OSM_NULLSYSCALL;
        OSM_NULLSYSCALL;
        OSM_NULLSYSCALL;
        OSM_NULLSYSCALL;
        OSM_NULLSYSCALL;
    }
    gettimeofday(&end_time, nullptr);

    double sec_diff = 0, micro_sec_diff = 0, final_res;
    sec_diff += (end_time.tv_sec - start_time.tv_sec);
    sec_diff = sec_diff * 1000000;
    micro_sec_diff += (end_time.tv_usec - start_time.tv_usec);
    final_res = (sec_diff + micro_sec_diff) * 1000 / (iterations);
    return final_res;
}
