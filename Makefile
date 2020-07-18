# tested on x86_64-pc-linux-gnu with gcc-9.1, clang-8, CompCert-3.5, tcc-0.9.27

VERS = 0.1

DESTDIR ?= /usr/local
LIBDIR ?= $(DESTDIR)/lib
INCLUDEDIR ?= $(DESTDIR)/include

LIB_OBJS = $(addprefix pic/,\
	kjson.o \
)
SLIB_OBJS = \
	kjson.o \

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

ifeq ($(OS),Windows_NT)
  OS = Windows
else
  OS = $(shell uname)
endif

DEPS = $(OBJS:.o=.d)

.PHONY: all install clean

all: libkjson.so libkjson.a

install: libkjson.so libkjson.a kjson.h kjson.hh $(LIBDIR)/pkgconfig/kjson.pc | $(LIBDIR)/ $(INCLUDEDIR)/
	install -t $(LIBDIR) -m 0644 libkjson.a
	install -t $(LIBDIR) -m 0755 libkjson.so
	install -t $(INCLUDEDIR) -m 0644 kjson.h kjson.hh

$(LIBDIR)/pkgconfig/kjson.pc: Makefile | $(LIBDIR)/pkgconfig/
	printf "\
Name: kjson\\n\
Version: %s\\n\
Description: A JSON parser in C\\n\
URL: https://github.com/fbrausse/kjson\\n\
Cflags: %s\\n\
Libs: %s\\n\
" "$(VERS)" "-I$(realpath $(INCLUDEDIR))" "-L$(realpath $(LIBDIR)) -lkjson" > $@

ifeq ($(OS),Darwin)
libkjson.so: override LDFLAGS += -dynamiclib \
	-install_name $(realpath $(LIBDIR))/libkjson.so
else
libkjson.so: override LDFLAGS += -shared
endif

libkjson.so: $(LIB_OBJS) | pic/
	$(CC) $(LDFLAGS) -o $@ $+ $(LDLIBS)

libkjson.a: $(SLIB_OBJS)
	$(RM) $@ && $(AR) rcs $@ $(SLIB_OBJS)

$(LIB_OBJS): pic/%.o: %.c | pic/
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(LIB_OBJS): override CFLAGS += -fPIC

%/:
	mkdir -p $@

test-kjson: $(OBJS)

$(OBJS): override CFLAGS += $(CSTD) $(DEPFLAGS) $(WARNS)
$(OBJS): %.o: %.c Makefile

test-kjson.o: override CPPFLAGS += -D_POSIX_C_SOURCE=200809L

clean:
	$(RM) $(OBJS) $(LIB_OBJS) $(DEPS) $(EXES)

-include $(DEPS)
