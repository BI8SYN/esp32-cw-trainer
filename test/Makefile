CC ?= cc
CFLAGS = -std=c11 -Wall -Wextra -Werror -g -I../main/core -I.
CORE_SRCS = $(wildcard ../main/core/*.c)
TESTS = $(patsubst %.c,%,$(wildcard test_*.c))

.PHONY: test clean
test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done

test_%: test_%.c $(CORE_SRCS) test_util.h
	$(CC) $(CFLAGS) -o $@ $< $(CORE_SRCS)

clean:
	rm -f $(TESTS)
