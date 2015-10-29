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

// for better readability
#define LEFT 0
#define RIGHT 1
#define NORMAL 0
#define EMERGENCY 1

// change to allow more/less vehicles on the bridge
#define MAX_VEHICLES_ON_BRIDGE 3

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
int direction = 0;
unsigned int on_bridge[2] = {0, 0}; // number of vehicles in direction
unsigned int waiting[2][2] = {{0, 0}, {0, 0}}; // vehicles waiting, with prio
struct semaphore lock;
struct semaphore wait_lock[2][2];


void test_narrow_bridge(void)
{
    //narrow_bridge(0, 0, 0, 0);
    //msg("CASE 01 COMPLETED");
    //narrow_bridge(3, 2, 0, 0);
    //msg("CASE 02 COMPLETED");
    //narrow_bridge(0, 0, 0, 1);
    //narrow_bridge(0, 4, 0, 0);
    //narrow_bridge(0, 0, 4, 0);
    //narrow_bridge(3, 3, 3, 3);
    //narrow_bridge(4, 3, 4 ,3);
    //narrow_bridge(7, 23, 17, 1);
    //narrow_bridge(40, 30, 0, 0);
    //narrow_bridge(30, 40, 0, 0);
    narrow_bridge(23, 23, 1, 11);
    //narrow_bridge(22, 22, 10, 10);
    //narrow_bridge(0, 0, 11, 12);
    //narrow_bridge(0, 10, 0, 10);
    //narrow_bridge(0, 10, 10, 0);
    pass();
}


void narrow_bridge(unsigned int num_vehicles_left, unsigned int num_vehicles_right,
        unsigned int num_emergency_left, unsigned int num_emergency_right)
{
    // init random number generator
    random_init (timer_ticks());
    
    // init semaphores
    sema_init(&lock, 1); // use as lock
    sema_init(&wait_lock[LEFT][NORMAL], 0); // no initial use
    sema_init(&wait_lock[LEFT][EMERGENCY], 0);
    sema_init(&wait_lock[RIGHT][NORMAL], 0);
    sema_init(&wait_lock[RIGHT][EMERGENCY], 0);

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

/* may only be called if lock `lock` is held */
static
void print_state(void) {
    msg("Direction: %s; Left {N: %d, E: %d}; Right {N: %d, E: %d}; On Bridge {L: %d, R: %d}",
        direction ? "right" : "left",
        waiting[0][0], waiting[0][1],
        waiting[1][0], waiting[1][1],
        on_bridge[0], on_bridge[1]);
}

static
void vehicle_left(UNUSED void *aux)
{
    one_vehicle(LEFT, NORMAL);
}
static
void vehicle_right(UNUSED void *aux)
{
    one_vehicle(RIGHT, NORMAL);
}
static
void emergency_left(UNUSED void *aux)
{
    one_vehicle(LEFT, EMERGENCY);
}
static
void emergency_right(UNUSED void *aux)
{
    one_vehicle(RIGHT, EMERGENCY);
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
    // change direction, if not emergency on other side && bridge not used by other side
    if (direction != direc && on_bridge[1-direc] == 0 && waiting[1-direc][EMERGENCY] == 0) {
        direction = 1-direction;
    }
    
    // we may enter the bridge now
    while (on_bridge[1-direc] > 0 // no vehicles in wrong direction
            || direction != direc // our turn
            || on_bridge[direc] == MAX_VEHICLES_ON_BRIDGE // not too many on bridge
            || (prio == NORMAL && // no higher priority vehicles are waiting
                (waiting[LEFT][EMERGENCY] + waiting[RIGHT][EMERGENCY]) > 0)) {
                
        // wait
        waiting[direc][prio]++;
        msg("%s is waiting", thread_name()); // print debugs
        print_state();
        // release lock
        sema_up(&lock);
        // wait till condition is met
        sema_down(&wait_lock[direc][prio]);
        // acquire lock
        sema_down(&lock);
        waiting[direc][prio]--;
    }
    
    // update state
    on_bridge[direc]++;
    ASSERT(on_bridge[LEFT] + on_bridge[RIGHT] <= MAX_VEHICLES_ON_BRIDGE);
    // release lock
    sema_up(&lock);

}
static
void cross_bridge(UNUSED int direc, UNUSED int prio)
{
    msg("%s enters the bridge", thread_name()); // print debugs
    timer_msleep(random_ulong() % 500); // wait up to half a second
    msg("%s leaves the bridge", thread_name());
}

static
void exit_bridge(int direc, UNUSED int prio)
{
    // acquire lock
    sema_down(&lock);
    
    // we were the last vehicle on bridge
    // determine new direction
    if (on_bridge[direc] == 1) {
        // check direction
        // change if we are the last vehicle of our priority and the other side
        // has a higher priority vehicle waiting than our side
        
        // emergency waiting on other side, but not ours
        if (waiting[direction][EMERGENCY] == 0
                && waiting[1-direction][EMERGENCY] > 0) {
            direction = 1-direction;
        } else
        // no vehicles on our side
        // but normal on other
        if (waiting[direction][EMERGENCY] == 0
                && waiting[direction][NORMAL] == 0
                && waiting[1-direction][NORMAL] > 0) {
            direction = 1-direction;
        }
    }
    
    // update state
    on_bridge[direc]--;
    print_state();
        
    int to_wake_up = 3 - on_bridge[direc];
    // wake up prio queues
    for (unsigned int i = 0;
            i < waiting[direction][EMERGENCY] && to_wake_up > 0;
            i++, to_wake_up--) {
        sema_up(&wait_lock[direction][EMERGENCY]);
    }
    // wake up normal ones if space left
    for (unsigned int i = 0;
            i < waiting[direction][NORMAL] && to_wake_up > 0;
            i++, to_wake_up--) {
        sema_up(&wait_lock[direction][NORMAL]);
    }
    
    // release lock
    sema_up(&lock);
}
