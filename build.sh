#!/usr/bin/env bash

: ${CC=gcc}
: ${AR=ar}
: ${MAKE=make}
: ${BIN=terminal_native}
: ${JOBS=4}

CFLAGS="$CFLAGS -fPIC -Ilib/lite-xl/resources/include"

$CC $CFLAGS src/*.c $@ -shared -o libterminal.so
