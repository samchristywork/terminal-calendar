VERSION = $(shell grep TERMINAL_CALENDAR_VERSION version.h | cut -d'"' -f2)

PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man
