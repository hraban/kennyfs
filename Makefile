NAME := kennyfs
CC := gcc
CCARGS := -Wall -O0 -g -DNDEBUG
FUSEARGS := `pkg-config fuse --cflags --libs`

FULLCC := $(CC) $(CCARGS) $(FUSEARGS)

.PHONY: all clean

all: $(NAME)

clean:
	rm -f $(NAME)

$(NAME): $(NAME).c
	$(FULLCC) $< -o $@
