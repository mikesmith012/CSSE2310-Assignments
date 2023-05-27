CC = gcc
CFLAGS = -Wall -pedantic -std=gnu99 -I/local/courses/csse2310/include
CARGS = -L/local/courses/csse2310/lib -lcsse2310a3
DEBUG = -g
.PHONY = all clean
.DEFAULT_GOAL = all

all: jobrunner

jobrunner: a3main.o
	$(CC) $(CFLAGS) $(CARGS) $^ -o $@

a3main.o: a3main.c

clean:
	rm -f jobrunner *.o
