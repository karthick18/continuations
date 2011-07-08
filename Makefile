CC := gcc
DEBUG := -g
CFLAGS := -Wall $(DEBUG)
LDLIBS := -lpthread
SRCS := continuation.c
OBJS := $(SRCS:%.c=%.o)
TARGET := continuation
ARCH_FLAGS :=

ifeq ("$(ARCH)", "x86")
	ARCH_FLAGS := -m32
endif

CFLAGS += $(ARCH_FLAGS)

all: $(TARGET)

continuation: $(OBJS)
	$(CC) $(ARCH_FLAGS) $(DEBUG) -o $@ $^ $(LDLIBS)

%.o:%.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJS) *~ $(TARGET)