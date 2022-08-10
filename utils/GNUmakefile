override INTERNALCFLAGS := -std=gnu11

all: init memstat

init: init.c
	$(CC) $(INTERNALCFLAGS) $< -o $@

memstat: memstat.c
	$(CC) $(INTERNALCFLAGS) $< -o $@

.PHONY: install
install: all
	install -Dm0755 init $(DESTDIR)/usr/sbin/init
	install -Dm0755 memstat $(DESTDIR)/usr/bin/memstat
