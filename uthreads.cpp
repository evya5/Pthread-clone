#include "uthreads.h"
#include <setjmp.h>
#include <sys/time.h>
#include <iostream>
#include <stdio.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <list>

//ADDITION
#define SECOND 1000000

#define NOT (0)
#define TRUE (1)
#define OK 0
#define BUFFER_VALUE 1
#define ERROR_VALUE (-1)

#define MAIN_THREAD_ID 0

#define LIB_ERROR_MSG "thread library error: "
#define SYS_ERROR_MSG "system error: "

#define INVALID_QUANTUM_USECS "invalid quantum usecs\n"
#define INVALID_TID_MSG "invalid thread id\n"
#define EXCEEDED_MAX_THREADS "exceeded maximum number of threads\n"
#define INVALID_ENTRY_POINT "invalid entry point for thread\n"
#define INVALID_TERMINATE_CANDIDATE "invalid teminate candidate thread\n"

#define LIB_ERROR_PREFIX std::cerr << LIB_ERROR_MSG;
#define SYS_ERROR_PREFIX std::cerr << SYS_ERROR_MSG;
#define DO_SIG_BLOCK sigemptyset(&sig_set); \
sigaddset(&sig_set,SIGVTALRM); \
sigprocmask(SIG_SETMASK,&sig_set, nullptr);

#define DO_SIG_UNBLOCK sigemptyset(&sig_set); \
sigaddset(&sig_set,SIGVTALRM); \
sigprocmask(SIG_UNBLOCK,&sig_set, nullptr);

#define TID_VALIDATION if(tid < 0 || tid >= MAX_THREAD_NUM) { \
LIB_ERROR_PREFIX; \
std::cerr << INVALID_TID_MSG; \
DO_SIG_UNBLOCK \
return ERROR_VALUE; \
}
#define NOT_MAIN_VALIDATION if(tid == MAIN_THREAD_ID) { \
LIB_ERROR_PREFIX; \
std::cerr << INVALID_TID_MSG; \
DO_SIG_UNBLOCK \
return ERROR_VALUE; \
}
#define THREAD_ALIVE_VALIDATION if(threads[tid] == nullptr) { \
LIB_ERROR_PREFIX; \
std::cerr << INVALID_TID_MSG; \
DO_SIG_UNBLOCK \
return ERROR_VALUE; \
}
#define NOT_NULL_VALIDATION if(entry_point == nullptr) { \
LIB_ERROR_PREFIX; \
std::cerr << INVALID_ENTRY_POINT; \
DO_SIG_UNBLOCK \
return ERROR_VALUE; \
}
#define NOT_NULL_TERMINATE_VALIDATION if(this_thread == nullptr) { \
LIB_ERROR_PREFIX; \
std::cerr << INVALID_TERMINATE_CANDIDATE; \
DO_SIG_UNBLOCK \
return ERROR_VALUE; \
}

#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address (address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
                 "rol    $0x9,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}
#endif



enum State { BLOCKED,SLEEP,READY, RUNNING };
class Thread {
private:
    int _id;
    int quantum_counter;
    int _alarm;
    State _state;
    sigjmp_buf _env;
    char *_stack;

public:

    Thread (int id, thread_entry_point task);
    int get_id () const;
    State get_state () const;
    void set_state(State new_state);
    sigjmp_buf &get_env();
    ~Thread ();
    int get_quantum_number () const;
    void inc_self_quantum();
    void decrease_alarm();
    int get_alarm() const;
    void set_alarm(int num_quantums);

};

/**
 * This class represents a single thread type
 */

Thread::Thread (int id, thread_entry_point entry_point)
{
    this->_state = State::READY;
    this->_id = id;
    this->quantum_counter = 0;
    if (id == 0) {
        this->quantum_counter = 1;
        sigsetjmp(_env,1);
        sigemptyset(&_env->__saved_mask);
    } else {
        this->_stack = new char[STACK_SIZE];
        this->_alarm = 0; // in case of sleep -> will be next quantum count to wake.
        auto sp = (address_t) this->_stack + STACK_SIZE - sizeof (address_t);
        auto pc = (address_t) entry_point;
        sigsetjmp(this->_env, BUFFER_VALUE);
        (_env->__jmpbuf)[JB_SP] = translate_address(sp);
        (_env->__jmpbuf)[JB_PC] = translate_address(pc);
        sigemptyset(&_env->__saved_mask);
    }

}
/**
 *
 * @return the ID of this thread
 */
int Thread::get_id () const
{
    return this->_id;
}
/**
 * @return the current state this thread is in (ready, sleeping, running..)
 */
State Thread::get_state () const
{
    return this->_state;
}

/**
 * Change current state for this thread to new state
 * @param new_state
 */
void Thread::set_state (State new_state)
{
    this->_state = new_state;
}

//TODO : DO WE NEED THIS?
int Thread::get_quantum_number () const{
    return this->quantum_counter;
}

/**
 * destructor : delete the stack for this thread
 */
Thread::~Thread ()
{
    delete[] _stack;
}

/**
 * Return the thread's environment
 * @return _env
 */
sigjmp_buf &Thread::get_env()
{
    return this->_env;
}

/**
 * decrease alarm's time for a sleeping thread
 * @param num_quantums
 */
void Thread::inc_self_quantum() {
    this->quantum_counter++;
}

/**
 * Increase alarm's time for a sleeping thread
 * @param num_quantums
 */
void Thread::decrease_alarm() {
    this->_alarm--;
}


/**
 * return alarm's time for a sleepong thread
 * @param num_quantums
 */
int Thread::get_alarm() const {
    return this->_alarm;
}

/**
 * set alarm for a sleepong thread
 * @param num_quantums
 */

void Thread::set_alarm(int num_quantums) {
    this->_alarm = num_quantums;
}








sigset_t sig_set;

Thread *threads[MAX_THREAD_NUM];
std::list<Thread *> ready;
Thread *running_now;
int q_usecs;
int quantum_sum = 0;
struct itimerval timer;
//Indicator for a run completed without interruptions
bool run_finished = false;
struct sigaction sa = {};
void resume_sleeping_threads();
int next_thread(State prev_new_state);
void time_handler(int sig);

//######## SCHEDULER ############

/**
 * This function starts a timer, that loops
 * constantly according to given quantum.
 * calling this function will restart the loop.
 * */

void init_timer ()
{
   sa.sa_handler = &time_handler;
   if (sigaction (SIGVTALRM, &sa, nullptr))
      {
         //TODO: what error do we need?
         printf ("sigaction error.");
      }

   // Configure the timer to expire after ? sec... */
   timer.it_value.tv_sec =
           q_usecs / SECOND;  // first time interval, seconds part
   timer.it_value.tv_usec =
           q_usecs % SECOND; // first time interval, microseconds part

   // configure the timer to expire every ? sec after that.
   timer.it_interval.tv_sec =
           q_usecs / SECOND; // following time intervals, seconds part
   timer.it_interval.tv_usec =
           q_usecs % SECOND;  // following time intervals, microseconds part

   // Start a virtual timer. It counts down whenever this process is executing.
   if (setitimer (ITIMER_VIRTUAL, &timer, nullptr))
      {
         //TODO: what error do we need?
         printf ("setitimer error.");
      }
}

/**
 * Terminate all threads in the Threads array
 */
void terminate_all_threads ()
{
   for (Thread *thread : threads)
      {

         delete thread;

      }
}

/**
 * This is a handler called every time the timer loops ends
 * @param sig
 */
void time_handler(int sig){
    DO_SIG_BLOCK
    run_finished = true;
    if (next_thread(READY) == BUFFER_VALUE) {
        DO_SIG_UNBLOCK
        return;
    }
    DO_SIG_UNBLOCK;
    siglongjmp(running_now->get_env(),BUFFER_VALUE);


//  printf("finished running thread from handler: %d\n",running_now->get_id());
//  if ((ready.empty()) && (running_now->get_id() == MAIN_THREAD_ID)) {
//      resume_sleeping_threads();
//      if (ready.empty()) {
//          quantum_sum++;
//          running_now->inc_self_quantum();
//          DO_SIG_UNBLOCK;
//          return;
//      } else {
//          running_now->set_state(READY);
//          ready.push_back(running_now);
//          next_thread();
//          DO_SIG_UNBLOCK;
//          return;
//      }
//  } else {
//      running_now->set_state(READY); // change state from running to ready
//      ready.push_back(running_now); // adding it to the back of the ready list
//      next_thread();
//      siglongjmp(running_now->get_env(),BUFFER_VALUE);
//      DO_SIG_UNBLOCK;
//  }

}

/**
 * This function simply takes out from ready the first thread
 * and updates it as running
 */
int next_thread (State prev_new_state)
{
    quantum_sum++;
    resume_sleeping_threads();
//   printf("thread id before env %d\n",running_now->get_id());

   // updating the running thread env details
   if (run_finished != true)
      {
          init_timer ();
      }
   if (running_now != nullptr) {
       if (prev_new_state == BLOCKED) {
           running_now->set_state(BLOCKED);
           if (sigsetjmp(running_now->get_env(), BUFFER_VALUE) == BUFFER_VALUE) {
               DO_SIG_UNBLOCK
               return BUFFER_VALUE;
           }
       } else if (prev_new_state == SLEEP) {
           running_now->set_state(SLEEP);
           if (sigsetjmp(running_now->get_env(), BUFFER_VALUE) == BUFFER_VALUE) {
               DO_SIG_UNBLOCK
               return BUFFER_VALUE;
           }
       } else if (prev_new_state == READY) {
           running_now->set_state(READY);
           ready.push_back(running_now);
           if (sigsetjmp(running_now->get_env(), BUFFER_VALUE) == BUFFER_VALUE) {
               DO_SIG_UNBLOCK
               return BUFFER_VALUE;
           }
       }
   }

    running_now = ready.front(); // getting the next thread in the ready list.
    ready.pop_front();
    running_now->set_state(RUNNING); // change the state to running
    running_now->inc_self_quantum();
    return OK;

}

/**
 * This functions goes over all the threads that are in sleep mode,
 * and check if they need to be resumed according to the curren quantum sum.
 *
 */
void resume_sleeping_threads(){
    for (int i=1 ; i < MAX_THREAD_NUM ; i++) {
        Thread * cur = threads[i];
        if (cur == nullptr || ((running_now != nullptr) && (running_now->get_id() == i))) {
            continue;
        }
        else if (cur->get_alarm() > 0) {
            cur->decrease_alarm();
            if (cur->get_state() != BLOCKED && cur->get_alarm() == 0) {
                ready.push_back(cur);
                cur->set_state(READY);
            }
        }
    }
}



//###### USER API ###########
/**
 * finds and returns the smallest ID in threads available
 * @return smallest ID in threads available
 */
int get_available_id ()
{
   for (int i = 1; i <= MAX_THREAD_NUM; i++)
      {
         if (threads[i] == nullptr)
            {
               return i;
            }
      }
   LIB_ERROR_PREFIX
   std::cerr << EXCEEDED_MAX_THREADS;
   return ERROR_VALUE;
}

/**
 * @brief initializes the thread library.
 *
 * You may assume that this function is called before any other thread library function, and that it is called
 * exactly once.
 * The input to the function is the length of a quantum in micro-seconds.
 * It is an error to call this function with non-positive quantum_usecs.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_init (int quantum_usecs)
{
    if (quantum_usecs <= 0)
    {
        LIB_ERROR_PREFIX
        std::cerr << INVALID_QUANTUM_USECS;
        return ERROR_VALUE;
    }
   q_usecs = quantum_usecs;
   threads[MAIN_THREAD_ID] = new Thread (MAIN_THREAD_ID, nullptr);
   running_now = threads[MAIN_THREAD_ID];
   running_now->set_state(RUNNING);
//   running_now->inc_self_quantum();
   quantum_sum++;
   init_timer();
   return OK;
}

/**
 * @brief Creates a new thread, whose entry point is the function _task with the signature
 * void _task(void).
 *
 * The thread is added to the end of the READY threads list.
 * The uthread_spawn function should fail if it would cause the number of concurrent threads to exceed the
 * limit (MAX_THREAD_NUM).
 * Each thread should be allocated with a stack of size STACK_SIZE bytes.
 *
 * @return On success, return the ID of the created thread. On failure, return -1.
*/
int uthread_spawn (thread_entry_point entry_point)
{
   DO_SIG_BLOCK
   NOT_NULL_VALIDATION
   int available_id = get_available_id ();
   if (available_id == ERROR_VALUE) {
       DO_SIG_UNBLOCK
       return ERROR_VALUE;
   }
   auto *new_thread = new Thread (available_id, entry_point);
   threads[available_id] = new_thread;
   ready.push_back (new_thread);
   DO_SIG_UNBLOCK
   return available_id;

}

/**
 * @brief Terminates the thread with ID tid and deletes it from all relevant control structures.
 *
 * All the resources allocated by the library for this thread should be released. If no thread with ID tid exists it
 * is considered an error. Terminating the main thread (tid == 0) will result in the termination of the entire
 * process using exit(0) (after releasing the assigned library memory).
 *
 * @return The function returns 0 if the thread was successfully terminated and -1 otherwise. If a thread terminates
 * itself or the main thread is terminated, the function does not return.
*/
int uthread_terminate (int tid)
{
   DO_SIG_BLOCK
   TID_VALIDATION
   Thread *this_thread = threads[tid];
   // check if thread terminates itself or main thread
   if (tid == MAIN_THREAD_ID)
      {
         terminate_all_threads ();
         exit (OK);
      }
   else NOT_NULL_TERMINATE_VALIDATION
   else if (this_thread->get_state () == RUNNING)
      {
         threads[tid] = nullptr;
         delete this_thread;
         threads[tid] = nullptr;
         running_now = nullptr;
         run_finished = false;
         next_thread(RUNNING);
         DO_SIG_UNBLOCK
         siglongjmp(running_now->get_env(),BUFFER_VALUE);
      }
   else
      {
         ready.remove (this_thread);
         delete this_thread;
         threads[tid] = nullptr;
      }
   DO_SIG_UNBLOCK
   return OK;
}



/**
 * @brief Blocks the thread with ID tid. The thread may be resumed later using uthread_resume.
 *
 * If no thread with ID tid exists it is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision should be made. Blocking a thread in
 * BLOCKED _state has no effect and is not considered an error.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_block (int tid)
{
   DO_SIG_BLOCK
   TID_VALIDATION
   NOT_MAIN_VALIDATION
   THREAD_ALIVE_VALIDATION
   Thread* cur = threads[tid];
   if (cur->get_state() != BLOCKED) {
       if (cur->get_state() == RUNNING) {
           run_finished = false;
           next_thread(BLOCKED);
           DO_SIG_UNBLOCK
           siglongjmp(running_now->get_env(),BUFFER_VALUE);
       } else if (cur->get_state() == READY) {
           cur->set_state(BLOCKED);
           ready.remove(cur);
       } else if (cur->get_state() == SLEEP) {
           cur->set_state(BLOCKED);
       }
   }
   DO_SIG_UNBLOCK
   return OK;
}


/**
 * @brief Resumes a blocked thread with ID tid and moves it to the READY _state.
 *
 * Resuming a thread in a RUNNING or READY _state has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_resume (int tid)
{
   DO_SIG_BLOCK
   TID_VALIDATION
   THREAD_ALIVE_VALIDATION
   Thread *cur = threads[tid];
   if (cur->get_state() == BLOCKED) {
       if (cur->get_alarm() > 0) {
           cur->set_state(SLEEP);
       } else {
           ready.push_back (threads[tid]);
           threads[tid]->set_state(READY);
       }
   }
    DO_SIG_UNBLOCK
    return OK;
}

/**
 * @brief Blocks the RUNNING thread for num_quantums quantums.
 *
 * Immediately after the RUNNING thread transitions to the BLOCKED _state a scheduling decision should be made.
 * After the sleeping time is over, the thread should go back to the end of the READY threads list.
 * The number of quantums refers to the number of times a new quantum starts, regardless of the reason. Specifically,
 * the quantum of the thread which has made the call to uthread_sleep isn’t counted.
 * It is considered an error if the main thread (tid==0) calls this function.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_sleep (int num_quantums)
{
  DO_SIG_BLOCK
  int tid = running_now->get_id();
  NOT_MAIN_VALIDATION
  if (num_quantums <= 0)
  {
      LIB_ERROR_PREFIX
      std::cerr << INVALID_QUANTUM_USECS;
      return ERROR_VALUE;
  }
  Thread *cur = running_now;
  cur->set_alarm(num_quantums); //alarm time
  run_finished = false;
  next_thread(SLEEP);
  DO_SIG_UNBLOCK
  siglongjmp(running_now->get_env(),BUFFER_VALUE);
  return OK;
}

/**
 * @brief Returns the thread ID of the calling thread.
 *
 * @return The ID of the calling thread.
*/
int uthread_get_tid ()
{
   return running_now->get_id ();
}

/**
 * @brief Returns the total number of quantums since the library was initialized, including the current quantum.
 *
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number should be increased by 1.
 *
 * @return The total number of quantums.
*/
int uthread_get_total_quantums ()
{
  return quantum_sum;
}

/**
 * @brief Returns the number of quantums the thread with ID tid was in RUNNING _state.
 *
 * On the first time a thread runs, the function should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING _state when this function is called, include
 * also the current quantum). If no thread with ID tid exists it is considered an error.
 *
 * @return On success, return the number of quantums of the thread with ID tid. On failure, return -1.
*/
int uthread_get_quantums (int tid)
{
  TID_VALIDATION
  THREAD_ALIVE_VALIDATION
  return threads[tid]->get_quantum_number();
}
