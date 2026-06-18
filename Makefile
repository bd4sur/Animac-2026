CC      := gcc
CFLAGS  := -Wall -Wextra -Iinclude
LDFLAGS := -lm

all: main test_closure test_map test_ast test_parser

main: main.c src/lexer.c src/highlight.c src/utils.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_closure: test/test_closure.c src/closure.c src/object.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_map: test/test_map.c src/map.c src/object.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_ast: test/test_ast.c src/ast.c src/list.c src/vocab.c src/heap.c src/scope.c src/map.c src/object.c src/utils.c src/lexer.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_parser: test/test_parser.c src/parser.c src/ast.c src/list.c src/vocab.c src/heap.c src/scope.c src/map.c src/object.c src/utils.c src/lexer.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f main test_map test_closure test_ast test_parser *.exe
