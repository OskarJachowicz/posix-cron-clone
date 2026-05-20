#ifndef SCR_PROJEKT_2_PROTOCOL_H
#define SCR_PROJEKT_2_PROTOCOL_H

#include <sys/types.h>
#include <unistd.h>

#define SERVER_QUEUE_NAME   "/scr_cron_mq_server"
#define CLIENT_QUEUE_FMT    "/scr_cron_client_%d"
#define MAX_CMD_LEN         256
#define MAX_MSG_SIZE        1024

typedef enum {
    CMD_ADD,
    CMD_LIST,
    CMD_CANCEL,
    CMD_STOP_SERVER
} CommandType;

typedef enum {
    MODE_RELATIVE,
    MODE_ABSOLUTE,
    MODE_CYCLIC
} ScheduleMode;

typedef struct {
    CommandType type;
    pid_t client_pid;

    ScheduleMode mode;
    long time_val;
    char command[MAX_CMD_LEN];

    int task_id;
} RequestMsg;

typedef struct {
    char text[MAX_MSG_SIZE];
    int is_last;
} ResponseMsg;

#endif //SCR_PROJEKT_2_PROTOCOL_H
