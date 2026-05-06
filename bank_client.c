#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <termios.h>

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

static void read_password(char *password, int max_len)
{
    struct termios oldt;
    struct termios newt;
    int i = 0;
    char ch;

    if (tcgetattr(STDIN_FILENO, &oldt) != 0)
    {
        if (fgets(password, max_len, stdin) == NULL)
        {
            password[0] = '\0';
            return;
        }

        password[strcspn(password, "\n")] = '\0';
        return;
    }

    newt = oldt;
    newt.c_lflag &= ~(ECHO | ICANON);

    if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) != 0)
    {
        if (fgets(password, max_len, stdin) == NULL)
        {
            password[0] = '\0';
            return;
        }

        password[strcspn(password, "\n")] = '\0';
        return;
    }

    while (i < max_len - 1)
    {
        ch = getchar();

        if (ch == '\n' || ch == '\r')
        {
            break;
        }
        else if (ch == 127 || ch == '\b')
        {
            if (i > 0)
            {
                i--;
                printf("\b \b");
                fflush(stdout);
            }
        }
        else
        {
            password[i] = ch;
            i++;
            printf("*");
            fflush(stdout);
        }
    }

    password[i] = '\0';

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\n");
}

static void print_menu()
{
    printf("\nChoose an operation:\n");
    printf("1) BalanceCheck\n");
    printf("2) Deposit\n");
    printf("3) Withdraw\n");
    printf("4) Transfer\n");
    printf("5) Register new user\n");
    printf("6) Quit\n");
    printf("Enter your choice: ");
}

static int send_request_and_get_response(BankRequest *request, BankResponse *response)
{
    int public_fd;
    int private_fd;

    public_fd = open(PUBLIC_FIFO, O_WRONLY);

    if (public_fd == -1)
    {
        perror("Failed to connect to server public FIFO");
        return 0;
    }

    if (write(public_fd, request, sizeof(BankRequest)) != sizeof(BankRequest))
    {
        perror("Failed to send request to server");
        close(public_fd);
        return 0;
    }

    close(public_fd);

    private_fd = open(request->private_fifo, O_RDONLY);

    if (private_fd == -1)
    {
        perror("Failed to open private FIFO");
        return 0;
    }

    if (read(private_fd, response, sizeof(BankResponse)) <= 0)
    {
        perror("Failed to read server response");
        close(private_fd);
        return 0;
    }

    close(private_fd);
    return 1;
}

int main()
{
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    char private_fifo[MAX_FIFO_NAME];
    int choice;
    int running = 1;

    snprintf(private_fifo, MAX_FIFO_NAME, "/tmp/smbs_client_%d_fifo", getpid());

    unlink(private_fifo);

    if (mkfifo(private_fifo, 0666) == -1)
    {
        perror("Failed to create private FIFO");
        exit(1);
    }

    printf("Username: ");
    scanf("%49s", username);
    clear_input_buffer();

    printf("Password: ");
    read_password(password, sizeof(password));

    while (running)
    {
        BankRequest request;
        BankResponse response;

        memset(&request, 0, sizeof(BankRequest));
        memset(&response, 0, sizeof(BankResponse));

        strncpy(request.username, username, MAX_USERNAME - 1);
        strncpy(request.password, password, MAX_PASSWORD - 1);
        strncpy(request.private_fifo, private_fifo, MAX_FIFO_NAME - 1);

        request.from_account = 0;
        request.to_account = 0;
        request.amount = 0.0;

        print_menu();
        choice = read_int_choice();

        if (choice == 1)
        {
            strncpy(request.operation, "BalanceCheck", MAX_OPERATION - 1);
        }
        else if (choice == 2)
        {
            strncpy(request.operation, "Deposit", MAX_OPERATION - 1);

            printf("Amount to deposit: ");
            request.amount = read_amount();
        }
        else if (choice == 3)
        {
            strncpy(request.operation, "Withdraw", MAX_OPERATION - 1);

            printf("Amount to withdraw: ");
            request.amount = read_amount();
        }
        else if (choice == 4)
        {
            strncpy(request.operation, "Transfer", MAX_OPERATION - 1);

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
            running = 0;
            continue;
        }
        else
        {
            printf("Invalid choice.\n");
            continue;
        }

        if (!send_request_and_get_response(&request, &response))
        {
            unlink(private_fifo);
            return 1;
        }

        printf("\nServer Response:\n");
        printf("Success: %d\n", response.success);
        printf("Message: %s\n", response.message);
        printf("Balance: %.2f\n", response.balance);
    }

    unlink(private_fifo);

    return 0;
}
