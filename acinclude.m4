
dnl
dnl BUILD_NUMBER
dnl
define([BUILD_NUMBER_STRING], patsubst(esyscmd([cat ./BUILD_NUMBER]), [
]))dnl
define([VERSION_STRING], patsubst(BUILD_NUMBER_STRING, [^\([^.]*\).\([^.]*\).\([^.]*\).*], [\1.\2]))dnl
define([RELEASE_STRING], patsubst(BUILD_NUMBER_STRING, [^\([^.]*\).\([^.]*\).\([^.]*\).*], [\1.\2.\3]))dnl
define([MAJOR_RELEASE_STRING], patsubst(BUILD_NUMBER_STRING, [^\([^.]*\).\([^.]*\).\([^.]*\).*], [\1.\2]))dnl
define([MAJOR_VERSION], patsubst(BUILD_NUMBER_STRING, [^\([^.]*\).\([^.]*\).\([^.]*\).*], [\1]))dnl
define([MINOR_VERSION], patsubst(BUILD_NUMBER_STRING, [^\([^.]*\).\([^.]*\).\([^.]*\).*], [\2]))dnl
define([PATCH_VERSION], patsubst(BUILD_NUMBER_STRING, [^\([^.]*\).\([^.]*\).\([^.]*\).*], [\3]))dnl

