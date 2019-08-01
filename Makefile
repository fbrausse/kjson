# tested on x86_64-pc-linux-gnu with gcc-9.1, clang-8, CompCert-3.5, tcc-0.9.27

OBJS = \
	kjson.o \
	test-kjson.o \

EXES = \
	test-kjson \

CFLAGS ?= -O2

ifeq ($(shell $(CC) --version 2>/dev/null | grep -o CompCert),CompCert)
  WARNS ?= -Wall
  override WARNS += -Wno-c11-extensions
  # CompCert only produces the dep-file in -M or -MM mode, no object file; set to nothing
  DEPFLAGS ?=
  # CompCert doesn't support the -std= arguments
  CSTD ?=
else
  WARNS ?= -Wall -Wextra
  DEPFLAGS ?= -MD
  CSTD = -std=c11
endif

DEPS = $(OBJS:.o=.d)

.PHONY: all clean

all: test-kjson

test-kjson: $(OBJS)

$(OBJS): override CFLAGS += $(CSTD) $(DEPFLAGS) $(WARNS)
$(OBJS): %.o: %.c Makefile

test-kjson.o: override CPPFLAGS += -D_POSIX_C_SOURCE=200809L

clean:
	$(RM) $(OBJS) $(DEPS) $(EXES)

-include $(DEPS)
