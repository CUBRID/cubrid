
dnl
dnl BUILD_NUMBER
dnl
define([RELEASE], [patsubst(esyscmd([cat ./BUILD_NUMBER | sed -e "s|\.[0-9]*$||"]), [
])])
