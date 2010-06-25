CC ?= gcc
LINKER ?= $(CC)
FSLINK ?= ln -fs
NAME ?= kennyfs
CCARGS := -Wall -O0 -g -Wunused-parameter -fPIC `pkg-config fuse --cflags` -DKFS_LOG_TRACE
CCINCARGS :=
LINKARGS := `pkg-config fuse --libs`
BRICKS := $(wildcard *_brick/)
CLEANBRICKS := $(patsubst %/,%_clean,$(BRICKS))
OFILES := $(patsubst %.c,%.o,$(wildcard kfs_*.c))
SOFILES := $(patsubst %,libkfs_brick_%.so,$(patsubst %_brick/,%,$(BRICKS)))

FULLCC := $(CC) $(CCARGS) $(CCINCARGS)
FULLLINK := $(LINKER) $(LINKARGS)

.PHONY: all clean $(BRICKS) $(CLEANBRICKS)

all: $(NAME) $(SOFILES) $(SUBDIRS)

clean: $(CLEANBRICKS)
	rm -f $(NAME) *.o lib*.so.?.? lib*.so.? lib*.so

$(NAME): $(OFILES) $(NAME).o
	$(FULLLINK) -o $@ $(OFILES) $(NAME).o

$(BRICKS): %:
	$(MAKE) -C $@

$(CLEANBRICKS): %_clean:
	$(MAKE) -C $* clean

libkfs_brick_%.so: %_brick/
	$(FSLINK) $*_brick/$@.0.0 $@
	$(FSLINK) $*_brick/$@.0.0 $@.0
	$(FSLINK) $*_brick/$@.0.0 $@.0.0

%.o: %.c
	$(FULLCC) -c $*.c -o $@
