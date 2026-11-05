# Multithreaded Banking Server

A client-server banking system in C on Linux where a POSIX-threaded server handles concurrent deposits, withdrawals and transfers safely, with salted password hashing and encrypted balance files.

## Features

- One detached `pthread` per client request, so many clients are served at the same time.
- One mutex per account; transfers lock both accounts in lower-ID-first order to prevent deadlock.
- Atomic withdrawals: a balance can never go negative, even under heavy concurrent access.
- Users stored in `users.auth` as `username:salt:sha256(salt + password):account_id`.
- Account balances encrypted at rest (XOR + hex) and decrypted only in memory during a transaction.
- Client and server talk through named pipes: one public FIFO for requests, one private FIFO per client for replies.
- Client CLI: register, log in, balance check, deposit, withdraw, transfer.

## Stack

- C (gcc, `-Wall -Wextra -pthread`), Linux
- POSIX threads and mutexes, named pipes (FIFOs), SHA-256
- Bash test scripts for concurrency checks

## How it works

1. The server creates `/tmp/smbs_public_fifo`, the `data/` folder and an empty `users.auth` on first run.
2. Each client creates its own `/tmp/smbs_client_<pid>_fifo` and writes a `BankRequest` to the public FIFO.
3. The server reads the request and spawns a thread for it.
4. The thread authenticates the user, locks the needed account mutex(es), decrypts the balance, applies the operation, re-encrypts and writes it back, then unlocks.
5. The thread sends a `BankResponse` back through the client's private FIFO.

| File | Purpose |
|---|---|
| `bank_server.c` | Server: FIFO listener, threads, auth, locking, encryption |
| `bank_client.c` | Client CLI |
| `bank_common.h` | Shared request/response structs and limits |
| `test_same_account_concurrency.sh` | Stress test: many users on one account |
| `Test Script.sh` | Wider test: registration, bad login, concurrent clients, opposite transfers |

## Build and run

```bash
make
./bank_server          # terminal 1
./bank_client          # terminal 2 (open more terminals for more clients)
```

Register a user from the client start menu, then log in. Clean with `make clean`.

Run the tests from the project folder (they use a temporary copy and do not touch your data):

```bash
bash test_same_account_concurrency.sh
bash "Test Script.sh"
```

## My role

I worked on all parts of the project with Priyan:

- Server: FIFO listener, one thread per request, per-account mutexes and ordered locking for transfers.
- Authentication with salted SHA-256 and encryption of balance files.
- Client CLI and the concurrency test scripts.

## Results

- Stress test: 5 users made 20 simultaneous withdrawals of 30 from one account holding 1000; the final balance was exactly 400.
- A second burst of 20 withdrawals tried to overdraw the account; it stopped at 10 and never went negative.
- Opposite transfers between two accounts at the same time completed with no deadlock.

## Team

- Sergio Azizi
- Priyan Wijeratne

CSIS221 Operating Systems, University of Balamand. Spring 2026.
