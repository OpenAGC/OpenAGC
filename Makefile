# openagc Makefile — host (generic) build

CC      ?= cc
AR      ?= ar
CFLAGS  ?= -O2 -Wall -Wextra -Wpedantic
CFLAGS  += -I include -I src -DOPENAGC_GENERIC

SRCS = \
	src/runtime.c \
	src/capabilities.c \
	src/memory.c \
	src/cb.c \
	src/cb_builders.c \
	src/driver_ops.c \
	src/driver_registry.c \
	src/driver_sony_exports.c \
	src/agc_workload_packet.c \
	src/agc_workload_state.c \
	src/videoout_patch.c \
	src/driver_generic.c \
	src/context_state.c \
	src/register_defaults.c \
	src/register_defaults_v0.c \
	src/register_defaults_v4.c \
	src/register_defaults_v5.c \
	src/register_defaults_v7.c \
	src/register_defaults_v8.c \
	src/register_defaults_v9.c \
	src/register_defaults_v10.c \
	src/register_defaults_v11.c \
	src/register_defaults_versions.c \
	src/acb.c \
	src/dcb.c \
	src/texture.c \
	src/shader.c \
	src/graphics.c \
	src/game_compat.c \
	src/videoout_generic.c

OBJS = $(SRCS:.c=.o)

.PHONY: all clean

all: libopenagc.a

libopenagc.a: $(OBJS)
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) libopenagc.a
