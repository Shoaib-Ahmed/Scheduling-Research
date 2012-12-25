/*
 * File:   main.c
 * Author: shoaiba
 *
 * Created on December 21, 2012, 11:42 AM
 */


#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

// Define a Kilo Byte
#define KB 1024
// Define a Mega Byte
#define MB 1024 * 1024

int main() {
    char *arr = (char *) malloc(12 * 1024 * 1024); // 12 MB character array
    unsigned int iterations = 256 * 1024 * 1024; // Iterations For accessing elements of the array
    
    int LengthFlag; // Flag To Indicate the end of a Given array size. 
    // Once end is reached the elements in the array are accessed again till the total iterations are expired.
    unsigned int i,s; // Loop iterators
    struct timeval start, stop; // Variables To Compute execution time

    printf("Size Of Char Is  %i \n", (sizeof (char))); // Printing out the size of char in terms of bytes

    int ArraySizeToAccess[] = 
                  {1 *   KB, 4 * KB, 8 *   KB, 16 * KB, 32 *   KB, 64 * KB, 128 *  KB, 256 *KB,
                   512 * KB, 1 * MB, 1.5 * MB, 2 *  MB, 2.5 *  MB, 3 *  MB, 3.5 *  MB, 4 *  MB,
                   4.5 * MB, 5 * MB, 5.5 * MB, 6 *  MB, 6.5 *  MB, 7 *  MB, 7.5 *  MB, 8 *  MB,
                   8.5 * MB, 9 * MB, 9.5 * MB, 10 * MB, 10.5 * MB, 11 * MB, 11.5 * MB, 12 * MB};
    
    unsigned volatile char A, B; // Declaring two volatile variables, to avoid compiler optimizations.
    A = 10; // Initialize A
    B = 20; // Initialize B

    printf("ArraySize, Execution Time\n");
    
    // Loop to access elements of the array for various array sizes.
    // This outer loop iterates through different array sizes.
    for (s = 0; s < sizeof (ArraySizeToAccess) / sizeof (int); s++) {

        // Setting LengthFlag so that when the end of current array Size is reached we start over again from first element
        LengthFlag = ArraySizeToAccess[s] - 1;
        
        long long elapsed = 0;
        double seconds = 0;
        
        // Start Counting Time
        gettimeofday(&start, 0);

        // Access an element within the array, elements are accessed a cache line apart (Cache line is 64 bytes)
        for (i = 0; i < iterations; i++) {
            // access and assign a value to the first byte of each cache line, for a given size
            // Continue this for a given number of iterations.
            arr[(i * 64) & LengthFlag] -= (B - A);
        }

        // Stop Counting Time
        gettimeofday(&stop, 0);
        
        // Calculating Execution Time
        elapsed = (stop.tv_sec - start.tv_sec)*1000000LL + stop.tv_usec - start.tv_usec;
        seconds = (elapsed * 1.00) / 1000000.00;

        // Printing out the Size of array and associated execution time.
        printf("%d, %f\t\n", ArraySizeToAccess[s] / 1024, seconds);
    }
    return 0;
}