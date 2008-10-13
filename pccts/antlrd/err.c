/*
 * A n t l r  E r r o r  T a b l e  H e a d e r
 *
 * Generated from: antlr.g
 *
 * (c) 1989, 1990 by Terence Parr, Hank Dietz and Will Cohen
 * Purdue University Electrical Engineering
 * ANTLR Version 1.1B
 *  $Revision: 1.3 $
 */

#include <stdio.h>
#include <stdlib.h>
#include "dlgdef.h"
#define nil ((unsigned)-1)
#ifdef MPW		/* Macintosh Programmer's Workshop */
#define ErrHdr "File \"%s\"; Line %d #"
#else
#define ErrHdr "\"%s\", line %d:"
#endif

extern char *tokens[];
char *aSourceFile = "stdin";	/* Set to current src filename */

unsigned *err_decode(p)
register unsigned char *p;
{
	static unsigned bitmask[] = {
		0x00000001, 0x00000002, 0x00000004, 0x00000008,
		0x00000010, 0x00000020, 0x00000040, 0x00000080,
	};
	register unsigned char *endp = &(p[4]);
	register unsigned *q,*r,e= 0;

	r = q = (unsigned *) malloc(33*sizeof(unsigned));
	if ( q == 0 ) return( 0 );
	do {
		register unsigned t = *p;
		register unsigned *b = &(bitmask[0]);
		do {
			if ( t & *b ) *q++ = e;
			e++;
		} while (++b < &(bitmask[8]));
	} while (++p < endp);
	*q = nil;
	return( r );
}

void err_print(e)
unsigned char *e;
{
	unsigned *p, *q;

	if ( (q=p=err_decode(e))==NULL ) return;
	fprintf(stderr, "{");
	while ( *p != nil )
	{
		fprintf(stderr, " %s", tokens[*p++]);
	}
	fprintf(stderr, " }");
	free(q);
}

void dfltErr(e)
unsigned char *e;
{
	fprintf(stderr, ErrHdr, aSourceFile, lex_line);
	fprintf(stderr, " missing ");
	err_print(e);
	fprintf(stderr, " (Was \"%s\"(%s))\n",
		(tokens[Token][0]=='@')?"eof":LexText, tokens[Token]);
}

void dfltE2(tok)
unsigned tok;
{
	fprintf(stderr, ErrHdr, aSourceFile, lex_line);
	fprintf(stderr, " missing %s (Was \"%s\"(%s))\n",
					tokens[(int)tok],
					(tokens[Token][0]=='@')?"eof":LexText,
					tokens[Token]);
}

char *tokens[31]={
	/* 00 */	"Invalid",
	/* 01 */	"[\t ]+",
	/* 02 */	"[\n\r]",
	/* 03 */	"/*",
	/* 04 */	"*/",
	/* 05 */	">>",
	/* 06 */	"Action",
	/* 07 */	"#header",
	/* 08 */	"Eof",
	/* 09 */	"NonTerminal",
	/* 10 */	"!",
	/* 11 */	"<",
	/* 12 */	"PassAction",
	/* 13 */	">",
	/* 14 */	"QuotedTerm",
	/* 15 */	":",
	/* 16 */	";",
	/* 17 */	"#lexaction",
	/* 18 */	"#lexclass",
	/* 19 */	"TokenTerm",
	/* 20 */	"#errclass",
	/* 21 */	"{",
	/* 22 */	"}",
	/* 23 */	"#token",
	/* 24 */	"|",
	/* 25 */	"^",
	/* 26 */	"(",
	/* 27 */	")",
	/* 28 */	"*",
	/* 29 */	"+",
	/* 30 */	"#[A-Za-z0-9_]*"
};
unsigned char err[][4]={
{ 0xc0, 0x02, 0x96, 0x00 },
{ 0x40, 0x02, 0x96, 0x00 },
{ 0x40, 0x03, 0x96, 0x00 },
{ 0x40, 0x01, 0x12, 0x00 },
{ 0x00, 0xfc, 0x00, 0x00 },
{ 0x00, 0x18, 0x00, 0x00 },
{ 0x00, 0xf8, 0x00, 0x00 },
{ 0x00, 0xe0, 0x00, 0x00 },
{ 0x00, 0xc0, 0x00, 0x00 },
{ 0x40, 0x03, 0x96, 0x00 },
{ 0x00, 0x40, 0x08, 0x00 },
{ 0x00, 0x42, 0x08, 0x00 },
{ 0x00, 0x42, 0x08, 0x00 },
{ 0x00, 0x42, 0x48, 0x00 },
{ 0x40, 0x43, 0x9e, 0x00 },
{ 0x40, 0x43, 0x96, 0x00 },
{ 0x40, 0x03, 0x96, 0x00 },
{ 0x00, 0x00, 0x41, 0x09 },
{ 0x40, 0xf2, 0x69, 0x3d },
{ 0x40, 0xf6, 0x69, 0x3f },
{ 0x40, 0xf6, 0x69, 0x3f },
{ 0x40, 0xfe, 0x69, 0x3d },
{ 0x00, 0x18, 0x00, 0x00 },
{ 0x40, 0xfa, 0x69, 0x3d },
{ 0x40, 0xf2, 0x69, 0x3d },
{ 0x40, 0xf2, 0x69, 0x3d },
{ 0x40, 0xf2, 0x69, 0x3d },
{ 0x40, 0xf2, 0x69, 0x3d },
{ 0x40, 0xf2, 0x28, 0x34 },
{ 0x00 }
};
