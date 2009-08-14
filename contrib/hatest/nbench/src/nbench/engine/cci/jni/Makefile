
INCLUDE = -I/usr/local/java/include -I/usr/local/java/include/linux
CFLAGS = -m32 -c -g -fPIC -Wall -D_REENTRANT

all: libnbenchcci.so

clean:
	-rm -rf *.so *.o libnbenchcci.so

libnbenchcci.so : CCIDriver.o
	gcc -m32 -shared -Wl,-soname,libnbenchcci.so -o libnbenchcci.so CCIDriver.o -lc -lpthread

CCIDriver.o : CCIDriver.c nbench_engine_cci_CCIDriver.h
	gcc $(CFLAGS) CCIDriver.c $(INCLUDE)
