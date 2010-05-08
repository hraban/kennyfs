CC ?= gcc
LINKER ?= $(CC)
FSLINK ?= ln -fs
NAME ?= kennyfs
CCARGS := -Wall -O0 -g -Wunused-parameter -fPIC `pkg-config fuse --cflags` -DKFS_LOG_TRACE
CCINCARGS :=
LINKARGS := `pkg-config fuse --libs`
BRICKS := $(wildcard kfs_brick_*.c)
OFILES := $(patsubst %.c,%.o,$(filter-out $(BRICKS),$(wildcard kfs_*.c)))
SOFILES := $(patsubst %.c,lib%.so,$(BRICKS))

FULLCC := $(CC) $(CCARGS) $(CCINCARGS)
FULLLINK := $(LINKER) $(LINKARGS)

.PHONY: all clean

all: $(NAME) $(SOFILES)

clean:
	rm -f $(NAME) *.o lib*.so.?.? lib*.so.? lib*.so

$(NAME): $(OFILES) $(NAME).o
	$(FULLLINK) -o $@ $(OFILES) $(NAME).o

lib%.so: %.o
	$(FULLLINK) -shared -Wl,-soname,$@.0 $< -o $@.0.0 $(OFILES)
	$(FSLINK) $@.0.0 $@
	$(FSLINK) $@.0.0 $@.0

%.o: %.c
	$(FULLCC) -c $*.c -o $@
