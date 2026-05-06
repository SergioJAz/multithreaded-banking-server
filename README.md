# OS-final-project
Enviroment Setup:

mkdir SMBS

cd SMBS

mkdir data auth

touch bank_common.h bank_server.c bank_client.c Makefile

# Default accounts to Use:
sergio / sergio123 / account 0

priyan / priyan123 / account 1

bob / bob123 / account 2

alice / alice123 / account 3

kal / kal / account 4

# How to run new code:

In terminal on linux:
 
make clean

make

./bank_server in terminal

./bank_client in another terminal

# Steps to follow:

~~Step 1: Server and client talk through FIFOs.~~ Done

~~Step 2: Client can send operation requests.~~ Done

~~Step 3: Server can read/write plain account balances.~~ Done

~~Step 4: Add mutexes.~~ Done

~~Step 5: Add transfers with ordered locking.~~ Done

~~Step 6: Add authentication.~~ Done

~~Step 7: Add encryption.~~ Done

