# KarnixOS – TempleOS-style Spine Intelligence OS
# Build from the Orion/ root:  make -f os/Makefile

CC      = gcc
CFLAGS  = -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter
LIBS    = -lncurses -lm
SRC     = os/main.c
OUT     = KarnixOS

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) $(SRC) $(LIBS) -o $(OUT)
	@echo ""
	@echo "  Build complete -> ./$(OUT)"
	@echo "  Run with:  ./$(OUT)"
	@echo ""

clean:
	rm -f $(OUT)

.PHONY: all clean
