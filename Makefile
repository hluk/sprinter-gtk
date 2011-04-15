# utils
CC = /usr/bin/gcc
PKG_CONFIG = /usr/bin/pkg-config
CONVERT = /usr/bin/convert
NOTIFY = /usr/bin/inotifywait
GDK_PIXBUF_CSOURCE = gdk-pixbuf-csource
CAT = /bin/cat
CP = /bin/cp
RM = /bin/rm -f

PKGS = gtk+-2.0 gdk-2.0
#CFLAGS = -Wall -O0 -ggdb `$(PKG_CONFIG) --cflags $(PKGS)`
#CFLAGS = -pedantic -std=c99 -Wall -Os -march=native -fomit-frame-pointer `$(PKG_CONFIG) --cflags $(PKGS)`
CFLAGS = -Wall -Os -march=native -fomit-frame-pointer `$(PKG_CONFIG) --cflags $(PKGS)`
LFLAGS = `$(PKG_CONFIG) --libs $(PKGS)`

.PHONY:all watch clean
all: sprinter

sprinter: main.c sprinter_icon.h
	$(CC) $(CFLAGS) $(LFLAGS) -o $@ $^

# TODO: char array instead cstring for pixbuf
sprinter_icon.h: sprinter.png sprinter_icon.h.head
	$(CP) $@{.head,}
	$(GDK_PIXBUF_CSOURCE) --raw --name=sprinter_icon $< >> $@

sprinter.png: sprinter.svg
	$(CONVERT) -background none $^ -filter Point -resize 64 -quality 100 $@

watch:
	while $(NOTIFY) main.c; do make; done

clean:
	$(RM) sprinter.png sprinter_icon.h *.o sprinter

