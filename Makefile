
CSTD = -std=c99

OBJS = \
	kjson.o \
	test-kjson.o \

EXES = \
	test-kjson \

DEPS = $(OBJS:.o=.d)

CFLAGS ?= -O2

.PHONY: all clean

all: test-kjson

test-kjson: $(OBJS)

$(OBJS): override CFLAGS += $(CSTD) -MMD -Wall -Wextra
$(OBJS): %.o: %.c Makefile

test-kjson.o: override CPPFLAGS += -D_POSIX_C_SOURCE=200809L

clean:
	$(RM) $(OBJS) $(DEPS) $(EXES)

-include $(DEPS)
