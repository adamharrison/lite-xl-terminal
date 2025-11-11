#!/usr/bin/env bash

: ${CC=gcc}
: ${BIN=libterminal.so}

CFLAGS="$CFLAGS -fPIC -Ilib/lite-xl/resources/include"
LDFLAGS=""

[[ "$@" == "clean" ]] && rm -f *.so *.dll && exit 0
[[ $OSTYPE != 'msys'* && $OSTYPE != 'cygwin'* && $CC != *'mingw'* ]] && LDFLAGS="$LDFLAGS -lutil"
$CC $CFLAGS src/*.c $@ -shared -o $BIN $LDFLAGS
