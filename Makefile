include config.mk

CC := gcc

LIBS := -lncurses -lcjson -lm
CFLAGS := -g -Wall -Wpedantic

all: build/terminal_calendar

build/terminal_calendar: src/cal.c src/version.h build/graphics.o
	mkdir -p build/
	${CC} ${CFLAGS} src/cal.c build/graphics.o -o $@ ${LIBS}

build/graphics.o: src/graphics.c src/graphics.h
	mkdir -p build/
	${CC} ${CFLAGS} -c src/graphics.c -o $@ ${LIBS}

install: build/terminal_calendar
	@echo "Installing terminal_calendar Version" $(VERSION)
	mkdir -p $(PREFIX)/bin
	mkdir -p $(MANPREFIX)/man1
	cp build/terminal_calendar $(PREFIX)/bin
	cp terminal_calendar.1 $(MANPREFIX)/man1/terminal_calendar.1
	chmod 755 $(PREFIX)/bin/terminal_calendar
	chmod 644 $(MANPREFIX)/man1/terminal_calendar.1

clean:
	rm -rf build/

.PHONY: clean
