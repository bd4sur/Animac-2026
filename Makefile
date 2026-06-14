CC      := gcc
CFLAGS  := -Wall -Wextra -Iinclude
LDFLAGS := -lm

all: main test_closure test_map

main: main.o src/lexer.o src/highlight.o src/utils.o
	$(CC) -o $@ $^ $(LDFLAGS)

test_closure: test/test_closure.o src/closure.o src/object.o
	$(CC) -o $@ $^ $(LDFLAGS)

test_map: test/test_map.o src/map.o src/object.o
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f main test_map test_closure *.o src/*.o test/*.o *.exe
