CC ?= gcc
LINKER ?= $(CC)
FSLINK ?= ln -fs
CCARGS := -Wall -O0 -g -Wunused-parameter -fPIC `pkg-config fuse --cflags` -DKFS_LOG_TRACE -I..
CCINCARGS :=
LINKARGS := `pkg-config fuse --libs` $(wildcard ../kfs_*.o)
BRICKS := $(wildcard kfs_brick_*.c)
OFILES := $(patsubst %.c,%.o,$(filter-out $(BRICKS),$(wildcard kfs_*.c)))
SOFILES := $(patsubst %.c,lib%.so,$(BRICKS))

FULLCC := $(CC) $(CCARGS) $(CCINCARGS)
FULLLINK := $(LINKER) $(LINKARGS)

.PHONY: all clean

all: $(SOFILES)

clean:
	rm -f *.o lib*.so.?.? lib*.so.? lib*.so

lib%.so: %.o
	$(FULLLINK) -shared -Wl,-soname,$@.0 $< -o $@.0.0 $(OFILES)

%.o: %.c
	$(FULLCC) -c $*.c -o $@
