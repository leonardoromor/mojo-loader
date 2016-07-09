
CC=gcc

all: loader

clean:
	rm -f mojo-loader

loader: mojo-loader

% : %.c
	$(CC) -Wall $^ -o $@
