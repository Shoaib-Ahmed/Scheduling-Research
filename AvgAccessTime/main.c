/* 
 * File:   main.c
 * Author: shoaiba
 *
 * Created on February 27, 2013, 12:04 PM
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <sys/time.h>

// Define a Kilo Byte
#define KB 1024
// Define a Mega Byte
#define MB 1024 * 1024

int main(int *argc, char** argv) {

    // Number of iterations for going through the entire breadth of data.
    int comm_itereations = 1000;
    if (argc > 1)
    {
        comm_itereations = atoi(argv[1]);
        printf("iteration count set to %i \n" , comm_itereations);
    }

    char *arr = (char *) malloc(256 * 1024 * 1024); // 256 MB character array     
    
    unsigned long long int LengthFlag; // Flag To Indicate the end of a Given array size.
    // Once end is reached the elements in the array are accessed again till the total iterations (specified by variable "comm_itereations") are expired.
    unsigned long long int i,s; // Loop iterators
    struct timeval start, stop; // Variables To Compute execution time

    unsigned long long int ArraySizeToAccess[] =
                  {8 * KB, 16 * KB, 32 * KB, 64 * KB, 128 * KB, 256 *KB,
                   512 * KB, 1 * MB, 2 * MB, 3 * MB, 4 * MB,
                   5 * MB, 6 * MB, 7 * MB, 8 * MB, 16 * MB, 32 * MB, 64 * MB, 128 * MB, 256 * MB};
    
    unsigned volatile char A, B; // Declaring two volatile variables, to avoid compiler optimizations.
    A = 10; // Initialize A
    B = 20; // Initialize B

    int Total_Chunks = sizeof (ArraySizeToAccess) / sizeof (long long int);
    
    // Loop to access elements of the array for various array sizes.
    // This outer loop iterates through different array sizes.
    for (s = 0; s < Total_Chunks; s++) {

        // Setting LengthFlag so that when the end of current array Size is reached we start over again from first element
        LengthFlag = ArraySizeToAccess[s] - 1;
        
        long long elapsed = 0;
        double seconds = 0;
        unsigned long long int iterations = ArraySizeToAccess[s];
        iterations = iterations/64;
        iterations = iterations * comm_itereations;

        gettimeofday(&start, 0);

        // Access an element within the array, elements are accessed a cache line apart (Cache line is 64 bytes)
        for (i = 0; i < iterations; i++) {
            arr[(i * 64) & LengthFlag] -= (B - A);
        }

        // Stop Counting Time
        gettimeofday(&stop, 0);
        
        // Calculating Execution Time
        elapsed = (stop.tv_sec - start.tv_sec)*1000000LL + stop.tv_usec - start.tv_usec;
        seconds = (elapsed * 1.00) / 1000000.00; 
        
        double xx = seconds/(iterations * 1.00);

        printf("%d, %30.20f\t\n", ArraySizeToAccess[s] / 1024, xx);
    }
    return 0;
}

