#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <sys/wait.h>

#include "defs.h"
#include "logger.h"
#include "cron.h"

typedef struct Task {
    int id;
    char command[MAX_CMD_LEN];
    ScheduleMode mode;
    timer_t timer_id;
    struct Task *next;
} Task;

static volatile sig_atomic_t server_running = 1;
static mqd_t server_mq = -1;
static int task_id_counter = 0;
static Task *task_list_head = NULL;

static pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;

static void sigint_handler(int sig) {
    (void)sig;
    server_running = 0;
}

static void cron_dump_state(FILE* stream) {
    fprintf(stream, "--- TASK LIST DUMP ---\n");
    pthread_mutex_lock(&list_mutex);
    Task *curr = task_list_head;
    if (!curr) {
        fprintf(stream, "(List is empty)\n");
    }
    while(curr) {
        const char* mstr = (curr->mode == MODE_CYCLIC) ? "CYCLIC" :
                           (curr->mode == MODE_ABSOLUTE) ? "ABSOLUTE" : "RELATIVE";
        fprintf(stream, "ID: %d | Mode: %s | Cmd: %s\n",
                curr->id, mstr, curr->command);
        curr = curr->next;
    }
    pthread_mutex_unlock(&list_mutex);
    fprintf(stream, "--- END DUMP ---\n");
}

static void execute_process(const char *full_cmd) {
    char *cmd_buf = strdup(full_cmd);
    if (!cmd_buf) return;

    char *argv[64];
    int argc = 0;

    char *token = strtok(cmd_buf, " ");
    while (token != NULL && argc < 63) {
        argv[argc++] = token;
        token = strtok(NULL, " ");
    }
    argv[argc] = NULL;

    if (argc > 0) {
        pid_t pid = fork();
        if (pid == 0) {

            sigset_t set;
            sigemptyset(&set);
            if (sigprocmask(SIG_SETMASK, &set, NULL) == -1) perror("sigprocmask error");

            execvp(argv[0], argv);
            perror("execvp error");
            exit(1);
        } else if (pid > 0) {
            logger_log(MSG_NORMAL, "Process started PID: %d", pid);
        }
    }
    free(cmd_buf);
}

static void timer_handler(union sigval sv) {
    Task *t = (Task*)sv.sival_ptr;

    logger_log(MSG_NORMAL, "Executing Task ID %d: %s", t->id, t->command);

    execute_process(t->command);

    if (t->mode != MODE_CYCLIC) {
        pthread_mutex_lock(&list_mutex);
        Task **pp = &task_list_head;
        int found = 0;
        while (*pp) {
            if (*pp == t) {
                *pp = t->next;
                found = 1;
                break;
            }
            pp = &(*pp)->next;
        }
        if (found) {
            int task_id = t->id;
            timer_delete(t->timer_id);
            free(t);
            logger_log(MSG_LOW, "Task ID %d finished and removed.", task_id);
        }
        pthread_mutex_unlock(&list_mutex);
    }
}

static int add_task(RequestMsg *req) {
    Task *new_task = malloc(sizeof(Task));
    if (!new_task) return -1;

    new_task->id = ++task_id_counter;
    strncpy(new_task->command, req->command, MAX_CMD_LEN);
    new_task->mode = req->mode;
    new_task->next = NULL;

    struct sigevent sev;
    memset(&sev, 0, sizeof(sev));
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = timer_handler;
    sev.sigev_value.sival_ptr = new_task;
    sev.sigev_notify_attributes = NULL;

    if (timer_create(CLOCK_REALTIME, &sev, &new_task->timer_id) == -1) {
        free(new_task);
        return -1;
    }

    int flags = 0;
    struct itimerspec its;
    memset(&its, 0, sizeof(its));
    time_t now = time(NULL);

    if (req->mode == MODE_RELATIVE) {
        its.it_value.tv_sec = req->time_val;
        flags = 0;
    }
    else if (req->mode == MODE_CYCLIC) {
        its.it_value.tv_sec = req->time_val;
        its.it_interval.tv_sec = req->time_val;
        flags = 0;
    }
    else if (req->mode == MODE_ABSOLUTE) {
        struct tm *tm_info = localtime(&now);
        tm_info->tm_hour = req->time_val / 10000;
        tm_info->tm_min = (req->time_val % 10000) / 100;
        tm_info->tm_sec = req->time_val % 100;

        time_t target = mktime(tm_info);
        if (target <= now) target += 24 * 3600;

        its.it_value.tv_sec = target;
        flags = TIMER_ABSTIME;
    }

    if (timer_settime(new_task->timer_id, flags, &its, NULL) == -1) {
        timer_delete(new_task->timer_id);
        free(new_task);
        return -1;
    }

    pthread_mutex_lock(&list_mutex);
    new_task->next = task_list_head;
    task_list_head = new_task;
    pthread_mutex_unlock(&list_mutex);

    return new_task->id;
}

static void cancel_task(int id) {
    pthread_mutex_lock(&list_mutex);
    Task **pp = &task_list_head;
    while (*pp) {
        if ((*pp)->id == id) {
            Task *to_del = *pp;
            *pp = to_del->next;
            timer_delete(to_del->timer_id);
            free(to_del);
            logger_log(MSG_NORMAL, "Task ID %d cancelled.", id);
            pthread_mutex_unlock(&list_mutex);
            return;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&list_mutex);
    logger_log(MSG_LOW, "Failed to cancel task ID %d (not found).", id);
}

static void send_list(pid_t client_pid) {
    char qname[64];
    snprintf(qname, sizeof(qname), CLIENT_QUEUE_FMT, client_pid);

    mqd_t client_mq = mq_open(qname, O_WRONLY);
    if (client_mq == (mqd_t)-1) {
        logger_log(MSG_LOW, "Could not open client queue for PID %d", client_pid);
        return;
    }

    pthread_mutex_lock(&list_mutex);
    Task *curr = task_list_head;
    ResponseMsg resp;

    if (!curr) {
        snprintf(resp.text, sizeof(resp.text), "Task list is empty.\n");
        resp.is_last = 1;
        mq_send(client_mq, (const char*)&resp, sizeof(resp), 0);
    } else {
        while (curr) {
            const char* mstr = (curr->mode == MODE_CYCLIC) ? "CYCLIC" :
                               (curr->mode == MODE_ABSOLUTE) ? "ABSOLUTE" : "RELATIVE";
            snprintf(resp.text, sizeof(resp.text), "[ID:%d] [%s] %s\n", curr->id, mstr, curr->command);
            resp.is_last = (curr->next == NULL);
            mq_send(client_mq, (const char*)&resp, sizeof(resp), 0);
            curr = curr->next;
        }
    }
    pthread_mutex_unlock(&list_mutex);
    mq_close(client_mq);
}

static void cleanup_server() {
    pthread_mutex_lock(&list_mutex);
    while (task_list_head) {
        Task *t = task_list_head;
        task_list_head = t->next;
        timer_delete(t->timer_id);
        free(t);
    }
    pthread_mutex_unlock(&list_mutex);
    pthread_mutex_destroy(&list_mutex);

    if (server_mq != -1) {
        mq_close(server_mq);
        mq_unlink(SERVER_QUEUE_NAME);
    }

    logger_log(MSG_HIGH, "Server stopped.");
    logger_close();
}

void run_server() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction SIGINT");
        exit(1);
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction SIGTERM");
        exit(1);
    }

    struct sigaction sa_chld;
    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = SIG_DFL;
    sa_chld.sa_flags = SA_NOCLDWAIT;

    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        perror("sigaction SIGCHLD");
        exit(1);
    }

    int res = logger_init(SIGRTMIN, SIGRTMIN+1, SIGRTMIN+2,
                          ".", "cron.log", cron_dump_state);
    if (res != 0) {
        fprintf(stderr, "Logger init failed: %d\n", res);
        exit(1);
    }

    logger_log(MSG_HIGH, "Cron Server Started (PID: %d)", getpid());

    server_mq = mq_open(SERVER_QUEUE_NAME, O_RDONLY);
    if (server_mq == (mqd_t)-1) {
        perror("mq_open server");
        logger_close();
        exit(1);
    }

    RequestMsg req;

    while (server_running) {
        ssize_t bytes_read = mq_receive(server_mq, (char*)&req, sizeof(req), NULL);

        if (bytes_read < 0) {
            if (errno == EINTR) {
                if (!server_running) break;
                continue;
            }

            perror("mq_receive");
            break;
        }

        switch (req.type) {
            case CMD_ADD: {
                int id = add_task(&req);
                logger_log(MSG_NORMAL, "Added Task ID %d: %s", id, req.command);
                break;
            }
            case CMD_CANCEL:
                cancel_task(req.task_id);
                break;
            case CMD_LIST:
                send_list(req.client_pid);
                break;
            case CMD_STOP_SERVER:
                server_running = 0;
                break;
        }
    }

    cleanup_server();
}