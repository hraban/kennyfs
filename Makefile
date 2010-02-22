NAME := kennyfs
CC := gcc
CCARGS := -Wall -O0 -g -Wunused-parameter
FUSEARGS := `pkg-config fuse --cflags --libs`
INCLUDES := 
LINK := 

FULLCC := $(CC) $(CCARGS) $(FUSEARGS) $(INCLUDES)

.PHONY: all clean

all: $(NAME)

clean:
	rm -f $(NAME)

$(NAME): $(NAME).c
	$(FULLCC) $< -o $@
