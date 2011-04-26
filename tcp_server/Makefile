CC ?= gcc
LINKER ?= $(CC)
FSLINK ?= ln -fs
NAME ?= server
CFLAGS += -I..
CCINCARGS := -rdynamic
LINKARGS := `pkg-config fuse --libs` -rdynamic
DEPENDENCIES :=  $(wildcard ../kfs_*.o) ../minini/minini.o
OFILES := $(patsubst %.c,%.o,$(wildcard *.c))

FULLCC := $(CC) $(CFLAGS) $(CCINCARGS)
FULLLINK := $(LINKER) $(LINKARGS)

.PHONY: all clean

all: $(NAME)

clean:
	rm -f $(NAME) $(OFILES)

$(NAME): $(OFILES)
	$(FULLLINK) $(DEPENDENCIES) $(OFILES) -o $@

%.o: %.c %.h
	$(FULLCC) -c $< -o $@
