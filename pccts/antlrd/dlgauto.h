/*
 * $Revision: 1.3 $
*/

#ifndef _DLGAUTO_H_
#define _DLGAUTO_H_

static int nextpos;

int cur_char;
static int cur_class;
static char ebuf[70];

#define _GETC \
	while ((cur_class = (((cur_char = getc(inputStream)) == EOF) ? \
	       _SHIFT(LEX_EOF) : _SHIFT(cur_char))) == NumAtoms) { \
		sprintf(ebuf,"illegal character ('%c') ignored", cur_char); \
		lex_err( ebuf ); \
	}

/*
 * Same as _GETC, but don't do illegal char check and return the character
 */
int nextChar() 
{
	(cur_class = (((cur_char = getc(inputStream)) == EOF) ?
	 _SHIFT(LEX_EOF) : _SHIFT(cur_char)));
	return cur_char;
}

void SetLexInputStream( f )
FILE *f;
{
	inputStream = f;
	lex_line = 1;
}

void CloseLexInputStream()
{
	fclose( inputStream );
}

void LexSkip()
{
	lex_mode = 1;
}

void LexMore()
{
	lex_mode = 2;
}

int GetToken()
{
	register int state, newstate;
	int temp;

lex_skip:
	nextpos = 0;
lex_more:
	state = 0;
	while ((newstate = dfa[state][_SHIFT(cur_char)]) != DfaStates) {
		state = newstate;
		/* Truncate matching buffer to size (not an error) */
		if (nextpos < LEX_BUF-1) LexText[nextpos++] = cur_char;
		_GETC;
	}
	if ( state != 0 )
	{
		LexText[nextpos] = '\0';
	}
	else
	{
		LexText[nextpos] = cur_char;
		LexText[nextpos+1] = '\0';
	}

	lex_mode = 0;
	temp = (*actions[accepts[state]])();
	switch (lex_mode) {
		case 1: goto lex_skip;
		case 2: goto lex_more;
	}
	return(temp);
}

void advance()
{
	_GETC;
}



void lex_err(s)
char *s;
{
        fprintf(stderr,
                "%s near line %d (Token was '%s')\n",
                ((s == NULL) ? "Lexical error" : s),
                lex_line,
				LexText);
}
#endif

