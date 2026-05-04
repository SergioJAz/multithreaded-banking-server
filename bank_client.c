#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "bank_common.h"

int main()
{
    BankRequest request;
    BankResponse response;

    snprintf(request.private_fifo, MAX_FIFO_NAME, "/tmp/smbs_client_%d_fifo", getpid());

    unlink(request.private_fifo);

    if (mkfifo(request.private_fifo, 0666) == -1)
    {
        perror("Failed to create private FIFO");
        exit(1);
    }

    printf("Username: ");
    scanf("%49s", request.username);

    printf("Password: ");
    scanf("%99s", request.password);

    printf("Operation: ");
    scanf("%29s", request.operation);

    request.from_account = 1;
    request.to_account = 2;
    request.amount = 0.0;

    int public_fd = open(PUBLIC_FIFO, O_WRONLY);

    if (public_fd == -1)
    {
        perror("Failed to connect to server public FIFO");
        unlink(request.private_fifo);
        exit(1);
    }

    write(public_fd, &request, sizeof(BankRequest));
    close(public_fd);

    int private_fd = open(request.private_fifo, O_RDONLY);

    if (private_fd == -1)
    {
        perror("Failed to open private FIFO");
        unlink(request.private_fifo);
        exit(1);
    }

    read(private_fd, &response, sizeof(BankResponse));

    printf("\nServer Response:\n");
    printf("Success: %d\n", response.success);
    printf("Message: %s\n", response.message);

    close(private_fd);
    unlink(request.private_fifo);

    return 0;
}