#CFLAGS = -Wall -pedantic -O0 -ggdb
CFLAGS = -Wall -O0 -ggdb

all: sprinter

sprinter: main.c sprinter_icon.h
	gcc $(CFLAGS) `pkg-config --cflags --libs gtk+-2.0 gdk-2.0` -o $@ $^

sprinter_icon.h: sprinter.png
	( \
		echo "/**"; \
		echo " * \file $@"; \
		echo " * Program's icon generated from resource file."; \
		echo " * After compiling the program's icon is included in executable. This file is generated using \c gdk-pixbuf-csource."; \
		echo " */"; \
		echo "#include <glib.h>"; \
		gdk-pixbuf-csource --raw --name=sprinter_icon $^; \
	) > $@

sprinter.png: sprinter.svg
	convert -background none $^ -filter Point -resize 64 -quality 100 $@

watch:
	while inotifywait main.c; do make; done

clean:
	rm -f sprinter.png sprinter_icon.h *.o sprinter

