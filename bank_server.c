#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>

#include "bank_common.h"

#define DATA_DIR "data"
#define ACCOUNT_FILE_TEMPLATE DATA_DIR "/account_%d.txt"
#define ACCOUNT_PATH_SIZE 128

static pthread_mutex_t account_mutex = PTHREAD_MUTEX_INITIALIZER;

static int ensure_data_directory(void)
{
    if (mkdir(DATA_DIR, 0777) == -1 && errno != EEXIST)
    {
        perror("Failed to create data directory");
        return 0;
    }

    return 1;
}

static int build_account_path(int account_number, char *path, size_t path_size)
{
    if (account_number < 0)
    {
        return 0;
    }

    int written = snprintf(path, path_size, ACCOUNT_FILE_TEMPLATE, account_number);

    if (written < 0 || (size_t)written >= path_size)
    {
        return 0;
    }

    return 1;
}

static int read_balance(int account_number, double *balance)
{
    char path[ACCOUNT_PATH_SIZE];
    FILE *file;

    if (!build_account_path(account_number, path, sizeof(path)))
    {
        return 0;
    }

    file = fopen(path, "r");

    if (file == NULL)
    {
        if (errno == ENOENT)
        {
            *balance = 0.0;
            return 1;
        }

        perror("Failed to read account file");
        return 0;
    }

    if (fscanf(file, "%lf", balance) != 1)
    {
        *balance = 0.0;
    }

    fclose(file);
    return 1;
}

static int write_balance(int account_number, double balance)
{
    char path[ACCOUNT_PATH_SIZE];
    FILE *file;

    if (!build_account_path(account_number, path, sizeof(path)))
    {
        return 0;
    }

    file = fopen(path, "w");

    if (file == NULL)
    {
        perror("Failed to write account file");
        return 0;
    }

    fprintf(file, "%.2f\n", balance);
    fclose(file);
    return 1;
}

static void handle_balance_check(const BankRequest *request, BankResponse *response)
{
    double balance;

    pthread_mutex_lock(&account_mutex);

    if (!read_balance(request->from_account, &balance))
    {
        pthread_mutex_unlock(&account_mutex);
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Could not read balance for account %d.", request->from_account);
        return;
    }

    pthread_mutex_unlock(&account_mutex);

    response->success = 1;
    response->balance = balance;
    snprintf(response->message, MAX_MESSAGE,
             "Account %d balance is %.2f.", request->from_account, balance);
}

static void handle_deposit(const BankRequest *request, BankResponse *response)
{
    double balance;

    if (request->amount <= 0)
    {
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Deposit amount must be greater than 0.");
        return;
    }

    pthread_mutex_lock(&account_mutex);

    if (!read_balance(request->from_account, &balance))
    {
        pthread_mutex_unlock(&account_mutex);
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Could not read balance for account %d.", request->from_account);
        return;
    }

    balance += request->amount;

    if (!write_balance(request->from_account, balance))
    {
        pthread_mutex_unlock(&account_mutex);
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Could not save balance for account %d.", request->from_account);
        return;
    }

    pthread_mutex_unlock(&account_mutex);

    response->success = 1;
    response->balance = balance;
    snprintf(response->message, MAX_MESSAGE,
             "Deposited %.2f into account %d. New balance is %.2f.",
             request->amount, request->from_account, balance);
}

static void handle_withdraw(const BankRequest *request, BankResponse *response)
{
    double balance;

    if (request->amount <= 0)
    {
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Withdraw amount must be greater than 0.");
        return;
    }

    pthread_mutex_lock(&account_mutex);

    if (!read_balance(request->from_account, &balance))
    {
        pthread_mutex_unlock(&account_mutex);
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Could not read balance for account %d.", request->from_account);
        return;
    }

    if (balance < request->amount)
    {
        pthread_mutex_unlock(&account_mutex);
        response->success = 0;
        response->balance = balance;
        snprintf(response->message, MAX_MESSAGE,
                 "Withdrawal denied. Account %d balance is %.2f.",
                 request->from_account, balance);
        return;
    }

    balance -= request->amount;

    if (!write_balance(request->from_account, balance))
    {
        pthread_mutex_unlock(&account_mutex);
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Could not save balance for account %d.", request->from_account);
        return;
    }

    pthread_mutex_unlock(&account_mutex);

    response->success = 1;
    response->balance = balance;
    snprintf(response->message, MAX_MESSAGE,
             "Withdrew %.2f from account %d. New balance is %.2f.",
             request->amount, request->from_account, balance);
}

static void handle_transfer(const BankRequest *request, BankResponse *response)
{
    double from_balance;
    double to_balance;

    if (request->amount <= 0)
    {
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Transfer amount must be greater than 0.");
        return;
    }

    if (request->from_account == request->to_account)
    {
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Transfer accounts must be different.");
        return;
    }

    pthread_mutex_lock(&account_mutex);

    if (!read_balance(request->from_account, &from_balance) ||
        !read_balance(request->to_account, &to_balance))
    {
        pthread_mutex_unlock(&account_mutex);
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Could not read one of the account balances.");
        return;
    }

    if (from_balance < request->amount)
    {
        pthread_mutex_unlock(&account_mutex);
        response->success = 0;
        response->balance = from_balance;
        snprintf(response->message, MAX_MESSAGE,
                 "Transfer denied. Account %d balance is %.2f.",
                 request->from_account, from_balance);
        return;
    }

    from_balance -= request->amount;
    to_balance += request->amount;

    if (!write_balance(request->from_account, from_balance) ||
        !write_balance(request->to_account, to_balance))
    {
        pthread_mutex_unlock(&account_mutex);
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Could not save transfer balances.");
        return;
    }

    pthread_mutex_unlock(&account_mutex);

    response->success = 1;
    response->balance = from_balance;
    snprintf(response->message, MAX_MESSAGE,
             "Transferred %.2f from account %d to account %d. Account %d balance is %.2f.",
             request->amount, request->from_account, request->to_account,
             request->from_account, from_balance);
}

static void build_response(const BankRequest *request, BankResponse *response)
{
    response->success = 0;
    response->balance = 0.0;
    memset(response->message, 0, MAX_MESSAGE);

    if (strcmp(request->operation, "BalanceCheck") == 0)
    {
        handle_balance_check(request, response);
    }
    else if (strcmp(request->operation, "Deposit") == 0)
    {
        handle_deposit(request, response);
    }
    else if (strcmp(request->operation, "Withdraw") == 0)
    {
        handle_withdraw(request, response);
    }
    else if (strcmp(request->operation, "Transfer") == 0)
    {
        handle_transfer(request, response);
    }
    else
    {
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Invalid operation: %s.", request->operation);
    }
}


static void *client_thread(void *arg)
{
    BankRequest request = *(BankRequest *)arg;
    free(arg);

    BankResponse response;
    build_response(&request, &response);

    int private_fd = open(request.private_fifo, O_WRONLY);

    if (private_fd == -1)
    {
        perror("Failed to open client private FIFO");
        return NULL;
    }

    if (write(private_fd, &response, sizeof(BankResponse)) != sizeof(BankResponse))
    {
        perror("Failed to send response to client");
    }

    close(private_fd);
    return NULL;
}

int main(void)
{
    int public_fd;
    BankRequest request;

    if (!ensure_data_directory())
    {
        exit(1);
    }

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

        ssize_t bytes_read;

        while ((bytes_read = read(public_fd, &request, sizeof(BankRequest))) > 0)
        {
            if (bytes_read != sizeof(BankRequest))
            {
                fprintf(stderr, "Ignored incomplete request.\n");
                continue;
            }

            printf("\nReceived request from user: %s\n", request.username);
            printf("Operation: %s\n", request.operation);
            printf("From account: %d\n", request.from_account);
            printf("To account: %d\n", request.to_account);
            printf("Amount: %.2f\n", request.amount);
            printf("Private FIFO: %s\n", request.private_fifo);

            BankRequest *thread_request = malloc(sizeof(BankRequest));

            if (thread_request == NULL)
            {
                perror("Failed to allocate request for client thread");
                continue;
            }

            *thread_request = request;

            pthread_t thread_id;

            if (pthread_create(&thread_id, NULL, client_thread, thread_request) != 0)
            {
                perror("Failed to create client thread");
                free(thread_request);
                continue;
            }

            pthread_detach(thread_id);
        }

        close(public_fd);
    }

    unlink(PUBLIC_FIFO);
    return 0;
}
