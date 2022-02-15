#!/bin/sh
set -e

srcdir=`dirname $0`
test -n "$srcdir" && cd "$srcdir"

echo "Updating build configuration files, please wait...."

autoreconf -fi
