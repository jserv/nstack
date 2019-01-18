CC ?= gcc
OUT ?= build

CFLAGS := -Wall -Wextra -Wno-unused-parameter -g
CFLAGS += --std=gnu99 -pthread
CFLAGS += -include config.h -I include

SRC = src

OBJS_core := \
	arp.o \
	ether.o \
	ether_fcs.o \
	icmp.o \
	ip.o \
	ip_defer.o \
	ip_fragment.o \
	ip_route.o \
	tcp.o \
	udp.o \
	nstack.o \
	linux/ether.o
OBJS_core := $(addprefix $(OUT)/, $(OBJS_core))

OBJS_socket := \
	socket.o
OBJS_socket := $(addprefix $(OUT)/, $(OBJS_socket))

OBJS := $(OBJS_core) $(OBJS_socket)
deps := $(OBJS:%.o=%.o.d)

SHELL_HACK := $(shell mkdir -p $(OUT))
SHELL_HACK := $(shell mkdir -p $(OUT)/linux)

EXEC = $(OUT)/inetd $(OUT)/tnetcat $(OUT)/unetcat

all: $(EXEC)

$(OUT)/%.o: $(SRC)/%.c
	$(CC) -o $@ $(CFLAGS) -c -MMD -MF $@.d $<

$(OUT)/inetd: $(OBJS_core)
	$(CC) $(CFLAGS) -o $@ $^

$(OUT)/tnetcat: $(OBJS_socket)
	$(CC) $(CFLAGS) -o $@ tests/tnetcat.c $^

$(OUT)/unetcat: $(OBJS_socket)
	$(CC) $(CFLAGS) -o $@ tests/unetcat.c $^

clean:
	$(RM) $(EXEC) $(OBJS) $(deps)
distclean: clean
	$(RM) -r $(OUT)

-include $(deps)
