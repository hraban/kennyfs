CC ?= gcc
LINKER ?= $(CC)
FSLINK ?= ln -fs
NAME ?= kennyfs
export CFLAGS += -Wall -g -Wunused-parameter `pkg-config fuse --cflags`
ifdef KFS_O
export CFLAGS += -O$(KFS_O)
else
export CFLAGS += -O3
endif
CCINCARGS := -rdynamic
LINKARGS := `pkg-config fuse --libs` -rdynamic
BRICKS := $(patsubst %_brick/,%,$(wildcard *_brick/))
CLEANBRICKS := $(patsubst %,%_clean,$(BRICKS))
OFILES := $(patsubst %.c,%.o,$(wildcard kfs_*.c)) minini/minini.o

FULLCC := $(CC) $(CFLAGS) $(CCINCARGS)
FULLLINK := $(LINKER) $(LINKARGS)

.PHONY: all clean test $(BRICKS) $(CLEANBRICKS)

all: $(NAME) $(BRICKS) 
	$(MAKE) -C tcp_server

clean: $(CLEANBRICKS)
	$(MAKE) -C tcp_server clean
	rm -f $(NAME) $(OFILES) *.o lib*.so*

test:
	$(MAKE) -C test

$(NAME): $(OFILES) $(NAME).o
	$(FULLLINK) -o $@ $(OFILES) $(NAME).o

$(CLEANBRICKS): %_clean:
	$(MAKE) -C $*_brick/ clean

$(BRICKS): %:
	$(MAKE) -C $@_brick/

%.o: %.c
	$(FULLCC) -c $*.c -o $@
