#!/bin/bash
set -u

PROJECT_DIR="$(pwd)"
WORK_DIR=""
SERVER_PID=""
PASS=0
FAIL=0

cleanup() {
    if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
    rm -f /tmp/smbs_public_fifo /tmp/smbs_same_account_*_fifo
    if [[ -n "$WORK_DIR" && -d "$WORK_DIR" ]]; then
        rm -rf "$WORK_DIR"
    fi
}
trap cleanup EXIT

ok() {
    PASS=$((PASS + 1))
    echo "PASS: $1"
}

bad() {
    FAIL=$((FAIL + 1))
    echo "FAIL: $1"
}

need_file() {
    if [[ ! -f "$PROJECT_DIR/$1" ]]; then
        echo "Missing $1. Run this script from your project folder."
        exit 1
    fi
}

get_field() {
    local output="$1"
    local key="$2"
    echo "$output" | awk -F'|' -v k="$key" '{for (i=1; i<=NF; i++) {split($i,a,"="); if (a[1] == k) print a[2]}}'
}

run_request() {
    timeout 8 ./smbs_same_account_helper "$1" "$2" "$3" "$4" "$5" "$6"
}

expect_success() {
    local label="$1"
    local output="$2"
    local expected="$3"
    local actual
    actual="$(get_field "$output" "success")"

    if [[ "$actual" == "$expected" ]]; then
        ok "$label"
    else
        bad "$label"
        echo "      output: $output"
    fi
}

expect_balance() {
    local label="$1"
    local user="$2"
    local pass="$3"
    local expected="$4"
    local output actual

    output="$(run_request "$user" "$pass" BalanceCheck 1 0 0 || true)"
    actual="$(get_field "$output" "balance")"

    if [[ "$actual" == "$expected" ]]; then
        ok "$label = $expected"
    else
        bad "$label expected $expected got $actual"
        echo "      output: $output"
    fi
}

need_file bank_server.c
need_file bank_client.c
need_file bank_common.h
need_file Makefile

WORK_DIR="$(mktemp -d /tmp/smbs_same_account_test.XXXXXX)"
cp "$PROJECT_DIR/bank_server.c" "$PROJECT_DIR/bank_client.c" "$PROJECT_DIR/bank_common.h" "$PROJECT_DIR/Makefile" "$WORK_DIR/"
cd "$WORK_DIR" || exit 1

cat > smbs_same_account_helper.c <<'HELPER'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "bank_common.h"

static void safe_copy(char *dest, const char *src, size_t size)
{
    if (size == 0) return;
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}

int main(int argc, char *argv[])
{
    if (argc != 7) return 2;

    BankRequest request;
    BankResponse response;
    memset(&request, 0, sizeof(request));
    memset(&response, 0, sizeof(response));

    safe_copy(request.username, argv[1], sizeof(request.username));
    safe_copy(request.password, argv[2], sizeof(request.password));
    safe_copy(request.operation, argv[3], sizeof(request.operation));
    request.from_account = atoi(argv[4]);
    request.to_account = atoi(argv[5]);
    request.amount = atof(argv[6]);

    snprintf(request.private_fifo, sizeof(request.private_fifo), "/tmp/smbs_same_account_%ld_fifo", (long)getpid());
    unlink(request.private_fifo);

    if (mkfifo(request.private_fifo, 0600) == -1) return 3;

    int public_fd = -1;
    for (int i = 0; i < 50; i++) {
        public_fd = open(PUBLIC_FIFO, O_WRONLY | O_NONBLOCK);
        if (public_fd != -1) break;
        usleep(100000);
    }

    if (public_fd == -1) {
        unlink(request.private_fifo);
        return 4;
    }

    if (write(public_fd, &request, sizeof(request)) != sizeof(request)) {
        close(public_fd);
        unlink(request.private_fifo);
        return 5;
    }
    close(public_fd);

    int private_fd = open(request.private_fifo, O_RDONLY);
    if (private_fd == -1) {
        unlink(request.private_fifo);
        return 6;
    }

    if (read(private_fd, &response, sizeof(response)) <= 0) {
        close(private_fd);
        unlink(request.private_fifo);
        return 7;
    }

    close(private_fd);
    unlink(request.private_fifo);

    printf("success=%d|account_id=%d|balance=%.2f|message=%s\n",
           response.success, response.account_id, response.balance, response.message);

    return response.success ? 0 : 1;
}
HELPER

echo "Running same-account concurrency test in a temporary folder."
echo "Your real users.auth and data folder will not be touched."
echo

if make clean >/dev/null 2>&1 && make >/dev/null 2>&1; then
    ok "project builds"
else
    bad "project builds"
    exit 1
fi

if gcc -Wall -Wextra smbs_same_account_helper.c -o smbs_same_account_helper >/dev/null 2>&1; then
    ok "test helper builds"
else
    bad "test helper builds"
    exit 1
fi

rm -f /tmp/smbs_public_fifo /tmp/smbs_same_account_*_fifo
./bank_server >/dev/null 2>&1 &
SERVER_PID=$!

for i in {1..50}; do
    [[ -p /tmp/smbs_public_fifo ]] && break
    sleep 0.1
done

if [[ -p /tmp/smbs_public_fifo ]]; then
    ok "server starts"
else
    bad "server starts"
    exit 1
fi

echo

echo "1) Create five users"
for i in 1 2 3 4 5; do
    output="$(run_request "shared$i" "pass$i" Register 0 0 0 || true)"
    expect_success "register shared$i" "$output" 1
done

echo

echo "2) Make all five users point to account 1 for this isolated test"
awk -F: 'BEGIN {OFS=":"} /^#/ {print; next} NF == 4 {$4 = 1; print; next} {print}' users.auth > users.auth.tmp && mv users.auth.tmp users.auth

all_login_ok=1
for i in 1 2 3 4 5; do
    output="$(run_request "shared$i" "pass$i" Login 1 0 0 || true)"
    actual="$(get_field "$output" account_id)"
    if [[ "$(get_field "$output" success)" == "1" && "$actual" == "1" ]]; then
        ok "shared$i logs into shared account 1"
    else
        bad "shared$i logs into shared account 1"
        echo "      output: $output"
        all_login_ok=0
    fi
done

echo

echo "3) Put starting money into shared account 1"
output="$(run_request shared1 pass1 Deposit 1 0 1000 || true)"
expect_success "deposit 1000 into shared account" "$output" 1
expect_balance "shared account starting balance" shared1 pass1 1000.00

echo

echo "4) Run 20 simultaneous withdrawals from the same account using 5 different users"
pids=()
for i in $(seq 1 20); do
    user_number=$(( ((i - 1) % 5) + 1 ))
    (run_request "shared$user_number" "pass$user_number" Withdraw 1 0 30 >/dev/null 2>&1) &
    pids+=("$!")
done

withdraw_ok=1
for pid in "${pids[@]}"; do
    wait "$pid" || withdraw_ok=0
done

if [[ "$withdraw_ok" == 1 ]]; then
    ok "20 same-account withdrawals finish successfully"
else
    bad "20 same-account withdrawals finish successfully"
fi

expect_balance "shared account after 20 withdrawals of 30" shared1 pass1 400.00

echo

echo "5) Try to overdraw the same account with many users at the same time"
pids=()
for i in $(seq 1 20); do
    user_number=$(( ((i - 1) % 5) + 1 ))
    (run_request "shared$user_number" "pass$user_number" Withdraw 1 0 30 >/dev/null 2>&1 || true) &
    pids+=("$!")
done

for pid in "${pids[@]}"; do
    wait "$pid"
done

expect_balance "shared account never goes negative" shared1 pass1 10.00

echo

echo "Result: $PASS passed, $FAIL failed."

if [[ "$FAIL" -eq 0 ]]; then
    exit 0
fi
exit 1
