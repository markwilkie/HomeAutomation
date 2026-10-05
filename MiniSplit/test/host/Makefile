CC ?= gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -I../../include -Istubs

test: test_control_logic
	./test_control_logic

test_control_logic: test_control_logic.c ../../src/control_logic.c ../../include/control_logic.h
	$(CC) $(CFLAGS) -o $@ test_control_logic.c ../../src/control_logic.c

clean:
	rm -f test_control_logic

.PHONY: test clean
