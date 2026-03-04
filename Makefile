NAME := allocator
BUILD := build
SRC := src
LIB := $(SRC)/lib

CFLAGS := -Wall -g -I

MAIN_SRC   := $(SRC)/main.c
ALLOC_SRC  := $(LIB)/allocator.c
ALLOC_HDR  := $(LIB)/allocator.h
TEST_SRC   := $(SRC)/test.c

all: clean build 

build:
	mkdir -p $(BUILD)
	gcc $(CFLAGS) $(LIB) $(MAIN_SRC) $(ALLOC_SRC) -o $(BUILD)/$(NAME)

clean:
	rm -rf $(BUILD)

run:
	./$(BUILD)/$(NAME)

test: target run