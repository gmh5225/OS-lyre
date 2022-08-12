CFLAGS ?= -g -O2 -pipe -Wall -Wextra

override CFLAGS += -std=gnu11

all: init lfetch memstat

init: init.c
	$(CC) $(CFLAGS) $< -o $@

lfetch: lfetch.c
	$(CC) $(CFLAGS) $< -o $@

memstat: memstat.c
	$(CC) $(CFLAGS) $< -o $@

.PHONY: install
install: all
	install -Dm0755 init $(DESTDIR)/usr/sbin/init
	install -Dm0755 lfetch $(DESTDIR)/usr/bin/lfetch
	install -Dm0755 memstat $(DESTDIR)/usr/bin/memstat
