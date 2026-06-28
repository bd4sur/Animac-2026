CC      := gcc
CFLAGS  := -Wall -Wextra -Wno-unused-function -Iinclude
LDFLAGS := -lm

all: main test_closure test_map test_ast test_parser test_linker test_wstring test_list test_vocab test_compiler test_continuation test_process test_runtime test_runtime_new_opcodes test_size

main: main.c src/lexer.c src/highlight.c src/utils.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_closure: test/test_closure.c src/closure.c src/object.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_map: test/test_map.c src/map.c src/object.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_ast: test/test_ast.c src/ast.c src/wstring.c src/list.c src/vocab.c src/heap.c src/scope.c src/map.c src/object.c src/utils.c src/lexer.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_parser: test/test_parser.c src/parser.c src/opcode.c src/ast.c src/wstring.c src/list.c src/vocab.c src/heap.c src/scope.c src/map.c src/object.c src/utils.c src/lexer.c src/highlight.c src/debug.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_linker: test/test_linker.c src/linker.c src/parser.c src/opcode.c src/ast.c src/wstring.c src/list.c src/vocab.c src/heap.c src/scope.c src/map.c src/object.c src/utils.c src/lexer.c src/highlight.c src/debug.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_wstring: test/test_wstring.c src/wstring.c src/object.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_list: test/test_list.c src/list.c src/object.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_vocab: test/test_vocab.c src/vocab.c src/object.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_compiler: test/test_compiler.c src/compiler.c src/linker.c src/parser.c src/opcode.c src/ast.c src/wstring.c src/list.c src/vocab.c src/heap.c src/scope.c src/map.c src/object.c src/utils.c src/lexer.c src/highlight.c src/debug.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_continuation: test/test_continuation.c src/continuation.c src/object.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_process: test/test_process.c src/process.c src/continuation.c src/closure.c src/compiler.c src/linker.c src/parser.c src/opcode.c src/ast.c src/wstring.c src/list.c src/vocab.c src/heap.c src/scope.c src/map.c src/object.c src/utils.c src/lexer.c src/highlight.c src/debug.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_runtime: test/test_runtime.c src/runtime.c src/native.c src/process.c src/continuation.c src/closure.c src/compiler.c src/linker.c src/parser.c src/opcode.c src/ast.c src/wstring.c src/list.c src/vocab.c src/heap.c src/scope.c src/map.c src/object.c src/utils.c src/lexer.c src/highlight.c src/debug.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_runtime_new_opcodes: test/test_runtime_new_opcodes.c src/runtime.c src/native.c src/process.c src/continuation.c src/closure.c src/compiler.c src/linker.c src/parser.c src/opcode.c src/ast.c src/wstring.c src/list.c src/vocab.c src/heap.c src/scope.c src/map.c src/object.c src/utils.c src/lexer.c src/highlight.c src/debug.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_size: test/test_size.c src/closure.c src/continuation.c src/list.c src/map.c src/wstring.c src/object.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f main test_map test_closure test_ast test_parser test_linker test_wstring test_list test_vocab test_compiler test_continuation test_process test_runtime test_runtime_new_opcodes test_size *.exe
