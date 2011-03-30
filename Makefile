CFLAGS = -Wall -pedantic -O0 -ggdb

all: sprinter

sprinter: main.c
	gcc $(CFLAGS) `pkg-config --cflags --libs gtk+-2.0` -o $@ $^

watch:
	while inotifywait main.c; do make; done

clean:
	rm -f *.o sprinter

