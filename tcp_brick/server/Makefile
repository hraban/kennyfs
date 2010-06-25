CC ?= gcc
LINKER ?= $(CC)
FSLINK ?= ln -fs
NAME ?= server
CCARGS := -Wall -O0 -g -Wunused-parameter -fPIC `pkg-config fuse --cflags` -DKFS_LOG_TRACE -I.. -I../..
CCINCARGS :=
LINKARGS := `pkg-config fuse --libs` $(wildcard ../../kfs_*.o)
OFILES := server.o handlers.o

FULLCC := $(CC) $(CCARGS) $(CCINCARGS)
FULLLINK := $(LINKER) $(LINKARGS)

.PHONY: all clean

all: $(NAME)

clean:
	rm -f $(NAME) *.o lib*.so.?.? lib*.so.? lib*.so

$(NAME): $(OFILES)
	$(FULLLINK) -o $@ $(OFILES)

%.o: %.c
	$(FULLCC) -c $*.c -o $@
