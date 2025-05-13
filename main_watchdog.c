#define _POSIX_C_SOURCE 200809L

#include <stdio.h>  /* for printf */
#include <unistd.h> /* for getpid */
#include "watchdog.h"

int main(int argc, char *argv[], char *envp[])
{
    size_t interval = 1;
    size_t threshold = 3;
    
    (void)argc;
    (void)argv;
    (void)envp;
    
    printf("Starting WATCHDOG process (PID %d)\n", getpid());
    
    WatchdogStart(interval, threshold);
    printf("Watchdog starting...\n");
    /* Add an infinite loop to keep the watchdog process running */
    while(1) 
    {
        sleep(10);
    }

    printf("Goodbye watchdog!\n");
    return 0;
}
