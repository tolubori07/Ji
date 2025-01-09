ji: ji.c
	zig cc ji.c -o ji.o -Wall -Wextra -pedantic -std=c99

clean:
	rm -rf ./ji.o
