CC      := gcc
CFLAGS  := -Wall -Wextra -Iinclude
LDFLAGS := -lm

all: main test_closure test_map

main: main.o
	$(CC) -o $@ $^ $(LDFLAGS)

test_closure: src/test_closure.o
	$(CC) -o $@ $^ $(LDFLAGS)

test_map: src/test_map.o
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f main test_map test_closure *.o src/*.o *.exe
