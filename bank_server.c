#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "bank_common.h"

int main()
{
    int public_fd;
    BankRequest request;

    unlink(PUBLIC_FIFO);

    if (mkfifo(PUBLIC_FIFO, 0666) == -1)
    {
        perror("Failed to create public FIFO");
        exit(1);
    }

    printf("SMBS Server started.\n");
    printf("Waiting for client requests...\n");

    while (1)
    {
        public_fd = open(PUBLIC_FIFO, O_RDONLY);

        if (public_fd == -1)
        {
            perror("Failed to open public FIFO");
            continue;
        }

        while (read(public_fd, &request, sizeof(BankRequest)) > 0)
        {
            printf("\nReceived request from user: %s\n", request.username);
            printf("Operation: %s\n", request.operation);
            printf("Private FIFO: %s\n", request.private_fifo);

            BankResponse response;
            response.success = 1;
            response.balance = 0.0;

            snprintf(response.message, MAX_MESSAGE, "Server received your %s request.", request.operation);

            int private_fd = open(request.private_fifo, O_WRONLY);

            if (private_fd == -1)
            {
                perror("Failed to open client private FIFO");
            }
            else
            {
                write(private_fd, &response, sizeof(BankResponse));
                close(private_fd);
            }
        }

        close(public_fd);
    }

    unlink(PUBLIC_FIFO);
    return 0;
}