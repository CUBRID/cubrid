aclocal -I . | exit $?
libtoolize --force -c | exit $?
automake --add-missing -c || exit $?
autoconf || exit $?
