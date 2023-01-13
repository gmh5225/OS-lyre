CFLAGS ?= -g -O2 -pipe -Wall -Wextra

override CFLAGS += -std=gnu11

all: init lfetch socket-test

init: init.c
	$(CC) $(CFLAGS) $< -o $@

lfetch: lfetch.c
	$(CC) $(CFLAGS) $< -o $@

socket-test: socket-test.c
	$(CC) $(CFLAGS) $< -o $@

.PHONY: install
install: all
	install --strip-program=$(STRIP) -s -Dm0755 init $(DESTDIR)/usr/bin/init
	install --strip-program=$(STRIP) -s -Dm0755 lfetch $(DESTDIR)/usr/bin/lfetch
	install --strip-program=$(STRIP) -s -Dm0755 socket-test $(DESTDIR)/usr/bin/socket-test
