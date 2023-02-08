#! /bin/sh

set -e\

rm -rf sysroot
./jinx sysroot
for f in $(find sysroot); do file "$f" | grep 'not stripped' || true; done
