#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "bank_common.h"

static void clear_input_buffer()
{
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF)
    {
    }
}

static int read_int_choice()
{
    int choice;

    if (scanf("%d", &choice) != 1)
    {
        clear_input_buffer();
        return -1;
    }

    clear_input_buffer();
    return choice;
}

static double read_amount()
{
    double amount;

    if (scanf("%lf", &amount) != 1)
    {
        clear_input_buffer();
        return -1.0;
    }

    clear_input_buffer();
    return amount;
}

int main()
{
    BankRequest request;
    BankResponse response;
    int choice;

    memset(&request, 0, sizeof(BankRequest));
    memset(&response, 0, sizeof(BankResponse));

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
    clear_input_buffer();

    printf("\nChoose an operation:\n");
    printf("1) BalanceCheck\n");
    printf("2) Deposit\n");
    printf("3) Withdraw\n");
    printf("4) Transfer\n");
    printf("5) Register new user\n");
    printf("6) Quit\n");
    printf("Enter your choice: ");

    choice = read_int_choice();

    request.from_account = 0;
    request.to_account = 0;
    request.amount = 0.0;

    if (choice == 1)
    {
        strncpy(request.operation, "BalanceCheck", MAX_OPERATION - 1);

        printf("Your account number: ");
        request.from_account = read_int_choice();
    }
    else if (choice == 2)
    {
        strncpy(request.operation, "Deposit", MAX_OPERATION - 1);

        printf("Your account number: ");
        request.from_account = read_int_choice();

        printf("Amount to deposit: ");
        request.amount = read_amount();
    }
    else if (choice == 3)
    {
        strncpy(request.operation, "Withdraw", MAX_OPERATION - 1);

        printf("Your account number: ");
        request.from_account = read_int_choice();

        printf("Amount to withdraw: ");
        request.amount = read_amount();
    }
    else if (choice == 4)
    {
        strncpy(request.operation, "Transfer", MAX_OPERATION - 1);

        printf("Your account number to transfer from: ");
        request.from_account = read_int_choice();

        printf("To account number: ");
        request.to_account = read_int_choice();

        printf("Amount to transfer: ");
        request.amount = read_amount();
    }
    else if (choice == 5)
    {
        strncpy(request.operation, "Register", MAX_OPERATION - 1);

        printf("New account number: ");
        request.from_account = read_int_choice();
    }
    else if (choice == 6)
    {
        printf("GoodBye\n");
        unlink(request.private_fifo);
        return 0;
    }
    else
    {
        printf("Invalid choice.\n");
        unlink(request.private_fifo);
        return 1;
    }

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
    printf("Balance: %.2f\n", response.balance);

    close(private_fd);
    unlink(request.private_fifo);

    return 0;
}
