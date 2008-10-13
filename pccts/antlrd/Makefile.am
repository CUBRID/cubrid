
SET=$(top_srcdir)/pccts/support/set

INCLUDES=-I. -I$(SET)
DEFS=-DUNIX
AM_CFLAGS=-m32 -O2

noinst_PROGRAMS=antlr
antlr_SOURCES=antlr.c scan.c err.c bits.c build.c fset2.c fset.c gen.c globals.c hash.c lex.c main.c misc.c $(SET)/set.c
