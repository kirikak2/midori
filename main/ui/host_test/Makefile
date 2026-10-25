# Host build for the Sequencer engine tests (main/ui/seq_engine.c).
# No FreeRTOS / ESP-IDF — just gcc/clang + libc.
#
#     make            # build + run
#     make clean

CC      ?= cc
CFLAGS  ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -std=c11 -fsanitize=address,undefined
LDFLAGS ?= -fsanitize=address,undefined

SEQ_SRC = ../seq_engine.c test_seq_engine.c
SEQ_BIN = test_seq_engine

all: run

$(SEQ_BIN): $(SEQ_SRC) ../seq_engine.h
	$(CC) $(CFLAGS) -o $@ $(SEQ_SRC) $(LDFLAGS)

run: $(SEQ_BIN)
	./$(SEQ_BIN)

clean:
	rm -f $(SEQ_BIN)

.PHONY: all run clean
