CC = gcc
CFLAGS = -Wall -pedantic -std=gnu99 -pthread -I/local/courses/csse2310/include
CARGS = -L/local/courses/csse2310/lib -lcsse2310a3 -lcsse2310a4 -ltinyexpr -lm
DEBUG = -g
.PHONY = all clean
.DEFAULT_GOAL = all

all: intclient intserver

intclient: a4client.o
	$(CC) $(CFLAGS) $(CARGS) $^ -o $@

intserver: a4server.o
	$(CC) $(CFLAGS) $(CARGS) $^ -o $@

a4client.o: a4client.c

a4server.o: a4server.c

clean:
	rm -f intclient intserver *.o
