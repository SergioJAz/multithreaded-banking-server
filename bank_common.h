#ifndef BANK_COMMON_H
#define BANK_COMMON_H

#define PUBLIC_FIFO "/tmp/smbs_public_fifo"

#define MAX_USERNAME 50
#define MAX_PASSWORD 100
#define MAX_OPERATION 30
#define MAX_MESSAGE 256
#define MAX_FIFO_NAME 100

typedef struct
{
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    char operation[MAX_OPERATION];

    int from_account;
    int to_account;
    double amount;

    char private_fifo[MAX_FIFO_NAME];
} BankRequest;

typedef struct
{
    int success;
    char message[MAX_MESSAGE];
    double balance;
    int account_id;
} BankResponse;

#endif