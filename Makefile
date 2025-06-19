# Compiler and flags
CC = gcc
CFLAGS = -std=c89 -pedantic -Wall -Wextra -I../../ds/include/
LDLIBS = -lpthread -ldl

# Build directory
BUILD_DIR = build

# Source file names (without path)
SRC_DS_SHORT = scheduler.c task.c p_queue.c ilrd_uid.c \
               sorted_ll.c doubly_ll.c singly_ll.c

# Full paths to source files
SRC_DS = $(addprefix ../../ds/src/, $(SRC_DS_SHORT))

# Main source files
CLIENT_MAIN_SRC = main_client.c
WATCHDOG_MAIN_SRC = main_watchdog.c
CLIENT_SRC = client.c

# Object files
LIB_OBJS = $(addprefix $(BUILD_DIR)/, $(SRC_DS_SHORT:.c=.o)) \
           $(BUILD_DIR)/client.o

CLIENT_OBJS = $(BUILD_DIR)/$(CLIENT_MAIN_SRC:.c=.o)
WATCHDOG_OBJS = $(BUILD_DIR)/$(WATCHDOG_MAIN_SRC:.c=.o)

# Targets
SHARED_LIB = libwatchdog.so
CLIENT_EXEC = client
WATCHDOG_EXEC = watchdog
CLIENT_DEBUG = client_debug
WATCHDOG_DEBUG = watchdog_debug

.PHONY: all clean debug release libs install help

# Default target
all: libs release

debug: $(CLIENT_DEBUG) $(WATCHDOG_DEBUG)

release: $(CLIENT_EXEC) $(WATCHDOG_EXEC)

libs: $(SHARED_LIB)

# Shared library
$(SHARED_LIB): $(LIB_OBJS)
	$(CC) -shared -o $@ $^ $(LDLIBS)

# Debug executables
$(CLIENT_DEBUG): $(CLIENT_MAIN_SRC) $(LIB_OBJS)
	$(CC) $(CFLAGS) -DDEBUG -g -o $@ $^ $(LDLIBS)

$(WATCHDOG_DEBUG): $(WATCHDOG_MAIN_SRC) $(LIB_OBJS)
	$(CC) $(CFLAGS) -DDEBUG -g -o $@ $^ $(LDLIBS)

# Release executables
$(CLIENT_EXEC): $(CLIENT_OBJS) $(SHARED_LIB)
	$(CC) $(CFLAGS) -O2 -o $@ $(CLIENT_OBJS) -L. -lwatchdog $(LDLIBS)

$(WATCHDOG_EXEC): $(WATCHDOG_OBJS) $(SHARED_LIB)
	$(CC) $(CFLAGS) -O2 -o $@ $(WATCHDOG_OBJS) -L. -lwatchdog $(LDLIBS)

# Generic rule for building object files from DS sources
$(BUILD_DIR)/%.o: ../../ds/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

# Compile client.c
$(BUILD_DIR)/client.o: client.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

# Compile main source files
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Install
install: release libs
	sudo cp $(CLIENT_EXEC) $(WATCHDOG_EXEC) /usr/local/bin/
	sudo cp $(SHARED_LIB) /usr/local/lib/
	sudo ldconfig
	@echo "Installed to system"

# Clean
clean:
	rm -f $(CLIENT_EXEC) $(WATCHDOG_EXEC) $(CLIENT_DEBUG) $(WATCHDOG_DEBUG)
	rm -f $(SHARED_LIB)
	rm -rf $(BUILD_DIR)
	rm -f *.o

# Help
help:
	@echo "Targets:"
	@echo "  make         - Build everything"
	@echo "  make debug   - Build debug versions"
	@echo "  make release - Build optimized release"
	@echo "  make libs    - Build shared library"
	@echo "  make install - Copy files to system"
	@echo "  make clean   - Remove build artifacts"
