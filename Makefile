include config.mk

CC := gcc

LIBS := -lncursesw -lcjson -lm -lz
CFLAGS := -g -Wall -Wpedantic

all: build/terminal_calendar

build/terminal_calendar: src/cal.c src/version.h build/graphics.o build/util.o
	mkdir -p build/
	${CC} ${CFLAGS} src/cal.c build/graphics.o build/util.o -o $@ ${LIBS}

build/graphics.o: src/graphics.c src/graphics.h
	mkdir -p build/
	${CC} ${CFLAGS} -c src/graphics.c -o $@ ${LIBS}

build/util.o: src/util.*
	mkdir -p build/
	${CC} ${CFLAGS} -c src/util.c -o $@ ${LIBS}

install: build/terminal_calendar
	mkdir -p $(PREFIX)/bin
	mkdir -p $(MANPREFIX)/man1
	cp build/terminal_calendar $(PREFIX)/bin
	cp terminal_calendar.1 $(MANPREFIX)/man1/terminal_calendar.1
	chmod 755 $(PREFIX)/bin/terminal_calendar
	chmod 644 $(MANPREFIX)/man1/terminal_calendar.1

clean:
	rm -rf build/

.PHONY: clean
