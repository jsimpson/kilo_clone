CC=gcc
CFLAGS=-Wall -Wextra -Werror -Wmissing-prototypes -pedantic -g -O3 -std=gnu11

kilo: kilo.c
	$(CC) $(CFLAGS) kilo.c -o kilo

clean:
	rm kilo
