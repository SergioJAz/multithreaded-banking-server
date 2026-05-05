CC = gcc
CFLAGS = -Wall -Wextra -pthread

all: bank_server bank_client

bank_server: bank_server.c bank_common.h
	$(CC) $(CFLAGS) bank_server.c -o bank_server

bank_client: bank_client.c bank_common.h
	$(CC) $(CFLAGS) bank_client.c -o bank_client

clean:
	rm -f bank_server bank_client
