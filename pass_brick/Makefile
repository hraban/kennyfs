CC ?= gcc
LINKER ?= $(CC)
FSLINK ?= ln -fs
CFLAGS += -fPIC -I..
CCINCARGS :=
LINKARGS := `pkg-config fuse --libs` $(wildcard ../kfs_*.o)
BRICKS := $(wildcard kfs_brick_*.c)
OFILES := $(patsubst %.c,%.o,$(filter-out $(BRICKS),$(wildcard *.c)))
SOFILES := $(patsubst %.c,lib%.so,$(BRICKS))

FULLCC := $(CC) $(CFLAGS) $(CCINCARGS)
FULLLINK := $(LINKER) $(LINKARGS)

.PHONY: all clean

all: $(SOFILES)

clean:
	rm -f *.o lib*.so*

lib%.so: %.o $(OFILES)
	$(FULLLINK) -shared -Wl,-soname,$@.0 $< -o $@.0.0 $(OFILES)
	$(FSLINK) $@.0.0 $@.0
	$(FSLINK) $@.0.0 $@

%.o: %.c
	$(FULLCC) -c $*.c -o $@
