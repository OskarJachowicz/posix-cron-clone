#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <errno.h>

#include "defs.h"
#include "cron.h"

int main(int argc, char *argv[]) {
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(RequestMsg);
    attr.mq_curmsgs = 0;

    mqd_t check = mq_open(SERVER_QUEUE_NAME, O_CREAT | O_EXCL | O_RDONLY, 0644, &attr);

    if (check != (mqd_t)-1) {
        mq_close(check);
        printf("No server instance found. Starting SERVER mode...\n");

        run_server();
    } else {
        if (errno == EEXIST) run_client(argc, argv);
        else {
            perror("Error opening queue");
            return 1;
        }
    }

    return 0;
}