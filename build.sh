#!/usr/bin/env bash

: ${CC=gcc}
: ${BIN=libterminal.so}

CFLAGS="$CFLAGS -fPIC -Ilib/lite-xl/resources/include"

$CC $CFLAGS src/*.c $@ -shared -o $BIN
