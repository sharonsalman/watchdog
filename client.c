#define _POSIX_C_SOURCE 200809L

#include "client.h"
#include "scheduler.h"

#include <semaphore.h>  /* sem_t, sem_open */
#include <signal.h>     /* sigaction, kill */
#include <stdio.h>      /* printf */
#include <unistd.h>     /* fork, execvp, getpid */
#include <stdlib.h>     /* malloc, free, getenv */
#include <time.h>       /* time */
#include <fcntl.h>      /* O_CREAT */
#include <pthread.h>    /* pthread */
#include <string.h>     /* strcmp */

/* === Role Detection === */
typedef enum {
    ROLE_CLIENT,
    ROLE_WATCHDOG
} process_role_t;

/* === Globals === */
static pthread_t g_user_thread = 0;
static volatile sig_atomic_t g_threshold_counter = 0;
static volatile sig_atomic_t g_peer_pid = 0;  /* client's watchdog PID or watchdog's client PID */
static sem_t g_failed_pings;
static sem_t g_user_ready;
static sem_t *g_wd_ready_sem;
static volatile sig_atomic_t g_stop_flag = 0;
scheduler_t *g_sched = NULL;
static size_t g_interval = 0;
static process_role_t g_current_role = ROLE_CLIENT;

/* === Internal Functions === */
static process_role_t DetectRole(void);
static void InitSignalHandlers(void);
static void PingReceivedHandler(int sig, siginfo_t *info, void *ucontext);
static void DNRHandler(int sig, siginfo_t *info, void *ucontext);
static void* RunScheduler(void* param);
static ssize_t ReviveTask(void *param);
static ssize_t SendPingTask(void *param);
static ssize_t DNRTask(void *param);
static void CleanupParam(void *param);
static void ForkAndExecPeer(void);
static void StartWatchdogMode(size_t interval, size_t threshold);

/* === Role Detection === */
static process_role_t DetectRole(void)
{
    char *role_env = getenv("PROCESS_ROLE");
    if (role_env && strcmp(role_env, "watchdog") == 0)
    {
        return ROLE_WATCHDOG;
    }
    return ROLE_CLIENT;
}

/* === Main Entry Points === */
void MMI(size_t interval, size_t threshold, char* argv[])
{
    g_current_role = DetectRole();
    
    if (g_current_role == ROLE_WATCHDOG)
    {
        StartWatchdogMode(interval, threshold);
        return;
    }
    
    /* Client mode logic */
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
        ForkAndExecPeer();
    }
    else
    {
        g_peer_pid = atoi(getenv("WATCHDOG_PID"));
    }

    InitSignalHandlers();
    
    g_sched = SchedCreate();
    if (!g_sched) return;
    
    SchedAddTask(g_sched, time(NULL),
    SendPingTask, respawn_threshold,
    CleanupParam, respawn_threshold, interval);
    
    SchedAddTask(g_sched, 1,
        DNRTask, g_sched,
        NULL, NULL, 1);
    
    /* wait for the wd to be ready */
    while(sem_wait(g_wd_ready_sem));
    /* user wait for the thread to be ready */
    sem_init(&g_user_ready, 0, 0);
    pthread_create(&g_user_thread, NULL, RunScheduler, g_sched);
    sem_wait(&g_user_ready);

    sem_close(g_wd_ready_sem);
    sem_unlink("/wd_ready");
}

/* === Watchdog Mode Entry Point === */
static void StartWatchdogMode(size_t interval, size_t threshold)
{
    size_t *threshold_alloc = NULL;
    char *client_pid_env = getenv("CLIENT_PID");

    if (NULL == client_pid_env)
    {
        fprintf(stderr, "CLIENT_PID not set\n");
        return;
    }
    g_peer_pid = atoi(client_pid_env);

    g_wd_ready_sem = sem_open("/wd_ready", O_CREAT, 0666, 0);
    if (SEM_FAILED == g_wd_ready_sem)
    {
        perror("sem_open");
        return;
    }

    InitSignalHandlers();

    g_sched = SchedCreate();
    if (!g_sched)
    {
        return;
    }

    threshold_alloc = malloc(sizeof(size_t));
    if (NULL == threshold_alloc)
    {
        SchedDestroy(g_sched);
        return;
    }
    *threshold_alloc = threshold;

    SchedAddTask(g_sched, time(NULL), SendPingTask,
    threshold_alloc, CleanupParam,
    threshold_alloc, interval);

    SchedAddTask(g_sched, time(NULL),
    DNRTask, g_sched,
    NULL, NULL, 1);

    pthread_create(&g_user_thread, NULL, RunScheduler, g_sched);

    sem_post(g_wd_ready_sem);
    sem_close(g_wd_ready_sem);
    
    /* Keep watchdog running */
    while(!g_stop_flag) 
    {
        sleep(1);
    }
    
    pthread_join(g_user_thread, NULL);
}

void DNR(void)
{
    g_stop_flag = 1;

    kill(g_peer_pid, SIGUSR2);

    /* wait for the DNRtask to be completed */
    while(sem_wait(&g_failed_pings));
    pthread_join(g_user_thread, NULL);
    sem_destroy(&g_failed_pings);

    g_peer_pid = 0;
    g_threshold_counter = 0;
    #ifdef DEBUG
    printf("Process shutdown complete\n");
    fflush(stdout);
    #endif 
}

/* === Fork and Exec Peer Process === */
static void ForkAndExecPeer(void)
{
    pid_t child_pid = fork();
    char pid_str[20];
    char client_str[20];
    char *args[] = {"./watchdog_debug", NULL};
    
    if (child_pid < 0)
    {
        perror("fork failed");
        return;
    }

    if (child_pid > 0) /* in client, save the wd id */
    {
        snprintf(pid_str, sizeof(pid_str), "%d", child_pid);
        setenv("WATCHDOG_PID", pid_str, 1);
        g_peer_pid = child_pid;
    }
    else /* in wd , save the client id and exec */
    {
        snprintf(client_str, sizeof(client_str), "%d", getppid());
        setenv("CLIENT_PID", client_str, 1);
        setenv("PROCESS_ROLE", "watchdog", 1);

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
    if (g_current_role == ROLE_CLIENT)
    {
        printf("Client received ping from WD: %d\n", g_peer_pid);
    }
    else
    {
        printf("WD got ping from client PID: %d\n", g_peer_pid);
    }
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

/* === Scheduler === */
static void* RunScheduler(void* param)
{
    sem_post(&g_user_ready);
    SchedStart((scheduler_t*) param);
    SchedDestroy((scheduler_t*) param);
    return NULL;
}

/* === Unified Tasks === */
static ssize_t SendPingTask(void *param)
{
    size_t threshold = *(size_t*)param;
    if (0 == g_stop_flag)
    {
        kill(g_peer_pid, SIGUSR1);
        ++g_threshold_counter;
    #ifdef DEBUG
        if (g_current_role == ROLE_CLIENT)
        {
            printf("Client sent ping to WD: %d\n", g_peer_pid);
        }
        else
        {
            printf("WD sent ping to client PID: %d, counter: %d\n", g_peer_pid, g_threshold_counter);
        }
        fflush(stdout);
    #endif
    }
    
    if((size_t)g_threshold_counter > threshold)
    {
        size_t *respawn_threshold = malloc(sizeof(size_t));
        if (respawn_threshold)
        {
            *respawn_threshold = threshold;
            SchedAddTask(g_sched, time(NULL), ReviveTask,
            respawn_threshold, CleanupParam,
            respawn_threshold, 0);
        }
    }
    return (g_current_role == ROLE_CLIENT) ? 1 : 0;
}

static ssize_t ReviveTask(void *param)
{
    size_t threshold = *(size_t*)param;
    char pid_str[20];
    char watchdog_pid_str[20];
    char *client_args[] = {"./client_debug", NULL};
    char *watchdog_args[] = {"./watchdog_debug", NULL};
    char **args = (g_current_role == ROLE_CLIENT) ? watchdog_args : client_args;
    pid_t child_pid;
    pid_t my_pid = getpid();

    if (g_stop_flag)
    {
        return 0;
    }

#ifdef DEBUG 
    printf("Checking peer status, counter: %d\n", g_threshold_counter);
    fflush(stdout);
#endif

    kill(g_peer_pid, SIGKILL);
    
    if (g_current_role == ROLE_WATCHDOG)
    {
        /* Watchdog reviving client - need to reconstruct client args */
        char arg_key[32];
        char *arg_count_str = getenv("CLIENT_ARG_COUNT");
        int arg_count = 0;
        char **client_argv = NULL;
        int i;
        
        snprintf(watchdog_pid_str, sizeof(watchdog_pid_str), "%d", my_pid);
        setenv("WATCHDOG_PID", watchdog_pid_str, 1);
        
        child_pid = fork();
        
        if (child_pid == 0)
        {
            /* Child process - reconstruct arguments */
            if (arg_count_str != NULL)
            {
                arg_count = atoi(arg_count_str);
                if (arg_count > 0 && arg_count < 100)
                {
                    client_argv = (char**)malloc((arg_count + 1) * sizeof(char*));
                    if (client_argv != NULL)
                    {
                        for (i = 0; i < arg_count; i++)
                        {
                            sprintf(arg_key, "CLIENT_ARG_%d", i);
                            client_argv[i] = getenv(arg_key);
                            if (client_argv[i] == NULL)
                            {
                                break;
                            }
                        }
                        
                        if (i == arg_count)
                        {
                            client_argv[arg_count] = NULL;
                            execvp(client_argv[0], client_argv);
                        }
                        free(client_argv);
                    }
                }
            }
            
            execvp(args[0], args);
            perror("execvp failed");
        }
        else if (child_pid > 0)
        {
            snprintf(pid_str, sizeof(pid_str), "%d", child_pid);
            setenv("CLIENT_PID", pid_str, 1);
            g_peer_pid = child_pid;
            g_threshold_counter = 0;
#ifdef DEBUG
            printf("Respawned client, new PID=%d\n", child_pid);
            fflush(stdout);
#endif

            g_wd_ready_sem = sem_open("/wd_ready", O_CREAT, 0666, 0);
            if (SEM_FAILED == g_wd_ready_sem)
            {
                perror("sem_open");
            }
            else
            {
                sem_post(g_wd_ready_sem);
                sem_close(g_wd_ready_sem);
            }
        }
    }
    else
    {
        /* Client reviving watchdog */
        child_pid = fork();
        if (child_pid == 0)
        {
            setenv("PROCESS_ROLE", "watchdog", 1);
            execvp(args[0], args);
            perror("execvp failed");
        }
        else if (child_pid > 0)
        {
            snprintf(pid_str, sizeof(pid_str), "%d", child_pid);
            setenv("WATCHDOG_PID", pid_str, 1);
            g_peer_pid = child_pid;
            g_threshold_counter = 0;
        #ifdef DEBUG 
            printf("Respawned WD, new PID: %d\n", child_pid);
            fflush(stdout);
        #endif

            g_wd_ready_sem = sem_open("/wd_ready", O_CREAT, 0666, 0);
            if (SEM_FAILED == g_wd_ready_sem)
            {
                perror("sem_open");
            }
            else
            {
                while(sem_wait(g_wd_ready_sem));
                sem_close(g_wd_ready_sem);
            }
        }
    }

    return -1;
}

static ssize_t DNRTask(void *param)
{
    if (g_stop_flag)
    {
        SchedStop((scheduler_t*)param);
        if (g_current_role == ROLE_CLIENT)
        {
            sem_post(&g_failed_pings);
        }
    #ifdef DEBUG 
        printf("Scheduler stopped via DNR\n");
        fflush(stdout);
    #endif
    }
    return 0;
}

/* === Cleanup === */
static void CleanupParam(void *param)
{
    free(param);
}
