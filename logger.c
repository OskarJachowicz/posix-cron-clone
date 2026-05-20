#include "logger.h"
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <stdatomic.h>
#include <semaphore.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>


static atomic_int current_level = LOG_LEVEL_STANDARD;
static atomic_int logging_enabled = 1;
static atomic_int dump_id = 0;

static FILE* log_file = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static DumpFunc dump_function = NULL;
static pthread_t dump_thread;
static sem_t dump_sem;
static atomic_bool is_running = 0;
static atomic_bool is_initialized = 0;

static char save_log_dir[256] = ".";

static void get_curr_time_str(char* buffer, size_t size) {
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    strftime(buffer, size, "%d-%m-%Y %H:%M:%S", &t);
}

static void generate_dump_filename(char* buffer, size_t size, int id) {
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    char time_part[32];
    strftime(time_part, sizeof(time_part), "%H-%M-%S", &t);

    snprintf(buffer, size, "%s/dump_id%d_%s.txt", save_log_dir, id, time_part);
}

void handler_dump(int signo, siginfo_t* info, void* other) {
    int val = info->si_value.sival_int;
    atomic_store(&dump_id, val);
    sem_post(&dump_sem);
}

void handler_toggle(int signo, siginfo_t* info, void* other) {
    int val = info->si_value.sival_int;
    if (val == 0 || val == 1) {
        atomic_store(&logging_enabled, val);
    }
}

void handler_level(int signo, siginfo_t* info, void* other) {
    int val = info->si_value.sival_int;
    if (val >= LOG_LEVEL_MIN && val <= LOG_LEVEL_MAX) {
        atomic_store(&current_level, val);
    }
}

void* dump_worker(void* arg) {
    while (atomic_load(&is_running)) {
        if (sem_wait(&dump_sem) == 0) {
            if (atomic_load(&is_running) == 0) break;

            int id = atomic_load(&dump_id);
            char filename[512];
            generate_dump_filename(filename, sizeof(filename), id);

            FILE* f = fopen(filename, "w");
            if (f) {
                char time_buf[64];
                get_curr_time_str(time_buf, sizeof(time_buf));
                fprintf(f, "DUMP TIME: %s\n", time_buf);
                fprintf(f, "DUMP ID: %d\n", id);
                fprintf(f, "-------------------------\n");

                if (dump_function != NULL) {
                    dump_function(f);
                }
                fclose(f);
            }
            else{
                perror("Nie udalo sie stworzyc pliku [dump]\n");
            }
        }
    }
    return NULL;
}

int logger_init(int sig_dump, int sig_toggle, int sig_level, const char* log_dir, const char* log_filename, DumpFunc cb_fun) {

    if(atomic_exchange(&is_initialized, 1) == 1){
        return -3;
    }

    if((sig_dump < SIGRTMIN || sig_dump > SIGRTMAX) || (sig_toggle < SIGRTMIN || sig_toggle > SIGRTMAX) || (sig_level < SIGRTMIN || sig_level > SIGRTMAX)){
        atomic_store(&is_initialized, 0);
        return -4;
    }

    if(sig_dump == sig_toggle || sig_dump == sig_level || sig_toggle == sig_level){
        atomic_store(&is_initialized, 0);
        return -4;
    }

    dump_function = cb_fun;

    if (log_dir && strlen(log_dir) > 0) {
        snprintf(save_log_dir, sizeof(save_log_dir), "%s", log_dir);
    } else {
        strcpy(save_log_dir, ".");
    }

    char full_log_path[512];
    snprintf(full_log_path, sizeof(full_log_path), "%s/%s", save_log_dir, log_filename);

    log_file = fopen(full_log_path, "a");
    if (!log_file){
        atomic_store(&is_initialized, 0);
        perror("Blad otwarcia pliku [log]\n");
        return -1;
    }

    sem_init(&dump_sem, 0, 0);
    atomic_store(&is_running, 1);

    if (pthread_create(&dump_thread, NULL, dump_worker, NULL) != 0) {
        atomic_store(&is_running, 0);
        sem_destroy(&dump_sem);
        fclose(log_file);
        atomic_store(&is_initialized, 0);
        return -2;
    }

    struct sigaction act;

    sigset_t set;
    sigfillset(&set);

    act.sa_mask = set;
    act.sa_flags = SA_SIGINFO;

    act.sa_sigaction = handler_dump;
    sigaction(sig_dump, &act, NULL);

    act.sa_sigaction = handler_toggle;
    sigaction(sig_toggle, &act, NULL);

    act.sa_sigaction = handler_level;
    sigaction(sig_level, &act, NULL);

    return 0;
}

void logger_close() {
    if(atomic_load(&is_initialized) == 0) return;

    atomic_store(&is_running, 0);
    sem_post(&dump_sem);
    pthread_join(dump_thread, NULL);

    sem_destroy(&dump_sem);
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }

    atomic_store(&is_initialized, 0);
}

void logger_log(MsgImportance importance, const char* format, ...) {
    if (atomic_load(&logging_enabled) == 0) return;

    if(format == NULL) return;

    int current_lvl = atomic_load(&current_level);

    if ((int)importance >= current_lvl) {

        char time_buf[64];
        get_curr_time_str(time_buf, sizeof(time_buf));

        const char* imp_str = "UNKNOWN";
        if (importance == MSG_LOW) imp_str = "LOW";
        else if (importance == MSG_NORMAL) imp_str = "NORMAL";
        else if (importance == MSG_HIGH) imp_str = "HIGH";

        pthread_mutex_lock(&log_mutex);
        if (log_file) {
            fprintf(log_file, "[%s] [%s] ", time_buf, imp_str);

            va_list args;
            va_start(args, format);
            vfprintf(log_file, format, args);
            va_end(args);

            fprintf(log_file, "\n");
            fflush(log_file);
        }
        pthread_mutex_unlock(&log_mutex);
    }
}