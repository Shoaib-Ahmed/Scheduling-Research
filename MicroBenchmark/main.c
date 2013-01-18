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
#include <sys/types.h>
#include <memory.h>
#include <malloc.h>
#include "papi.h"

#include <perfmon/pfmlib_perf_event.h>
#include "perf_util.h"

static const char *gen_events[] = {
    "L1-DCACHE-LOAD-MISSES",
    "L1-DCACHE-LOADS",
    "PERF_COUNT_HW_CACHE_L1D",
    "cache-misses",
    "cache-references",
    "LLC_MISSES",
    "LLC_REFERENCES",
    NULL
};

// Define a Kilo Byte
#define KB 1024
// Define a Mega Byte
#define MB 1024 * 1024

#define INDEX 200

typedef struct cacheline {
    //struct cacheline *next;
    long int pad[8];
} cacheline, *cacheline_t;

static int N_THREADS = 1;

enum BenchmarkingMode {
    Simple, Simple_Same_Core, Circular, Circular_With_Cache_Wiping
};

enum TimingMode {
    GetTimeOfTheDay, ClockCycles
};

typedef struct thread_arg {
    char *arr;
    char *c;
    char *A;
    char *B;
    int *size;
    int *sizeOfWipingDataStructure;
    double seconds;
    int core_id;
    int cache_wiping_flag;
    enum TimingMode tmode;
} thread_arg, * thread_arg_t;

int test_function() {
    return 0;
}
int * thread_start(void * arg);
int * thread_start_Using_Libpfm(void * arg);

print_counts(perf_event_desc_t *fds, int num_fds, const char *msg) {
    uint64_t val;
    uint64_t values[3];
    double ratio;
    int i;
    ssize_t ret;

    /*
     * now read the results. We use pfp_event_count because
     * libpfm guarantees that counters for the events always
     * come first.
     */
    memset(values, 0, sizeof (values));
    printf("%s %'20",
            msg);

    for (i = 0; i < num_fds; i++) {

        ret = read(fds[i].fd, values, sizeof (values));
        if (ret < (ssize_t)sizeof (values)) {
            if (ret == -1)
                err(1, "cannot read results: %s", strerror(errno));
            else
                warnx("could not read event %d", i);
        }
        /*
         * scaling is systematic because we may be sharing the PMU and
         * thus may be multiplexed
         */
        val = perf_scale(values);
        ratio = perf_scale_ratio(values);

        printf("%'5"PRIu64" %s ||\t ",
                val,
                fds[i].name);
    }
    printf("\n");
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

const char* get_mode_name(enum BenchmarkingMode mode) {
    switch (mode) {
        case Simple:
            return "Simple Mode";
            break;
        case Simple_Same_Core:
            return "Simple_Same_Core Mode";
            break;
        case Circular:
            return "Circular Mode";
            break;
        case Circular_With_Cache_Wiping:
            return "Circular_With_Cache_Wiping Mode";
            break;
        default:
            break;
    }
}

static void
display_sched_attr(int policy, struct sched_param *param) {
    printf("    policy=%s, priority=%d\n",
            (policy == SCHED_FIFO) ? "SCHED_FIFO" :
            (policy == SCHED_RR) ? "SCHED_RR" :
            (policy == SCHED_OTHER) ? "SCHED_OTHER" :
            "???",
            param->sched_priority);
}

static void
display_thread_sched_attr(char *msg) {
    int policy, s;
    struct sched_param param;

    s = pthread_getschedparam(pthread_self(), &policy, &param);
    if (s != 0)
        printf("Error Getting Thread Priority");

    printf("%s : ", msg);
    display_sched_attr(policy, &param);
}

void do_analysis(double *times, int sz, char * c, enum
        BenchmarkingMode mode, int circular_repetitions, int cores, int sizeOfWipingDataStructure, enum TimingMode tmode) {
    unsigned volatile char A, B; // Declaring two volatile variables, to avoid compiler optimizations.
    A = 10; // Initialize A
    B = 20; // Initialize B   

    //    struct sched_param my_param;
    //    my_param.sched_priority = -20;
    //    pthread_attr_t hp_attr;
    //    int ret;
    //    /* initialize an attribute to the default value */
    //    ret = pthread_attr_init(&hp_attr);
    //    pthread_attr_setschedparam(&hp_attr, &my_param);   

    pthread_attr_t custom_sched_attr;
    int fifo_prio;
    struct sched_param fifo_param;

    pthread_attr_init(&custom_sched_attr);
    pthread_attr_setinheritsched(&custom_sched_attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&custom_sched_attr, SCHED_FIFO);
    //pthread_attr_setschedpolicy(&custom_sched_attr, SCHED_OTHER);

    fifo_prio = sched_get_priority_max(SCHED_FIFO);

    fifo_param.sched_priority = fifo_prio;

    pthread_attr_setschedparam(&custom_sched_attr, &fifo_param);

    char *dataArray = (char *) malloc(sz);
    memset(dataArray, 0, sizeof (dataArray));
    int size_to_use = sz;
    unsigned int i, s; // Loop iterators
    thread_arg_t arg = (thread_arg_t) malloc(sizeof (thread_arg));
    arg->arr = dataArray;
    arg->A = A;
    arg->B = B;
    arg->c = c;
    arg->size = size_to_use;
    arg->tmode = tmode;
    arg->cache_wiping_flag = 0;
    arg->sizeOfWipingDataStructure = sizeOfWipingDataStructure;

    int rpt = 0;

    //    for (i = 0; i < size_to_use; i++) {
    //        dataArray[i] = (rand() % 64);
    //    }
    //
    //    int size_to_use_clear = 64 * 1024 * 1024;
    //    char *dataArray_clear_temp = (char *) malloc(size_to_use_clear);
    //    memset(dataArray, 0, sizeof (dataArray_clear_temp));
    //    for (i = 0; i < size_to_use_clear; i++) {
    //        dataArray_clear_temp[i] = (rand() % 64);
    //    }

    switch (mode) {
        case Simple:
        {
            do {
                s = 1;
                do {
                    //For Three Executions On Core 3 and last execution on core 4.
                    arg->core_id = 2;
                    if (s == 3)
                        arg->core_id = 3;

                    pthread_t * threads = (pthread_t *) malloc(sizeof (pthread_t) * (N_THREADS * 1));
                    for (i = 0; i < N_THREADS; i++) {
                        pthread_create(&threads[i], & custom_sched_attr, thread_start, (void *) arg);
                    }

                    for (i = 0; i < N_THREADS; i++) {
                        void *value;
                        pthread_join(threads[i], &value);
                    }

                    times[s] += arg->seconds;

                    s++;
                } while (s < cores);
                rpt = rpt + 1;
            } while (rpt < circular_repetitions);

            for (i = 1; i < cores; i++) {
                times[i] /= (circular_repetitions);
            }
            break;
        }
        case Simple_Same_Core:
        {
            do {
                s = 1;
                do {
                    //For N Times Execute On Core 3.
                    arg->core_id = 3;

                    pthread_t * threads = (pthread_t *) malloc(sizeof (pthread_t) * (N_THREADS * 1));
                    for (i = 0; i < N_THREADS; i++) {
                        pthread_create(&threads[i], & custom_sched_attr, thread_start, (void *) arg);
                    }

                    for (i = 0; i < N_THREADS; i++) {
                        void *value;
                        pthread_join(threads[i], &value);
                    }

                    times[s] += arg->seconds;

                    s++;
                } while (s < cores);
                rpt = rpt + 1;
            } while (rpt < circular_repetitions);

            for (i = 1; i < cores; i++) {
                times[i] /= (circular_repetitions);
            }
            break;
        }
        case Circular:
        case Circular_With_Cache_Wiping:
        {
            do {
                s = 1;
                do {
                    arg->core_id = s;

                    pthread_t * threads = (pthread_t *) malloc(sizeof (pthread_t) * (N_THREADS * 1));
                    for (i = 0; i < N_THREADS; i++) {
                        if (mode == Circular_With_Cache_Wiping) {
                            arg->cache_wiping_flag = 1;
                        }
                        pthread_create(&threads[i], & custom_sched_attr, thread_start, (void *) arg);
                    }

                    for (i = 0; i < N_THREADS; i++) {
                        void *value;
                        pthread_join(threads[i], &value);
                    }

                    times[s] += arg->seconds;

                    s++;
                } while (s < cores);
                rpt = rpt + 1;
            } while (rpt < circular_repetitions);

            for (i = 1; i < cores; i++) {
                times[i] /= (circular_repetitions);
            }
            break;
        }
        default:
            break;
    }
}

void do_analysis_libPFM(double *times, int sz, char * c,
        enum BenchmarkingMode mode, int circular_repetitions, int cores, int sizeOfWipingDataStructure, enum TimingMode tmode) {
    unsigned volatile char A, B; // Declaring two volatile variables, to avoid compiler optimizations.
    A = 10; // Initialize A
    B = 20; // Initialize B   

    //    struct sched_param my_param;
    //    my_param.sched_priority = -20;
    //    pthread_attr_t hp_attr;
    //    int ret;
    //    /* initialize an attribute to the default value */
    //    ret = pthread_attr_init(&hp_attr);
    //    pthread_attr_setschedparam(&hp_attr, &my_param);

    pthread_attr_t custom_sched_attr;
    int fifo_prio;
    struct sched_param fifo_param;

    pthread_attr_init(&custom_sched_attr);
    pthread_attr_setinheritsched(&custom_sched_attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&custom_sched_attr, SCHED_FIFO);
    //pthread_attr_setschedpolicy(&custom_sched_attr, SCHED_OTHER);

    fifo_prio = sched_get_priority_max(SCHED_FIFO);

    fifo_param.sched_priority = fifo_prio;

    pthread_attr_setschedparam(&custom_sched_attr, &fifo_param);

    char *dataArray = (char *) malloc(sz);
    memset(dataArray, 0, sizeof (dataArray));
    int size_to_use = sz;
    unsigned int i, s; // Loop iterators
    thread_arg_t arg = (thread_arg_t) malloc(sizeof (thread_arg));
    arg->arr = dataArray;
    arg->A = A;
    arg->B = B;
    arg->c = c;
    arg->size = size_to_use;
    arg->tmode = tmode;
    arg->cache_wiping_flag = 0;
    arg->sizeOfWipingDataStructure = sizeOfWipingDataStructure;

    int rpt = 0;

    //    for (i = 0; i < size_to_use; i++) {
    //        dataArray[i] = (rand() % 64);
    //    }
    //
    //    int size_to_use_clear = 64 * 1024 * 1024;
    //    char *dataArray_clear_temp = (char *) malloc(size_to_use_clear);
    //    memset(dataArray, 0, sizeof (dataArray_clear_temp));
    //    for (i = 0; i < size_to_use_clear; i++) {
    //        dataArray_clear_temp[i] = (rand() % 64);
    //    }

    switch (mode) {
        case Simple:
        {
            do {
                s = 1;
                do {
                    //For Three Executions On Core 3 and last execution on core 4.
                    arg->core_id = 2;
                    if (s == 3)
                        arg->core_id = 3;

                    pthread_t * threads = (pthread_t *) malloc(sizeof (pthread_t) * (N_THREADS * 1));
                    for (i = 0; i < N_THREADS; i++) {
                        pthread_create(&threads[i], & custom_sched_attr, thread_start_Using_Libpfm, (void *) arg);
                    }

                    for (i = 0; i < N_THREADS; i++) {
                        void *value;
                        pthread_join(threads[i], &value);
                    }

                    times[s] += arg->seconds;

                    s++;
                } while (s < cores);
                rpt = rpt + 1;
            } while (rpt < circular_repetitions);

            for (i = 1; i < cores; i++) {
                times[i] /= (circular_repetitions);
            }
            break;
        }
        case Simple_Same_Core:
        {
            do {
                s = 1;
                do {
                    //For N Times Execute On Core 3.
                    arg->core_id = 3;

                    pthread_t * threads = (pthread_t *) malloc(sizeof (pthread_t) * (N_THREADS * 1));
                    for (i = 0; i < N_THREADS; i++) {
                        pthread_create(&threads[i], & custom_sched_attr, thread_start_Using_Libpfm, (void *) arg);
                    }

                    for (i = 0; i < N_THREADS; i++) {
                        void *value;
                        pthread_join(threads[i], &value);
                    }

                    times[s] += arg->seconds;

                    s++;
                } while (s < cores);
                rpt = rpt + 1;
            } while (rpt < circular_repetitions);

            for (i = 1; i < cores; i++) {
                times[i] /= (circular_repetitions);
            }
            break;
        }
        case Circular:
        case Circular_With_Cache_Wiping:
        {
            do {
                s = 1;
                do {
                    arg->core_id = s;

                    pthread_t * threads = (pthread_t *) malloc(sizeof (pthread_t) * (N_THREADS * 1));
                    for (i = 0; i < N_THREADS; i++) {
                        if (mode == Circular_With_Cache_Wiping) {
                            arg->cache_wiping_flag = 1;
                        }
                        pthread_create(&threads[i], & custom_sched_attr, thread_start_Using_Libpfm, (void *) arg);
                    }

                    for (i = 0; i < N_THREADS; i++) {
                        void *value;
                        pthread_join(threads[i], &value);
                    }

                    times[s] += arg->seconds;

                    s++;
                    if (mode == Circular_With_Cache_Wiping) {
                        int p, l;
//                        for (l = 0; l < 256; l++) {
                            for (p = 1; p < cores; p++) {
                                stick_this_thread_to_core(p);
                                for (i = 0; i < sizeOfWipingDataStructure; i++)
                                    c[i] = (rand() % 50);
                            }
//                        }
                        stick_this_thread_to_core(0);
                    }
                } while (s < cores);
                rpt = rpt + 1;
            } while (rpt < circular_repetitions);

            for (i = 1; i < cores; i++) {
                times[i] /= (circular_repetitions);
            }
            break;
        }
        default:
            break;
    }
}

int main(int argc, char** argv) {

    struct sched_param param;
    param.sched_priority = 99;
    printf("Thread Priority is : %i\n", param.sched_priority);
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    display_thread_sched_attr("Scheduler attributes of new thread");

    // Pinning Main Thread To Core 0 always.
    int *evntarray = malloc(sizeof (int) * 2);
    evntarray[0] = PAPI_L2_DCM;
    evntarray[1] = PAPI_L2_DCA;
    long_long * values = malloc(sizeof (long_long) * 2);
    values[0] = 0;
    values[1] = 0;
    int i = 0;

    //    if (PAPI_start_counters(evntarray, 2) != PAPI_OK)
    //        printf("error \n");
    //
    //    if (PAPI_read_counters(values, 2) != PAPI_OK)
    //        printf("error \n");
    //
    //    for (i = 0; i < 2; i++) {
    //        if (i == 0)
    //            printf("Immediate Misses %i \n", values[i]);
    //        if (i == 1)
    //            printf("Immediate Accesses %i \n\n", values[i]);
    //    }
    //
    //    //test_function();    
    //
    //    if (PAPI_read_counters(values, 2) != PAPI_OK)
    //        printf("error \n");
    //
    //    for (i = 0; i < 2; i++) {
    //        if (i == 0)
    //            printf("L2 Data Cache Misses %lld \n", values[i]);
    //        if (i == 1)
    //            printf("Total L2 Cache Accesses %lld \n\n", values[i]);
    //    }
    //
    //    if (PAPI_stop_counters(values, 2) != PAPI_OK)
    //        printf("error \n");
    //
    //    PAPI_shutdown();

    stick_this_thread_to_core(0);

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////    
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    //const int szClear = 256 * 1024;// LLC Size
    const int szClear = 8 * 1024 * 1024;// L2 Size
    char *c = (char *) malloc(szClear);
    //    const int szClearAll = 256 * 1024 * 1024;
    //    char *clearall = (char *) malloc(szClear);
    const int cores = sysconf(_SC_NPROCESSORS_ONLN);

    printf("Size of \"Char\" is  %i \n", (sizeof (char))); // Printing out the size of char in terms of bytes
    printf("Size of \"Int\" is  %i \n", (sizeof (int))); // Printing out the size of char in terms of bytes
    printf("Size of \"Struct\" is  %i \n", (sizeof (struct cacheline))); // Printing out the size of char in terms of bytes
    printf("Number of \"Cores\" are %i \n", cores);

    unsigned volatile int A1, B1;

    A1 = B1 = 10;
    int sizes[] = {256 * 1024}; //{1 * KB, 4 * KB, 8 * KB, 16 * KB, 32 * KB, 64 * KB, 128 * KB, 192 * KB, 256 * KB, 1 * MB, 2 * MB, 4 * MB};
    //int sizes[] = {1 * KB, 4 * KB, 8 * KB, 16 * KB, 32 * KB, 256 * KB, 1 * MB, 4 * MB, 8 * MB, 16 * MB, 32 * MB};
    double *times = malloc(sizeof (double) * (cores));

    enum BenchmarkingMode modes[3];
    modes[0] = Simple_Same_Core;
    modes[1] = Circular;
    modes[2] = Circular_With_Cache_Wiping;
    enum TimingMode tmode = GetTimeOfTheDay;

    int circular_repetitons = 6; //100;
    int repetitons = 1;

    int x = 0;
    int s = 0;


    for (i = 1; i < cores; i++) {
        times[i] = 0;
    }


    for (s = 0; s < sizeof (sizes) / sizeof (int); s++) {
        for (i = 0; i < sizeof (modes) / sizeof (int); i++) {
            printf("\n*****************************************************************************\n");
            printf("Starting For Size :  %i KB With %s", (sizes[s] / KB), get_mode_name(modes[i]));
            printf("\n*****************************************************************************\n");
            do {
                do_analysis(times, sizes[s], c, modes[i], circular_repetitons, sysconf(_SC_NPROCESSORS_ONLN), szClear, tmode);
                x++;
            } while (x < repetitons);

            if (modes[i] == Simple) {
                printf("\nAverage Execution Time for Thread 1 On Core 2 (First Execution on Core 3, No data in L1 Cache) is :  %f\t\n", times[1] / repetitons);
                printf("Average Execution Time for Thread 1 On Core 2 (Second Execution on Core 3, Data in L1 Cache) is   %f\t\n", times[2] / repetitons);
                printf("Average Execution Time for Thread 1 On Core 3 (First Execution on Core 4, No data L1 in Cache) is :  %f\t\n\n", times[3] / repetitons);
            }

            int t;

            for (t = 1; t < cores; t++) {
                printf("Average Execution Time for Thread 1 On Core %i  %f\t\n", t, times[t] / repetitons);
                times[t] = 0;
            }

            x = 0;

            do {
                printf("\n*****************************************************************************\n");
                printf("Doing Same But With LIB PFM Based Counter Readings (%i KB With %s)", (sizes[s] / KB), get_mode_name(modes[i]));
                printf("\n*****************************************************************************\n");
                do_analysis_libPFM(times, sizes[s], c, modes[i], circular_repetitons, sysconf(_SC_NPROCESSORS_ONLN), szClear, tmode);
                x++;
            } while (x < repetitons);

            if (modes[i] == Simple) {
                printf("\nAverage Execution Time for Thread 1 On Core 2 (First Execution on Core 3, No data in L1 Cache) is :  %f\t\n", times[1] / repetitons);
                printf("Average Execution Time for Thread 1 On Core 2 (Second Execution on Core 3, Data in L1 Cache) is   %f\t\n", times[2] / repetitons);
                printf("Average Execution Time for Thread 1 On Core 3 (First Execution on Core 4, No data L1 in Cache) is :  %f\t\n\n", times[3] / repetitons);
            }

            for (t = 1; t < cores; t++) {
                printf("Average Execution Time for Thread 1 On Core %i  %f\t\n", t, times[t] / repetitons);
                times[t] = 0;
            }

            x = 0;
        }
    }

    A1 = B1 - 10;
    return (EXIT_SUCCESS);
}

int * thread_start(void * arg) {
    struct thread_arg *argument = arg;
    int AffinityStatus = stick_this_thread_to_core(argument->core_id);
    if (AffinityStatus != 0)
        printf("Affinity Status %i\n", AffinityStatus);
    int CpuID = sched_getcpu();
    //printf("CPU ID Is %i\t%i\t%i\n", sched_getcpu(), (int) (argument->size) / 1024, (int) (argument->core_id));
    long long elapsed = 0;

    //display_thread_sched_attr("Scheduler attributes of new thread");

    //// Iterations For accessing elements of the array
    //unsigned int iterations = 256*1024*1024; // For Fixed Constant Iterations 
    //unsigned int iterations = (((int) (argument->size)) / 64)*1; //Going Through Entire breadth of working set for a Single Iteration
    //unsigned int iterations =  (((int) (argument->size)) / 64)*N;//Going Through Entire breadth of working set for an N Number Of Iteration

    unsigned int iterations = (((int) (argument->size)) / 64)*1; // Going Through Entire breadth of working set for a constant number of iterations.
    unsigned int i, j;
    ////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////Code For Capturing Cache Misses/////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////////
    int retval;

    retval = PAPI_library_init(PAPI_VER_CURRENT);
    if (retval != PAPI_VER_CURRENT) {
        printf("Error! PAPI_library_init %d\n", retval);
    }

    retval = PAPI_query_event(PAPI_L2_DCM);
    if (retval != PAPI_OK) {
        printf("PAPI_L3_DCA not available\n");
    }

    retval = PAPI_query_event(PAPI_L2_DCA);
    if (retval != PAPI_OK) {
        printf("PAPI_L3_DCA not available\n");
    }

    int *evntarray = malloc(sizeof (int) * 2);
    evntarray[0] = PAPI_L2_DCM;
    evntarray[1] = PAPI_L2_DCA;
    long_long * values = malloc(sizeof (long_long) * 2);
    values[0] = 0;
    values[1] = 0;
    //
    //    long_long * values1 = malloc(sizeof (long_long) * 2);
    //    values1[0] = 0;
    //    values1[1] = 0;
    //
    //    long_long * values2 = malloc(sizeof (long_long) * 2);
    //    values2[0] = 0;
    //    values2[1] = 0;
    //
    //    long_long * values3 = malloc(sizeof (long_long) * 2);
    //    values3[0] = 0;
    //    values3[1] = 0;
    //
    //    long_long * values4 = malloc(sizeof (long_long) * 2);
    //    values4[0] = 0;
    //    values4[1] = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////////

    if (PAPI_start_counters(evntarray, 2) != PAPI_OK)
        printf("error \n");

    struct timeval start, stop; // Variables To Compute execution time
    clock_t startc, finish;
    startc = clock();
    gettimeofday(&start, 0);

    for (i = 0; i < iterations; i++) {
        //argument->arr[(i * 64) & LengthFlag] -= (argument->B - argument->A);
        //argument->arr[((i * 64) * rand()) % (int) argument->size] -= (argument->B - argument->A);
        //argument->arr[((i * 64) * rand()) % (1024 * 256)] -= 20 - 10;
        argument->arr[(i * 64) % (int) (argument->size)] -= (argument->B - argument->A);
    }

    if (argument->tmode == GetTimeOfTheDay) {
        gettimeofday(&stop, 0);
        elapsed = (stop.tv_sec - start.tv_sec)*1000000LL + stop.tv_usec - start.tv_usec;
        argument->seconds = (elapsed * 1.00) / 1000000.00;
    } else {
        finish = clock();
        argument->seconds = (finish - startc);
    }


    if (PAPI_read_counters(values, 2) != PAPI_OK)
        printf("error \n");

    ////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////Code For Capturing Cache Misses/////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////////////////////////////////

    //    printf("Initial Misses,initial Accesses || Misses Before Starting Accessing, Accesses Before Starting Accessing || "
    //            "Misses After Completing Accessing, Accesses After Completing Accessing || Misses After Completing, Accesses After Completing\n");
    //
    //    printf("%lld, % lld || %lld, % lld || %lld, % lld || %lld, % lld\n", values[0], values[1], values1[0] - values[0], values1[1] - values[1]
    //            , values2[0] - values1[0], values2[1] - values1[1], values3[0] - values2[0], values3[1] - values2[1]);
    //
    //    printf("%lld, % lld || %lld, % lld || %lld, % lld || %lld, % lld\n", values[0], values[1], values1[0], values1[1]
    //            , values2[0], values2[1], values3[0], values3[1]);

    for (i = 0; i < 2; i++) {
        if (i == 0)
            printf("L2 Data Cache Misses %lld and execution time %f on core %i\n", values[i], argument->seconds, argument->core_id);
        if (i == 1)
            printf("Total L2 Cache Accesses %lld and execution time %f on core %i\n\n", values[i], argument->seconds, argument->core_id);
    }

    if (PAPI_stop_counters(values, 2) != PAPI_OK)
        printf("error \n");

    PAPI_shutdown();
    ////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////////

    //    if (argument->cache_wiping_flag == 1) {
    //        //        const int size = 256 * 1024; // Allocate 20M. Set much larger then L2
    //        //        char *c = (char *) malloc(size);
    //        //        for (i = 0; i < 1; i++)
    //        //            for (j = 0; j < size; j++)
    //        //                c[j] = i * j;
    //        // Wiping out Cache data
    //        const int size = argument->sizeOfWipingDataStructure;
    //        for (i = 0; i < size; i++)
    //            argument->c[i] = (rand() % 255);
    //    }

    return CpuID;
}

int * thread_start_Using_Libpfm(void * arg) {
    struct thread_arg *argument = arg;
    int AffinityStatus = stick_this_thread_to_core(argument->core_id);
    if (AffinityStatus != 0)
        printf("Affinity Status %i\n", AffinityStatus);
    int CpuID = sched_getcpu();

    long long elapsed = 0;

    //display_thread_sched_attr("Scheduler attributes of new thread");

    unsigned int iterations = (((int) (argument->size)) / 64)*1; // Going Through Entire breadth of working set for a constant number of iterations.
    unsigned int i, j;
    ////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////Code For Capturing Cache Misses/////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////////

    perf_event_desc_t *fds = NULL;
    int ret, num_fds = 0;

    /*
     * Initialize pfm library (required before we can use it)
     */
    ret = pfm_initialize();
    if (ret != PFM_SUCCESS)
        errx(1, "Cannot initialize library: %s", pfm_strerror(ret));

    ret = perf_setup_argv_events(gen_events, &fds, &num_fds);
    if (ret || !num_fds)
        errx(1, "cannot setup events");

    fds[0].fd = -1;
    for (i = 0; i < num_fds; i++) {
        /* request timing information necessary for scaling */
        fds[i].hw.read_format = PERF_FORMAT_SCALE;

        fds[i].hw.disabled = 1; /* do not start now */

        /* each event is in an independent group (multiplexing likely) */
        fds[i].fd = perf_event_open(&fds[i].hw, 0, -1, -1, 0);
        if (fds[i].fd == -1)
            err(1, "cannot open event %d", i);
    }
    /*
     * enable all counters attached to this thread and created by it
     */
    ret = prctl(PR_TASK_PERF_EVENTS_ENABLE);
    print_counts(fds, num_fds, "INITIAL: ");
    ////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////////

    struct timeval start, stop; // Variables To Compute execution time
    clock_t startc, finish;
    startc = clock();
    gettimeofday(&start, 0);

    for (i = 0; i < iterations; i++) {
        //argument->arr[(i * 64) & LengthFlag] -= (argument->B - argument->A);
        //argument->arr[((i * 64) * rand()) % (int) argument->size] -= (argument->B - argument->A);
        //argument->arr[((i * 64) * rand()) % (1024 * 256)] -= 20 - 10;
        argument->arr[((i * 64)) % (int) (argument->size)] -= (argument->B - argument->A);
    }

    if (argument->tmode == GetTimeOfTheDay) {
        gettimeofday(&stop, 0);
        elapsed = (stop.tv_sec - start.tv_sec)*1000000LL + stop.tv_usec - start.tv_usec;
        argument->seconds = (elapsed * 1.00) / 1000000.00;
    } else {
        finish = clock();
        argument->seconds = (finish - startc);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////Code For Capturing Cache Misses/////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////////////////////////////////

    print_counts(fds, num_fds, "FINAL_:  ");

    ////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////////

    printf("\n****************************************");
    printf("Execution time %f on core %i", argument->seconds, argument->core_id);
    printf("********************************************************************************************************************************************************\n");

    //print_counts(fds, num_fds, "INITIAL: ");

    //    if (argument->cache_wiping_flag == 1) {
    //        //        const int size = 256 * 1024; // Allocate 20M. Set much larger then L2
    //        //        char *c = (char *) malloc(size);
    //        //        for (i = 0; i < 1; i++)
    //        //            for (j = 0; j < size; j++)
    //        //                c[j] = i * j;
    //        // Wiping out Cache data
    //        const int size = argument->sizeOfWipingDataStructure;
    //        for (i = 0; i < size; i++)
    //            argument->c[i] = (rand() % 255);
    //    }
    //print_counts(fds, num_fds, "FINAL_:  ");

    printf("*********************************************************************************");
    printf("************************************************************************************************************************************************\n");


    return CpuID;
}