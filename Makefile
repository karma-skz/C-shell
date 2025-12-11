## Makefile for the mini shell
##
## How to use:
## - make          -> builds the shell binary (shell.out)
## - make clean    -> removes object files and the binary
##
## Notes for learners:
## - CC: which compiler to use
## - CFLAGS: compiler options (C standard and warnings); the feature macros
##   enable POSIX/XSI APIs that we use (like sigaction, tcsetpgrp, etc.)
## - INCLUDES: where to find header files for this project
## - SRCS/OBJS/HDRS: lists of source/object/header files that make tracks
##
CC = gcc
CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
         -Wall -Wextra -Werror -Wno-unused-parameter -fno-asm
INCLUDES = -Iinclude

SRCS = src/main.c src/prompt.c src/parser.c src/hop.c src/reveal.c src/ping.c src/activities.c src/signals.c src/jobs.c src/executor.c src/log.c
OBJS = $(SRCS:.c=.o)
HDRS = include/prompt.h include/parser.h include/hop.h include/reveal.h include/ping.h include/activities.h include/signals.h include/jobs.h include/executor.h include/log.h

.PHONY: all clean
all: shell.out

shell.out: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

src/%.o: src/%.c $(HDRS)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJS) shell.out

