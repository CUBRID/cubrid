BAG=../../bin/bag
SRC=test.c rexpr.c
OBJ=test.o rexpr.o
CFLAGS = 

test: $(OBJ) $(SRC)
	cc -o test $(OBJ)

shar:
	shar makefile test.c rexpr.c > rexpr.shar

archive:
	$(BAG) makefile test.c rexpr.c > rexpr.bag
