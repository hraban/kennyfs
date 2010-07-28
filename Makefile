CC ?= gcc
LINKER ?= $(CC)
FSLINK ?= ln -fs
NAME ?= kennyfs
export CFLAGS += -Wall -g -Wunused-parameter `pkg-config fuse --cflags`
CCINCARGS :=
LINKARGS := `pkg-config fuse --libs` -pthread
BRICKS := $(patsubst %_brick/,%,$(wildcard *_brick/))
CLEANBRICKS := $(patsubst %,%_clean,$(BRICKS))
OFILES := $(patsubst %.c,%.o,$(wildcard kfs_*.c)) minIni/minIni.o

FULLCC := $(CC) $(CFLAGS) $(CCINCARGS)
FULLLINK := $(LINKER) $(LINKARGS)

.PHONY: all clean $(BRICKS) $(CLEANBRICKS)

all: $(NAME) $(BRICKS) 
	$(MAKE) -C tcp_server

clean: $(CLEANBRICKS)
	$(MAKE) -C tcp_server clean
	rm -f $(NAME) $(OFILES) *.o lib*.so*

$(NAME): $(OFILES) $(NAME).o
	$(FULLLINK) -o $@ $(OFILES) $(NAME).o

$(CLEANBRICKS): %_clean:
	$(MAKE) -C $*_brick/ clean

$(BRICKS): %:
	$(MAKE) -C $@_brick/

%.o: %.c
	$(FULLCC) -c $*.c -o $@
