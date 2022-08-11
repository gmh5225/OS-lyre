override INTERNALCFLAGS := -std=gnu11

all: init lfetch memstat

init: init.c
	$(CC) $(INTERNALCFLAGS) $< -o $@

lfetch: lfetch.c
	$(CC) $(INTERNALCFLAGS) $< -o $@

memstat: memstat.c
	$(CC) $(INTERNALCFLAGS) $< -o $@

.PHONY: install
install: all
	install -Dm0755 init $(DESTDIR)/usr/sbin/init
	install -Dm0755 lfetch $(DESTDIR)/usr/bin/lfetch
	install -Dm0755 memstat $(DESTDIR)/usr/bin/memstat
