#!/bin/bash
set -x
libtoolize --automake --copy
aclocal -I config
autoheader
autoconf
automake --add-missing --copy
#./configure --prefix=$HOME/xdbms
