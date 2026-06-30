CC      := gcc
CFLAGS  := -Wall -Wextra -Wno-unused-function -Iinclude
LDFLAGS := -lm -lrt

all: main

main: main.c src/runtime.c src/native.c src/native_System.c src/native_Math.c src/native_String.c src/process.c src/continuation.c src/closure.c src/compiler.c src/linker.c src/parser.c src/opcode.c src/ast.c src/wstring.c src/list.c src/vocab.c src/heap.c src/scope.c src/map.c src/object.c src/utils.c src/lexer.c src/highlight.c src/debug.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f main *.exe
