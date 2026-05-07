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
    rm -f /tmp/smbs_public_fifo /tmp/smbs_clean_*_fifo
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
    timeout 8 ./smbs_clean_helper "$1" "$2" "$3" "$4" "$5" "$6"
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
    local account="$4"
    local expected="$5"
    local output actual

    output="$(run_request "$user" "$pass" BalanceCheck "$account" 0 0 || true)"
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

WORK_DIR="$(mktemp -d /tmp/smbs_clean_test.XXXXXX)"
cp "$PROJECT_DIR/bank_server.c" "$PROJECT_DIR/bank_client.c" "$PROJECT_DIR/bank_common.h" "$PROJECT_DIR/Makefile" "$WORK_DIR/"
cd "$WORK_DIR" || exit 1

cat > smbs_clean_helper.c <<'HELPER'
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

    snprintf(request.private_fifo, sizeof(request.private_fifo), "/tmp/smbs_clean_%ld_fifo", (long)getpid());
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

echo "Running clean SMBS demo tests in a temporary folder."
echo "Your real users.auth and data folder will not be touched."
echo

if make clean >/dev/null 2>&1 && make >/dev/null 2>&1; then
    ok "project builds"
else
    bad "project builds"
    exit 1
fi

if gcc -Wall -Wextra smbs_clean_helper.c -o smbs_clean_helper >/dev/null 2>&1; then
    ok "test helper builds"
else
    bad "test helper builds"
    exit 1
fi

rm -f /tmp/smbs_public_fifo /tmp/smbs_clean_*_fifo
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

echo "1) Registering 5 demo users"
for i in 1 2 3 4 5; do
    output="$(run_request "demo$i" "pass$i" Register 0 0 0 || true)"
    expect_success "register demo$i" "$output" 1
done

echo

echo "2) Setting starting balances"
output="$(run_request demo1 pass1 Deposit 1 0 1000 || true)"
expect_success "deposit account 1" "$output" 1
output="$(run_request demo2 pass2 Deposit 2 0 1000 || true)"
expect_success "deposit account 2" "$output" 1
for i in 3 4 5; do
    output="$(run_request "demo$i" "pass$i" Deposit "$i" 0 100 || true)"
    expect_success "deposit account $i" "$output" 1
done

echo

echo "3) Authentication checks"
output="$(run_request demo1 wrongpass Login 1 0 0 || true)"
expect_success "bad password is rejected" "$output" 0
output="$(run_request demo1 pass1 Login 1 0 0 || true)"
expect_success "good login works" "$output" 1
account_id="$(get_field "$output" account_id)"
if [[ "$account_id" == "1" ]]; then
    ok "login returns account ID"
else
    bad "login returns account ID"
    echo "      output: $output"
fi

echo

echo "4) Five concurrent clients"
pids=()
for i in 1 2 3 4 5; do
    (run_request "demo$i" "pass$i" BalanceCheck "$i" 0 0 >/dev/null 2>&1) &
    pids+=("$!")
done
concurrent_ok=1
for pid in "${pids[@]}"; do
    wait "$pid" || concurrent_ok=0
done
if [[ "$concurrent_ok" == 1 ]]; then
    ok "5 concurrent client requests finish"
else
    bad "5 concurrent client requests finish"
fi
expect_balance "account 1 after concurrent clients" demo1 pass1 1 1000.00
expect_balance "account 2 after concurrent clients" demo2 pass2 2 1000.00
expect_balance "account 3 after concurrent clients" demo3 pass3 3 100.00
expect_balance "account 4 after concurrent clients" demo4 pass4 4 100.00
expect_balance "account 5 after concurrent clients" demo5 pass5 5 100.00

echo "5) Race-condition test with same-account withdrawals"
pids=()
for i in $(seq 1 20); do
    (run_request demo1 pass1 Withdraw 1 0 30 >/dev/null 2>&1) &
    pids+=("$!")
done
withdraw_ok=1
for pid in "${pids[@]}"; do
    wait "$pid" || withdraw_ok=0
done
if [[ "$withdraw_ok" == 1 ]]; then
    ok "20 simultaneous withdrawals finish"
else
    bad "20 simultaneous withdrawals finish"
fi
expect_balance "account 1 after withdrawals" demo1 pass1 1 400.00

echo

echo "6) Deadlock test with opposite transfers"
pids=()
for i in $(seq 1 20); do
    (run_request demo1 pass1 Transfer 1 2 5 >/dev/null 2>&1) &
    pids+=("$!")
    (run_request demo2 pass2 Transfer 2 1 5 >/dev/null 2>&1) &
    pids+=("$!")
done
transfer_ok=1
for pid in "${pids[@]}"; do
    wait "$pid" || transfer_ok=0
done
if [[ "$transfer_ok" == 1 ]]; then
    ok "opposite transfers finish without deadlock"
else
    bad "opposite transfers finish without deadlock"
fi
expect_balance "account 1 after opposite transfers" demo1 pass1 1 400.00
expect_balance "account 2 after opposite transfers" demo2 pass2 2 1000.00

echo

echo "7) Encrypted storage check"
encrypted_ok=1
for account in 1 2 3 4 5; do
    file="data/account_${account}.txt"
    if [[ ! -f "$file" ]]; then
        bad "$file exists"
        encrypted_ok=0
        continue
    fi
    content="$(cat "$file")"
    if [[ "$content" == SMBSX1:* ]] && ! echo "$content" | grep -Eq '400\.00|1000\.00|100\.00'; then
        ok "$file is encrypted"
    else
        bad "$file is encrypted"
        encrypted_ok=0
    fi
done

echo

echo "Result: $PASS passed, $FAIL failed."

if [[ "$FAIL" -eq 0 ]]; then
    exit 0
fi
exit 1
