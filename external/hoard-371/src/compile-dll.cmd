@echo off
echo Building libhoard.dll and libhoard.lib (ignore linker warnings).
: We need to execute this if we change libhoard.cpp.
@echo off
cl /Iheaplayers /Iheaplayers/util /c /Zi /DNDEBUG /MD /Ox /Zp8 /Oy libhoard.cpp
nm -g libhoard.obj > @@@.@@@ 2>NUL
grep ' T ?' @@@.@@@ > @@@.@@1 2>NUL
grep ' T \_' @@@.@@@ > @@@.@@2 2>NUL
echo EXPORTS > libhoard.def
sed 's/.* T //' @@@.@@1 | grep -v DllMain >> libhoard.def 2>NUL
sed 's/.* T \_//' @@@.@@2 | grep -v DllMain >> libhoard.def 2>NUL
erase @@@.@@@
erase @@@.@@1
erase @@@.@@2
cl /Iheaplayers /Iheaplayers/util /c /MD /DNDEBUG /Ox /Zp8 /Oy libhoard.cpp
cl /Zi /LD libhoard.obj /o libhoard.dll /Ox /link /def:libhoard.def /force:multiple /subsystem:console /entry:_DllMainCRTStartup@12
 
