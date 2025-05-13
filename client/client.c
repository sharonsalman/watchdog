#define _POSIX_C_SOURCE 200809L

#include "client.h"
#include "scheduler.h"

#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>

/* === Globals === */
static pthread_t g_user_thread = 0;
static volatile sig_atomic_t g_threshold_counter = 0;
static volatile sig_atomic_t g_watchdog_pid = 0;
static sem_t g_failed_pings;
static sem_t *g_wd_ready_sem;
static volatile sig_atomic_t g_stop_flag = 0;
scheduler_t *g_sched = NULL;
static size_t g_interval = 0;

/* === Internal Functions === */
static void InitSignalHandlers(void);
static void PingReceivedHandler(int sig, siginfo_t *info, void *ucontext);
static void DNRHandler(int sig, siginfo_t *info, void *ucontext);
static void* RunScheduler(void* param);
static ssize_t ReviveWDTask(void *param);
static ssize_t SendPingWDTask(void *param);
static ssize_t DNRTask(void *param);
static void CleanupParam(void *param);
static void ForkAndExecWatchdog(void);

void MMI(size_t interval, size_t threshold, char* argv[])
{
    size_t *respawn_threshold = malloc(sizeof(size_t));
    char pid_str[20];
    char arg_count_str[20];
    char arg_key[32];
    int i = 0;

    g_interval = interval;
    if (NULL == respawn_threshold)
    {
        return;
    }
    *respawn_threshold = threshold;

    /* Save command line arguments to environment */
    if (argv != NULL)
    {
        /* Count arguments */
        for (i = 0; argv[i] != NULL; ++i);
        
        /* Save argument count */
        sprintf(arg_count_str, "%d", i);
        setenv("CLIENT_ARG_COUNT", arg_count_str, 1);
        
        /* Save each argument */
        for (i = 0; argv[i] != NULL; i++)
        {
            sprintf(arg_key, "CLIENT_ARG_%d", i);
            setenv(arg_key, argv[i], 1);
        }
    }
    else
    {
        setenv("CLIENT_ARG_COUNT", "0", 1);
    }

    /* save client pid as an environment variable*/
    sprintf(pid_str, "%d", getpid());
    setenv("CLIENT_PID", pid_str, 1);

    sem_init(&g_failed_pings, 0, 0);

    g_wd_ready_sem = sem_open("/wd_ready", O_CREAT, 0666, 0);
    if (SEM_FAILED == g_wd_ready_sem) 
    {
        perror("sem_open");
    }

    /* Check for existing watchdog and update global variable */
    if (NULL == getenv("WATCHDOG_PID")) 
    {
        ForkAndExecWatchdog();
    }
    else
    {
        g_watchdog_pid = atoi(getenv("WATCHDOG_PID"));
    }

    InitSignalHandlers();
    
    g_sched = SchedCreate();
    if (!g_sched) return;
    
    SchedAddTask(g_sched, time(NULL),
    SendPingWDTask, NULL,
    NULL, NULL, interval);
    
    SchedAddTask(g_sched, time(NULL), ReviveWDTask,
        respawn_threshold, CleanupParam,
        respawn_threshold, interval);
        
    SchedAddTask(g_sched, 1,
        DNRTask, g_sched,
        NULL, NULL, 1);
    
    /* wait for the wd to be ready */
    while(sem_wait(g_wd_ready_sem));

    pthread_create(&g_user_thread, NULL, RunScheduler, g_sched);
    
    sem_close(g_wd_ready_sem);
    sem_unlink("/wd_ready");
}

void DNR(void)
{
    g_stop_flag = 1;

    kill(g_watchdog_pid, SIGUSR2);

    /* wait for the DNRtask to be completed */
    while(sem_wait(&g_failed_pings));
    pthread_join(g_user_thread, NULL);
    sem_destroy(&g_failed_pings);

    g_watchdog_pid = 0;
    g_threshold_counter = 0;
    #ifdef DEBUG
    printf("Client shutdown complete\n");
    fflush(stdout);
    #endif 
}

/* === New WD === */

static void ForkAndExecWatchdog(void)
{
    pid_t child_pid = fork();
    char pid_str[20];
    char client_str[20];
    char *args[] = {"./watchdog_debug.out", NULL};
    
    if (child_pid < 0)
    {
        perror("fork failed");
        return;
    }

    if (child_pid > 0) /* in client, save the wd id */
    {
        snprintf(pid_str, sizeof(pid_str), "%d", child_pid);
        setenv("WATCHDOG_PID", pid_str, 1);
        g_watchdog_pid = child_pid;
    }
    else /* in wd , save the client id and exec */
    {
        snprintf(client_str, sizeof(client_str), "%d", getppid());
        setenv("CLIENT_PID", client_str, 1);

        execvp(args[0], args);
        perror("execvp failed");
    }
}

/* === Signals === */
static void InitSignalHandlers(void)
{
    struct sigaction sa = {0};
    sa.sa_sigaction = PingReceivedHandler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGUSR1, &sa, NULL);
    sigemptyset(&sa.sa_mask);

    sa.sa_sigaction = DNRHandler;
    sigaction(SIGUSR2, &sa, NULL);
}

/* === Handlers === */
static void PingReceivedHandler(int sig, siginfo_t *info, void *ucontext)
{
    (void)sig;
    (void)ucontext;
    (void)info;

    g_threshold_counter = 0;
#ifdef DEBUG
    printf("Client received ping from WD: %d\n", g_watchdog_pid);
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

/* === Sched === */
static void* RunScheduler(void* param)
{
    SchedStart((scheduler_t*) param);
    SchedDestroy((scheduler_t*) param);
    return NULL;
}

/* === Tasks === */
static ssize_t SendPingWDTask(void *param)
{
    (void)param;
    if (0 == g_stop_flag)
    {
        kill(g_watchdog_pid, SIGUSR1);
        ++g_threshold_counter;
    #ifdef DEBUG 
        printf("Client sent ping to WD: %d\n", g_watchdog_pid);
        fflush(stdout);
    #endif
    }
    
    return 1;
}

static ssize_t ReviveWDTask(void *param)
{
    size_t threshold = *(size_t*)param;
    char *args[] = {"./watchdog_debug.out", NULL};
    char pid_str[20];
    pid_t child_pid;

    if (g_stop_flag)
    {
        return 0;
    }

#ifdef DEBUG 
    printf("Checking WD status, counter: %d\n", g_threshold_counter);
#endif

    if ((size_t)g_threshold_counter > threshold)
    {
        kill(g_watchdog_pid, SIGKILL);
        child_pid = fork();
        if (child_pid == 0) /* inside child */
        {
            execvp(args[0], args);
            perror("execvp failed");
        }
        else if (child_pid > 0) /* in parent */
        {
            snprintf(pid_str, sizeof(pid_str), "%d", child_pid);
            setenv("WATCHDOG_PID", pid_str, 1);
            g_watchdog_pid = child_pid;
            g_threshold_counter = 0;
        #ifdef DEBUG 
            printf("Respawned WD, new PID: %d\n", child_pid);
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
        sem_post(&g_failed_pings);
    #ifdef DEBUG 
        printf("Client scheduler stopped via DNR\n");
        fflush(stdout);
    #endif
    }
    #ifdef DEBUG 
    printf("Inside DNR, continue process.\n");
    fflush(stdout);
#endif  
    return 0;
}

/* === Cleanup === */
static void CleanupParam(void *param)
{
    free(param);
}
