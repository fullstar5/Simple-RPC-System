CC=gcc
RPC_SYSTEM=rpc.o
LDFLAGS=-lm

.PHONY: format all

all: $(RPC_SYSTEM)

$(RPC_SYSTEM): rpc.c rpc.h
	$(CC) -Wall -c -o $@ $< $(LDFLAGS)

# RPC_SYSTEM_A=rpc.a
# $(RPC_SYSTEM_A): rpc.o
# 	ar rcs $(RPC_SYSTEM_A) $(RPC_SYSTEM)

format:
	clang-format -style=file -i *.c *.h

clean:
	rm -f *.o 
