# openagc Makefile — host (generic) build

CC      ?= cc
AR      ?= ar
CFLAGS  ?= -O2 -Wall -Wextra -Wpedantic
CFLAGS  += -I include -I src -DOPENAGC_GENERIC

SRCS = \
	src/cb.c \
	src/cb_builders.c \
	src/driver_generic.c \
	src/context_state.c \
	src/register_defaults.c \
	src/acb.c \
	src/dcb.c \
	src/texture.c \
	src/shader.c

OBJS = $(SRCS:.c=.o)

TEST_SRCS = \
	tests/test_main.c \
	tests/test_types.c \
	tests/test_acb.c \
	tests/test_cb.c \
	tests/test_dcb.c \
	tests/test_driver.c \
	tests/test_texture.c \
	tests/test_shader.c \
	tests/test_ioctl.c \
	tests/test_register_defaults.c

TEST_OBJS = $(TEST_SRCS:.c=.o)

.PHONY: all clean test

all: libopenagc.a

libopenagc.a: $(OBJS)
	$(AR) rcs $@ $^

openagc_tests: $(TEST_OBJS) libopenagc.a
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS) -L. -lopenagc

test: openagc_tests
	./openagc_tests

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TEST_OBJS) libopenagc.a openagc_tests
