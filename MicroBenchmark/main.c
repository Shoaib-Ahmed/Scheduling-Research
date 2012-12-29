/*
 * File:   main.c
 * Author: shoaiba
 *
 * Created on December 26, 2012, 7:07 PM
 */

#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <linux/unistd.h>
#include <errno.h>


/*
 *
 */

// Define a Kilo Byte
#define KB 1024
// Define a Mega Byte
#define MB 1024 * 1024

static int N_THREADS = 1;

typedef struct thread_arg {
    char *arr;
    char *A;
    char *B;
    int *size;
    double seconds;
    int core_id;
} thread_arg, * thread_arg_t;

int * thread_start(void * arg);

void print_cpu() {
    int cpuid_out;

    __asm__(
            "cpuid;"
            : "=b"(cpuid_out)
            : "a"(1)
            :);

    printf("I am running on CPU %i\n", (cpuid_out >> 24));
}

int stick_this_thread_to_core(int core_id) {
    int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (core_id >= num_cores)
        return -1;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();
    int return_val = pthread_setaffinity_np(current_thread, sizeof (cpu_set_t), &cpuset);

    return return_val;
}

int main(int argc, char** argv) {

    unsigned volatile char A, B; // Declaring two volatile variables, to avoid compiler optimizations.
    A = 10; // Initialize A
    B = 20; // Initialize B   

    printf("Size Of Char Is  %i \n", (sizeof (char))); // Printing out the size of char in terms of bytes
    printf("Size Of Int Is  %i \n", (sizeof (int))); // Printing out the size of char in terms of bytes
    printf("Number Of Cores  %i \n", sysconf(_SC_NPROCESSORS_ONLN));

    char *DataArray = (char *) malloc(12 * 1024 * 1024);
    int size_to_use = (12 * MB);
    unsigned int i, s; // Loop iterators
    thread_arg_t arg = (thread_arg_t) malloc(sizeof (thread_arg));
    arg->arr = DataArray;
    arg->A = A;
    arg->B = B;
    arg->size = size_to_use;    

    s = 0;

    do {
        pthread_t * threads = (pthread_t *) malloc(sizeof (pthread_t) * N_THREADS);
        for (i = 0; i < N_THREADS; i++) {
            pthread_create(&threads[i], NULL, thread_start, (void *) arg);
        }
        arg->core_id = s;
        for (i = 0; i < N_THREADS; i++) {
            void *value;
            pthread_join(threads[i], &value);
            printf("The Time Taken By Thread Is  %f \n", arg->seconds);
        }
        s++;
        // Printing out the Size of array and associated execution time.
        //printf("%d, %f\t\n", ArraySizeToAccess[s] / 1024, seconds);
    } while (s < 8);

    return (EXIT_SUCCESS);
}

int * thread_start(void * arg) {
    struct thread_arg *argument = arg;
    //stick_this_thread_to_core(argument->core_id);
    printf("Affinity Status %i\n", stick_this_thread_to_core(argument->core_id));
    print_cpu();
    int CpuID = sched_getcpu();
    printf("CPU ID Is %i\n", CpuID);
    long long elapsed = 0;
    argument->seconds = 5;
    struct timeval start, stop; // Variables To Compute execution time

    gettimeofday(&start, 0);

    unsigned int iterations = 256 * 1024 * 1024; // Iterations For accessing elements of the array
    unsigned int i; // Loop iterators

    int LengthFlag = ((argument->size) - 1);
    // Access an element within the array, elements are accessed a cache line apart (Cache line is 64 bytes)
    for (i = 0; i < iterations; i++) {
        // access and assign a value to the first byte of each cache line, for a given size
        // Continue this for a given number of iterations.
        argument->arr[(i * 64) & LengthFlag] -= (argument->B - argument->A);
        //        argument->arr[(i * 64) & LengthFlag] = ((argument->arr[(i * 64) & LengthFlag] * i) % 255);
        //        if (argument->arr[(i * 64) & LengthFlag] < 100)
        //            argument->arr[(i * 64) & LengthFlag] += 100;
        //        else if (argument->arr[(i * 64) & LengthFlag] > 100 && argument->arr[(i * 64) & LengthFlag] < 200)
        //            argument->arr[(i * 64) & LengthFlag] += 50;
        //        else if (argument->arr[(i * 64) & LengthFlag] < 255)
        //            argument->arr[(i * 64) & LengthFlag] += 1;
    }

    gettimeofday(&stop, 0);
    elapsed = (stop.tv_sec - start.tv_sec)*1000000LL + stop.tv_usec - start.tv_usec;
    argument->seconds = (elapsed * 1.00) / 1000000.00;

    //printf("%i , %i \n", argument->A, (argument->B - argument->A));
    return CpuID;
}
