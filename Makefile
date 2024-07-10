CC=/usr/bin/gcc
CFLAGS= -std=gnu17 -Wall -Wextra -O2 -I./src/server/include -I./src/client/include

all: server client

server: src/server/src/server.c
	$(CC) src/server/src/server.c -o server $(CFLAGS)

client: src/client/src/client.c
	$(CC) src/client/src/client.c -o client $(CFLAGS)

.PHONY: clean

clean:
	rm -f server client
