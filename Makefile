CC      := gcc
CFLAGS  := -Wall -Wextra -Iinclude
LDFLAGS := -lm

all: main test_closure test_map test_ast test_parser

main: main.o src/lexer.o src/highlight.o src/utils.o
	$(CC) -o $@ $^ $(LDFLAGS)

test_closure: test/test_closure.o src/closure.o src/object.o
	$(CC) -o $@ $^ $(LDFLAGS)

test_map: test/test_map.o src/map.o src/object.o
	$(CC) -o $@ $^ $(LDFLAGS)

test_ast: test/test_ast.o src/ast.o src/list.o src/vocab.o src/heap.o src/scope.o src/map.o src/object.o src/utils.o src/lexer.o
	$(CC) -o $@ $^ $(LDFLAGS)

test_parser: test/test_parser.o src/parser.o src/ast.o src/list.o src/vocab.o src/heap.o src/scope.o src/map.o src/object.o src/utils.o src/lexer.o
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f main test_map test_closure test_ast test_parser *.o src/*.o test/*.o *.exe
