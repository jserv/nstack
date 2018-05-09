CC ?= gcc
OUT ?= build

CFLAGS := -Wall -Wextra -Wno-unused-parameter -g
CFLAGS += --std=gnu99 -pthread -MMD
CFLAGS += -include config.h -I include

.SUFFIXES: .c .o

OBJS := \
	src/arp.o \
	src/ether.o \
	src/ether_fcs.o \
	src/icmp.o \
	src/ip.o \
	src/ip_defer.o \
	src/ip_fragment.o \
	src/ip_route.o \
	src/tcp.o \
	src/udp.o \
	src/socket.o

OBJS += \
	src/linux/ether.o

# stack entry
OBJS += src/nstack.o

deps := $(OBJS:%.o=%.d)
SHELL_HACK := $(shell mkdir -p $(OUT))

EXEC = $(OUT)/inetd

all: $(EXEC)

$(OUT)/inetd: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	$(RM) $(EXEC) $(OBJS) $(deps)

-include $(deps)
