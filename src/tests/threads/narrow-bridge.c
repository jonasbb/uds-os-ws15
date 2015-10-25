/* Tests producer/consumer communication with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */

#include <stdio.h>
#include <random.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"


void narrow_bridge(unsigned int num_vehicles_left, unsigned int num_vehicles_right,
        unsigned int num_emergency_left, unsigned int num_emergency_right);
static void vehicle_left(void *aux);
static void vehicle_right(void *aux);
static void emergency_left(void *aux);
static void emergency_right(void *aux);

static void one_vehicle(int direc, int prio);
static void arrive_bridge(int direc, int prio);
static void cross_bridge(int direc, int prio);
static void exit_bridge(int direc, int prio);

// bridge state
unsigned int direction = 0;
unsigned int on_bridge[2] = {0, 0}; // number of vehicles in direction
unsigned int waiting[2][2] = {{0, 0}, {0, 0}}; // vehicles waiting, with prio
struct semaphore lock;
struct semaphore wait_lock[2][2];


void test_narrow_bridge(void)
{
    /*narrow_bridge(0, 0, 0, 0);
    narrow_bridge(1, 0, 0, 0);
    narrow_bridge(0, 0, 0, 1);
    narrow_bridge(0, 4, 0, 0);
    narrow_bridge(0, 0, 4, 0);
    narrow_bridge(3, 3, 3, 3);
    narrow_bridge(4, 3, 4 ,3);
    narrow_bridge(7, 23, 17, 1);
    narrow_bridge(40, 30, 0, 0);
    narrow_bridge(30, 40, 0, 0);
    narrow_bridge(23, 23, 1, 11);
    narrow_bridge(22, 22, 10, 10);
    narrow_bridge(0, 0, 11, 12);
    narrow_bridge(0, 10, 0, 10);*/
    narrow_bridge(0, 10, 10, 0);
    pass();
}


void narrow_bridge(unsigned int num_vehicles_left, unsigned int num_vehicles_right,
        unsigned int num_emergency_left, unsigned int num_emergency_right)
{
    // init random number generator
    random_init (timer_ticks());
    
    // init semaphores
    sema_init(&lock, 1); // use as lock
    sema_init(&wait_lock[0][0], 3); // up to 3 may wake up simultaniousely
    sema_init(&wait_lock[0][1], 3); // up to 3 may wake up simultaniousely
    sema_init(&wait_lock[1][0], 3); // up to 3 may wake up simultaniousely
    sema_init(&wait_lock[1][1], 3); // up to 3 may wake up simultaniousely

    // init threads
    int nice = thread_get_nice();
#define THREAD_NAME_LENGTH 20
    char thread_name[THREAD_NAME_LENGTH];
    
    for (unsigned int i = 0; i < num_vehicles_left; i++) {
        // create thread name
        snprintf((char *)&thread_name, THREAD_NAME_LENGTH, "l_n_%05d", i);
        thread_create((char *)&thread_name, nice, vehicle_left, NULL);
    }
    for (unsigned int i = 0; i < num_vehicles_right; i++) {
        // create thread name
        snprintf((char *)&thread_name, THREAD_NAME_LENGTH, "r_n_%05d", i);
        thread_create((char *)&thread_name, nice, vehicle_right, NULL);
    }
    for (unsigned int i = 0; i < num_emergency_left; i++) {
        // create thread name
        snprintf((char *)&thread_name, THREAD_NAME_LENGTH, "l_e_%05d", i);
        thread_create((char *)&thread_name, nice, emergency_left, NULL);
    }
    for (unsigned int i = 0; i < num_emergency_right; i++) {
        // create thread name
        snprintf((char *)&thread_name, THREAD_NAME_LENGTH, "r_e_%05d", i);
        thread_create((char *)&thread_name, nice, emergency_right, NULL);
    }
}
    
// left == 0 (direc)
// right == 1
// normal == 0 (prio)
// emergency == 1
static
void vehicle_left(UNUSED void *aux)
{
    one_vehicle(0, 0);
}
static
void vehicle_right(UNUSED void *aux)
{
    one_vehicle(1, 0);
}
static
void emergency_left(UNUSED void *aux)
{
    one_vehicle(0, 1);
}
static
void emergency_right(UNUSED void *aux)
{
    one_vehicle(1, 1);
}

// OneVehicle(int direc, int prio) {
// ArriveBridge(direc,prio);
//  CrossBridge(direc,prio);
//  ExitBridge(direc,prio);
//}
static
void one_vehicle(int direc, int prio)
{
    arrive_bridge(direc, prio);
    cross_bridge(direc, prio);
    exit_bridge(direc, prio);
}

static
void arrive_bridge(int direc, int prio)
{
    // acquire lock
    sema_down(&lock);
    
    // determine direction and whether I may drive
    if (on_bridge[0] + on_bridge[1] < 3) {
        sema_up(&wait_lock[direc][prio]);
    }
    
    // wait
    waiting[direc][prio]++;
    // wait if bridge is in use by other direction
    // or the direction is wrong
    while (on_bridge[1-direc] > 0 || direction != direc) {
        // release lock
        sema_up(&lock);
        // wait till condition is met
        sema_down(&wait_lock[direc][prio]);
        // acquire lock
        sema_down(&lock);
    }
    waiting[direc][prio]--;
    
    // update state
    on_bridge[direc]++;
    ASSERT(on_bridge[0] + on_bridge[1] <= 3);
    // release lock
    sema_up(&lock);
}

static
void cross_bridge(UNUSED int direc, UNUSED int prio)
{
    printf("%s enters the bridge\n", thread_name()); // print debugs
    timer_msleep(random_ulong() % 500); // wait up to half a second
    printf("%s leaves the bridge\n", thread_name());
}

static
void exit_bridge(int direc, int prio)
{
    // acquire lock
    sema_down(&lock);
    
    if (on_bridge[0] + on_bridge[1] == 3) {
        sema_up(&wait_lock[direc][prio]);
    }
    // update state
    on_bridge[direc]--;
    // select new vehicle to drive
    
    
    // release lock
    sema_up(&lock);
}
