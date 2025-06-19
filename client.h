#ifndef CLIENT_H
#define CLIENT_H

#include <pthread.h>

/* Activate watchdog monitoring over the user critical code.
    The function receives inteval in seconds,
    max number of unrecived signals and importants args that the user wants to keep. */
void MMI(size_t interval, size_t threshold, char* argv[]);

/* Shutdown function of the watchdog.
    User code continues without the monitoring of the watchdog. */
void DNR(void);

#endif
