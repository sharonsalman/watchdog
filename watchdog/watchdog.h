#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stddef.h> /* For size_t */

void WatchdogStart(size_t interval, size_t threshold);

#endif  

