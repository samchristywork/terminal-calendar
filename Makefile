include config.mk

CC := gcc

LIBS := -lncurses -lcjson
CFLAGS := -g -Wall -Wpedantic

all: build/terminal_calendar

build/terminal_calendar: cal.c
	mkdir -p build/
	${CC} $^ -o $@ ${LIBS}

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
