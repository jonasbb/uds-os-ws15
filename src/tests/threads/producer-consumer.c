/* Tests producer/consumer communication with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

// function declarations
void producer_consumer(unsigned int num_producer, unsigned int num_consumer);
static void producer(void *aux);
static void consumer(void *aux);
static void put(char c);
static char pop(void);

// size of buffer
#define BUF_SIZE 4
// bounded buffer
// protected by buf_lock
char buf[BUF_SIZE];

// next free position in buffer
// next position with character set
// count of chars in buffer
// protected by buf_lock
unsigned int buf_in, buf_out, buf_count;

// conditions
// requires buf_lock
struct condition cond_non_empty; // buf_count == 0
// requires buf_lock
struct condition cond_non_full; // buf_count == BUF_SIZE

// locks
struct lock buf_lock; // lock for variables buf and buf_*

void test_producer_consumer(void)
{
    /*producer_consumer(0, 0);
    producer_consumer(1, 0);
    producer_consumer(0, 1);
    producer_consumer(1, 1);
    producer_consumer(3, 1);
    producer_consumer(1, 3);
    producer_consumer(4, 4);
    producer_consumer(7, 2);
    producer_consumer(2, 7);*/
    producer_consumer(6, 6);
    pass();
}


void producer_consumer(unsigned int num_producer, unsigned int num_consumer)
{
    // init global variable
    for (int i = 0; i < BUF_SIZE; i++) {
        buf[i] = 0;
    }
    buf_in = 0;
    buf_out = 0;
    buf_count = 0;
    
    // conditions
    cond_init(&cond_non_empty);
    cond_init(&cond_non_full);
    
    // locks
    lock_init(&buf_lock);
    
    // init threads
    int nice = thread_get_nice();
#define THREAD_NAME_LENGTH 10
    char thread_name[THREAD_NAME_LENGTH];
    
    for (unsigned int i = 0; i < num_producer; i++) {
        // create thread name
        snprintf((char *)&thread_name, THREAD_NAME_LENGTH, "prod%05d", i);
        thread_create((char *)&thread_name, nice, producer, NULL);
    }
    for (unsigned int i = 0; i < num_consumer; i++) {
        // create thread name
        snprintf((char *)&thread_name, THREAD_NAME_LENGTH, "cons%05d", i);
        thread_create((char *)&thread_name, nice, consumer, NULL);
    }
}

static
void producer(UNUSED void *aux)
{
    char* string = "Hello world";
    while (*string != 0) {
        put(*string); //blocking
        string++;
    }
}

static
void consumer(UNUSED void *aux)
{
    while(1) {
        putchar(pop()); // blocking
    }
}

/*  Pushes a character onto the buffer.
    Blocks if the buffer is full until space is available again
 */
void put(char c)
{
    // locking
    lock_acquire(&buf_lock);
    // check for insert
    while(buf_count == BUF_SIZE) { // full
        cond_wait(&cond_non_full, &buf_lock);
    }
    
    // insert char
    buf[buf_in] = c;
    buf_in++;
    buf_in %= BUF_SIZE;
    buf_count++;
    
    // signal waiting thread
    // signal is enough, no broadcast needed, as every waiting
    // thread will consume one char and thus the buffer will be
    // empty again and the condition false
    cond_signal(&cond_non_empty, &buf_lock);
    // locking
    lock_release(&buf_lock);
}

/*  Pops a character from the buffer and returns the value.
    If the buffer is empty this function blocks until data
    will be available again.
 */
char pop(void)
{
    // locking
    lock_acquire(&buf_lock);
    // check for remove
    while(buf_count == 0) { // empty
        cond_wait(&cond_non_empty, &buf_lock);
    }
    
    // remove char
    char c = buf[buf_out];
    buf_out++;
    buf_out %= BUF_SIZE;
    buf_count--;
    
    // signal waiting thread
    // signal is enough, no broadcast needed, as every waiting
    // thread will produce one char and thus the buffer will be
    // full again and the condition false
    cond_signal(&cond_non_full, &buf_lock);
    // locking
    lock_release(&buf_lock);
    
    return c;
}
