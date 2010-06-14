CC ?= gcc
LINKER ?= $(CC)
FSLINK ?= ln -fs
NAME ?= server
CCARGS := -Wall -O0 -g -Wunused-parameter -fPIC `pkg-config fuse --cflags` -DKFS_LOG_TRACE -I..
CCINCARGS :=
LINKARGS := `pkg-config fuse --libs`
BRICKS := $(wildcard ../kfs_brick_*.c)
OFILES := $(patsubst %.c,%.o,$(filter-out $(BRICKS),$(wildcard ../kfs_*.c))) handlers.o

FULLCC := $(CC) $(CCARGS) $(CCINCARGS)
FULLLINK := $(LINKER) $(LINKARGS)

.PHONY: all clean

all: $(NAME)

clean:
	rm -f $(NAME) *.o lib*.so.?.? lib*.so.? lib*.so

$(NAME): $(OFILES) $(NAME).o
	$(FULLLINK) -o $@ $(OFILES) $(NAME).o

%.o: %.c
	$(FULLCC) -c $*.c -o $@
