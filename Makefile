CC := gcc

LIBS := -lncurses -lcjson
CFLAGS := -g -Wall -Wpedantic

all: build/cal

build/cal: cal.c
	mkdir -p build/
	${CC} cal.c -o $@ ${LIBS}

clean:
	rm -rf build/

.PHONY: clean
