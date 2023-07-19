#!/usr/bin/env bash

: ${CC=gcc}
: ${BIN=libterminal.so}

CFLAGS="$CFLAGS -fPIC -Ilib/lite-xl/resources/include"

[[ "$@" == "clean" ]] && rm -f *.so *.dll && exit 0
$CC $CFLAGS src/*.c $@ -shared -o $BIN
