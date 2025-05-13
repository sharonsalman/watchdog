#define _POSIX_C_SOURCE 200809L

#include "watchdog.h"
#include "scheduler.h"

#include <stdio.h>      /* printf */
#include <stdlib.h>     /* malloc, free, getenv */
#include <signal.h>     /* sigaction, kill */
#include <unistd.h>     /* fork, execvp, getpid */
#include <time.h>       /* time */
#include <semaphore.h>  /* sem_t, sem_open */
#include <fcntl.h>      /* O_CREAT */


/* === Global Variables === */
static pthread_t g_wd_thread = 0;
static volatile sig_atomic_t g_failed_pings = 0;
static volatile sig_atomic_t g_client_pid = 0;
static volatile sig_atomic_t g_stop_flag = 0;
static sem_t *g_wd_ready_sem = NULL;

/* === Function Declarations === */
static void InitSignalHandlers(void);
static void PingReceivedHandler(int sig, siginfo_t *info, void *ucontext);
static void DNRHandler(int sig, siginfo_t *info, void *ucontext);
static ssize_t ReviveClientTask(void *param);
static ssize_t SendPingClientTask(void *param);
static ssize_t DNRTask(void *param);
static void CleanupThreshold(void *param);
static void* RunScheduler(void *param);

/* === Watchdog Main Entry Point === */
void WatchdogStart(size_t interval, size_t threshold)
{
    scheduler_t *sched = NULL;
    size_t *threshold_alloc = NULL;
    char *client_pid_env = getenv("CLIENT_PID");

    if (NULL == client_pid_env)
    {
        fprintf(stderr, "CLIENT_PID not set\n");
        return;
    }
    g_client_pid = atoi(client_pid_env);

    g_wd_ready_sem = sem_open("/wd_ready", O_CREAT, 0666, 0);
    if (SEM_FAILED == g_wd_ready_sem)
    {
        perror("sem_open");
        return;
    }

    InitSignalHandlers();

    sched = SchedCreate();
    if (!sched)
    {
        return;
    }

    threshold_alloc = malloc(sizeof(size_t));
    if (NULL == threshold_alloc)
    {
        SchedDestroy(sched);
        return;
    }
    *threshold_alloc = threshold;

    SchedAddTask(sched, time(NULL), SendPingClientTask,
    NULL, NULL,
    NULL, interval);

    SchedAddTask(sched, time(NULL), ReviveClientTask,
    threshold_alloc, CleanupThreshold,
    threshold_alloc, interval);

    SchedAddTask(sched, time(NULL),
    DNRTask, sched,
    NULL, NULL, 1);

    pthread_create(&g_wd_thread, NULL, RunScheduler, sched);

    sem_post(g_wd_ready_sem);
    sem_close(g_wd_ready_sem);
}

/* === Scheduler Thread === */
static void* RunScheduler(void *param)
{
    SchedStart((scheduler_t*)param);
    SchedDestroy((scheduler_t*)param);
    return NULL;
}

/* === Signal Handlers === */
static void InitSignalHandlers(void)
{
    struct sigaction sa = {0};

    sa.sa_sigaction = PingReceivedHandler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    sa.sa_sigaction = DNRHandler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR2, &sa, NULL);
}

static void PingReceivedHandler(int sig, siginfo_t *info, void *ucontext)
{
    (void)sig;
    (void)ucontext;
    (void)info;

    g_failed_pings = 0;
    #ifdef DEBUG
        printf("WD got ping from client PID: %d\n", g_client_pid);
        fflush(stdout);
    #endif
}

static void DNRHandler(int sig, siginfo_t *info, void *ucontext)
{
    (void)sig;
    (void)info;
    (void)ucontext;

    g_stop_flag = 1;
}

/* === Tasks === */
static ssize_t SendPingClientTask(void *param)
{
    (void)param;
    if (0 == g_stop_flag)
    {
        kill(g_client_pid, SIGUSR1);
        ++g_failed_pings;
        #ifdef DEBUG
            printf("WD sent ping to client PID: %d, counter: %d\n", g_client_pid, g_failed_pings);
            fflush(stdout);
        #endif
    }

    return 0;
}

static ssize_t ReviveClientTask(void *param)
{
    size_t threshold = *(size_t*)param;
    char pid_str[20];
    char watchdog_pid_str[20];
    char *args[] = {"./client_debug.out", NULL};
    pid_t child_pid;
    pid_t my_pid = getpid();
    char arg_key[32];
    char *arg_count_str = NULL;
    int arg_count = 0;
    char **client_args = NULL;
    int i;

    if (g_stop_flag)
    {
        return 0;
    }

#ifdef DEBUG
    printf("Checking client status, counter: %d\n", g_failed_pings);
    fflush(stdout);
#endif

    if ((size_t)g_failed_pings > threshold)
    {
        kill(g_client_pid, SIGKILL);
        
        /* Set our PID in environment before forking */
        snprintf(watchdog_pid_str, sizeof(watchdog_pid_str), "%d", my_pid);
        setenv("WATCHDOG_PID", watchdog_pid_str, 1);
        
        child_pid = fork();

        if (child_pid == 0)
        {
            /* Child process - reconstruct arguments (pass back) */
            arg_count_str = getenv("CLIENT_ARG_COUNT");
            if (arg_count_str != NULL)
            {
                arg_count = atoi(arg_count_str);
                /* limit of args is 100 */
                if (arg_count > 0 && arg_count < 100)
                {
                    client_args = (char**)malloc((arg_count + 1) * sizeof(char*));
                    if (client_args != NULL)
                    {
                        for (i = 0; i < arg_count; i++)
                        {
                            sprintf(arg_key, "CLIENT_ARG_%d", i);
                            client_args[i] = getenv(arg_key);
                            /* Check if argument was retrieved successfully */
                            if (client_args[i] == NULL)
                            {
                                break;
                            }
                        }
                        
                        /* Only use reconstructed args if we got all of them */
                        if (i == arg_count)
                        {
                            client_args[arg_count] = NULL;
                            execvp(client_args[0], client_args);
                        }
                        /* Free if execvp fails or if we didn't get all args */
                        free(client_args);
                    }
                }
            }
            
            /* Go back to default args if any step fails */
            execvp(args[0], args);
            perror("execvp failed");
        }
        else if (child_pid > 0)
        {
            snprintf(pid_str, sizeof(pid_str), "%d", child_pid);
            setenv("CLIENT_PID", pid_str, 1);
            g_client_pid = child_pid;
            g_failed_pings = 0;
#ifdef DEBUG
            printf("Respawned client, new PID=%d\n", child_pid);
            fflush(stdout);
#endif
        }
    }

    return 0;
}

static ssize_t DNRTask(void *param)
{
    if (g_stop_flag)
    {
        SchedStop((scheduler_t*)param);
    #ifdef DEBUG
        printf("WD received stop flag. Shutting down gracefully.\n");
        fflush(stdout);
    #endif
    }
    return 0;
}

/* === Cleanup === */
static void CleanupThreshold(void *param)
{
    free(param);
}