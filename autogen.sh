#!/bin/sh
set -e

srcdir=`dirname $0`
test -n "$srcdir" && cd "$srcdir"

echo "Updating build configuration files, please wait...."

test -z "$AUTOMAKE" && AUTOMAKE="automake"

AUTOMAKE="$AUTOMAKE --foreign" autoreconf -fi
