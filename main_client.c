#define _POSIX_C_SOURCE 200809L

#include <stdio.h>    /* printf */
#include <unistd.h>   /* sleep */
#include "client.h"

int main(int argc, char *argv[])
{
    size_t interval = 1;
    size_t threshold = 3;
    size_t i = 0;
    (void)argv;
    (void)argc;
    
    printf("Starting CLIENT process (PID %d)\n", getpid());
    /* Start watchdog monitoring */
    MMI(interval, threshold, argv);
    printf("Simulating client workload...\n");
    
    /* Simulate some real work */
    for (i = 0; i < 10; ++i)
    {
        printf("MAIN THREAD: Working... (%lu)\n", i + 1);
        sleep(1);
    }
    
    DNR();
    printf("CLIENT finished gracefully.\n");
    return 0;
}
