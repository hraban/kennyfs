CC ?= gcc
LINKER ?= $(CC)
FSLINK ?= ln -s

NAME := kfs_frontend_simple
CCARGS := -Wall -O0 -g -Wunused-parameter -fPIC `pkg-config fuse --cflags`
CCINCARGS :=
LINKARGS := `pkg-config fuse --libs`
OFILES := $(patsubst %.c,%.o,$(wildcard *.c))
SOFILES := $(patsubst %.c,lib%.so,$(wildcard kfs_backend_*.c))

FULLCC := $(CC) $(CCARGS) $(CCINCARGS)
FULLLINK := $(LINKER) $(LINKARGS)

.PHONY: all clean

all: $(NAME) $(SOFILES)

clean:
	rm -f $(NAME) *.o lib*.so.?.? lib*.so.? lib*.so

$(NAME): $(NAME).o
	$(FULLLINK) -o $@ $<

lib%.so: %.o
	$(FULLLINK) -shared -Wl,-soname,$@.0 $< -o $@.0.0
	$(FSLINK) $@.0.0 $@
	$(FSLINK) $@.0.0 $@.0

%.o: %.c
	$(FULLCC) -c $*.c -o $@
