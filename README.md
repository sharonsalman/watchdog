# 🛡️ Watchdog - Fault-Tolerant Supervision System in ANSI C

This project implements a **self-recovering watchdog mechanism** in **pure ANSI C (C89)**, designed to provide *mutual supervision* between a **client process** and a **watchdog process**. It is designed for Linux and includes task scheduling, process respawning, and inter-process signaling.

---

## 🗂 Project Layout

```
watchdog/
├── client.c             # Core logic shared between client and watchdog
├── client.h             # Header file for shared declarations
├── main_client.c        # Entry point for client binary
├── main_watchdog.c      # Entry point for watchdog binary
├── Makefile             # Build system
├── libwatchdog.so       # Shared library built from core components
└── build/               # (Created automatically) Stores .o files
```

External dependency (required for build):
- Located in `../../../ds/src/` – includes:
  - `scheduler.c`, `task.c` — handles timed tasks
  - `p_queue.c`, `sorted_ll.c`, `doubly_ll.c`, `singly_ll.c` — generic data structures
  - `ilrd_uid.c` — unique identifiers for tasks

---

## ⚙️ Build Instructions

### 🔨 Prerequisites

- `gcc` compiler
- GNU `make`
- Linux OS with POSIX threads support

### 🧱 Build Targets

Run from inside the `watchdog/` directory:

```bash
make all         # Builds both release executables and shared library
make debug       # Builds with -g, -DDEBUG, without optimization
make clean       # Cleans up all build files
make install     # Installs binaries and .so to /usr/local
```

### 🔍 Output Files

| Output         | Description                             |
|----------------|------------------------------------------|
| `client`       | Release version of client executable     |
| `watchdog`     | Release version of watchdog executable   |
| `client_debug` | Debug version with DEBUG logs            |
| `watchdog_debug`| Debug version of watchdog               |
| `libwatchdog.so` | Shared library of common logic        |

---

## 🚀 Usage

To start the system, run the **client**. It will automatically spawn the watchdog.

```bash
./client_debug
```

The watchdog will run in the background, and both processes will monitor each other.

---

## 🧠 How It Works – Design Overview

### 🧭 Role Detection

Both `client_debug` and `watchdog_debug` use the same core logic. Role is determined at runtime via the environment variable `PROCESS_ROLE`.

```c
if (getenv("PROCESS_ROLE") == "watchdog")
    role = WATCHDOG;
else
    role = CLIENT;
```

---

### ⛓ Mutual Monitoring

Both client and watchdog:

1. Create a **scheduler** (custom scheduler based on a task queue).
2. Periodically execute a **SendPingTask**:
   - Send `SIGUSR1` to the peer process.
   - If no response received (`PingReceivedHandler`) → increment a `threshold_counter`.
3. If threshold exceeded → schedule **ReviveTask**:
   - Kill dead peer using `SIGKILL`
   - Fork a new peer
   - Pass command-line args via `execvp()` and `setenv()`

---

### 🔁 Scheduler Tasks

Each process schedules the following:

| Task         | Purpose                                           |
|--------------|---------------------------------------------------|
| `SendPingTask` | Sends SIGUSR1 to peer, monitors ping failures    |
| `DNRTask`      | Stops scheduler upon SIGUSR2                     |
| `ReviveTask`   | Created dynamically if peer is unresponsive      |

---

### 🔐 Synchronization

| Mechanism           | Purpose                                      |
|---------------------|----------------------------------------------|
| `sem_open("/wd_ready")` | Ensures client waits for watchdog readiness |
| `g_user_ready` (sem_t) | Synchronizes main thread ↔ user thread       |
| `pthread_create`     | Launches scheduler thread                   |

---

### 📡 Signals Used

- `SIGUSR1`: Ping signal
- `SIGUSR2`: DNR ("Do Not Resuscitate") - stops peer
- `SIGKILL`: Used to terminate failed peer before revival

---

## 🧪 Debug Output Example

```bash
./client_debug
```

```
Starting CLIENT process (PID 667235)
Starting WATCHDOG process (PID 667236)
Client sent ping to WD: 667236
MAIN THREAD: Working... (1)
MAIN THREAD: Working... (2)
WD got ping from client PID: 667235
...
Respawned WD, new PID: 667300
```

Debug messages show:
- Pings sent/received
- Failures detected
- Automatic respawn logic

---

## 🔁 Respawn Logic (Environment-based)

To revive a peer, arguments are saved in environment:

- `CLIENT_ARG_COUNT` – Number of args
- `CLIENT_ARG_0`, `CLIENT_ARG_1`, ..., `CLIENT_ARG_n` – Each arg
- `CLIENT_PID` / `WATCHDOG_PID` – Used to set peer PID
- `PROCESS_ROLE=watchdog` – Used by `DetectRole()` on startup

---

## 🧼 Cleanup

Upon receiving a DNR signal:

- Stop the scheduler (via `g_stop_flag`)
- Call `SchedStop()`, wait on semaphore
- Join user thread
- Clean state and semaphores

---

## 🧰 Features Summary

✅ Fully written in ANSI C (`-std=c89`)  
✅ Thread-safe scheduling  
✅ Mutual supervision  
✅ Dynamic respawning  
✅ POSIX semaphore-based synchronization  
✅ Clean logging in debug mode  
✅ Minimal dependency footprint

---

## 🧑‍💻 Author

Developed by Sharon as part of a system programming project in the Infinity Labs Embedded & System Training Program.

Focus areas:
- Robust inter-process communication
- Fault-tolerant system design
- Modular, testable architecture
- Low-level resource handling and C debugging

---

## 📜 License

This project is released for educational and internal use.
For commercial use, please contact the author.

---

## 🧭 Future Improvements

- Use `shm_open()` to share state between processes
- Add timeout protection for ping waits
- Support more than one client per watchdog
- Log state changes to file or syslog
