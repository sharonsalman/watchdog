#define _POSIX_C_SOURCE 200809L

#include <stdio.h>  /* for printf */
#include <unistd.h> /* for getpid */
#include <stdlib.h> /* for setenv */
#include "client.h"  /* Now uses unified client code */

int main(int argc, char *argv[], char *envp[])
{
    size_t interval = 1;
    size_t threshold = 3;
    
    (void)argc;
    (void)argv;
    (void)envp;
    
    /* Set the role to watchdog mode */
    setenv("PROCESS_ROLE", "watchdog", 1);
    
    printf("Starting WATCHDOG process (PID %d)\n", getpid());
    
    /* Use the unified MMI function which will detect watchdog role */
    MMI(interval, threshold, NULL);
    
    printf("Goodbye watchdog!\n");
    return 0;
}
