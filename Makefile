CC = gcc
CFLAGS = -std=c89 -pedantic -Wall -Wextra -I../../ds/include/
LDLIBS = -lpthread
SRC_DS = ../../ds/src/scheduler.c \
 ../../ds/src/task.c \
 ../../ds/src/p_queue.c \
 ../../ds/src/ilrd_uid.c \
 ../../ds/src/sorted_ll.c \
 ../../ds/src/doubly_ll.c \
 ../../ds/src/singly_ll.c
CLIENT_SRCS = ./client/main_client.c ./client/client.c
WATCHDOG_SRCS = ./watchdog/main_watchdog.c ./watchdog/watchdog.c

# Default target
.PHONY: all clean debug

all: client_debug.out watchdog_debug.out

debug: client_debug.out watchdog_debug.out

client_debug.out: $(CLIENT_SRCS) $(SRC_DS)
	$(CC) $(CFLAGS) -DDEBUG -g -o client_debug.out $^ $(LDLIBS)

watchdog_debug.out: $(WATCHDOG_SRCS) $(SRC_DS)
	$(CC) $(CFLAGS) -DDEBUG -g -o watchdog_debug.out $^ $(LDLIBS)

client.out: $(CLIENT_SRCS) $(SRC_DS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

watchdog.out: $(WATCHDOG_SRCS) $(SRC_DS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f client.out watchdog.out client_debug.out watchdog_debug.out