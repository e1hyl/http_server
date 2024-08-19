CC=gcc
CFLAGS=-g -Wall -Wextra -fsanitize=address
LDFLAGS=-lz

# %.o: %.c
# 	$(CC) -c -o $@ $< $(CFLAGS)

server: server.c
	$(CC) $(CFLAGS) -o server server.c $(LDFLAGS)

clean: 
	rm -rf *.o server 