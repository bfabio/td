CFLAGS=-O3 -Wall -Werror -Wpedantic $(shell pkg-config --cflags --libs glib-2.0)

all: chatd.c
	$(CC) $(CFLAGS) chatd.c -o chatd
	strip chatd
