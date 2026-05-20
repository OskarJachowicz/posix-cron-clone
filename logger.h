#ifndef SCR_PROJEKT_2_LOGGER_H
#define SCR_PROJEKT_2_LOGGER_H

#include <stdio.h>

typedef enum {
    LOG_LEVEL_MIN = 0,
    LOG_LEVEL_STANDARD = 1,
    LOG_LEVEL_MAX = 2
} LogLevel;

typedef enum {
    MSG_LOW = 0,
    MSG_NORMAL = 1,
    MSG_HIGH = 2
} MsgImportance;

typedef void (*DumpFunc)(FILE* stream);

int logger_init(int sig_dump, int sig_toggle, int sig_level, const char* log_dir, const char* log_filename, DumpFunc cb_fun);

void logger_close();

void logger_log(MsgImportance importance, const char* format, ...);

#endif //SCR_PROJEKT_2_LOGGER_H
