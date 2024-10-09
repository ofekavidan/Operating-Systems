#include "MapReduceFramework.h"
#include <vector>
#include <sys/types.h>
#include <pthread.h>
#include <atomic>
#include <iostream>
#include <algorithm>
#include "Barrier.h"
#include <queue>
#include <set>
#include <semaphore.h>

#define SYSTEM_ERROR_MSG "system error: "
#define MUTEX_DESTROY_ERR "mutex destroy error"
#define PTHREAD_CREATE_ERR "couldn't create thread"
#define ALLOCATION_ERR "allocation failed"
#define MUTEX_ERR "mutex error"
#define FAIL_CODE_EXIT 1
#define MASK_31_LSB 0x7fffffff

#define SHUFFLE_STAGE (2UL << 62)
#define REDUCE_STAGE (3UL << 62)
#define MAP_STAGE (1ULL << 62)
#define PERCENTAGE 100
#define COUNTER_TOTAL_SIZE 31
#define COUNTER_ALREADY_PROCESSED 31
#define STATE_BITS 62

using std::vector;
using std::set;
using std::priority_queue;

/**
 * comparator for the sort step
 */
struct sort_comparator {
    bool
    operator()(const IntermediatePair &first_pair, const IntermediatePair &second_pair) const {
        return *first_pair.first < *second_pair.first;
    }
};

/**
 * comparator for the set of distinct keys
 */
struct distinct_comparator {
    bool operator()(const K2 *first, const K2 *second) {
        return *second < *first;
    }
};


/** a declaration for struct JobContext, since there is a circularity in the two following structs */
struct JobContext;

/** struct that represents a thread context */
typedef struct ThreadContext {
    int tid;
    JobContext *job;
    IntermediateVec *mapOutput;

    //constructor
    ThreadContext(int thread_tid, JobContext *thread_job) : tid(thread_tid), job(thread_job) {
      mapOutput = new(std::nothrow) vector<IntermediatePair>(0);
      if (mapOutput == nullptr) {
        std::cout << SYSTEM_ERROR_MSG << ALLOCATION_ERR << std::endl;
        exit(FAIL_CODE_EXIT);
      }
    }

    //destructor
    ~ThreadContext() {
        delete mapOutput;
    }
} ThreadContext;

/** struct that represent a job context */
typedef struct JobContext {
    const MapReduceClient *client;
    int multiThreadLevel;
    const InputVec *input_vec;
    OutputVec *output_vec;

    //atomics
    std::atomic<u_int64_t> *state_counter;
    std::atomic_int map_counter{0};

    // data structures for intermediate phases
    vector<IntermediateVec> shuffleOutput;
    set<K2 *, distinct_comparator> *mapOutputDistinct{};

    unsigned long shuffle_total;
    bool is_waited;

    //mutexes
    pthread_mutex_t map_mutex;
    pthread_mutex_t reduce_mutex;
    pthread_mutex_t emit2_mutex;
    pthread_mutex_t get_job_mutex;
    pthread_mutex_t emit3_mutex;
    sem_t wait_for_job_mutex;

    // a barrier pointer
    Barrier *barrier;

    //threads
    vector<ThreadContext *> *threads_contexts;
    pthread_t *pthreads;

    //constructor
    JobContext(const MapReduceClient *job_client, int job_multiThreadLevel,
               const InputVec *job_input_vec, OutputVec *job_output_vec,
               std::atomic<uint64_t> *atomic)
            : client(job_client), multiThreadLevel(job_multiThreadLevel),
              input_vec(job_input_vec), output_vec(job_output_vec),
              state_counter(atomic), shuffle_total(0UL),
              map_mutex(PTHREAD_MUTEX_INITIALIZER), reduce_mutex(PTHREAD_MUTEX_INITIALIZER),
              emit2_mutex(PTHREAD_MUTEX_INITIALIZER),
              get_job_mutex(PTHREAD_MUTEX_INITIALIZER), emit3_mutex(PTHREAD_MUTEX_INITIALIZER) {
        if (sem_init(&wait_for_job_mutex, 0, 1) != 0) {
            std::cout << SYSTEM_ERROR_MSG << ALLOCATION_ERR << std::endl;
            exit(FAIL_CODE_EXIT);
        }
        threads_contexts = new(std::nothrow) vector<ThreadContext *>(multiThreadLevel);
        if (threads_contexts == nullptr) {
            std::cout << SYSTEM_ERROR_MSG << ALLOCATION_ERR << std::endl;
            exit(FAIL_CODE_EXIT);
        }
        barrier = new(std::nothrow) Barrier(multiThreadLevel);
        if (barrier == nullptr) {
            std::cout << SYSTEM_ERROR_MSG << ALLOCATION_ERR << std::endl;
            exit(FAIL_CODE_EXIT);
        }

        pthreads = new(std::nothrow) pthread_t[multiThreadLevel];
        if (pthreads == nullptr) {
            std::cout << SYSTEM_ERROR_MSG << ALLOCATION_ERR << std::endl;
            exit(FAIL_CODE_EXIT);
        }

        mapOutputDistinct = new(std::nothrow) set<K2 *, distinct_comparator>();
        if (mapOutputDistinct == nullptr) {
            std::cout << SYSTEM_ERROR_MSG << ALLOCATION_ERR << std::endl;
            exit(FAIL_CODE_EXIT);
        }
        is_waited = false;
    }

    //destructor
    ~JobContext() {
        for (int i = 0; i < (int) threads_contexts->size(); i++) {
            delete threads_contexts->at(i);
        }
        delete threads_contexts;
        delete[] pthreads;
        delete mapOutputDistinct;
        delete state_counter;
        delete barrier;
    }
} JobContext;

/**
 * map step for each thread
 * @param threadContext thread context
 */
void mapStep(ThreadContext *threadContext) {
    while (true) {
        int cur_ind = threadContext->job->map_counter++;
        if (cur_ind < (int) threadContext->job->input_vec->size()) {
            InputPair cur_pair = threadContext->job->input_vec->at(cur_ind);
            if (pthread_mutex_lock(&threadContext->job->map_mutex) != 0) {
                std::cout << SYSTEM_ERROR_MSG << MUTEX_ERR << std::endl;
                exit(FAIL_CODE_EXIT);
            }
            threadContext->job->client->map(cur_pair.first, cur_pair.second, threadContext);
            (*(threadContext->job->state_counter)).fetch_add(1);
            if (pthread_mutex_unlock(&threadContext->job->map_mutex) != 0) {
                std::cout << SYSTEM_ERROR_MSG << MUTEX_ERR << std::endl;
                exit(FAIL_CODE_EXIT);
            }
        } else {
            break;
        }
    }
}

/**
 * Called from the clients map, pushes pairs to the intermediate vec
 * @param key the key to push
 * @param value the value to push
 * @param context the thread context
 */
void emit2(K2 *key, V2 *value, void *context) {
    ThreadContext *thread_context = static_cast<ThreadContext *> (context);

    if (pthread_mutex_lock(&thread_context->job->emit2_mutex) != 0) {
        std::cout << SYSTEM_ERROR_MSG << MUTEX_ERR << std::endl;
        exit(FAIL_CODE_EXIT);
    }
    thread_context->job->mapOutputDistinct->insert(key);
    thread_context->mapOutput->push_back(IntermediatePair(key, value));
    thread_context->job->shuffle_total++;
    if (pthread_mutex_unlock(&thread_context->job->emit2_mutex) != 0) {
        std::cout << SYSTEM_ERROR_MSG << MUTEX_ERR << std::endl;
        exit(FAIL_CODE_EXIT);
    }
}

/**
 * called from the clients reduce, pushes pairs to the output vec
 * @param key the key to push
 * @param value the value to push
 * @param context the thread context
 */
void emit3(K3 *key, V3 *value, void *context) {
    ThreadContext *thread_context = static_cast<ThreadContext *> (context);

    if (pthread_mutex_lock(&thread_context->job->emit3_mutex) != 0) {
        std::cout << SYSTEM_ERROR_MSG << MUTEX_ERR << std::endl;
        exit(FAIL_CODE_EXIT);
    }
    thread_context->job->output_vec->push_back(OutputPair(key, value));
    if (pthread_mutex_unlock(&thread_context->job->emit3_mutex) != 0) {
        std::cout << SYSTEM_ERROR_MSG << MUTEX_ERR << std::endl;
        exit(FAIL_CODE_EXIT);
    }
}

/**
 * sort step for each thread
 * @param threadContext thread context
 */
void sortStep(ThreadContext *threadContext) {
    std::sort(threadContext->mapOutput->begin(), threadContext->mapOutput->end(),
              sort_comparator());
}

/**
 * shuffle step, only thread 0 runs here
 * @param threadContext thread context
 */
void shuffleStep(ThreadContext *threadContext) {
    // for each max key in mapOutputDistinct,build a new vector
    for (auto cur_max_key: *threadContext->job->mapOutputDistinct) {
        IntermediateVec max_key_vec;

        // search in al of the threads for the max key
        for (int i = 0; i < threadContext->job->multiThreadLevel; i++) {
            auto cur_vec = (*(threadContext->job->threads_contexts))[i]->mapOutput;

            if (cur_vec == NULL || cur_vec->empty() ||
                (!cur_vec->empty() && (*((*cur_vec)[cur_vec->size() - 1].first)) < *cur_max_key)) {
                continue;
            } else {
                //if the end of the vec is not smaller than the cur_max_key.
                // it must be equal to it from trichotomy (because it's the max key).
                while (!cur_vec->empty()) {
                    if (!((*((*cur_vec)[cur_vec->size() - 1].first)) < *cur_max_key)) {
                        max_key_vec.push_back(IntermediatePair((*cur_vec)[cur_vec->size() - 1].first,
                                                               (*cur_vec)[cur_vec->size() - 1].second));
                        cur_vec->pop_back();
                        threadContext->job->state_counter->fetch_add(1);
                    } else {
                        break;
                    }
                }
            }
        }
        threadContext->job->shuffleOutput.push_back(max_key_vec);
    }
}

/**
 * reduce step for each thread
 * @param threadContext thread context
 */
void reduceStep(ThreadContext *threadContext) {
    while (true) {
        if (pthread_mutex_lock(&threadContext->job->reduce_mutex) != 0) {
            std::cout << SYSTEM_ERROR_MSG << MUTEX_ERR << std::endl;
            exit(FAIL_CODE_EXIT);
        }
        if (threadContext->job->shuffleOutput.size() <= 0) {
            if (pthread_mutex_unlock(&threadContext->job->reduce_mutex) != 0) {
                std::cout << SYSTEM_ERROR_MSG << MUTEX_ERR << std::endl;
                exit(FAIL_CODE_EXIT);
            }
            break;
        }
        auto cur_vec = (threadContext->job->shuffleOutput[threadContext->job->shuffleOutput.size() - 1]);
        threadContext->job->shuffleOutput.pop_back();
        if (pthread_mutex_unlock(&threadContext->job->reduce_mutex) != 0) {
            std::cout << SYSTEM_ERROR_MSG << MUTEX_ERR << std::endl;
            exit(FAIL_CODE_EXIT);
        }
        threadContext->job->client->reduce(&cur_vec, threadContext);
        threadContext->job->state_counter->fetch_add(cur_vec.size());
    }
}


/**
 * the entry point for each thread- the whole map reduce algorithm
 * @param threadContext_input the thread context input
 * @return nullptr
 */
void *mapReduceAlgo(void *threadContext_input) {
    ThreadContext *thread_context = (ThreadContext *) (threadContext_input);
    mapStep(thread_context);
    sortStep(thread_context);
    //barrier
    thread_context->job->barrier->barrier();
    //only thread 0 does the shuffle
    if (thread_context->tid == 0) {
        *(thread_context->job->state_counter) = SHUFFLE_STAGE;
        thread_context->job->state_counter->fetch_add((thread_context->job->shuffle_total) << COUNTER_TOTAL_SIZE);
        shuffleStep(thread_context);
        *(thread_context->job->state_counter) = REDUCE_STAGE;
        thread_context->job->state_counter->fetch_add(thread_context->job->shuffle_total << COUNTER_TOTAL_SIZE);
    }
    thread_context->job->barrier->barrier();
    reduceStep(thread_context);

    return nullptr;
}

/**
 * the function that starts the map reduce job
 * @param client client
 * @param inputVec input vector
 * @param outputVec output vector
 * @param multiThreadLevel number of threads
 * @return the job
 */
JobHandle
startMapReduceJob(const MapReduceClient &client, const InputVec &inputVec, OutputVec &outputVec,
                  int multiThreadLevel) {
    std::atomic<uint64_t> *atomic_counter = new(std::nothrow) std::atomic<uint64_t>(0);
    if (atomic_counter == nullptr) {
        std::cout << SYSTEM_ERROR_MSG << ALLOCATION_ERR << std::endl;
        exit(FAIL_CODE_EXIT);
    }
    JobContext *job_context = new(std::nothrow) JobContext(&client, multiThreadLevel, &inputVec,
                                                           &outputVec, atomic_counter);
    if (job_context == nullptr) {

        std::cout << SYSTEM_ERROR_MSG << ALLOCATION_ERR << std::endl;
        exit(FAIL_CODE_EXIT);
    }

    (*(job_context->state_counter)) = MAP_STAGE;
    (*(job_context->state_counter)).fetch_add(inputVec.size() << COUNTER_TOTAL_SIZE);

    for (int i = 0; i < multiThreadLevel; i++) {
        ((job_context->threads_contexts))->at(i) = new(std::nothrow) ThreadContext(i, job_context);
        if (((job_context->threads_contexts))->at(i) == nullptr) {
            std::cout << SYSTEM_ERROR_MSG << ALLOCATION_ERR << std::endl;
            exit(FAIL_CODE_EXIT);
        }
        if (pthread_create(job_context->pthreads + i, NULL, mapReduceAlgo,
                           (*(job_context->threads_contexts))[i])) {
            std::cout << SYSTEM_ERROR_MSG << PTHREAD_CREATE_ERR << std::endl;
            exit(FAIL_CODE_EXIT);
        }
    }
    return static_cast<JobHandle>(job_context);
}

/**
 * terminates the threads of the job
 * @param job the job
 */
void waitForJob(JobHandle job) {
    JobContext *job_context = static_cast<JobContext *>(job);
    if (sem_wait(&(job_context->wait_for_job_mutex)) != 0) {
        std::cout << SYSTEM_ERROR_MSG << MUTEX_ERR << std::endl;
        exit(FAIL_CODE_EXIT);
    }
    if (job_context->is_waited) {
        return;
    }
    job_context->is_waited = true;
    for (int i = 0; i < job_context->multiThreadLevel; ++i) {
        pthread_join(job_context->pthreads[i], NULL);
    }
    if (sem_post(&job_context->wait_for_job_mutex) != 0) {
        std::cout << SYSTEM_ERROR_MSG << MUTEX_ERR << std::endl;
        exit(FAIL_CODE_EXIT);
    }
}


/**
 * updates the job state
 * @param job the job
 * @param state the state
 */
void getJobState(JobHandle job, JobState *state) {
    JobContext *job_context = static_cast<JobContext *>(job);
    if (pthread_mutex_lock(&job_context->get_job_mutex) != 0) {
        std::cout << SYSTEM_ERROR_MSG << MUTEX_ERR << std::endl;
        exit(FAIL_CODE_EXIT);
    }
    auto curr_state_counter = job_context->state_counter->load();
    auto num_state = curr_state_counter >> STATE_BITS;
    unsigned long already_processed;
    unsigned long total;
    if (num_state == UNDEFINED_STAGE) {
        state->stage = UNDEFINED_STAGE;
        state->percentage = 0;
    } else {
        state->stage = static_cast<stage_t>(num_state);
        already_processed = (curr_state_counter & MASK_31_LSB);
        total = ((curr_state_counter >> COUNTER_ALREADY_PROCESSED) & MASK_31_LSB);
        state->percentage =
                (float) ((float) already_processed / (float) total) * PERCENTAGE;
    }
    if (pthread_mutex_unlock(&job_context->get_job_mutex) != 0) {
        std::cout << SYSTEM_ERROR_MSG << MUTEX_ERR << std::endl;
        exit(FAIL_CODE_EXIT);
    }
}

/**
 * deletes all the job resources
 * @param job
 */
void closeJobHandle(JobHandle job) {
    JobContext *job_context = static_cast<JobContext *>(job);
    waitForJob(job);
    if (pthread_mutex_destroy(&job_context->map_mutex) != 0) {
        std::cout << SYSTEM_ERROR_MSG << MUTEX_DESTROY_ERR << std::endl;
        exit(FAIL_CODE_EXIT);
    }
    if (pthread_mutex_destroy(&job_context->reduce_mutex) != 0) {
        std::cout << SYSTEM_ERROR_MSG << MUTEX_DESTROY_ERR << std::endl;
        exit(FAIL_CODE_EXIT);
    }
    if (pthread_mutex_destroy(&job_context->emit2_mutex) != 0) {
        std::cout << SYSTEM_ERROR_MSG << MUTEX_DESTROY_ERR << std::endl;
        exit(FAIL_CODE_EXIT);
    }
    if (pthread_mutex_destroy(&job_context->get_job_mutex) != 0) {
        std::cout << SYSTEM_ERROR_MSG << MUTEX_DESTROY_ERR << std::endl;
        exit(FAIL_CODE_EXIT);
    }
    if (pthread_mutex_destroy(&job_context->emit3_mutex) != 0) {
        std::cout << SYSTEM_ERROR_MSG << MUTEX_DESTROY_ERR << std::endl;
        exit(FAIL_CODE_EXIT);
    }
    if (sem_destroy(&job_context->wait_for_job_mutex) != 0) {
        std::cout << SYSTEM_ERROR_MSG << MUTEX_DESTROY_ERR << std::endl;
        exit(FAIL_CODE_EXIT);
    }
    delete job_context;
}