/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/* $Revision: 1.5 $ */

#header
<<
#ident "$Id$"

#ifdef LL_K
#undef LL_K
#endif
#define LL_K	1

#define ZZLEXBUFSIZE        17000
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* typedef long Attrib; */
#include "utility.h"
#include "dbtype.h"
#include "language_support.h"
#include "message_catalog.h"
#include "int.h"
#include "loader.h"

#define ZZERRSTD_SUPPLIED 1
#define ZZSYNSUPPLIED     1

extern void ldr_push_c(long c);

int Lex_Buffer_Size;
long ldr_reread[10];
long ldr_rereadtop;

/*
 * This little piece of state is required to tell us whether we should be
 * interpreting double-quoted strings as delimited identifiers or as
 * backward-compatible string literals.
 *
 * Be careful about when you set this: it's very easy to be thwarted by
 * the lookahead mechanism.  Initializing it in act_init() isn't good
 * enough, because by the time act_init() has been called we've already
 * consumed the first token of input, and "initialization" may undo
 * anything that the first token production might have done.  That's very
 * bad news if the first token in a CMD_ID or CMD_CLASS token.
 */
int in_instance_line;

#include "memory_alloc.h"

>>


<<

/* Input function type for loader.  */
FILE *input;

/* Input function type for loader.  */
int (*ldr_lang_fgetc)(FILE * input_stream)=fgetc;

static void init_loader_lang(void)
{
  /* Set the appropriate language handler */
  ldr_lang_fgetc = fgetc;
}


long get_next()
{
	if (ldr_rereadtop > 0) {
		--ldr_rereadtop;
		return ldr_reread[ldr_rereadtop];
	}
	return ((ldr_lang_fgetc)(input));
}

void do_loader_parse(void *data)
{

  input = fp;

  init_loader_lang();

  /*
   * Pretend that the first line we'll see is an instance line; if it
   * isn't, this state flag will be set to 0 by the CMD_ID or CMD_CLASS
   * tokens.  Do this before starting the parser, or the effects of
   * lookahead processing will make you lose if the first line is a
   * command line (which it almost always is).
   */
  in_instance_line = 1;

  ANTLRf(loader_lines(), get_next);
}

#define zzEOF_TOKEN 1
void zzsyn(const char *text, long tok, const char *egroup,
		  unsigned long *eset, long etok)
{
    if (zzEOF_TOKEN == tok
    && eset && strstr(zzedecode(eset), "EOF")) {
	/* This patch eliminates "expecting EOF at EOF" messages.
	 * The parser generator has a bug which consumes
	 * the EOF token, and generates another EOF token
	 * that does not match. zzEOF_TOKEN is always 1,
	 * and is always all of the tokens read after EOF.
	 * The EOF token is some generated value besides 1.
	 */
	return;
    }
    ldr_increment_err_total(ldr_Current_context);
	fprintf(stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
					MSGCAT_UTIL_SET_LOADDB,
					LOADDB_MSG_SYNTAX_ERR),
		zzlineLA[0], (tok==zzEOF_TOKEN)?"EOF":text);
	fprintf(stderr, " %s ", msgcat_message (MSGCAT_CATALOG_UTILS,
						MSGCAT_UTIL_SET_LOADDB,
						LOADDB_MSG_SYNTAX_MISSING));
	if ( !etok && eset) fprintf(stderr, "%s", zzedecode(eset));
	else fprintf(stderr, "%s", zztokens[etok]);
	if ( strlen(egroup) > 0 ) {
		fprintf(stderr, " ");
		fprintf(stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
						MSGCAT_UTIL_SET_LOADDB,
						LOADDB_MSG_SYNTAX_IN), egroup);
	}
	fprintf(stderr, "\n");
}
>>

<<
static int String_Body_Size;
static char *String_Buffer = NULL;
static int String_Buffer_Size = 0;

/*
 * Japanized migdb must not use default error report function,
 * because one 8bit-set character is not printable.
 * So, Japanese version must have its own error report function.
 */
void zzerrstd(s)
const char *s;
{
  long length;
  char buffer[DB_MAX_IDENTIFIER_LENGTH * 2];

  if (LANG_VARIABLE_CHARSET(lang_charset())) {
	  length = strlen(zzlextext);
	  if (length > 0) {
	    if (((zzlextext[length - 1]) & (0x80) ) != 0) {
	      strcpy(buffer, zzlextext);
	      buffer[length] = (unsigned char)zzchar;
	      buffer[length+1] = '\0';
	      zzreplstr(buffer);
	    }
	  }
  }
  strcpy(buffer, msgcat_message (MSGCAT_CATALOG_UTILS,
				 MSGCAT_UTIL_SET_LOADDB,
				 LOADDB_MSG_LEX_ERROR));

  fprintf(stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				  MSGCAT_UTIL_SET_LOADDB,
				  LOADDB_MSG_STD_ERR),
	  ((s == NULL) ? buffer : s),
	  zzline,
          ((zzlextext == NULL) ? "" : zzlextext));
}
>>

#lexclass START
#token NL         "{[\r]}[\n]" <<zzline++; zzendcol = 0; >>

#token NULL_      "[Nn][Uu][Ll][Ll]"
#token CLASS	  "[Cc][Ll][Aa][Ss][Ss]"
#token SHARED     "[Ss][Hh][Aa][Rr][Ee][Dd]"
#token DEFAULT    "[Dd][Ee][Ff][Aa][Uu][Ll][Tt]"

#token DATE       "[Dd][Aa][Tt][Ee]"
#token TIME       "[Tt][Ii][Mm][Ee]"
#token UTIME      "[Uu][Tt][Ii][Mm][Ee]"
#token TIMESTAMP  "[Tt][Ii][Mm][Ee][Ss][Tt][Aa][Mm][Pp]"

/* command tokens */
#token CMD_ID 	  "\%[Ii][Dd]"			<< in_instance_line = 0; >>
#token CMD_CLASS  "\%[Cc][Ll][Aa][Ss][Ss]"	<< in_instance_line = 0; >>
#token CMD_CONSTRUCTOR "\%[Cc][Oo][Nn][Ss][Tt][Rr][Uu][Cc][Tt][Oo][Rr]"

/* System object identifiers */
#token REF_ELO_INT "\^[Ii]"
#token REF_ELO_EXT "\^[Ee]"
#token REF_USER	   "\^[Uu]"
#token REF_CLASS   "\^[Cc]"

/* Object references */
#token OBJECT_REFERENCE "\@"

/* Sets */
#token SET_START_BRACE "\{"
#token SET_END_BRACE   "\}"

#token REAL_LIT   "{[\+\-]}(([0-9]+[Ee]{[\+\-]}[0-9]+{[fFlL]})|([0-9]*.[0-9]+{[Ee]{[\+\-]}[0-9]+}{[fFlL]})|([0-9]+.[0-9]*{[Ee]{[\+\-]}[0-9]+}{[fFlL]}))"
#token INT_LIT    "{[\+\-]}[0-9]+"
#token OID        "[0-9]+:" << ; >>

/* #token STRING_LIT "( ('(('')|(\\\n)|~['\n])*') | (\"((\\~[\n])|(\\\n)|~[\"\\\n])*\") )" */

#token TIME_LIT4  "[0-9]+:[0-9]+:[0-9]+[\ \t]*[aApP][mM]"
#token TIME_LIT42 "[0-9]+:[0-9]+:[0-9]+[\ \t]*"
#token TIME_LIT3  "[0-9]+:[0-9]+[\ \t]*[aApP][mM]"
#token TIME_LIT31 "[0-9]+:[0-9]+[\ \t]*"
#token TIME_LIT2  "[0-9]+:[0-9]+:[0-9]+"
#token TIME_LIT1  "[0-9]+:[0-9]+"

/* #token TIME_LIT   "[0-9]+:[0-9]+{:[0-9]+}{[\ \t]*[aApP][mM]}" */

#token YEN_SYMBOL "\0xa1\0xef"
#token WON_SYMBOL "\0xa3\0xdc"
#token BACKSLASH "\\"
#token DOLLAR_SYMBOL "$"

/* #token IDENTIFIER "[a-zA-Z\0x80-\0xff_%#][a-zA-Z\0x80-\0xff_%#0-9]*" << ; >> */
#token IDENTIFIER "([a-zA-Z_%#]|(\0xa1[\0xa2-\0xee\0xf3-\0xfe])|([\0xa2-\0xfe][\0xa1-\0xfe])|(\0x8e[\0xa1-\0xfe]))([a-zA-Z_%#0-9]|(\0xa1[\0xa2-\0xfe])|([\0xa1-\0xfe])|(\0x8e[\0xa1-\0xfe]))*" << ; >>

#token DATE_LIT2   "[0-9]+/[0-9]+/[0-9]+" << ; >>

/* allow escaped newline as a continuation character */
#token "\\\n"  << zzreplstr ( ""   ); zzmore(); zzline++; zzendcol = 0; >>

/* Skip whitespace */
#token "[\ \t]+" <<zzskip();>>

/* this matches, skips, and counts new lines */
/* #token "[\n]"    <<zzline++; zzendcol = 0; zzskip();>> */

/*----------------------- scan strings ------------------------------*/

/*-- if 'string', switch to string scanner --*/
#token Quote  "[\']"      << zzmode(SQS); >>
#token NQuote "[nN][\']"  << zzmode(SQS); >>
#token BQuote "[bB][\']"  << zzmode(SQS); >>
#token XQuote "[xX][\']"  << zzmode(SQS); >>

/*
 * -- if "string", switch to appropriate "string" scanner --
 *
 * See the comment accompanying the dq_string production for further
 * explanation.
 */
#token "[\"]"  << zzmode((in_instance_line ? DQS : DELIMITED_ID)); zzskip(); >>

/*----------------------- remove comments ------------------------------*/
/* C++ comment     */
#token "[\/][\/]~[\n\r]*"  << zzline++; zzendcol = 0; zzskip(); >>

/* -- sql comment  */
#token "[\-][\-]~[\n\r]*"  << zzline++; zzendcol = 0; zzskip(); >>

/* C-style comment */
#token "/\*"        << zzmode(COMMENT); zzskip(); >>


/****************************************************************************/
/* GRAMMAR */
/****************************************************************************/

eof : "@" ; /*-- @ is the predefined ANTLR eof code ---*/

end_of_line : NL << in_instance_line = 1; >> ;

loader_lines : (one_line | end_of_line)*
               eof
               << ldr_act_finish(ldr_Current_context, 0); >> ;
               << ldr_act_finish(ldr_Current_context, 1); >>

one_line : command_line | (instance_line <<
             ldr_act_finish_line(ldr_Current_context);>>);

command_line : class_command | id_command;

/* id_command : CMD_ID  IDENTIFIER <<act_start_id(zzlextext); >>  */
id_command : CMD_ID  IDENTIFIER <<
        skipCurrentclass = false;
	ldr_act_start_id(ldr_Current_context, zzlextext);
	>>
	        INT_LIT <<ldr_act_set_id(ldr_Current_context, $3); >>;

/* class_command : CMD_CLASS IDENTIFIER <<act_set_class(zzlextext); >>  */
class_command : CMD_CLASS IDENTIFIER <<
        ldr_act_set_skip_current_class (zzlextext, strlen(zzlextext));
        ldr_act_init_context (ldr_Current_context, zzlextext, strlen(zzlextext));
	>>
                attribute_list_qualifier attribute_list constructor_spec ;

/* Kludge, since dlg crashes when we add the SHARED token, recognize it
   with a literal string until we can fix it */

attribute_list_qualifier : CLASS << ldr_act_restrict_attributes(ldr_Current_context,
                                                     LDR_ATTRIBUTE_CLASS); >> |
			   SHARED <<ldr_act_restrict_attributes(ldr_Current_context,
                                      LDR_ATTRIBUTE_SHARED); >> |
  		           DEFAULT <<ldr_act_restrict_attributes(ldr_Current_context,
                                      LDR_ATTRIBUTE_DEFAULT); >> | ;

attribute_list : "\(" attribute_names "\)" << ldr_act_check_missing_non_null_attrs(ldr_Current_context); >> ;

attribute_names : (attribute_name (("," | ) attribute_name)*) | ;

/* attribute_name : IDENTIFIER <<act_add_attribute(zzlextext);>>; */
attribute_name : IDENTIFIER <<
	ldr_act_add_attr(ldr_Current_context, zzlextext, strlen(zzlextext));
	>>;

/* constructor_spec : CMD_CONSTRUCTOR IDENTIFIER <<act_set_constructor(zzlextext); >> */
constructor_spec : CMD_CONSTRUCTOR IDENTIFIER <<
	ldr_act_set_constructor(ldr_Current_context, zzlextext);
	>>
			constructor_argument_list | ;

constructor_argument_list : "\(" argument_names "\)";

argument_names : (argument_name (("," |) argument_name)*) | ;

/* argument_name : IDENTIFIER <<act_add_argument(zzlextext);>>; */
argument_name : IDENTIFIER <<
	ldr_act_add_argument(ldr_Current_context, zzlextext);
	>> ;

instance_line : (object_id ((constant (constant)*) |)) |
		(null_id (constant (constant)*));

null_id : << ldr_act_start_instance(ldr_Current_context, -1); >>;

object_id : OID << ldr_act_start_instance(ldr_Current_context, ($1)); >>;

class_identifier: INT_LIT <<ldr_act_set_ref_class_id(ldr_Current_context, ($1));>> |
/*                  IDENTIFIER <<act_set_ref_class(zzlextext);>>; */
                  IDENTIFIER <<
				ldr_act_set_ref_class(ldr_Current_context, zzlextext);
				>> ;

object_reference : OBJECT_REFERENCE class_identifier instance_number;

instance_number : ("\|" INT_LIT
                    <<
                    {
                      bool ignore_class = false;
                      char * class_name;
                      DB_OBJECT *ref_class = NULL;

                      ldr_act_set_instance_id(ldr_Current_context, $2);
                      ref_class = ldr_act_get_ref_class(ldr_Current_context);
                      if (ref_class != NULL)
                        {
                          class_name = db_get_class_name (ref_class);
                          ignore_class =
                            ldr_is_ignore_class(class_name, strlen(class_name));
                        }
                      (*ldr_act)(ldr_Current_context, zzlextext, strlen(zzlextext),
                               (ignore_class)?LDR_NULL:LDR_OID);
                    }
                    >>
                  ) |
                    <<
                    {
                      bool ignore_class = false;
                      char * class_name;
                      DB_OBJECT *ref_class = NULL;

                      ldr_act_set_instance_id(ldr_Current_context, $2);

                      ref_class = ldr_act_get_ref_class(ldr_Current_context);
                      if (ref_class != NULL)
                        {
                          class_name = db_get_class_name (ref_class);
                          ignore_class =
                            ldr_is_ignore_class(class_name, strlen(class_name));
                        }

                      (*ldr_act)(ldr_Current_context, zzlextext, strlen(zzlextext),
                               (ignore_class)?LDR_NULL:LDR_CLASS_OID);
                    }
                    >> ;

set_constant : SET_START_BRACE << (*ldr_act)(ldr_Current_context,
                                  zzlextext,
                                  strlen(zzlextext),
                                  LDR_COLLECTION); >>
	       set_elements
               SET_END_BRACE << ldr_act_attr(ldr_Current_context, NULL, 0,
                                             LDR_COLLECTION); >>;

/* set_elements: (constant (("," | ) ("\#" |) constant)*) | ; */

set_elements: (constant (("," | ) { NL } constant)*) | ;

elo_internal : REF_ELO_INT sys_string[LDR_ELO_INT] ;
elo_external : REF_ELO_EXT sys_string[LDR_ELO_EXT] ;
user_object  : REF_USER    sys_string[LDR_SYS_USER] ;
class_object : REF_CLASS   sys_string[LDR_SYS_CLASS] ;

system_object_reference : elo_internal | elo_external | user_object | class_object;

/* currency is ignored for the time being, need to add this to
   the action routines  */

currency : DOLLAR_SYMBOL | YEN_SYMBOL | WON_SYMBOL | BACKSLASH;
monetary : currency REAL_LIT
           <<
             (*ldr_act)(ldr_Current_context, zzlextext, strlen(zzlextext),
                        LDR_MONETARY);
           >>;

/* 	| TIME_LIT	<< act_time(zzlextext); >> */

constant
	: ansi_string
	| dq_string
	| nchar_string
	| bit_string
	| sql2_date
	| sql2_time
	| sql2_timestamp
	| utime
	| NULL_		<< (*ldr_act)(ldr_Current_context, NULL, 0, LDR_NULL); >>
	| TIME_LIT4	<< (*ldr_act)(ldr_Current_context, zzlextext,
                                      strlen(zzlextext), LDR_TIME); >>
	| TIME_LIT42	<< (*ldr_act)(ldr_Current_context, zzlextext,
                                      strlen(zzlextext), LDR_TIME); >>
	| TIME_LIT3	<< (*ldr_act)(ldr_Current_context, zzlextext,
                                      strlen(zzlextext), LDR_TIME); >>
	| TIME_LIT31	<< (*ldr_act)(ldr_Current_context, zzlextext,
                                      strlen(zzlextext), LDR_TIME); >>
	| TIME_LIT2	<< (*ldr_act)(ldr_Current_context, zzlextext,
                                      strlen(zzlextext), LDR_TIME); >>
	| TIME_LIT1	<< (*ldr_act)(ldr_Current_context, zzlextext,
                                      strlen(zzlextext), LDR_TIME); >>
	| INT_LIT	<< (*ldr_act)(ldr_Current_context, zzlextext,
                                      strlen(zzlextext), LDR_INT); >>
	| REAL_LIT <<
                     /* Float notation */
                     if (strchr(zzlextext, 'F') != NULL  ||
		         strchr(zzlextext, 'f') != NULL) {
                       (*ldr_act)(ldr_Current_context, zzlextext,
                                  strlen(zzlextext), LDR_FLOAT);
                     } else  {
		       /* Scientific Notation */
		       if (strchr(zzlextext, 'E') != NULL  ||
		           strchr(zzlextext, 'e') != NULL ) {
                         (*ldr_act)(ldr_Current_context, zzlextext,
                                    strlen(zzlextext), LDR_DOUBLE);
		       } else {  /* regular numeric */
                         (*ldr_act)(ldr_Current_context, zzlextext,
                                    strlen(zzlextext), LDR_NUMERIC);
                       }
                    }
	          >>
	| DATE_LIT2	<< (*ldr_act)(ldr_Current_context, zzlextext,
                                      strlen(zzlextext), LDR_DATE); >>
        | monetary
	| object_reference
	| set_constant
	| system_object_reference
	;

ansi_string     : Quote string_body
/*                 << act_string(String_Buffer, String_Body_Size, DB_TYPE_CHAR); >> ; */
                <<
			(*ldr_act)(ldr_Current_context, String_Buffer,
                                   String_Body_Size, LDR_STR);

		 >> ;
nchar_string    : NQuote string_body
/*                << act_string(String_Buffer, String_Body_Size, DB_TYPE_NCHAR); >> ; */
                <<
			(*ldr_act)(ldr_Current_context,
                                   String_Buffer,
                                   String_Body_Size,
                                   LDR_NSTR);
		 >> ;

/*
 * This production is a little different from the other string
 * productions because it doesn't explicitly call out the introductory
 * quote token.  That's because the double-quote is discovered in a
 * production common to both delimited-id's and double-quoted strings.
 * In the id case, it's extrememly inconvenient to have to eat the extra
 * token that would appear if we produced one for the double-quote.  It's
 * easier, if inconsistent, just not to require one for the double-quoted
 * string case.
 */
dq_string	: DQS_String_Body
                <<
			(*ldr_act)(ldr_Current_context,
                                   zzlextext,
                                   strlen(zzlextext),
                                   LDR_STR);
		 >>
		;

sql2_date       : DATE Quote string_body
                << (*ldr_act)(ldr_Current_context, String_Buffer, String_Body_Size,
                              LDR_DATE); >> ;
sql2_time       : TIME Quote string_body
                << (*ldr_act)(ldr_Current_context, String_Buffer, String_Body_Size,
                              LDR_TIME); >> ;
sql2_timestamp  : TIMESTAMP Quote string_body
                << (*ldr_act)(ldr_Current_context, String_Buffer, String_Body_Size,
                              LDR_TIMESTAMP); >> ;
utime           : UTIME Quote string_body
                << (*ldr_act)(ldr_Current_context, String_Buffer, String_Body_Size,
                              LDR_TIMESTAMP); >> ;

bit_string      : BQuote string_body	      <<
                    (*ldr_act)(ldr_Current_context, String_Buffer,
                               strlen(String_Buffer), LDR_BSTR); >>
	        | XQuote string_body	      <<
                    (*ldr_act)(ldr_Current_context, String_Buffer,
                               strlen(String_Buffer), LDR_XSTR); >>;

sys_string < [LDR_TYPE context]
	: Quote string_body	<<
            (*ldr_act)(ldr_Current_context, String_Buffer,
                       strlen(String_Buffer), context); >>
	;

string_body :
	<< int current;
	   if (String_Buffer == NULL) {
		String_Buffer_Size = 100;
		String_Buffer = malloc(String_Buffer_Size);
	   }
	   String_Body_Size = 0;
	   current = 0;
	   >>
	(String_Prefix
		<< current = String_Body_Size;
		   String_Body_Size += Lex_Buffer_Size;
		   if (String_Body_Size >= String_Buffer_Size) {
			String_Buffer_Size= String_Body_Size +1;
			String_Buffer = realloc
				(String_Buffer, String_Buffer_Size);
		   }
		   memcpy(&String_Buffer[current], zzlextext, Lex_Buffer_Size);
		>>
	NL Quote
	)*
	(
	/* have to put this BEFORE the so the lextext buffer
	 * will not have been consumed */
		<< current = String_Body_Size;
		   String_Body_Size += Lex_Buffer_Size;
		   if (String_Body_Size >= String_Buffer_Size) {
			String_Buffer_Size= String_Body_Size +1;
			String_Buffer = realloc
				(String_Buffer, String_Buffer_Size);
		   }
		   memcpy(&String_Buffer[current], zzlextext, Lex_Buffer_Size);
		   String_Buffer[String_Body_Size] = 0;
		>>
	(String_Completion | String_Completion2))

	;

#lexclass COMMENT  /* C-style comments */
#token "\*/"      << zzmode(START); zzskip(); >>
#token "~[\*\n]*" << zzskip(); >>
#token "[\n]"     <<zzline++; zzendcol = 0; zzskip(); >>
#token "\*~[/]"   << zzskip(); >>


/* Simple ANSI single quoted strings */

#lexclass SQS  /* single quoted strings */
#token "\'\'"  << zzreplchar( '\'' ); zzmore(); >>  /* embedded ''  */
#token "~[\']" << zzmore(); >>
#token String_Completion "\'[ \t]"
	       << Lex_Buffer_Size = zzendexpr-zzlextext-1;
                  zzmode(START);
                  >>
#token String_Prefix "\'\+"
		<< Lex_Buffer_Size = zzendexpr-zzlextext-1;
		   zzmode(START);
		   >>
#token String_Completion2 "\'"
	       << Lex_Buffer_Size = zzendexpr-zzlextext;
                  zzmode(START);
		>>

/*
 * C-style double quoted strings with escapes
 *
 * This still accepts the usual C escape sequences, since we used to
 * accept them in old-style double-quote strings.  In particular, this
 * uses <backslash>-<doublequote> to embed a <doublequote> character in a
 * string (contrast with delimited identifiers, below).
 */

#lexclass DQS  /* double-quoted strings, for backward compatibility */
#token "\\n"   << zzreplchar( '\n' ); zzmore(); >>
#token "\\t"   << zzreplchar( '\n' ); zzmore(); >>
#token "\\f"   << zzreplchar( '\f' ); zzmore(); >>
#token "\\r"   << zzreplchar( '\r' ); zzmore(); >>
#token "\\[0-7]{[0-7]{[0-7]}}"
	       <<zzreplchar((char)strtol(&zzbegexpr[1], NULL, 8)); zzmore(); >>
#token "\\x[0-9a-fA-F]{[0-9a-fA-F]}"
	       <<zzreplchar((char)strtol(&zzbegexpr[2], NULL, 16)); zzmore(); >>
#token "\\~[]" << zzreplchar(zzbegexpr[1]); zzmore(); >>	/* self evaluating */
#token "~[\"]" << zzmore(); >>
#token DQS_String_Body "\""
	       << Lex_Buffer_Size = zzendexpr-zzlextext;
           	  zzreplchar( 0 ); zzmode(START); >>

/*
 * Delimited identifiers, ANSI-style.
 *
 * Notice that, unlike the C-style strings above, there is *no* escape
 * syntax, save for an embedded <doublequote>, which is represented by a
 * pair of adjacent <doublequote>s, per the ANSI standard.
 */

#lexclass DELIMITED_ID
#token "\"\""		<< zzreplchar( '\"' ); zzmore(); >>
#token "~[\"]"		<< zzmore(); >>
#token IDENTIFIER "\""  << zzreplchar( 0 ); zzmode(START); >>
#lexaction
<<
long ldr_rereadtop = 0;
extern long zzcharfull;

void ldr_push_c(long c)
{
       /* The zzchar bit is a kludge on top of the kludge */
       /* to back out the one character already in antlr memory. */
       if (zzcharfull) ldr_reread[ldr_rereadtop++] = zzchar;
       zzcharfull = 0;
       ldr_reread[ldr_rereadtop++] = c;
}
>>
