/* 
 * $Revision: 1.3 $
*/

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "set.h"
#include "syn.h"
#include "hash.h"
#include "generic.h"
#include "dlgdef.h"
#include "attrib.h"
#include "tokens.h"

#define LEX_BUF 4000
#define LEX_EOF -1

#define OvfChk(s,n)												\
			{if ( ((s)+n) >= &(LexText[LEX_BUF-3]) ) {			\
			fatalFL( eMsgd("action/parameter buffer overflow; size %d",LEX_BUF),\
					 FileStr[CurFile], lex_line);}}
#define AddChar(s,c)	addc(&(s), (c))
#define AddStr(s,b,n)	adds(&(s), (b), (n))

static void
next()
{
	if ( cur_char == '\n' || cur_char == '\r' ) lex_line++;
	nextChar();
}

static void
addc(s,c)
char **s;
char c;
{
	OvfChk(*s,1);
	**s = c;
	(*s)++;
}

static void
adds(s,b,n)
char **s, *b;
int n;
{
	OvfChk(*s,n);
	strcpy(*s, b);
	(*s) += n;
}

/* Scarf everything in [...] or (...) etc... */
char *
scarfPAction(s, begin, end)
char *s;
char begin, end;
{
#ifdef __STDC__
	char *scarfAttr(char *), *scarfASTvar(char *);
#else
	char *scarfAttr(), *scarfASTvar();
#endif
	int level = 1;
	
	while ( cur_char != LEX_EOF )
	{
		if ( cur_char == '>' )
		{
			next;
			if ( cur_char == '>' )
			{
				warn("unexpected end-of-action");
				return s;
			}
			AddChar(s, '>');
			AddChar(s, cur_char);
		}
		if ( cur_char == '"' )	/* scarf "..." ignoring \" */
		{
			AddChar(s,'"');
			next();
			while ( cur_char != '"' )
			{
				if ( cur_char == '\n' )
				{
					warn("eoln found in string (in parameter action)");
					return s;
				}
				if ( cur_char == LEX_EOF )
				{
					warn("unexpected EOF in action");
					return s;
				}
				if ( cur_char == '\\' )
				{
					AddChar(s,cur_char);
					next();
				}
				AddChar(s,cur_char);
				next();
			}
			AddChar(s,'"');
			next();
		}
		if ( cur_char == '#' ) {	/* expression can have # var */
			next();
			s = scarfASTvar(s);
		}
		if ( cur_char == '$' ) {	/* expression can have $ var */
			next();
			s = scarfAttr(s);
		}
		else if ( cur_char == begin ) { 	/* Nest a level of begin */
			AddChar(s,cur_char);
			next();
			level++;
		}
		else if ( cur_char == end ) {
			--level;				/* Back down a level */
			if ( level == 0 )		/* Back to level 0? */
			{
				next();
				*s = '\0';
				return s;
			}
			AddChar(s,cur_char);
			next();
		}
		else if ( cur_char == '\\' ) {
			next();
			switch ( cur_char )
			{
				case '>'    :   AddChar(s,'>'); break;
				case '$'    :   AddChar(s,'$'); break;
				default     :   AddChar(s,'\\'); AddChar(s,cur_char); break;
			}
			next();
		}
		else {
			AddChar(s,cur_char);
			next();
		}
	}
	*s = '\0';

	return s;
}

void
scarfComment()
{
	while ( cur_char != LEX_EOF )
	{
		if ( cur_char == '*' )
		{
			next();
			if ( cur_char == '/' )
			{
				next();
				return;
			}
			continue;
		}
		next();
	}
}

void
scarfAction()
{
	char *s;
	char *scarfAttr(), *scarfASTvar();

	s = LexText;
	while ( cur_char != LEX_EOF )
	{
		if ( cur_char == '"' )	/* scarf "..." ignoring \" */
		{
			AddChar(s,'"');
			next();
			while ( cur_char != '"' )
			{
				if ( cur_char == '\n' )
				{
					warn("eoln found in string (in user action)");
					return;
				}
				if ( cur_char == LEX_EOF )
				{
					warn("unexpected EOF in user action");
					return;
				}
				if ( cur_char == '\\' )
				{
					AddChar(s,cur_char);
					next();
				}
				AddChar(s,cur_char);
				next();
			}
			AddChar(s,'"');
			next();
		}
		if ( cur_char == '$' )
		{
			next();
			s = scarfAttr(s);
			continue;
		}
		if ( cur_char == '#' )
		{
			next();
			s = scarfASTvar(s);
			continue;
		}
		if ( cur_char == '\\' )
		{
			next();
			switch ( cur_char )
			{
				case '>'    :	AddChar(s,cur_char); break;
				case '$'    :	AddChar(s,cur_char); break;
				default     :	AddChar(s,'\\'); AddChar(s,cur_char); break;
			}
			next();
		}
		else
		if ( cur_char == '>' )
		{
			next();
			if ( cur_char == '>' )
			{
				*s = '\0';
				next();
				return;
			}
			AddChar(s,'>');
			AddChar(s,cur_char);
			next();
		}
		else
		{
			AddChar(s,cur_char);
			next();
		}
	}
}

void
scarfQuotedTerm()
{
	char *s;

	s = LexText+2;
	/*if ( cur_char == '"' && LexText[1] == '\\' )*/
	if ( LexText[1] == '\\' )
	{
		AddChar(s, cur_char);
		next();
	}
	while ( cur_char != LEX_EOF )
	{
		if ( cur_char == '\n' )
		{
			warn("eoln found in quoted terminal");
			return;
		}
		if ( cur_char == '\\' )
		{
			next();
			AddChar(s,'\\'); AddChar(s,cur_char);
			next();
		}
		else if ( cur_char == '"' )
		{
			next();
			AddChar(s,'"');
			*s = '\0';
			return;
		}
		else
		{
			AddChar(s,cur_char);
			next();
		}
	}
}

/*
 * Process # AST references;
 * #i is the AST node associated with the ith attrib.
 *
 * #0		-->	"*_root"
 * #i		--> "zzastArg(i)"
 * #[args]	--> "zzmk_ast(zzastnew(), args)"
 * #[]		--> "zzastnew()"
 * #( root child1 ... childn )	--> "zztmake(root, child1, ...., childn, NULL)"
 * #()		--> "NULL"
 *
 * Maximum size of 20 char for i.
 */
char *
scarfASTvar(s)
char *s;
{
	char *p, var[21];

	if ( !isdigit(cur_char) && cur_char!='[' && cur_char!='(' )
	{
		AddChar(s,'#');
		AddChar(s,cur_char);
		next();
		return(s);
	}

	if ( !GenAST )
		warn("use of #... without command-line option for AST's");

	if ( cur_char == '0' )
	{
		if ( CurRuleNode != NULL )
			if ( !CurRuleNode->noAST )
				warn("use of #0 in rule with automatic AST construction");
		AddStr(s, "(*_root)", 8);
		next();
		return(s);
	}
	if ( cur_char == '[' )
	{
		next();
		while ( cur_char==' ' || cur_char=='\t' || cur_char=='\n' ) {next();}
		if ( cur_char == ']' ) {next(); AddStr(s, "zzastnew()", 10); return s;}
		AddStr(s, "zzmk_ast(zzastnew(),", 20);
		s = scarfPAction(s, '[', ']');
		AddChar(s, ')');
		return s;
	}
	else if ( cur_char == '(' )
	{
		next();
		if ( cur_char == ')' ) {next(); AddStr(s, "NULL", 4); return s;}
		AddStr(s, "zztmake(", 8);
		s = scarfPAction(s, '(', ')');
		AddStr(s, ",NULL)", 6);
		return s;
	}
	AddStr(s, "zzastArg(", 9);
	p = var;
	while ( isdigit(cur_char) && p < &(var[20]) )
	{
		*p++ = cur_char;
		next();
	}
	*p = '\0';
	AddStr(s, var, strlen(var));
	AddChar(s,')');
	return(s);
}

/*
 * Process $i.j attribute references.  i is the level, j is the attr #
 *
 * $alnum	--> "alnum"			(yields first alphanumeric word; or '_')
 * $j		--> "zzaArg(current zztasp, j)"
 * $i.j		--> "zzaArg(zztaspi, j)"
 * $i.nondigit> "zzaArg(current zztasp, i).nondigit"
 * $$		--> "zzaRet"
 * $rule	--> "zzaRet"
 * $[token, text] --> "zzconstr_attr(token, text)"
 * $[]		--> "zzempty_attr()"
 *
 * Maximum size of 20 char for both i and j.
 * Check $var for membership in parameter list or return def list.
 */
char *
scarfAttr(s)
char *s;
{
	char *p, lev[21], at[21];	/* Space for i and j */
	
	if ( cur_char == '$' )
	{
		next();
		AddStr(s, "zzaRet", 6);
		return(s);
	}
	if ( cur_char == '[' )
	{
		next();
		if ( cur_char == ']' ) {next();AddStr(s,"zzempty_attr()", 14);return s;}
		AddStr(s, "zzconstr_attr(", 14);
		s = scarfPAction(s, '[', ']');
		AddChar(s, ')');
		return s;
	}
	if ( cur_char == '_' || isalpha(cur_char) )
	{
		char *start = s, *reset = s, *end, *p, *var;

		while ( cur_char == '_' || isdigit(cur_char) || isalpha(cur_char) )
		{
			AddChar(s, cur_char);
			next();
		}
		end = s;
		p = var = malloc(end-start+1);
		require(p!=NULL, "cannot alloc string");
		while ( start!=end ) *p++ = *start++;/* get var in a separate buffer */
		*p++ = '\0';
		if ( strcmp(CurRule, var) == 0 )
		{
			s = reset;
			AddStr(s, "zzaRet", 6);
			free(var);
			return s;
		}
		if ( CurRetDef != NULL )
		{
			if ( strmember(CurRetDef, var) )
			{
				/* convert $retvar --> _retv.retvar if > 1 return values
				 * else convert --> _ret if only one return value
				 */
				if ( HasComma(CurRetDef) )
				{
					s = reset;
					AddStr(s, "_retv.", 6);
					AddStr(s, var, strlen(var));
				}
				else {s=reset; AddStr(s, "_retv", 5);}
				free(var);
				return s;
			}
		}
		if ( CurParmDef != NULL )
		{
			if ( !strmember(CurParmDef, var) )
				warn(eMsg1("$%s not parameter or return value",var));
		}
		else warn(eMsg1("$%s not parameter or return value",var));
		free( var );
		return(s);
	}
	if ( !isdigit(cur_char) )
	{
		AddChar(s,'$');
		AddChar(s,cur_char);
		next();
		return(s);
	}
	AddStr(s, "zzaArg(", 7);
	p = lev;
	while ( isdigit(cur_char) && p < &(lev[20]) )
	{
		*p++ = cur_char;
		next();
	}
	*p = '\0';
	if ( cur_char != '.' )
	{
		AddStr(s, "zztasp", 6);
		sprintf(at,"%d",BlkLevel-1);
		AddStr(s, at, strlen(at));
		AddChar(s,',');
		AddStr(s, lev, strlen(lev));	/* at,lev used backwards here */
		AddChar(s,')');
		return(s);
	}
	next();
	if ( !isdigit(cur_char) )
	{
		AddStr(s, "zztasp", 6);
		sprintf(at,"%d",BlkLevel-1);
		AddStr(s, at, strlen(at));
		AddChar(s,',');
		AddStr(s, lev, strlen(lev));	/* at,lev used backwards here */
		AddChar(s,')');
		AddChar(s, '.');
		return(s);
	}
	/* else we have an $i.j situation */
	p = at;
	while ( isdigit(cur_char) && p < &(at[20]) )
	{
		*p++ = cur_char;
		next();
	}
	*p = '\0';
	AddStr(s, "zztasp", 6);
	AddStr(s, lev, strlen(lev));
	AddChar(s,',');
	AddStr(s, at, strlen(at));
	AddChar(s,')');
	return(s);
}
