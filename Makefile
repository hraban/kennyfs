CC ?= gcc
LINKER ?= $(CC)
FSLINK ?= ln -s
NAME := kennyfs
CCARGS := -Wall -O0 -g -Wunused-parameter -fPIC `pkg-config fuse --cflags`
CCINCARGS :=
LINKARGS := `pkg-config fuse --libs`
BRICKS := $(wildcard kfs_brick_*.c)
OFILES := $(patsubst %.c,%.o,$(filter-out $(BRICKS),$(wildcard *.c)))
SOFILES := $(patsubst %.c,lib%.so,$(BRICKS))

FULLCC := $(CC) $(CCARGS) $(CCINCARGS)
FULLLINK := $(LINKER) $(LINKARGS)

.PHONY: all clean

all: $(NAME) $(SOFILES)

clean:
	rm -f $(NAME) *.o lib*.so.?.? lib*.so.? lib*.so

$(NAME): $(OFILES)
	$(FULLLINK) -o $@ $(OFILES)

lib%.so: %.o
	$(FULLLINK) -shared -Wl,-soname,$@.0 $< -o $@.0.0
	$(FSLINK) $@.0.0 $@
	$(FSLINK) $@.0.0 $@.0

%.o: %.c
	$(FULLCC) -c $*.c -o $@
