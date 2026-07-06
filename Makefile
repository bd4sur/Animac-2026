CC      := gcc
CFLAGS  := -O3 -Wall -Wextra -Wno-unused-function -Iinclude
LDFLAGS := -lm -lrt

SRCS    := src/allocator.c src/runtime.c src/native.c src/native_System.c src/native_Math.c src/native_String.c src/native_LLM.c src/native_Table.c src/process.c src/continuation.c src/closure.c src/compiler.c src/linker.c src/parser.c src/opcode.c src/ast.c src/wstring.c src/list.c src/vocab.c src/heap.c src/scope.c src/map.c src/object.c src/utils.c src/lexer.c src/highlight.c src/debug.c src/module.c src/macro.c

all: main repl

main: main.c $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

repl: repl.c $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f main repl *.exe
