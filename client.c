#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <errno.h>

#include "defs.h"
#include "cron.h"

void run_client(int argc, char *argv[]) {
    RequestMsg req;
    memset(&req, 0, sizeof(req));
    req.client_pid = getpid();

    int opt;
    int action = 0;
    char *cmd_str = NULL;

    while ((opt = getopt(argc, argv, "a:r:c:t:lk:q")) != -1) {
        switch (opt) {
            case 'a':
                strncpy(req.command, optarg, MAX_CMD_LEN - 1);
                req.command[MAX_CMD_LEN - 1] = '\0';
                cmd_str = optarg;
                break;
            case 'r':
                req.type = CMD_ADD;
                req.mode = MODE_RELATIVE;
                req.time_val = atol(optarg);
                action = 1;
                break;
            case 'c':
                req.type = CMD_ADD;
                req.mode = MODE_CYCLIC;
                req.time_val = atol(optarg);
                action = 1;
                break;
            case 't':
                req.type = CMD_ADD;
                req.mode = MODE_ABSOLUTE;
                req.time_val = atol(optarg);
                action = 1;
                break;
            case 'l':
                req.type = CMD_LIST;
                action = 1;
                break;
            case 'k':
                req.type = CMD_CANCEL;
                req.task_id = atoi(optarg);
                action = 1;
                break;
            case 'q':
                req.type = CMD_STOP_SERVER;
                action = 1;
                break;
            default:
                fprintf(stderr, "Format: %s [-l] [-q] [-k id] [-a command -r sec|-c sec|-t HHMMSS]\n", argv[0]);
                exit(1);
        }
    }

    if (!action) {
        fprintf(stderr, "No action specified.\n");
        exit(1);
    }
    if (req.type == CMD_ADD && cmd_str == NULL) {
        fprintf(stderr, "Error: -a command is required for scheduling.\n");
        exit(1);
    }

    mqd_t serv_mq = mq_open(SERVER_QUEUE_NAME, O_WRONLY);
    if (serv_mq == (mqd_t)-1) {
        fprintf(stderr, "Cannot connect to server\n");
        exit(1);
    }

    mqd_t resp_mq = -1;
    char my_qname[64];
    my_qname[0] = '\0';
    if (req.type == CMD_LIST) {
        snprintf(my_qname, sizeof(my_qname), CLIENT_QUEUE_FMT, getpid());
        struct mq_attr attr;
        attr.mq_flags = 0;
        attr.mq_maxmsg = 10;
        attr.mq_msgsize = sizeof(ResponseMsg);

        resp_mq = mq_open(my_qname, O_CREAT | O_RDONLY, 0644, &attr);
        if (resp_mq == (mqd_t)-1) {
            perror("Failed to create response queue");
            mq_close(serv_mq);
            exit(1);
        }
    }

    if (mq_send(serv_mq, (const char*)&req, sizeof(req), 0) == -1) {
        perror("mq_send");

        if (resp_mq != -1) {
            mq_close(resp_mq);
            mq_unlink(my_qname);
        }
        mq_close(serv_mq);

        exit(1);
    }

    if (req.type == CMD_LIST) {
        ResponseMsg resp;
        printf("--- SCHEDULED TASKS ---\n");
        while (1) {
            if (mq_receive(resp_mq, (char*)&resp, sizeof(resp), NULL) > 0) {
                printf("%s", resp.text);
                if (resp.is_last) break;
            } else {
                perror("mq_receive error");
                break;
            }
        }
        mq_close(resp_mq);
        mq_unlink(my_qname);
    } else {
        printf("Command sent successfully.\n");
    }

    mq_close(serv_mq);
}