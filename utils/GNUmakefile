override INTERNALCFLAGS := -std=gnu11

all: init

init: init.c
	$(CC) $(INTERNALCFLAGS) $< -o $@

.PHONY: install
install: all
	install -Dm0755 init $(DESTDIR)/usr/sbin/init
