#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>

#include "bank_common.h"

#define DATA_DIR "data"
#define ACCOUNT_FILE_TEMPLATE DATA_DIR "/account_%d.txt"
#define ACCOUNT_PATH_SIZE 128
#define MAX_ACCOUNTS 1000
#define USERS_AUTH_FILE "users.auth"
#define SALT_SIZE 64
#define HASH_SIZE 65
#define AUTH_LINE_SIZE 256
#define ENCRYPTED_FILE_PREFIX "SMBSX1:"
#define ENCRYPTION_KEY "CSIS221_SMBS_MASTER_KEY"

static pthread_mutex_t account_mutexes[MAX_ACCOUNTS];
static pthread_mutex_t users_auth_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ---------------- SHA-256 HASHING ---------------- */

typedef struct
{
    uint8_t data[64];
    uint32_t datalen;
    unsigned long long bitlen;
    uint32_t state[8];
} SHA256_CTX;

static const uint32_t k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static uint32_t rotr(uint32_t a, uint32_t b)
{
    return (((a) >> (b)) | ((a) << (32 - (b))));
}

static uint32_t choose(uint32_t e, uint32_t f, uint32_t g)
{
    return (e & f) ^ (~e & g);
}

static uint32_t majority(uint32_t a, uint32_t b, uint32_t c)
{
    return (a & b) ^ (a & c) ^ (b & c);
}

static uint32_t sig0(uint32_t x)
{
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

static uint32_t sig1(uint32_t x)
{
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

static uint32_t ep0(uint32_t x)
{
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

static uint32_t ep1(uint32_t x)
{
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

static void sha256_transform(SHA256_CTX *ctx, const uint8_t data[])
{
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t m[64];

    for (int i = 0, j = 0; i < 16; i++, j += 4)
    {
        m[i] = ((uint32_t)data[j] << 24) |
               ((uint32_t)data[j + 1] << 16) |
               ((uint32_t)data[j + 2] << 8) |
               ((uint32_t)data[j + 3]);
    }

    for (int i = 16; i < 64; i++)
    {
        m[i] = sig1(m[i - 2]) + m[i - 7] + sig0(m[i - 15]) + m[i - 16];
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (int i = 0; i < 64; i++)
    {
        uint32_t t1 = h + ep1(e) + choose(e, f, g) + k[i] + m[i];
        uint32_t t2 = ep0(a) + majority(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(SHA256_CTX *ctx)
{
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;

        if (ctx->datalen == 64)
        {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void sha256_final(SHA256_CTX *ctx, uint8_t hash[])
{
    uint32_t i = ctx->datalen;

    if (ctx->datalen < 56)
    {
        ctx->data[i++] = 0x80;
        while (i < 56)
        {
            ctx->data[i++] = 0x00;
        }
    }
    else
    {
        ctx->data[i++] = 0x80;
        while (i < 64)
        {
            ctx->data[i++] = 0x00;
        }
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }

    ctx->bitlen += ctx->datalen * 8;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    sha256_transform(ctx, ctx->data);

    for (i = 0; i < 4; i++)
    {
        hash[i] = (ctx->state[0] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 4] = (ctx->state[1] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 8] = (ctx->state[2] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 12] = (ctx->state[3] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 16] = (ctx->state[4] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 20] = (ctx->state[5] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 24] = (ctx->state[6] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 28] = (ctx->state[7] >> (24 - i * 8)) & 0x000000ff;
    }
}

static void sha256_hex(const char *input, char output[HASH_SIZE])
{
    uint8_t hash[32];
    SHA256_CTX ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)input, strlen(input));
    sha256_final(&ctx, hash);

    for (int i = 0; i < 32; i++)
    {
        snprintf(output + (i * 2), 3, "%02x", hash[i]);
    }

    output[64] = '\0';
}

/* ---------------- ACCOUNT LOCKING ---------------- */

static int valid_account_number(int account_number)
{
    return account_number >= 0 && account_number < MAX_ACCOUNTS;
}

static void lock_account(int account_number)
{
    pthread_mutex_lock(&account_mutexes[account_number]);
}

static void unlock_account(int account_number)
{
    pthread_mutex_unlock(&account_mutexes[account_number]);
}

static void lock_two_accounts_ordered(int first_account, int second_account)
{
    if (first_account < second_account)
    {
        lock_account(first_account);
        lock_account(second_account);
    }
    else
    {
        lock_account(second_account);
        lock_account(first_account);
    }
}

static void unlock_two_accounts_ordered(int first_account, int second_account)
{
    if (first_account < second_account)
    {
        unlock_account(second_account);
        unlock_account(first_account);
    }
    else
    {
        unlock_account(first_account);
        unlock_account(second_account);
    }
}

static int initialize_account_mutexes(void)
{
    for (int i = 0; i < MAX_ACCOUNTS; i++)
    {
        if (pthread_mutex_init(&account_mutexes[i], NULL) != 0)
        {
            fprintf(stderr, "Failed to initialize mutex for account %d.\n", i);
            return 0;
        }
    }

    return 1;
}

/* ---------------- FILE SETUP ---------------- */

static int ensure_data_directory(void)
{
    if (mkdir(DATA_DIR, 0777) == -1 && errno != EEXIST)
    {
        perror("Failed to create data directory");
        return 0;
    }

    return 1;
}

static int ensure_users_auth_file(void)
{
    FILE *file = fopen(USERS_AUTH_FILE, "r");

    if (file != NULL)
    {
        fclose(file);
        return 1;
    }

    file = fopen(USERS_AUTH_FILE, "w");

    if (file == NULL)
    {
        perror("Failed to create users.auth");
        return 0;
    }

    const char *users[][4] = {
        {"sergio", "salt_sergio_01", "sergio123", "0"},
        {"alice", "salt_alice_04", "alice123", "3"},
        {"bob", "salt_bob_03", "bob123", "2"},
        {"priyan123", "salt_priyan123_02", "priyan123", "1"}};

    fprintf(file, "# Format: username:salt:sha256(salt + password):account_id\n");

    for (int i = 0; i < 4; i++)
    {
        char salted_password[MAX_PASSWORD + SALT_SIZE];
        char hash[HASH_SIZE];

        snprintf(salted_password, sizeof(salted_password), "%s%s", users[i][1], users[i][2]);
        sha256_hex(salted_password, hash);
        fprintf(file, "%s:%s:%s:%s\n", users[i][0], users[i][1], hash, users[i][3]);
    }

    fclose(file);

    printf("Created default users.auth file.\n");
    printf("Default users: sergio/sergio123 account 0, priyan123/priyan123 account 1, bob/bob123 account 2, alice/alice123 account 3.\n");

    return 1;
}

static int build_account_path(int account_number, char *path, size_t path_size)
{
    if (!valid_account_number(account_number))
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


static void xor_crypt_buffer(char *buffer, size_t length)
{
    const char *key = ENCRYPTION_KEY;
    size_t key_length = strlen(key);

    if (key_length == 0)
    {
        return;
    }

    for (size_t i = 0; i < length; i++)
    {
        buffer[i] = (char)(buffer[i] ^ key[i % key_length]);
    }
}

static int write_hex_byte(FILE *file, unsigned char value)
{
    return fprintf(file, "%02x", value) == 2;
}

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0';
    }

    if (ch >= 'a' && ch <= 'f')
    {
        return ch - 'a' + 10;
    }

    if (ch >= 'A' && ch <= 'F')
    {
        return ch - 'A' + 10;
    }

    return -1;
}

static int decode_hex_string(const char *hex, char *output, size_t output_size, size_t *output_length)
{
    size_t hex_length = strlen(hex);

    if (hex_length % 2 != 0 || (hex_length / 2) >= output_size)
    {
        return 0;
    }

    *output_length = hex_length / 2;

    for (size_t i = 0; i < *output_length; i++)
    {
        int high = hex_value(hex[i * 2]);
        int low = hex_value(hex[i * 2 + 1]);

        if (high < 0 || low < 0)
        {
            return 0;
        }

        output[i] = (char)((high << 4) | low);
    }

    output[*output_length] = '\0';
    return 1;
}

static int decrypt_balance_text(const char *file_text, double *balance)
{
    const char *encrypted_hex;
    char encrypted_bytes[128];
    size_t encrypted_length;

    if (strncmp(file_text, ENCRYPTED_FILE_PREFIX, strlen(ENCRYPTED_FILE_PREFIX)) != 0)
    {
        return sscanf(file_text, "%lf", balance) == 1;
    }

    encrypted_hex = file_text + strlen(ENCRYPTED_FILE_PREFIX);

    if (!decode_hex_string(encrypted_hex, encrypted_bytes, sizeof(encrypted_bytes), &encrypted_length))
    {
        return 0;
    }

    xor_crypt_buffer(encrypted_bytes, encrypted_length);
    encrypted_bytes[encrypted_length] = '\0';

    return sscanf(encrypted_bytes, "%lf", balance) == 1;
}

static int read_balance(int account_number, double *balance)
{
    char path[ACCOUNT_PATH_SIZE];
    FILE *file;
    char file_text[128];

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

    if (fgets(file_text, sizeof(file_text), file) == NULL)
    {
        *balance = 0.0;
        fclose(file);
        return 1;
    }

    fclose(file);
    file_text[strcspn(file_text, "\r\n")] = '\0';

    if (!decrypt_balance_text(file_text, balance))
    {
        *balance = 0.0;
    }

    return 1;
}

static int write_balance(int account_number, double balance)
{
    char path[ACCOUNT_PATH_SIZE];
    FILE *file;
    char plain_text[64];
    size_t plain_length;

    if (!build_account_path(account_number, path, sizeof(path)))
    {
        return 0;
    }

    snprintf(plain_text, sizeof(plain_text), "%.2f", balance);
    plain_length = strlen(plain_text);
    xor_crypt_buffer(plain_text, plain_length);

    file = fopen(path, "w");

    if (file == NULL)
    {
        perror("Failed to write account file");
        return 0;
    }

    fprintf(file, "%s", ENCRYPTED_FILE_PREFIX);

    for (size_t i = 0; i < plain_length; i++)
    {
        if (!write_hex_byte(file, (unsigned char)plain_text[i]))
        {
            fclose(file);
            return 0;
        }
    }

    fprintf(file, "\n");
    fclose(file);
    return 1;
}

/* ---------------- AUTHENTICATION ---------------- */

static void generate_salt(const char *username, int account_number, char *salt, size_t salt_size)
{
    snprintf(salt, salt_size, "salt_%s_%02d", username, account_number + 1);
}

static int user_or_account_exists(const char *new_username, int new_account)
{
    FILE *file = fopen(USERS_AUTH_FILE, "r");
    char line[AUTH_LINE_SIZE];

    if (file == NULL)
    {
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char line_copy[AUTH_LINE_SIZE];
        char *username;
        char *salt;
        char *stored_hash;
        char *account_text;
        char *save_ptr;

        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '\0' || line[0] == '#')
        {
            continue;
        }

        strncpy(line_copy, line, sizeof(line_copy) - 1);
        line_copy[sizeof(line_copy) - 1] = '\0';

        username = strtok_r(line_copy, ":", &save_ptr);
        salt = strtok_r(NULL, ":", &save_ptr);
        stored_hash = strtok_r(NULL, ":", &save_ptr);
        account_text = strtok_r(NULL, ":", &save_ptr);

        if (username == NULL || salt == NULL || stored_hash == NULL || account_text == NULL)
        {
            continue;
        }

        if (strcmp(username, new_username) == 0 || atoi(account_text) == new_account)
        {
            fclose(file);
            return 1;
        }
    }

    fclose(file);
    return 0;
}

static void handle_register_user(const BankRequest *request, BankResponse *response)
{
    char salt[SALT_SIZE];
    char salted_password[MAX_PASSWORD + SALT_SIZE];
    char hash[HASH_SIZE];
    FILE *file;

    if (request->username[0] == '\0' || request->password[0] == '\0')
    {
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE, "Registration failed. Username and password are required.");
        return;
    }

    if (!valid_account_number(request->from_account))
    {
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE, "Registration failed. Invalid account number %d.", request->from_account);
        return;
    }

    pthread_mutex_lock(&users_auth_mutex);

    if (user_or_account_exists(request->username, request->from_account))
    {
        pthread_mutex_unlock(&users_auth_mutex);
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE, "Registration failed. Username or account number already exists.");
        return;
    }

    generate_salt(request->username, request->from_account, salt, sizeof(salt));
    snprintf(salted_password, sizeof(salted_password), "%s%s", salt, request->password);
    sha256_hex(salted_password, hash);

    file = fopen(USERS_AUTH_FILE, "a");

    if (file == NULL)
    {
        pthread_mutex_unlock(&users_auth_mutex);
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE, "Registration failed. Could not open users.auth.");
        return;
    }

    fprintf(file, "%s:%s:%s:%d\n", request->username, salt, hash, request->from_account);
    fclose(file);

    if (!write_balance(request->from_account, 0.0))
    {
        pthread_mutex_unlock(&users_auth_mutex);
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE, "User was added, but account file could not be created.");
        return;
    }

    pthread_mutex_unlock(&users_auth_mutex);

    response->success = 1;
    response->balance = 0.0;
    snprintf(response->message, MAX_MESSAGE, "User %s registered for account %d.", request->username, request->from_account);
}

static int authenticate_user(const BankRequest *request, int *owned_account, BankResponse *response)
{
    pthread_mutex_lock(&users_auth_mutex);
    FILE *file = fopen(USERS_AUTH_FILE, "r");
    char line[AUTH_LINE_SIZE];

    if (file == NULL)
    {
        pthread_mutex_unlock(&users_auth_mutex);
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE, "Authentication failed. users.auth was not found.");
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char line_copy[AUTH_LINE_SIZE];
        char *username;
        char *salt;
        char *stored_hash;
        char *account_text;
        char *save_ptr;

        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '\0' || line[0] == '#')
        {
            continue;
        }

        strncpy(line_copy, line, sizeof(line_copy) - 1);
        line_copy[sizeof(line_copy) - 1] = '\0';

        username = strtok_r(line_copy, ":", &save_ptr);
        salt = strtok_r(NULL, ":", &save_ptr);
        stored_hash = strtok_r(NULL, ":", &save_ptr);
        account_text = strtok_r(NULL, ":", &save_ptr);

        if (username == NULL || salt == NULL || stored_hash == NULL || account_text == NULL)
        {
            continue;
        }

        if (strcmp(username, request->username) == 0)
        {
            char salted_password[MAX_PASSWORD + SALT_SIZE];
            char computed_hash[HASH_SIZE];
            int account_number = atoi(account_text);

            snprintf(salted_password, sizeof(salted_password), "%s%s", salt, request->password);
            sha256_hex(salted_password, computed_hash);
            fclose(file);
            pthread_mutex_unlock(&users_auth_mutex);

            if (strcmp(computed_hash, stored_hash) != 0)
            {
                response->success = 0;
                snprintf(response->message, MAX_MESSAGE, "Authentication failed. Incorrect password.");
                return 0;
            }

            if (!valid_account_number(account_number))
            {
                response->success = 0;
                snprintf(response->message, MAX_MESSAGE, "Authentication failed. Invalid account in users.auth.");
                return 0;
            }

            *owned_account = account_number;
            return 1;
        }
    }

    fclose(file);
    pthread_mutex_unlock(&users_auth_mutex);
    response->success = 0;
    snprintf(response->message, MAX_MESSAGE, "Authentication failed. Unknown username.");
    return 0;
}

static int check_account_ownership(const BankRequest *request, int owned_account, BankResponse *response)
{
    if (strcmp(request->operation, "BalanceCheck") == 0 ||
        strcmp(request->operation, "Deposit") == 0 ||
        strcmp(request->operation, "Withdraw") == 0 ||
        strcmp(request->operation, "Transfer") == 0)
    {
        if (request->from_account != owned_account)
        {
            response->success = 0;
            snprintf(response->message, MAX_MESSAGE,
                     "Access denied. User %s owns account %d, not account %d.",
                     request->username, owned_account, request->from_account);
            return 0;
        }
    }

    return 1;
}

/* ---------------- BANK OPERATIONS ---------------- */

static void handle_balance_check(const BankRequest *request, BankResponse *response)
{
    double balance;

    if (!valid_account_number(request->from_account))
    {
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Invalid account number %d.", request->from_account);
        return;
    }

    lock_account(request->from_account);

    if (!read_balance(request->from_account, &balance))
    {
        unlock_account(request->from_account);
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Could not read balance for account %d.", request->from_account);
        return;
    }

    unlock_account(request->from_account);

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

    if (!valid_account_number(request->from_account))
    {
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Invalid account number %d.", request->from_account);
        return;
    }

    lock_account(request->from_account);

    if (!read_balance(request->from_account, &balance))
    {
        unlock_account(request->from_account);
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Could not read balance for account %d.", request->from_account);
        return;
    }

    balance += request->amount;

    if (!write_balance(request->from_account, balance))
    {
        unlock_account(request->from_account);
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Could not save balance for account %d.", request->from_account);
        return;
    }

    unlock_account(request->from_account);

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

    if (!valid_account_number(request->from_account))
    {
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Invalid account number %d.", request->from_account);
        return;
    }

    lock_account(request->from_account);

    if (!read_balance(request->from_account, &balance))
    {
        unlock_account(request->from_account);
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Could not read balance for account %d.", request->from_account);
        return;
    }

    if (balance < request->amount)
    {
        unlock_account(request->from_account);
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
        unlock_account(request->from_account);
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Could not save balance for account %d.", request->from_account);
        return;
    }

    unlock_account(request->from_account);

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

    if (!valid_account_number(request->from_account) ||
        !valid_account_number(request->to_account))
    {
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Invalid transfer account number.");
        return;
    }

    lock_two_accounts_ordered(request->from_account, request->to_account);

    if (!read_balance(request->from_account, &from_balance) ||
        !read_balance(request->to_account, &to_balance))
    {
        unlock_two_accounts_ordered(request->from_account, request->to_account);
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Could not read one of the account balances.");
        return;
    }

    if (from_balance < request->amount)
    {
        unlock_two_accounts_ordered(request->from_account, request->to_account);
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
        unlock_two_accounts_ordered(request->from_account, request->to_account);
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Could not save transfer balances.");
        return;
    }

    unlock_two_accounts_ordered(request->from_account, request->to_account);

    response->success = 1;
    response->balance = from_balance;
    snprintf(response->message, MAX_MESSAGE,
             "Transferred %.2f from account %d to account %d. Account %d balance is %.2f.",
             request->amount, request->from_account, request->to_account,
             request->from_account, from_balance);
}

static void build_response(const BankRequest *request, BankResponse *response)
{
    BankRequest authenticated_request;
    int owned_account = -1;

    response->success = 0;
    response->balance = 0.0;
    memset(response->message, 0, MAX_MESSAGE);

    if (strcmp(request->operation, "Register") == 0)
    {
        handle_register_user(request, response);
        return;
    }

    if (!authenticate_user(request, &owned_account, response))
    {
        return;
    }

    authenticated_request = *request;

    authenticated_request.from_account = owned_account;

    if (!check_account_ownership(&authenticated_request, owned_account, response))
    {
        return;
    }

    if (strcmp(authenticated_request.operation, "BalanceCheck") == 0)
    {
        handle_balance_check(&authenticated_request, response);
    }
    else if (strcmp(authenticated_request.operation, "Deposit") == 0)
    {
        handle_deposit(&authenticated_request, response);
    }
    else if (strcmp(authenticated_request.operation, "Withdraw") == 0)
    {
        handle_withdraw(&authenticated_request, response);
    }
    else if (strcmp(authenticated_request.operation, "Transfer") == 0)
    {
        handle_transfer(&authenticated_request, response);
    }
    else
    {
        response->success = 0;
        snprintf(response->message, MAX_MESSAGE,
                 "Invalid operation: %s.", authenticated_request.operation);
    }
}

/* ---------------- THREAD AND SERVER LOOP ---------------- */

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

    if (!initialize_account_mutexes())
    {
        exit(1);
    }

    if (!ensure_data_directory())
    {
        exit(1);
    }

    if (!ensure_users_auth_file())
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
