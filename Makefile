CC:=gcc
AS:=as

INCDIR:=include
BIN:=libopenrtl.so

SRC:=lib.c regalloc.c
OBJ:=lib.o regalloc.o
INC:=$(INCDIR)/openrtl.h

CFLAGS:=-g -ggdb -Wall -Wextra -pedantic -std=c11 -D_GNU_SOURCE=1 -fPIC
LDFLAGS:=
ASFLAGS:=

.PHONY: all build clean mrproper

all: $(BIN)

build: $(BIN)

$(BIN): $(OBJ) $(INC)
	$(CC) -shared -o $(BIN) $(OBJ) $(LDFLAGS)

$(OBJ): %.o: %.c $(INC)
	$(CC) -c -o $@ $< $(CFLAGS)

openasm/libopenrtli.so:
	make -C openasm

clean:
	rm -rf $(OBJ)

mrproper: clean
	rm -rf $(BIN)
