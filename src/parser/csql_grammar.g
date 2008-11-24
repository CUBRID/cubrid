/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/*
 * csql_grammar.g - SQL grammar file
 */

#header <<
#ident "$Id$"

#include "config.h"

#define ZZLEXBUFSIZE        17000
#define ZZERRSTD_SUPPLIED   1
#define ZZRDFUNC_SUPPLIED   1
#define ZZRDSTREAM_SUPPLIED 1
#define ZZSYNSUPPLIED       1
#define ZZRESYNCHSUPPLIED   1
#define ZZ_PREFIX gr_
#define ZZ_MAX_SYNTAX_ERRORS 40
#define D_TextSize 255

#include <ctype.h>
#include <math.h>

#include "charbuf.h"
#include "parser.h"
#include "parser_message.h"
#include "dbdef.h"
#include "language_support.h"
#include "environment_variable.h"
#include "transaction_cl.h"
#define JP_MAXNAME 256
#if defined(WINDOWS)
#define snprintf _snprintf
#endif /* WINDOWS */
#include "memory_alloc.h"
>>

#lexaction
<<
#define IS_WHITE_CHAR(c) \
                    ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\0')

PT_HINT hint_table[] = {
  { "ORDERED",   PT_HINT_ORDERED,   NULL },
  { "USE_NL",    PT_HINT_USE_NL,    NULL },
  { "USE_IDX",   PT_HINT_USE_IDX,   NULL },
  { "USE_MERGE", PT_HINT_USE_MERGE, NULL },
  { "RECOMPILE", PT_HINT_RECOMPILE, NULL },
  { "LOCK_TIMEOUT", PT_HINT_LK_TIMEOUT, NULL },
  { "NO_LOGGING", PT_HINT_NO_LOGGING, NULL },
  { "RELEASE_LOCK", PT_HINT_REL_LOCK, NULL },
  { "QUERY_CACHE", PT_HINT_QUERY_CACHE, NULL },
  { "REEXECUTE", PT_HINT_REEXECUTE, NULL },
  { "JDBC_CACHE", PT_HINT_JDBC_CACHE, NULL },
  { NULL,        -1,                NULL } /* mark as end */
};

bool is_hint_comment = false, prev_is_white_char = false;
PT_HINT_ENUM hint;
char *hint_p, hint_str[JP_MAXNAME];

>>

#token "[\/][\/]~[\n\r]*"
       << hint = PT_HINT_NONE; /* init */

          if (zzlextext[2] == '+') { /* read hint info */
              (void) pt_check_hint(zzlextext, hint_table, &hint, false);
          }

          if (hint != PT_HINT_NONE) {                   /* C++-style hint */
              /* convert to hint token */
              int i, l;
              strcpy(hint_str, "//+ ");
              l = strlen(hint_str);
              for (i = 0; hint_table[i].tokens; i++) {
                if ((hint & hint_table[i].hint) && l < JP_MAXNAME) {
                    l += strlen(hint_table[i].tokens) + 1;
                    if (l < JP_MAXNAME)
                        strcat(strcat(hint_str, hint_table[i].tokens), " ");
                }
              }
              zzreplstr(hint_str);
              LA(1) = CPP_STYLE_HINT;
          } else {
              zzskip();                                 /* C++ comment */
          }
       >>
#token "[\-][\-]~[\n\r]*"
       << hint = PT_HINT_NONE; /* init */

          if (zzlextext[2] == '+') { /* read hint info */
              (void) pt_check_hint(zzlextext, hint_table, &hint, false);
          }

          if (hint != PT_HINT_NONE) {                   /* -- sql-style hint */
              /* convert to hint token */
              int i, l;
              strcpy(hint_str, "--+ ");
              l = strlen(hint_str);
              for (i = 0; hint_table[i].tokens; i++) {
                if ((hint & hint_table[i].hint) && l < JP_MAXNAME) {
                    l += strlen(hint_table[i].tokens) + 1;
                    if (l < JP_MAXNAME)
                        strcat(strcat(hint_str, hint_table[i].tokens), " ");
                }
              }
              zzreplstr(hint_str);
              LA(1) = SQL_STYLE_HINT;
          } else {
              zzskip();                                 /* -- sql comment */
          }
       >>
#token "/\*"        << is_hint_comment = false;
                       prev_is_white_char = false;
                       hint = PT_HINT_NONE; /* init */
                       zzmode(COMMENT); zzskip(); >>    /* C-style comment */

/* token Definitions */
/*
 KEYWORDS in alphabetical order. Make sure that the name of any keyword
 does not conflict with any C keyword, function name, macro name, e.g. NULL.
 The string representation of these keywords are in pt_sqlscan.h,
 where a case-insensitive table lookup is used to distinguish
 identifiers from SQL keywords.
 */

#token ABORT
#token ABS
#token ABSOLUTE
#token ACTION
#token ACTIVE
#token ADD
#token ADD_MONTHS
#token AFTER
#token ALIAS
#token ALL
#token ALLOCATE
#token ALTER
#token ANALYZE
#token AND
#token ANY
#token ARE
#token AS
#token ASC
#token ASSERTION
#token ASYNC
#token AT
#token ATTACH
#token ATTRIBUTE
#token AUTHORIZATION
#token AUTO_INCREMENT
#token AVG
#token BEFORE
#token BEGIN
#token BETWEEN
#token BIT
#token BIT_LENGTH
#token BOOLEAN
#token BOTH
#token BREADTH
#token BY
#token CALL
#token CACHE
#token CASCADE
#token CASCADED
#token CASE
#token CAST
#token CATALOG
#token CHANGE
#token CHARACTER
#token CHARACTER_LENGTH
#token CHAR_LENGTH
#token CHECK
#token CHR
#token CLASS
#token CLASSES
#token CLOSE
#token CLUSTER
#token COALESCE
#token COLLATE
#token COLLATION
#token COLUMN
#token COMMIT
#token COMMITTED
#token COMPLETION
#token CONNECT
#token CONNECTION
#token CONSTRAINT
#token CONSTRAINTS
#token CONTINUE
#token CONVERT
#token CORRESPONDING
#token COST
#token COUNT
#token CREATE
#token CROSS
#token CURRENT
#token CURRENT_DATE
#token CURRENT_TIME
#token CURRENT_TIMESTAMP
#token CURRENT_USER
#token CURRENT_VALUE
#token CURSOR
#token CYCLE
#token Char
#token DATA
#token DATA_TYPE
#token DAY
#token DEALLOCATE
#token DECAY_CONSTANT
#token DECLARE
#token DECODE_
#token DECR
#token DECREMENT
#token DECRYPT
#token DEFAULT
#token DEFERRABLE
#token DEFERRED
#token DEFINED
#token DELETE
#token DEPTH
#token DESC
#token DESCRIBE
#token DESCRIPTOR
#token DIAGNOSTICS
#token DICTIONARY
#token DIFFERENCE
#token DIRECTORY
#token DISCONNECT
#token DISTINCT
#token Domain
#token DRAND
#token DRANDOM
#token DROP
#token Date
#token Double
#token EACH
#token ELSE
#token ELSEIF
#token ENCRYPT
#token END
#token EQUALS
#token ESCAPE
#token EVALUATE
#token EVENT
#token EXCEPT
#token EXCEPTION
#token EXCLUDE
#token EXEC
#token EXECUTE
#token EXISTS
#token EXP
#token EXTERNAL
#token EXTRACT
#token FETCH
#token FIRST
#token FOREIGN
#token FOUND
#token FROM
#token FULL
#token FUNCTION
#token False
#token File
#token Float
#token For
#token GDB
#token GENERAL
#token GET
#token GE_INF
#token GE_LE
#token GE_LT
#token GLOBAL
#token GO
#token GOTO
#token GRANT
#token GREATEST
#token GROUP
#token GROUPBY_NUM
#token GROUPS
#token GT_INF
#token GT_LE
#token GT_LT
#token HASH
#token HAVING
#token HOST
#token HOUR
#token IDENTIFIED
#token IDENTITY
#token IF
#token IGNORE
#token IMMEDIATE
#token IN
#token INACTIVE
#token INCR
#token INCREMENT
#token INDEX
#token INDICATOR
#token INFINITE
#token INFO
#token INF_LE
#token INF_LT
#token INHERIT
#token INITIALLY
#token INNER
#token INOUT
#token INPUT
#token INSENSITIVE
#token INSERT
#token INSTANCES
#token INST_NUM
#token INTERSECT
#token INTERSECTION
#token INTERVAL
#token INTO
#token INTRINSIC
#token INVALIDATE
#token IS
#token ISOLATION
#token Int
#token Integer
#token JAVA
#token JOIN
#token KEY
#token LANGUAGE
#token LAST
#token LAST_DAY
#token LDB
#token LEADING
#token LEAST
#token LEAVE
#token LEFT
#token LENGTH
#token LENGTHB
#token LESS
#token LEVEL
#token LIKE
#token LIMIT
#token LIST
#token LOCAL
#token LOCAL_TRANSACTION_ID
#token LOCK
#token LOG
#token LOOP
#token LOWER
#token LPAD
#token LTRIM
#token MATCH
#token MAXIMUM
#token MAXVALUE
#token MAX_ACTIVE
#token MEMBERS
#token METHOD
#token MINUTE
#token MINVALUE
#token MIN_ACTIVE
#token MODIFY
#token MODULE
#token MODULUS
#token MONTH
#token MONTHS_BETWEEN
#token MULTISET
#token MULTISET_OF
#token Max
#token Min
#token Monetary
#token NA
#token NAME
#token NAMES
#token NATIONAL
#token NATURAL
#token NCHAR
#token NEW
#token NEXT
#token NEXT_VALUE
#token NO
#token NOCYCLE
#token NOMAXVALUE
#token NOMINVALUE
#token NONE
#token NOT
#token NULLIF
#token NUMERIC
#token Null
#token NVL
#token NVL2
#token OBJECT
#token OBJECT_ID
#token OCTET_LENGTH
#token OF
#token OFF_
#token OID
#token OLD
#token ON_
#token ONLY
#token OPEN
#token OPERATION
#token OPERATORS
#token OPTIMIZATION
#token OPTION
#token OR
#token ORDER
#token ORDERBY_NUM
#token OTHERS
#token OUT_
#token OUTER
#token OUTPUT
#token OVERLAPS
#token PARAMETERS
#token PARTIAL
#token PARTITION
#token PARTITIONING
#token PARTITIONS
#token PASSWORD
#token PENDANT
#token PROXY
#token POSITION
#token PRECISION
#token PREORDER
#token PREPARE
#token PRESERVE
#token PRIMARY
#token PRINT
#token PRIOR
#token PRIORITY
#token PRIVILEGES
#token PROCEDURE
#token PROTECTED
#token Private
#token QUERY
#token RAND
#token RANDOM
#token RANGE
#token READ
#token Real
#token RECURSIVE
#token REF
#token REFERENCES
#token REFERENCING
#token REGISTER
#token REJECT
#token RELATIVE
#token REMOVE
#token RENAME
#token REORGANIZE
#token REPEATABLE
#token REPLACE
#token RESET
#token RESIGNAL
#token RESTRICT
#token RETAIN
#token RETURN
#token RETURNS
#token REVERSE
#token REVOKE
#token RIGHT
#token ROLE
#token ROLLBACK
#token ROUND
#token ROUTINE
#token ROW
#token ROWNUM
#token ROWS
#token RPAD
#token RTRIM
#token SAVEPOINT
#token SCHEMA
#token SCOPE
#token SCROLL
#token SEARCH
#token SECOND
#token SECTION
#token SELECT
#token SENSITIVE
#token SEQUENCE
#token SEQUENCE_OF
#token SERIAL
#token SERIALIZABLE
#token SESSION
#token SESSION_USER
#token SET
#token SETEQ
#token SETNEQ
#token SET_OF
#token SHARED
#token SIGNAL
#token SIMILAR
#token SIZE
#token SOME
#token SQL
#token SQLCODE
#token SQLERROR
#token SQLEXCEPTION
#token SQLSTATE
#token SQLWARNING
#token SQRT
#token STABILITY
#token START_
#token STATEMENT
#token STATISTICS
#token STATUS
#token STDDEV
#token STOP
#token STRUCTURE
#token SUBCLASS
#token SUBSET
#token SUBSETEQ
#token SUBSTR
#token SUBSTRB
#token SUBSTRING
#token SUM
#token SUPERCLASS
#token SUPERSET
#token SUPERSETEQ
#token SWITCH
#token SYS_DATE
#token SYS_TIME_
#token SYS_TIMESTAMP
#token SYSTEM
#token SYSTEM_USER
#token SYS_USER
#token SmallInt
#token String
#token TABLE
#token TEMPORARY
#token TEST
#token TEXT
#token THEN
#token THERE
#token TIMEOUT
#token TIMESTAMP
#token TIMEZONE_HOUR
#token TIMEZONE_MINUTE
#token TO
#token TO_CHAR
#token TO_DATE
#token TO_TIME
#token TO_TIMESTAMP
#token TO_NUMBER
#token TRACE
#token TRAILING
#token TRANSACTION
#token TRANSLATE
#token TRANSLATION
#token TRIGGER
#token TRIGGERS
#token TRIM
#token TRUNC
#token TYPE
#token THAN
#token Time
#token True
#token UNCOMMITTED
#token UNDER
#token UNIQUE
#token UNKNOWN
#token UPDATE
#token UPPER
#token USAGE
#token USE
#token USER
#token USING
#token Union
#token Utime
#token VALUE
#token VALUES
#token VARCHAR
#token VARIABLE
#token VARIANCE
#token VARYING
#token VCLASS
#token VIEW
#token VIRTUAL
#token VISIBLE
#token WAIT
#token WHEN
#token WHENEVER
#token WHERE
#token WHILE
#token WITH
#token WITHOUT
#token WORK
#token WORKSPACE
#token WRITE
#token YEAR
#token ZONE

/* other tokens defined by regular expressions */
/* if 'string', switch to string scanner */
#token Quote  "[\']"      << zzmode(SQS); >>
#token NQuote "[nN][\']"  << zzmode(SQS); >>
#token BQuote "[bB][\']"  << zzmode(SQS); >>
#token XQuote "[xX][\']"  << zzmode(SQS); >>

/* if "string", switch to "string" scanner */
#token "[\"]"  << zzmode(DQS); zzskip(); >>

/* if [srting], switch to bracket string scanner */
#token "[\[]"  << zzmode(BQS); zzskip(); >>

/* This is for 8bit ascii character sets, and multi-byte character
 * sets which have a convention that the second byte is either > 0x80
 * or is the same byte value as an asci alpha-numeric character.
 *
 * Note that ANSI (and hence MIA) SQL identifiers
 * do NOT attempt to restrict legal characters in identifiers.
 * They only require double quotes around identifiers with SQL3
 * control characters.
 *
 * Consequently, we try to parse as many possible identifiers as
 * possible here. If a byte NOT included here is to be part of
 * an identifier (as could happen with some multi-byte character sets)
 * The identifier should be embedded in double quotes.
 *
 * ######KLUDGE ALERT#######
 *      antlr/pccts/dlg advertise accepting 0-255 character codes.
 * 	However, this is mostly bunk. To get the lexical portion, dlg,
 * 	to accept this it must be compiled under gcc with -funsigned-char
 * 	AND must have the 'letter' portion of the 'Attrib' struct declared
 * 	short instead of char.
 *
 * 	Also, the 'output.c' module needs to be modified to change
 * 	generated table declarations from 'static' to 'static const'.
 */

#token IdName "([a-zA-Z_%#]|(\0xa1[\0xa2-\0xee\0xf3-\0xfe])|([\0xa2-\0xfe][\0xa1-\0xfe])|(\0x8e[\0xa1-\0xfe]))([a-zA-Z_%#0-9]|(\0xa1[\0xa2-\0xfe])|([\0xa2-\0xfe][\0xa1-\0xfe])|(\0x8e[\0xa1-\0xfe]))*" <<
	LA(1) = pt_identifier_or_keyword(zzlextext);
>>

#token UNSIGNED_INTEGER "[0-9]+"
#token UNSIGNED_REAL    "([0-9]+[Ee]{[\+\-]}[0-9]+{[fF]})|([0-9]*.[0-9]+{[Ee]{[\+\-]}[0-9]+}{[fF]})|([0-9]+.[0-9]*{[Ee]{[\+\-]}[0-9]+}{[fF]})"

#token "[\0\t\ ]"      	 << zzskip(); >>
#token "[\r\n]"      	 << zzskip(); >>
#token SEMI_COLON "[\;]" << ; >>
#token PLUS "[\+]"       << ; >>
#token MINUS "[\-]"      << ; >>
#token STAR "[\*]"       << ; >>
#token SLASH "[\/]"      << ; >>
#token Left_Brace "[\{]"      << ; >>
#token Right_Brace "[\}]"     << ; >>
#token COLON_ "[:]"     << ; >>


#token YEN_SIGN		"(\0xa1\0xef)"  << ; >>
#token DOLLAR_SIGN	"$"		<< ; >>
#token WON_SIGN		"\\"		<< ; >>

/*
 * Unless we do the following kludge, SQLM is not LL(k) parsable for any
 * reasonable k (eg, k <= 4).  Left parentheses followed by the SELECT
 * keyword are changed into '\01'.  All other left parentheses become '\02'.
 * And then, the modified input is rescanned.
 */
#token "\(" << pt_fix_left_parens(); zzskip(); >>

/*
 * After the above change to the input, the '\01' and the '\02' are
 * rescanned either as LEFT_PAREN (the '(' followed by SELECT)
 * or as Left_Paren (the '(' followed by anything else.)
 */
#token LEFT_PAREN  "\01" << zzreplchar('('); >>
#token Left_Paren  "\02" << zzreplchar('('); >>

#token Right_Paren "\)" <<;>>

<<
#include "csql_grammar_scan.h"
/* define pccts environment storage frame struct
 * to support reentrant parsing */

typedef struct PT_PCCTS_ENV
{
  PARSER_CONTEXT *this_parser;

  /* defined at pccts/h/antlr.h */
  long zztokenLA[LL_K];
  char zztextLA[LL_K][ZZLEXBUFSIZE];
  char *zztextend[LL_K];
  char *zzlextextend;
  long zzlap;
  long zzlineLA[LL_K];
  long zzcolumnLA[LL_K];

  /* defined at pccts/h/dlgauto.h */
  char *zzlextext;		/* text of most recently matched token */
  char *zzbegexpr;		/* beginning of last reg expr recogn. */
  char *zzendexpr;		/* beginning of last reg expr recogn. */
  long zzbufsize;		/* number of characters in zzlextext */
  long zzbegcol;		/* column that first character of token is in */
  long zzendcol;		/* column that last character of token is in */
  long zzline;			/* line current token is on */
  long zzchar;			/* character to determine next state */
  long zzbufovfcnt;
  long zzcharfull;
  char *zznextpos;		/* points to next available position in zzlextext */
  long zzclass;
  FILE *zzstream_in;
  long (*zzfunc_in) ();
  long zzauto;
  long zzadd_erase;
  char zzebuf[70];
} PT_PCCTS_ENV;

void save_pccts_env (PT_PCCTS_ENV * p);
void restore_pccts_env (PT_PCCTS_ENV * p);
int parse_one_statement (int state);

void
save_pccts_env (PT_PCCTS_ENV * p)
{
  int i;

  p->this_parser = this_parser;

  /* defined at pccts/h/antlr.h */
  p->zzlap = zzlap;
  for (i = 0; i < LL_K; i++)
    {
      p->zztokenLA[i] = zztokenLA[i];
      memcpy (p->zztextLA[i], zztextLA[i], ZZLEXBUFSIZE);
      p->zztextend[i] = zztextend[i];
      p->zzlineLA[i] = zzlineLA[i];
      p->zzcolumnLA[i] = zzcolumnLA[i];
    }
  p->zzlextextend = zzlextextend;

  /* defined at pccts/h/dlgauto.h */
  p->zzlextext = zzlextext;
  p->zzbegexpr = zzbegexpr;
  p->zzendexpr = zzendexpr;
  p->zzbufsize = zzbufsize;
  p->zzbegcol = zzbegcol;
  p->zzendcol = zzendcol;
  p->zzline = zzline;
  p->zzchar = zzchar;
  p->zzbufovfcnt = zzbufovfcnt;
  p->zzcharfull = zzcharfull;
  p->zznextpos = zznextpos;
  p->zzclass = zzclass;
  p->zzstream_in = zzstream_in;
  p->zzfunc_in = zzfunc_in;
  p->zzauto = zzauto;
  p->zzadd_erase = zzadd_erase;
  memcpy (p->zzebuf, zzebuf, 70);
}

void
restore_pccts_env (PT_PCCTS_ENV * p)
{
  int i;

  this_parser = p->this_parser;

  /* defined at pccts/h/antlr.h */
  zzlap = p->zzlap;
  for (i = 0; i < LL_K; i++)
    {
      zztokenLA[i] = p->zztokenLA[i];
      memcpy (zztextLA[i], p->zztextLA[i], ZZLEXBUFSIZE);
      zztextend[i] = p->zztextend[i];
      zzlineLA[i] = p->zzlineLA[i];
      zzcolumnLA[i] = p->zzcolumnLA[i];
    }
  zzlextextend = p->zzlextextend;

  /* defined at pccts/h/dlgauto.h */
  zzlextext = p->zzlextext;
  zzbegexpr = p->zzbegexpr;
  zzendexpr = p->zzendexpr;
  zzbufsize = p->zzbufsize;
  zzbegcol = p->zzbegcol;
  zzendcol = p->zzendcol;
  zzline = p->zzline;
  zzchar = p->zzchar;
  zzbufovfcnt = p->zzbufovfcnt;
  zzcharfull = p->zzcharfull;
  zznextpos = p->zznextpos;
  zzclass = p->zzclass;
  zzstream_in = p->zzstream_in;
  zzfunc_in = p->zzfunc_in;
  zzauto = p->zzauto;
  zzadd_erase = p->zzadd_erase;
  memcpy (zzebuf, p->zzebuf, 70);
}

>>

<<
PT_NODE **
parser_main (PARSER_CONTEXT * parser)
{
  long desc_index = 0;
  long i, top;
  PT_PCCTS_ENV pt_pccts_env;

  if (!parser)
    return 0;

  output_host_index = input_host_index = desc_index = 0;
  save_pccts_env (&pt_pccts_env);
  this_parser = parser;
  lp_look_state = 0;

  dbcs_start_input ();
  ANTLRf (start (), pt_nextchar);

  if (parser->error_msgs || parser->stack_top <= 0 || !parser->node_stack)
    {
      parser->statements = NULL;
    }
  else
    {
      /* create array of result statements */
      parser->statements = (PT_NODE **) parser_alloc (parser,
      							 (1 + parser->stack_top) * sizeof (PT_NODE *));

      if (parser->statements)
	{
	  for (i = 0, top = parser->stack_top; i < top; i++)
	    {
	      parser->statements[i] = parser->node_stack[i];
	    }
	  parser->statements[top] = NULL;
	}
      /* record input_host_index into parser->host_var_count for later use;
         e.g. pt_set_host_variables(), auto-parameterized query */
      if ((parser->host_var_count = input_host_index) > 0)
	{
	  /* allocate place holder for host variables */
	  parser->host_variables = (DB_VALUE *)
	    malloc (parser->host_var_count * sizeof (DB_VALUE));
	  if (!parser->host_variables)
	    {
	      parser->statements = NULL;
	    }
	  else
	    {
	      (void) memset (parser->host_variables, 0,
			     parser->host_var_count * sizeof (DB_VALUE));
	    }
	}
    }
  restore_pccts_env (&pt_pccts_env);
  return parser->statements;
}

/* xxxnum_check: 0 not allowed, no compatibility check
                 1 allowed, compatibility check (search_condition)
                 2 allowed, no compatibility check (select__list) */
static int instnum_check = 0;
static int groupbynum_check = 0;
static int orderbynum_check = 0;
static int within_join_condition = 0;

/* check Oracle style outer-join operatior: '(+)' */
static bool found_Oracle_outer = false;

/* check sys_date, sys_time, sys_timestamp, local_transaction_id */
static bool si_timestamp = false;
static bool si_tran_id = false;

/* check the condition that the statment is not able to be prepared */
static bool cannot_prepare = false;

/* check the condition that the result of a query is not able to be cached */
static bool cannot_cache = false;

/* check if INCR is used legally */
static int select_level = -1;

/* handle inner increment exprs in select list */
static PT_NODE *hidden_incr_list = NULL;

int
parse_one_statement (int state)
{
  if (state == 0)
    {
      zzbufsize = ZZLEXBUFSIZE;
      zzenterANTLRf (pt_nextchar);
    }
  else if (state == 1)
    {
      goto loop;
    }
  {
    zzRULE;
    zzBLOCK (zztasp1);
    zzMake0;
    {
      zzBLOCK (zztasp2);
      zzMake0;
      {
	statement_OK = 1;

	if (state == 0)
	  return 0;
      }
    }

  loop:
    zzasp = ZZA_STACKSIZE;
    {
      zzBLOCK (zztasp1);
      {
	zzBLOCK (zztasp2);
	this_parser->statement_number = 0;
	while (1)
	  {
	    if ((zzsetwd1[LA (1)] & 0x1))
	      {
		statement_ ();
		if (statement_OK)
		  this_parser->statement_number = 1;
		else
		  statement_OK = 1;
		zzLOOP (zztasp2);
		return 0;
	      }
	    else if ((LA (1) == SEMI_COLON))
	      {
		zzmatch (SEMI_COLON);
		zzCONSUME;
	      }
	    else
	      break;
	    zzLOOP (zztasp2);
	  }
	zzEXIT (zztasp2);
      }
      eof ();
      zzEXIT (zztasp1);
      return 1;
    fail:
      zzEXIT (zztasp1);
      zzsyn (zzlextext, LA (1), "statement list", zzMissSet, zzMissTok);
      zzresynch (zzsetwd1, 0x2);
    }
    return 1;
  }
}

>>

/*
   A BNF for the global database language

   Notation is for the PCCTS antlr compiler tool.
   ( )  are for grouping
   ( )* 0 or more times
   ( )+ 1 or more times
   { }  denotes an optional clause
    |   separates alternatives
   [ ]  are for char recognition as in grep [a-z]
   " "  are for keywords in the parsed language
  << >> are for actions (C-code)

   Production names (variables) begin with lower-case.
    (since productions are turned into C-functions with
     the same name, you can't use a C-keyword as a name:
       const, int, define , read, abs, do,  ... )

   Productions are of the form:
     prod_name : << actions >> sub_clauses << actions >> ;

   Token names ( lex types recognized by the scanner
          such as: IdName, UNSIGNED_INTEGER, ... )
          begin with upper-case and are defined in
          a #token statement such as:
          #token UNSIGNED_INTEGER "[0-9]+"
   Quoted items (keywords and tokens) MUST escape
   the characters ()[]+*;{}  etc
 */

start "statement list"
	:
	  << PT_NODE *node; >>
	  ( 		<< statement_OK = 1; >>
	    << instnum_check = 0;
	       groupbynum_check = 0;
	       orderbynum_check = 0; >>
            << select_level = -1; >>
	    statement_	<< if (statement_OK)
			     this_parser->statement_number++;
			   else
			     statement_OK = 1;
			>>
	  | SEMI_COLON
	  )*
	  <<
	    if (zzlextextend - zzlextext > 0) {
	        node = pt_pop(this_parser);
		if (node) {
		    PT_ERRORf(this_parser, node, "check syntax of illegal %s statement.", pt_show_node_type(node));
		} else {
		    /* make dummy node */
		    node = parser_new_node(this_parser, PT_SELECT);
		    PT_ERROR(this_parser, node, "check syntax of query, illegal statement." );
		}
	    }
	  >>
	  eof
	;

eof : "@" ; /* @ is the predefined ANTLR eof code */


/* A session wth the SQL environment */

statement_ "statement"
        :
        <<  PT_NODE *node;
            bool  saved_si_timestamp, saved_si_tran_id,
                     saved_cannot_prepare;

            saved_si_timestamp = si_timestamp;
            si_timestamp = false;
            saved_si_tran_id = si_tran_id;
            si_tran_id = false;
            saved_cannot_prepare = cannot_prepare;
            cannot_prepare = false;
        >>
        (
          create_statement
        | data_type_statement
        | alter_statement
        | rename_statement
        | update_statistics_statement
        | get_statistics_statement
        | drop_statement
        | esql_query_statement
        | evaluate_statement
        | insert_statement
        | update_statement
        | delete_statement
        | call_statement
        | grant_statement
        | revoke_statement
        | commit_statement
        | rollback_statement
        | savepoint_statement
        | scope_statement
        | get_transaction_statement
        | set_transaction_statement
        | get_optimization_statement
        | set_optimization_statement
        | set_system_parameters_statement
        | execute_deferred_trigger_statement
        | get_trigger_statement
        | set_trigger_statement
        | prepare_to_commit_statement
        | attach_statement
        )
        <<
            node = pt_pop(this_parser);
            if (node) {
                node->si_timestamp = (si_timestamp == true) ? 1: 0;
                node->si_tran_id = (si_tran_id == true) ? 1 : 0;
                node->cannot_prepare = (cannot_prepare == true) ? 1 : 0;
                pt_push(this_parser, node);
            }
            si_timestamp = saved_si_timestamp;
            si_tran_id = saved_si_tran_id;
            cannot_prepare = saved_cannot_prepare;
        >>
        ;

/* commands to update the database schema and catalog */

create_statement "create statement"
	: CREATE
		( create_class_statement
		| create_vclass_statement
		| create_index_statement
                | create_user_statement
		| create_trigger_statement
		| create_serial_statement
                | create_stored_procedure_statement )
	;

alter_statement "alter statement"
	: ALTER
		( alter_class_statement
                | alter_user_statement
		| alter_trigger_statement
		| alter_serial_statement
		| alter_index_statement )
	;

rename_statement "rename statement"
	: RENAME
		( rename_class_statement
		| rename_trigger_statement)
	;

drop_statement "drop statement"
	: DROP
		( drop_class_statement
		| drop_index_statement
                | drop_user_statement
		| drop_trigger_statement
		| drop_deferred_statement
		| drop_variable_statement
		| drop_serial_statement
		| drop_stored_procedure_statement )
	;

drop_class_statement "drop class statement"
	:	<< PT_NODE *dcs=parser_new_node(this_parser, PT_DROP);
	           PT_MISC_TYPE t=PT_MISC_DUMMY;
		>>
	  { class_type > [t] }
	  class_specification_list
		<<
                   if(dcs)
		   { dcs->info.drop.spec_list=pt_pop(this_parser);
		     dcs->info.drop.entity_type=t;
	           }
                   pt_push(this_parser, dcs);
		>>
	;

drop_variable_statement "drop variable statement"
	: 	<< PT_NODE *dv=parser_new_node(this_parser, PT_DROP_VARIABLE); >>
	  VARIABLE identifier
	     ( "," identifier << PLIST; >> )*
		<< if (dv) dv->info.drop_variable.var_names=pt_pop(this_parser);
		   pt_push(this_parser, dv);
		>>
	;

/* database performance tuning commands */
create_index_statement "create index statement"
        : << PT_NODE *index_name, *indexed_class, *columns, *stmt;
             int      reverse = 0;

             stmt = parser_new_node(this_parser, PT_CREATE_INDEX);
             stmt->info.index.unique  = 0;
	     stmt->info.index.reverse = 0;
          >>
          { REVERSE << stmt->info.index.reverse = reverse = 1; >> }
          { UNIQUE  << stmt->info.index.unique  = 1; >> }
          index_on_this[reverse] > [index_name, indexed_class, columns]
                << if (stmt)
                   {
                     stmt->info.index.indexed_class = indexed_class;
                     stmt->info.index.column_names  = columns;
                     stmt->info.index.index_name  = index_name;
                   }
                   pt_push(this_parser, stmt);
                >>
        ;

drop_index_statement "drop index statement"
        : << PT_NODE *index_name, *indexed_class, *columns, *stmt;
             int      reverse = 0;

             stmt = parser_new_node(this_parser, PT_DROP_INDEX);
             stmt->info.index.unique  = 0;
             stmt->info.index.reverse = 0;
          >>
          { REVERSE << stmt->info.index.reverse = reverse = 1; >> }
          { UNIQUE  << stmt->info.index.unique  = 1; >> }
          index_on_that[reverse] > [index_name, indexed_class, columns]
                << if (stmt)
                   {
                     stmt->info.index.indexed_class = indexed_class;
                     stmt->info.index.column_names  = columns;
                     stmt->info.index.index_name  = index_name;
                   }
                   pt_push(this_parser, stmt);
                >>
        ;

alter_index_statement "alter index statement"
        : << PT_NODE *index_name, *indexed_class, *columns, *stmt;
             int      reverse = 0;

             stmt = parser_new_node(this_parser, PT_ALTER_INDEX);
             stmt->info.index.unique  = 0;
             stmt->info.index.reverse = 0;
          >>
          { REVERSE << stmt->info.index.reverse = reverse = 1; >> }
          { UNIQUE  << stmt->info.index.unique  = 1; >> }
          index_on_that[reverse] > [index_name, indexed_class, columns]
                << if (stmt)
                   {
                     stmt->info.index.indexed_class = indexed_class;
                     stmt->info.index.column_names  = columns;
                     stmt->info.index.index_name  = index_name;
                   }
                   pt_push(this_parser, stmt);
                >>
	  REBUILD
        ;


index_on_this < [int reverse] > [PT_NODE *index_name, PT_NODE *indexed_class, PT_NODE *columns]
        : << $index_name = NULL; >>
          INDEX { identifier << $index_name = pt_pop(this_parser); >> }
          ON_ {ONLY} class__name << $indexed_class = pt_pop(this_parser); >>
          index_column_name_list[reverse] << $columns = pt_pop(this_parser); >>
        ;

index_on_that < [int reverse] > [PT_NODE *index_name, PT_NODE *indexed_class, PT_NODE *columns]
        : << $index_name = NULL; $indexed_class = NULL; $columns = NULL; >>
          INDEX
            ( ON_ {ONLY} class__name << $indexed_class = pt_pop(this_parser); >>
              index_column_name_list[reverse] << $columns = pt_pop(this_parser); >>
            | identifier << $index_name = pt_pop(this_parser); >>
              { ON_ {ONLY} class__name << $indexed_class = pt_pop(this_parser); >>
                { index_column_name_list[reverse] << $columns = pt_pop(this_parser); >> }
              }
            )
	;

index_column_name_list < [int reverse]
        :
          Left_Paren
          index_column_name[reverse] ("," index_column_name[reverse] << PLIST; >>)*
          Right_Paren
	;

index_column_name < [int reverse]
        : << PT_MISC_TYPE isdescending = reverse ? PT_DESC : PT_ASC;
             PT_NODE *node = parser_new_node(this_parser, PT_SORT_SPEC);
          >>
          attribute_name
          { ASC | DESC << isdescending = PT_DESC; >> }
                << if (node) {
                     node->info.sort_spec.asc_or_desc = isdescending;
                     node->info.sort_spec.expr = pt_pop(this_parser);
                   }
                   pt_push(this_parser, node);
                >>
	;

update_statistics_statement "update statistics statement"
	: 	<< PT_NODE *classes; long all;
		   PT_NODE *ups = parser_new_node(this_parser, PT_UPDATE_STATS);
		>>
	  UPDATE STATISTICS ON_ update_statistics_classes > [classes,all]
		<< if (ups)
		   {
		     ups->info.update_stats.class_list  = classes;
		     ups->info.update_stats.all_classes = all;
		   }
		   pt_push(this_parser, ups);
		>>
	;

update_statistics_classes > [PT_NODE *classes, long all]
	: << $classes = NULL; $all = 0; >>
        (
         {ONLY} class__name ("," {ONLY} class__name << PLIST; >>)*
		      << $classes = pt_pop(this_parser); $all = 0; >>
	 | ALL CLASSES << $classes = NULL    ; $all = 1; >>
         | CATALOG CLASSES << $classes = NULL; $all = -1; >>
        )
	;

get_statistics_statement "get statistics statement"
        : << PT_NODE *class = NULL, *args = NULL, *var = NULL,
                     *gts = parser_new_node(this_parser, PT_GET_STATS);
          >>
          GET STATISTICS char_string_literal >[args]
          OF class__name  << class = pt_pop(this_parser); >>
          { into_clause >[var]
                 << if (gts)
                        gts->info.get_stats.into_var = var;
                 >>
          }
          << if (gts) {
                gts->info.get_stats.class_ = class;
                gts->info.get_stats.args = args;
             }
             pt_push(this_parser, gts);
          >>
          ;

get_optimization_statement "get optimization option statement"
	: 	<< PT_NODE *lvl = NULL,
			   *gopt = parser_new_node(this_parser, PT_GET_OPT_LVL),
			   *args = NULL;
		   PT_MISC_TYPE option = PT_OPT_LVL;
		>>
	  GET OPTIMIZATION
		( LEVEL
			<< option = PT_OPT_LVL; >>
		| COST { OF } char_string_literal >[args]
			<< option = PT_OPT_COST; >>
		)
	  { into_clause >[lvl]
		<< if (gopt)
		       gopt->info.get_opt_lvl.into_var = lvl;
		>>
	  }	<< if (gopt) {
		       gopt->info.get_opt_lvl.option = option;
		       gopt->info.get_opt_lvl.args = args;
		   }
		   pt_push(this_parser, gopt);
		>>
	;

set_optimization_statement "set optimization option statement"
	: 	<< PT_NODE *sopt = parser_new_node(this_parser, PT_SET_OPT_LVL);
		   PT_MISC_TYPE option = PT_OPT_LVL;
		   PT_NODE *plan, *cost;
		>>
	  SET OPTIMIZATION
		( LEVEL { TO | "=" } opt_level_spec[sopt]
			<< option = PT_OPT_LVL; >>
		| COST { OF } char_string_literal >[plan] { TO | "=" } literal_
			<< option = PT_OPT_COST;
			   cost = pt_pop(this_parser);
			   if (plan)
			       plan->next = cost;
			   if (sopt)
			       sopt->info.set_opt_lvl.val = plan;
			>>
		)
		<< if (sopt)
		       sopt->info.set_opt_lvl.option = option;
		   pt_push(this_parser, sopt);
		>>
	;

opt_level_spec < [PT_NODE *sopt] "optimization level specification"
	:	<< PT_NODE *val; >>
	( 	<< val = parser_new_node(this_parser, PT_VALUE); >>
	  ( ON_	<< if (val) val->info.value.data_value.i = -1; >>
	  | OFF_ << if (val) val->info.value.data_value.i =  0; >>
	  )	<< if (val) val->type_enum = PT_TYPE_INTEGER;  >>
	| unsigned_integer >[val]
	| parameter_	   >[val]
	| host_parameter[&input_host_index] >[val]
	)
		<< if (sopt) sopt->info.set_opt_lvl.val = val; >>
	;

opt_limit_spec < [PT_NODE *sopt] "optimization limit specification"
	:	<< PT_NODE *val; >>
	(
	  unsigned_integer >[val]
	| parameter_ >[val]
	| host_parameter[&input_host_index] >[val]
	)
		<< if (sopt) sopt->info.set_opt_lvl.val = val; >>
	;

set_system_parameters_statement "set parameters statement"
        : << PT_NODE *val = NULL, *sps = parser_new_node(this_parser, PT_SET_SYS_PARAMS);
          >>
          SET SYSTEM PARAMETERS char_string_literal >[val]
             << pt_push(this_parser, val); >>
             ( "," char_string_literal >[val]
               << pt_push(this_parser, val); PLIST; >> )*
          << if (sps) {
                sps->info.set_sys_params.val = pt_pop(this_parser);
             }
             pt_push(this_parser, sps);
          >>
          ;

/* table specification productions */

table_specification_list "table specification list"
	:
	  table_specification
	  ( ","
	    table_specification << PLIST; >> )*
	;

extended_table_specification_list < [bool *found_ANSI_join_ptr] "table specification list"
        : << *found_ANSI_join_ptr = false; /* init */ >>
          table_specification
          (
            ( "," table_specification  << PLIST; >>
            | join_table_specification << *found_ANSI_join_ptr = true;
                                          PLIST; >>
            )
          )*
        ;

join_table_specification "join table specification"
        :
        ( cross_join_table_specification
        | qualified_join_table_specification
        )
        ;

cross_join_table_specification "cross join table specification"
        : << PT_NODE *sopt; >>
          CROSS JOIN table_specification
            << sopt = pt_pop(this_parser);
               sopt->info.spec.join_type = PT_JOIN_CROSS;
               pt_push(this_parser, sopt);
            >>
        ;

qualified_join_table_specification "qualified join table specification"
        : << PT_NODE *sopt;
             PT_JOIN_TYPE join_type = PT_JOIN_INNER;
             bool natural = false;
          >>
          /*{ NATURAL << natural = true; >> } -- dose not support natural join */
          {
            ( INNER << join_type = PT_JOIN_INNER; >>
            | ( LEFT << join_type = PT_JOIN_LEFT_OUTER; >>
              | RIGHT << join_type = PT_JOIN_RIGHT_OUTER; >>
            /*| FULL << join_type = PT_JOIN_FULL_OUTER; >> -- dose not support full outer join */
              ) { OUTER }
            /*| Union << join_type = PT_JOIN_UNION; >> -- dose not support union join */
            )
          }
          JOIN table_specification
          << sopt = pt_pop(this_parser);
             if (sopt) {
                sopt->info.spec.natural = natural;
                sopt->info.spec.join_type = join_type;
             }
             pt_push(this_parser, sopt);
          >>
          join_specification[sopt]
          /* if we would support natural join, shoud be
          { join_specification[sopt] } */
        ;

join_specification < [PT_NODE *sopt] "join specification"
        :
        ( join_condition
          << if (sopt)
                sopt->info.spec.on_cond = pt_pop(this_parser);
          >>
        /*| named_columns_join -- dose not support named columns join
          << if (sopt)
                sopt->info.spec_using_cond = pt_pop(this_parser); */
        /*| constraint_join -- dose not support constraint join */
        )
        ;

join_condition "join condition"
        : << int saved_wjc, saved_ic; >>
          ON_ << saved_wjc = within_join_condition; within_join_condition = 1;
                 saved_ic = instnum_check; instnum_check = 1; >>
          search_condition
              << within_join_condition = saved_wjc;
                 instnum_check = saved_ic; >>
        ;

table_specification "table specification"
	: 	<< PT_NODE *ent, *range, *cols; >>
	  (class_specification   << ent = pt_pop(this_parser); >>
	    { {AS} identifier << range=pt_pop(this_parser); cols=NULL; >>
	      { Left_Paren
	        attribute_name ( "," attribute_name << PLIST; >> )*
	        Right_Paren   << cols=pt_pop(this_parser); >>
	      }	<< if(ent)
	           { ent->info.spec.range_var = range;
	             ent->info.spec.as_attr_list = cols;
		   }
		>>
 	    }

	    { WITH Left_Paren
	   	 ( READ UNCOMMITTED )
		 << ent->info.spec.lock_hint |= LOCKHINT_READ_UNCOMMITTED; >>
		 Right_Paren
	    }
	    << pt_push(this_parser, ent); >>
	  )

	  |

	  (( meta_class_specification   << ent = pt_pop(this_parser); >>
	  | subquery << ent = parser_new_node(this_parser, PT_SPEC);
	  		if (ent) {
	  		    ent->info.spec.derived_table = pt_pop(this_parser);
	  		    ent->info.spec.derived_table_type = PT_IS_SUBQUERY;
			}
	  	     >>
	  | TABLE l_paren set_expression Right_Paren
		     << ent = parser_new_node(this_parser, PT_SPEC);
	  		if (ent) {
	  		    ent->info.spec.derived_table = pt_pop(this_parser);
	  		    ent->info.spec.derived_table_type = PT_IS_SET_EXPR;
			}
	  	     >>
	  )
	  { {AS} identifier << range=pt_pop(this_parser); cols=NULL; >>
	    { Left_Paren
	      attribute_name ( "," attribute_name << PLIST; >> )*
	      Right_Paren   << cols=pt_pop(this_parser); >>
	    }	<< if(ent)
	           { ent->info.spec.range_var = range;
	             ent->info.spec.as_attr_list = cols;
		   }
		>>
 	  } 	<<
                   if (ent
                   &&  ent->info.spec.derived_table_type == PT_IS_SUBQUERY
                   &&  ent->info.spec.as_attr_list == NULL /* no attr_list */) {
                     PT_NODE *subq, *new_ent;

                     /* remove dummy select from FROM clause
                      *
                      * for example:
                      * case 1 (simple spec):
                      *    FROM (SELECT * FROM x) AS s
                      * -> FROM x AS s
                      * case 2 (nested spec):
                      *    FROM (SELECT * FROM (SELECT a, b FROM bas) y(p, q)) x
                      * -> FROM (SELECT a, b FROM bas) x(p, q)
                      */
                     if ((subq = ent->info.spec.derived_table)
                     &&  subq->node_type == PT_SELECT
                     &&  PT_SELECT_INFO_IS_FLAGED(subq, PT_SELECT_INFO_DUMMY)) {
                       new_ent = subq->info.query.q.select.from;
                       subq->info.query.q.select.from = NULL;

                       /* free, reset new_spec's range_var, as_attr_list */
                       if (new_ent->info.spec.range_var) {
                         parser_free_node(this_parser, new_ent->info.spec.range_var);
                         new_ent->info.spec.range_var = NULL;
                       }
                       new_ent->info.spec.range_var = ent->info.spec.range_var;
                       ent->info.spec.range_var = NULL;

                       /* free old ent, reset to new_ent */
                       parser_free_node(this_parser, ent);
                       ent = new_ent;
                     }
                   }
                   pt_push(this_parser, ent);
                >>
          )
	;

/* class specification productions */

class_specification_list "class specification list"
	:
	  class_specification
	  ( ","
	    class_specification << PLIST; >> )*
	;

class_specification "class specification"
    :   << PT_NODE *p;
           bool sublist = false; >>
      only_all_class_specification

    | Left_Paren
      only_all_class_specification
          ( "," only_all_class_specification << sublist = true; PLIST; >>
          )*
      Right_Paren
        <<
           /* if the list entry is one, then do not manage as sublist.
            * instead, manage as single entry class
            *
            * example)
            * [Q] select ... from (x) t;
            * here, manage [Q] as [Q']
            * ---> [Q'] select ... from x t;
            */
           if (sublist == true) {
               p = parser_new_node(this_parser, PT_SPEC);
               if (p) p->info.spec.entity_name = pt_pop(this_parser);
               pt_push(this_parser, p);
           } /* if (sublist == true) */
        >>
	;

meta_class_specification "meta class specification"
	: <<PT_NODE * p; >>
	CLASS  only_class_specification
		<< p=pt_pop(this_parser);
                   if(p) p->info.spec.meta_class = PT_META_CLASS;
                   pt_push(this_parser, p);
		>>
	;

only_all_class_specification "class specification"
	: only_class_specification
	|	<< PT_NODE *cts=NULL, *exc=NULL; >>
	  all_class_specification
	  { Left_Paren EXCEPT class_specification_list Right_Paren
	  	<< exc=pt_pop(this_parser);
	  	   cts=pt_pop(this_parser);
                   if(cts)
		   {
                     cts->info.spec.except_list = exc;
		   }
                   pt_push(this_parser, cts);
		>>
	}
	;

only_class_specification "class specification"
	: 	<< PT_NODE *e_nam, *ocs=parser_new_node(this_parser, PT_SPEC); >>
	{ONLY} class__name	<<
		e_nam = pt_pop(this_parser);
		   if(ocs) {
		   	ocs->info.spec.entity_name=e_nam;
		   	ocs->info.spec.only_all=PT_ONLY;
			ocs->info.spec.meta_class = PT_CLASS;
		   }
                   pt_push(this_parser, ocs);
		>>
	;


all_class_specification "class specification"
	: 	<< PT_NODE *e_nam, *acs=parser_new_node(this_parser, PT_SPEC); >>
	ALL class__name	<<
		e_nam = pt_pop(this_parser);
		   if(acs) {
		   	acs->info.spec.entity_name=e_nam;
		   	acs->info.spec.only_all=PT_ALL;
			acs->info.spec.meta_class = PT_CLASS;
		   }
                   pt_push(this_parser, acs);
		>>
	;


/* class name productions */

class__name "class name"
	: 	<< PT_NODE *e_nam, *usr; >>
        ( user__name "\." identifier
		        	<< e_nam=pt_pop(this_parser); usr  =pt_pop(this_parser);
				   if (e_nam && usr) {
				   	e_nam->info.name.resolved
				   		= usr->info.name.original;
				   }
				   pt_push(this_parser, e_nam);
				 >>
	| identifier
	)
	;

rename_class_statement "rename class statement"
	:	<< PT_NODE *node=parser_new_node(this_parser, PT_RENAME);
		   PT_MISC_TYPE t=PT_CLASS;
		>>
	  { class_type > [t] }
	  {ONLY} class__name
	  AS {ONLY} class__name
		<<
                    if (node)
		    {
			node->info.rename.new_name = pt_pop(this_parser);
                    	node->info.rename.old_name = pt_pop(this_parser);
			node->info.rename.entity_type = t;
                    }
                    pt_push(this_parser, node);
                >>
	;

class_type > [PT_MISC_TYPE t]
	:
          VCLASS    << $t=PT_VCLASS; >>
        | VIEW      << $t=PT_VCLASS; >>
        | CLASS     << $t=PT_CLASS;  >>
        | TABLE     << $t=PT_CLASS;  >>
        | TYPE      << $t=PT_ADT;  >>
	;

alter_class_statement "alter class statement"
	: 	<< PT_NODE      *alt = parser_new_node(this_parser, PT_ALTER);
		   PT_MISC_TYPE  t   = PT_MISC_DUMMY;
		>>
	  { class_type > [t] }
          {ONLY} class__name
		<<
                   if(alt)
		   { alt->info.alter.entity_name=pt_pop(this_parser);
                     alt->info.alter.entity_type=t;
                   }
                   pt_push(this_parser, alt);
		>>
	  alter__clause
		<<
		   if (alt)
		     pt_gather_constraints(this_parser, alt);
		>>
	;

alter__clause "alter clause"
	: ADD alter_add_clause resolution_list_option
	| DROP alter_drop_clause resolution_list_option
	| CHANGE alter_change_clause
	| RENAME alter_rename_clause resolution_list_option
  | alter_partition_clause
	| 	<< PT_NODE *alt = pt_pop(this_parser);
		   if (alt) alt->info.alter.code = PT_RENAME_RESOLUTION;
		>>
	  inherit_resolution_list
		<< if (alt) alt->info.alter.super.resolution_list = pt_pop(this_parser);
	  	   pt_push(this_parser, alt);
		>>
	;

resolution_list_option "resolution list"
	: 	<< PT_NODE *alt = pt_pop(this_parser); >>
	  { inherit_resolution_list
		<< if (alt)
		     alt->info.alter.super.resolution_list = pt_pop(this_parser);
		>>
	  }	<< pt_push(this_parser, alt); >>
	;

alter_rename_clause "alter rename clause"
	: 	<< PT_NODE *old, *ren = pt_pop(this_parser);
		   PT_MISC_TYPE etyp = PT_ATTRIBUTE, cls = PT_NORMAL;
		   if (ren)
		     ren->info.alter.code = PT_RENAME_ATTR_MTHD;
		>>
	  ( { ATTRIBUTE << etyp = PT_ATTRIBUTE; >>
	    | COLUMN    << etyp = PT_ATTRIBUTE; >>
	    | METHOD    << etyp = PT_METHOD;    >>
	    }
	    { CLASS	<< cls  = PT_META_ATTR;  >>
	    }
	    attribute_name
	    AS attribute_name
		<< if (ren)
		   {
		     ren->info.alter.alter_clause.rename.element_type = etyp;
		     ren->info.alter.alter_clause.rename.meta = cls;
		     ren->info.alter.alter_clause.rename.new_name = pt_pop(this_parser);
		     ren->info.alter.alter_clause.rename.old_name = pt_pop(this_parser);
		   }
		>>
	  | FUNCTION     << old = NULL; >>
	    { identifier << old = pt_pop(this_parser); >> }
	    OF { CLASS << cls = PT_META_ATTR; >> }
	    method__name
	    AS identifier
		<< if (ren)
		   {
		     ren->info.alter.alter_clause.rename.element_type = PT_FUNCTION_RENAME;
		     ren->info.alter.alter_clause.rename.meta = cls;
		     ren->info.alter.alter_clause.rename.new_name = pt_pop(this_parser);
		     ren->info.alter.alter_clause.rename.mthd_name = pt_pop(this_parser);
		     ren->info.alter.alter_clause.rename.old_name = old;
		   }
		>>
	  | File file_path_name
	    AS file_path_name
		<< if (ren)
		   {
		     ren->info.alter.alter_clause.rename.element_type = PT_FILE_RENAME;
		     ren->info.alter.alter_clause.rename.new_name = pt_pop(this_parser);
		     ren->info.alter.alter_clause.rename.old_name = pt_pop(this_parser);
		   }
		>>
	  )
		<< pt_push(this_parser, ren); >>
	;

alter_file_rename < [PT_NODE *alt] "alter file rename clause"
	: /* << PT_NODE *tmp = NULL;>> */

            File file_path_name
            AS file_path_name
                << if (alt)
                   {
                     alt->info.alter.alter_clause.rename.element_type = PT_FILE_RENAME;
                     alt->info.alter.alter_clause.rename.new_name = pt_pop(this_parser);
                     alt->info.alter.alter_clause.rename.old_name = pt_pop(this_parser);
                   }
                >>


       ;

alter_add_clause "alter add clause"
	: << PT_NODE *alt = pt_pop(this_parser); >>
	   ( PARTITION << pt_push(this_parser, alt); >> add_partition_clause
    | attr_method_file_list[alt]
		<<
                   if(alt) alt->info.alter.code = PT_ADD_ATTR_MTHD;
		>>
	  | SUPERCLASS superclass_list
            	<< if(alt)
		   {
                     alt->info.alter.code = PT_ADD_SUPCLASS;
                     alt->info.alter.super.sup_class_list=pt_pop(this_parser);
                   }
		>>
	  | QUERY csql_query
		<< if(alt)
		   {
                     alt->info.alter.code = PT_ADD_QUERY;
                     alt->info.alter.alter_clause.query.query=pt_pop(this_parser);
                   }
		>>
	  )	<< pt_push(this_parser, alt); >>
	;

add_partition_clause "add partition clause"
	: 	<< PT_NODE *h, *alt = pt_pop(this_parser); >>
    ( PARTITIONS unsigned_integer > [h]
      <<
      if(alt) alt->info.alter.code = PT_ADD_HASHPARTITION;
      if(alt) alt->info.alter.alter_clause.partition.size = h;
      >>
      | l_paren
      partition_definition_list
      Right_Paren <<
      if(alt) alt->info.alter.code = PT_ADD_PARTITION;
      if (alt) alt->info.alter.alter_clause.partition.parts =
      pt_pop(this_parser);
      >> )
  ;

alter_drop_clause "alter drop clause"
	: 	<< PT_NODE *qno, *alt = pt_pop(this_parser); >>
	  ( {ATTRIBUTE | COLUMN | METHOD}
	    normal_or_class_attribute ( ","
	    	normal_or_class_attribute << PLIST; >> )*
		<< if (alt)
		   {
		     alt->info.alter.code = PT_DROP_ATTR_MTHD;
		     alt->info.alter.alter_clause.attr_mthd.attr_mthd_name_list
		     	= pt_pop(this_parser);
		   }
		>>
	  | method_files
		<< if (alt)
		   {
		     alt->info.alter.code = PT_DROP_ATTR_MTHD;
		     alt->info.alter.alter_clause.attr_mthd.mthd_file_list = pt_pop(this_parser);
		   }
		>>
	  | SUPERCLASS superclass_list
            	<< if(alt)
		   {
                     alt->info.alter.code = PT_DROP_SUPCLASS;
                     alt->info.alter.super.sup_class_list=pt_pop(this_parser);
                   }
		>>
	  | CONSTRAINT identifier
            	<< if(alt)
		   {
                     alt->info.alter.code = PT_DROP_CONSTRAINT;
                     alt->info.alter.constraint_list=pt_pop(this_parser);
                   }
		>>
	  | QUERY << qno = NULL; >>
	    { query_number_list << qno = pt_pop(this_parser); >> }
		<< if(alt)
		   {
                     alt->info.alter.code = PT_DROP_QUERY;
                     alt->info.alter.alter_clause.query.query_no_list=qno;
                   }
		>>
    | PARTITION alter_partition_name_list <<
        if(alt) alt->info.alter.code = PT_DROP_PARTITION;
        if(alt) alt->info.alter.alter_clause.partition.name_list =
        pt_pop(this_parser);
    >>
	  )	<< pt_push(this_parser, alt); >>
	;

alter_change_clause "alter change clause"
	: << PT_NODE *alt = pt_pop(this_parser); >>
	( attr_method[alt]
		<<
		   if(alt) alt->info.alter.code = PT_MODIFY_ATTR_MTHD;
		   >>
	  | 	<< PT_NODE *nam, *names, *val, *values;
		   if (alt) alt->info.alter.code = PT_MODIFY_DEFAULT;
		>>
	    normal_or_class_attribute DEFAULT default__value
		<< values = pt_pop(this_parser); names = pt_pop(this_parser);	>>
	    ( "," normal_or_class_attribute DEFAULT default__value
		<< val = pt_pop(this_parser); nam = pt_pop(this_parser);
		   if (values && val) parser_append_node(val, values);
		   if (names && nam) parser_append_node(nam, names);
		>>
	    )*
		<< if (alt)
		   {
		     alt->info.alter.alter_clause.ch_attr_def.attr_name_list = names;
		     alt->info.alter.alter_clause.ch_attr_def.data_default_list = values;
		   }
		>>
	  | 	/* the query_number is not required  */
		<< PT_NODE *qno = NULL; >>
            QUERY
	    {query_number << qno = pt_pop(this_parser); >>}
	    csql_query
            	<< if(alt)
		   {
                     alt->info.alter.code = PT_MODIFY_QUERY;
                     alt->info.alter.alter_clause.query.query=pt_pop(this_parser);
                     alt->info.alter.alter_clause.query.query_no_list=qno;
                   }
		>>
	  | 	alter_file_rename[alt] /* rename file function  */
	    	<<
		   if (alt) alt->info.alter.code = PT_RENAME_ATTR_MTHD;
		>>

	  )	<< pt_push(this_parser, alt); >>
	;

attr_method_file_list < [PT_NODE *alt] "attribute method file list"
	:
	    attr_method[alt]
           { method_files
            	<< if($alt)
                     $alt->info.alter.alter_clause.attr_mthd.mthd_file_list=
                     	pt_pop(this_parser);
		>>
	   }

	;

attr_method < [PT_NODE *alt] "attribute method only, no file list"
	: << PT_NODE *tmp = NULL;>>

          { CLASS ATTRIBUTE
                attribute_definition_list[PT_META_ATTR]
                << if ($alt)
                   {
                     tmp = pt_pop(this_parser);
                     $alt->info.alter.alter_clause.attr_mthd.attr_def_list=tmp;
                   }
                >>
          }
          { {ATTRIBUTE | COLUMN} attribute_definition_list[PT_NORMAL]
                << if($alt) {
                        /* this works assuming tmp->next is NULL */
                        if(tmp) tmp->next = pt_pop(this_parser);
                        else tmp = pt_pop(this_parser);
                        $alt->info.alter.alter_clause.attr_mthd.attr_def_list
                                = tmp;
                   }
                >>
          }
          { method_definition_list
                << if($alt)
                     $alt->info.alter.alter_clause.attr_mthd.mthd_def_list
                        =pt_pop(this_parser);
                >>
          }

	;

normal_or_class_attribute "normal or class attribute"
	: 	<< PT_NODE *att; PT_MISC_TYPE cls=PT_NORMAL; >>
	  { CLASS << cls = PT_META_ATTR; >> }
	  attribute_name
		<< att = pt_pop(this_parser);
		   if (att) att->info.name.meta_class = cls;
		   pt_push(this_parser, att);
		>>
	;


query_number_list "list of query numbers"
	: query_number ( "," query_number << PLIST; >> )*
	;

/* Updates to the local databases via virtual classes */

/* INSERT object(s) into a virtual class */

insert_statement "insert statement"
	: << PT_NODE *ins =parser_new_node(this_parser, PT_INSERT);
		PT_NODE *param = NULL;
		if (ins) ins->info.insert.is_value = PT_IS_VALUE;
	>>
	insert_name_clause[ins]
	insert_statement_value_clause[ins]
	  { into_clause > [param]
	  	<< if(ins) ins->info.insert.into_var = param;
	  	>>
	  }
	  << pt_push(this_parser, ins); >>
	;

insert_expression  "insert expression"
	: << PT_NODE *ins=parser_new_node(this_parser, PT_INSERT);
		PT_NODE *param = NULL;
		if (ins) ins->info.insert.is_value = PT_IS_VALUE;
	 >>
	(
	 insert_name_clause[ins]
	 insert_expression_value_clause[ins]
	 | Left_Paren
		insert_name_clause[ins]
		insert_expression_value_clause[ins]

		  { into_clause > [param]
		  	<< if(ins) ins->info.insert.into_var = param;  	>>
		  }
	   Right_Paren)
          << pt_push(this_parser, ins); >>
	;

insert_name_clause < [PT_NODE *ins] "insert statement"
	: 	<< char *hint_comment; >>
      INSERT
      { ( CPP_STYLE_HINT
          << if (ins) {
                hint_comment = pt_makename($1.text);
                (void) pt_get_hint(hint_comment, hint_table, ins);
	         }
          >>
        | SQL_STYLE_HINT
          << if (ins) {
                hint_comment = pt_makename($1.text);
                (void) pt_get_hint(hint_comment, hint_table, ins);
	         }
          >>
        | C_STYLE_HINT
          << if (ins) {
                hint_comment = pt_makename($1.text);
                (void) pt_get_hint(hint_comment, hint_table, ins);
	         }
          >>
        )+
      }
      INTO only_class_specification
                << if($ins) $ins->info.insert.spec = pt_pop(this_parser); >>
	  { Left_Paren
	    { attribute_list
		<< if($ins) $ins->info.insert.attr_list = pt_pop(this_parser); >>
	    }
	    Right_Paren
	  }
	;

attribute_list "attribute list"
	:  attribute_name
     (","  attribute_name  << PLIST; >> )*
	;

insert_statement_value_clause < [PT_NODE *ins] "insert values clause"
	: << PT_NODE * values = NULL; >>
	  VALUES
	  insert_value_clause > [values]
		  << if ($ins) {
		  	$ins->info.insert.is_value = PT_IS_VALUE;
			$ins->info.insert.value_clause = values;
		     } 	>>
	| csql_query
	  << if ($ins) {
	  	$ins->info.insert.is_value = PT_IS_SUBQUERY;
		$ins->info.insert.value_clause = pt_pop(this_parser);
	     } 	>>
	| DEFAULT { VALUES }
	  << if ($ins) {
	  	$ins->info.insert.is_value = PT_IS_DEFAULT_VALUE;
		$ins->info.insert.value_clause = NULL;
	     } 	>>
	;

insert_expression_value_clause < [PT_NODE *ins] "insert values clause"
	: << PT_NODE * values = NULL; >>
	  VALUES
	  insert_value_clause > [values]
		  << if ($ins) {
		  	$ins->info.insert.is_value = PT_IS_VALUE;
			$ins->info.insert.value_clause = values;
		     } 	>>
	| DEFAULT { VALUES }
	  << if ($ins) {
	  	$ins->info.insert.is_value = PT_IS_DEFAULT_VALUE;
		$ins->info.insert.value_clause = NULL;
	     } 	>>
	;

into_clause > [PT_NODE * param] "into clause"
	: << PT_NODE *lbl=NULL; >>
	  (INTO | TO)
	    to_parameter > [lbl]
	  << $param = lbl; >>
	;

l_paren
	: LEFT_PAREN
	| Left_Paren
	;

insert_value_clause > [PT_NODE * values] "insert value clause"
	:
	l_paren
	{ insert_value_list << $values = pt_pop(this_parser); >>}
	Right_Paren
	;

insert_value_list "insert value list"
	: insert_value ( "," insert_value << PLIST; >> )*
	;

insert_value "insert value"
	: select_statement
	| expression_
	;

/* UPDATE  a virtual class */

update_statement "update statement"
	: 	<< PT_NODE *upd = parser_new_node(this_parser, PT_UPDATE);
		   PT_NODE *obj_param = NULL; PT_NODE *ent; PT_NODE *range;
	       char *hint_comment;
		   int saved_ic; >>
	  UPDATE
      { ( CPP_STYLE_HINT
          << if (upd) {
                hint_comment = pt_makename($1.text);
                (void) pt_get_hint(hint_comment, hint_table, upd);
	         }
          >>
        | SQL_STYLE_HINT
          << if (upd) {
                hint_comment = pt_makename($1.text);
                (void) pt_get_hint(hint_comment, hint_table, upd);
	         }
          >>
        | C_STYLE_HINT
          << if (upd) {
                hint_comment = pt_makename($1.text);
                (void) pt_get_hint(hint_comment, hint_table, upd);
	         }
          >>
        )+
      }
	  ( (class_specification | meta_class_specification)
	    { {AS} identifier << range=pt_pop(this_parser);
	    			 if (range) {
	    			     ent = pt_pop(this_parser);
	    			     if (ent) {
	    			         ent->info.spec.range_var = range;
	    			         pt_push(this_parser, ent);
	    			     }
	    			 }
	    			 >>
	    }
	    << if (upd) {
	           upd->info.update.spec = pt_pop(this_parser);
	       }
	    >>
            SET update_assignment ( "," update_assignment << PLIST; >> )*
                <<
		   if(upd)
		   {
                     upd->info.update.assignment = pt_pop(this_parser);
                   }
		>>
	    { WHERE
              ( << saved_ic = instnum_check; instnum_check = 1; >>
                search_condition
                << instnum_check = saved_ic; >>
                << if (upd) upd->info.update.search_cond = pt_pop(this_parser); >>
	      | CURRENT OF cursor
		<< if (upd) upd->info.update.cursor_name=pt_pop(this_parser); >>
	      )
            }
            { using_index_clause
                << if (upd) upd->info.update.using_index = pt_pop(this_parser); >>
            }
	  | OBJECT from_parameter > [obj_param]
	        << if (upd) upd->info.update.object_parameter = obj_param; >>
	    SET update_assignment ( "," update_assignment << PLIST; >> )*
		<< if (upd) upd->info.update.assignment = pt_pop(this_parser); >>
	  )	<< pt_push(this_parser, upd); >>
	;

update_assignment "update attribute clause"
        : ( path_expression
            "="             << PFOP(PT_ASSIGN); >>
            expression_     << PSOP; >>
          | parentheses_path_expression_set
            "="             << PFOP(PT_ASSIGN); >>
            primary
               <<
                  { PT_NODE *exp, *tmp, *list;
                    PT_NODE *arg1, *arg2, *e1, *e2 = NULL, *e1_next, *e2_next;
                    bool is_subquery = false;

                    arg2 = pt_pop(this_parser);

                    /* primary is parentheses expr set value */
                    if (arg2->node_type == PT_VALUE &&
                        (arg2->type_enum == PT_TYPE_NULL || arg2->type_enum == PT_TYPE_EXPR_SET)) {
                        /* flatten multi-column assignment expr */
                        exp = pt_pop(this_parser);
                        arg1 = exp->info.expr.arg1;

			if (arg1->node_type == PT_EXPR) {
                            /* get elements and free set node */
                            e1 = arg1->info.expr.arg1;
                            arg1->info.expr.arg1 = NULL; /* cut-off link */
                            parser_free_node(this_parser, exp);   /* free exp, arg1 */

                            if (arg2->type_enum == PT_TYPE_NULL) {
                                ; /* nop */
                            } else {
                                e2 = arg2->info.value.data_value.set;
                                arg2->info.value.data_value.set = NULL; /* cut-off link */
                            }
                            parser_free_node(this_parser, arg2);

                            list = NULL; /* init */
                            for ( ; e1; e1 = e1_next) {
                                e1_next = e1->next;
                                e1->next = NULL;
                                if (arg2->type_enum == PT_TYPE_NULL) {
                                    if ((e2 = parser_new_node(this_parser, PT_VALUE)) == NULL)
                                        break; /* error */
                                    e2->type_enum = PT_TYPE_NULL;
                                } else {
                                    if (e2 == NULL)
                                        break; /* error */
                                }
                                e2_next = e2->next;
                                e2->next = NULL;

                                tmp = parser_new_node(this_parser, PT_EXPR);
                                if (tmp) {
                                    tmp->info.expr.op = PT_ASSIGN;
                                    tmp->info.expr.arg1 = e1;
                                    tmp->info.expr.arg2 = e2;
                                }
                                list = parser_append_node(tmp, list);

                                e2 = e2_next;
                            }

                            /* expression number check */
                            if (e1 || e2) {
                                PT_ERRORf(this_parser, list, "check syntax at %s, different number of elements in each expression.",
                                      pt_show_binopcode(PT_ASSIGN));
                            }

                            pt_push(this_parser, list);
			}
			else {
			    /* something wrong */
			    exp->info.expr.arg2 = arg2;
			    pt_push(this_parser, exp);
			}
                    } else {
                        if (pt_is_query(arg2)) {
                            /* primary is subquery. go ahead */
                            is_subquery = true;
                        }
                        pt_push(this_parser, arg2);
                        PSOP;

                        /* unknown error check */
                        if (is_subquery == false) {
                            exp = pt_pop(this_parser);
                            PT_ERRORf(this_parser, exp, "check syntax at %s",
                                      pt_show_binopcode(PT_ASSIGN));
                            pt_push(this_parser, exp);
                        }
                    }
                  }
               >>
          )
        ;

parentheses_path_expression_set "update attribute clause"
        :      << PT_NODE *list, *p; >>
          Left_Paren path_expression_list Right_Paren
               <<
                  list = pt_pop(this_parser);

                  p = parser_new_node(this_parser, PT_EXPR);
                  if (p) {
                      p->info.expr.op = PT_PATH_EXPR_SET;
                      p->info.expr.paren_type = 1;
                      p->info.expr.arg1 = list;
                  }
                  pt_push(this_parser, p);
               >>
        ;

path_expression_list "update attribute clause"
        : path_expression
          ( "," path_expression <<PLIST;>> )*
        ;

/* DELETE  object(s) of a virtual class */

delete_statement "delete statement"
	: 	<< PT_NODE *del=parser_new_node(this_parser, PT_DELETE);
	       char *hint_comment;
		   int saved_ic; >>
	  DELETE
      { ( CPP_STYLE_HINT
          << if (del) {
                hint_comment = pt_makename($1.text);
                (void) pt_get_hint(hint_comment, hint_table, del);
	         }
          >>
        | SQL_STYLE_HINT
          << if (del) {
                hint_comment = pt_makename($1.text);
                (void) pt_get_hint(hint_comment, hint_table, del);
	         }
          >>
        | C_STYLE_HINT
          << if (del) {
                hint_comment = pt_makename($1.text);
                (void) pt_get_hint(hint_comment, hint_table, del);
	         }
          >>
        )+
      }
		/* this is non-ansi, and makes no sense */
          { class__name <<if(del)del->info.delete_.class_name=pt_pop(this_parser);>>
	  }
	  FROM table_specification_list
		<< if(del)del->info.delete_.spec=pt_pop(this_parser); >>
          { WHERE
            ( << saved_ic = instnum_check; instnum_check = 1; >>
              search_condition
              << instnum_check = saved_ic; >>
		<< if(del)del->info.delete_.search_cond=pt_pop(this_parser); >>
	    | CURRENT OF cursor
		<< if(del)del->info.delete_.cursor_name=pt_pop(this_parser); >>
	    )
          }
          { using_index_clause
                << if (del) del->info.delete_.using_index = pt_pop(this_parser); >>
          }
	    << pt_push(this_parser, del); >>
	;

/* Authorization commands */

grant_statement "grant statement"
	: 	<< PT_NODE *users, *entities;
		   PT_NODE *gra=parser_new_node(this_parser, PT_GRANT);
		   PT_MISC_TYPE w=PT_NO_GRANT_OPTION;
		>>
	  GRANT author_cmd_list grantees > [users, entities]
          { WITH GRANT OPTION << w=PT_GRANT_OPTION; >>
	  }
		<< if (gra)
		   {
                     gra->info.grant.grant_option=w;
                     gra->info.grant.user_list=users;
                     gra->info.grant.spec_list=entities;
                     gra->info.grant.auth_cmd_list=pt_pop(this_parser);
                   }
                   pt_push(this_parser, gra);
		>>
	;

grantees > [PT_NODE *users, PT_NODE *entities]
	: ON_ class_specification_list TO user__list
		<< $users    = pt_pop(this_parser); $entities = pt_pop(this_parser); >>
	| TO user__list                ON_ class_specification_list
		<< $entities = pt_pop(this_parser); $users    = pt_pop(this_parser); >>
	;

revoke_statement "revoke statement"
	:	<< PT_NODE *users, *entities;
		   PT_NODE *rev=parser_new_node(this_parser, PT_REVOKE);
		>>
	  REVOKE author_cmd_list revokees > [users,entities]
		<<
                   if (rev)
		   {
                     rev->info.revoke.user_list=users;
		     rev->info.revoke.spec_list=entities;
                     rev->info.revoke.auth_cmd_list=pt_pop(this_parser);
                   }
                   pt_push(this_parser, rev);
		>>
	;

revokees > [PT_NODE *users, PT_NODE *entities]
	: ON_   class_specification_list FROM user__list
		<< $users    = pt_pop(this_parser); $entities = pt_pop(this_parser); >>
	| FROM user__list                ON_   class_specification_list
		<< $entities = pt_pop(this_parser); $users    = pt_pop(this_parser); >>
	;

author_cmd_list "list of privileges"
	: authorized_cmd
          ( "," authorized_cmd << PLIST; >> )*
	;

grant_attribute_method_list "grant attribute method list"
	: Left_Paren
	  attribute_name
	  ( "," attribute_name << PLIST; >> )*
	  Right_Paren
	;

authorized_cmd "privilege clause"
	:	<< PT_PRIV_TYPE t; PT_NODE *node,*qqq=0;
		   node=parser_new_node(this_parser, PT_AUTH_CMD);
		>>
	  ( SELECT  << t=PT_SELECT_PRIV; >>
      	  | INSERT  << t=PT_INSERT_PRIV; >>
      	  | INDEX   << t=PT_INDEX_PRIV; >>
 	  | DELETE  << t=PT_DELETE_PRIV; >>
	  | UPDATE  << t=PT_UPDATE_PRIV;>>
            { grant_attribute_method_list
              << qqq=pt_pop(this_parser);
                 PT_ERRORmf(this_parser, node,
                            MSGCAT_SET_PARSER_SYNTAX,
                            MSGCAT_SYNTAX_ATTR_IN_PRIVILEGE,
                            parser_print_tree_list(this_parser, qqq));
              >> }
	  | ALTER   << t=PT_ALTER_PRIV; >>
	  | ADD     << t=PT_ADD_PRIV; >>
	  | DROP    << t=PT_DROP_PRIV; >>
	  | EXECUTE << t=PT_EXECUTE_PRIV; >>
          | REFERENCES         << t=PT_REFERENCES_PRIV; >>
	  | ALL { PRIVILEGES } << t=PT_ALL_PRIV; >>
	  )
		<<
                   if(node)
		   {
                     node->info.auth_cmd.auth_cmd = t;
                     node->info.auth_cmd.attr_mthd_list = qqq;
                   }
                   pt_push(this_parser, node);
		>>
	;

user__list "list of grantees"
	: user__name
          ( "," user__name << PLIST; >> )*
	;

users_ > [PT_NODE *users] "list of groups/members"
        : user__name ( "," user__name << PLIST; >> )*
        << $users = pt_pop(this_parser); >>
        ;

create_user_statement "create user statement"
        : << PT_NODE *create_user = parser_new_node(this_parser, PT_CREATE_USER);
             PT_NODE *password = NULL, *groups = NULL, *members = NULL; >>
          USER user__name
          { PASSWORD char_string_literal > [password] }
          { GROUPS users_ > [groups] }
          { MEMBERS users_  > [members] }
          << if (create_user) {
                create_user->info.create_user.user_name = pt_pop(this_parser);
                create_user->info.create_user.password = password;
                create_user->info.create_user.groups = groups;
                create_user->info.create_user.members = members;
             }
             pt_push(this_parser, create_user);
          >>
        ;

drop_user_statement "drop user statement"
        : << PT_NODE *drop_user = parser_new_node(this_parser, PT_DROP_USER); >>
          USER user__name
            << if (drop_user)
                drop_user->info.drop_user.user_name = pt_pop(this_parser);
               pt_push(this_parser, drop_user);
            >>
        ;

alter_user_statement "alter user statement"
        : << PT_NODE *alter_user = parser_new_node(this_parser, PT_ALTER_USER);
             PT_NODE *password = NULL; >>
          USER user__name PASSWORD char_string_literal > [password]
          << if (alter_user && password) {
                alter_user->info.alter_user.user_name = pt_pop(this_parser);
                alter_user->info.alter_user.password = password;
             }
             pt_push(this_parser, alter_user);
          >>
        ;

/* Call a method, i.e., execute it */

call_statement "call statement"
	:	<< PT_NODE *node_, *var;
		>>
	  CALL method_call
		<< node_ = pt_pop(this_parser);
		   if (node_)
	  	     node_->info.method_call.call_or_expr = PT_IS_CALL_STMT;
		>>

	  { ON_ call_target
		<< if(node_) node_->info.method_call.on_call_target=pt_pop(this_parser);>>
	  }
	  { into_clause > [var]
		<< if(node_) node_->info.method_call.to_return_var=var;>>
	  }

	  << pt_push(this_parser, node_); >>
	;

method_call "method call clause"
	: << PT_NODE *node_;
  	     node_=parser_new_node(this_parser, PT_METHOD_CALL);
	     if (node_)
   	       node_->info.method_call.call_or_expr = PT_IS_MTHD_EXPR;
	  >>

	method__name
		<< if(node_) node_->info.method_call.method_name=pt_pop(this_parser); >>
	  Left_Paren
	  { argument_value /* args optional, () arn't */
            ( "," argument_value << PLIST; >> )*
		<< if(node_) node_->info.method_call.arg_list=pt_pop(this_parser); >>
	  }
	  Right_Paren

	  << pt_push(this_parser, node_);
	     cannot_prepare = true;
	     cannot_cache = true;
	  >>
	;


call_target "call target clause"
	: primary
	;

argument_value "call argument clause"
	: expression_
	;

/* Data Definition Statements */

/* CREATE class schema */

create_class_statement "create class statement"
	: 	<< PT_NODE *qc, *e;
		   PT_MISC_TYPE t = PT_CLASS;
                   qc = parser_new_node(this_parser, PT_CREATE_ENTITY);
		>>
	  (CLASS << t=PT_CLASS; >>
	  | TABLE << t=PT_CLASS; >>
	  | TYPE << t=PT_ADT; >>)
	  class__name
		<< e = pt_pop(this_parser);
                   if(qc)
		   {
                     qc->info.create_entity.entity_name = e;
                     qc->info.create_entity.entity_type=t;
		   }
		>>
	  { subtable_clause
		<< if(qc) qc->info.create_entity.supclass_list =
			pt_pop(this_parser); >>
	  }
	  { class_attribute_definition_list
	  	<<  if(qc)qc->info.create_entity.class_attr_def_list = pt_pop(this_parser); >>
	  }
	  { Left_Paren class_or_normal_attribute_definition_list Right_Paren
		<< if(qc)qc->info.create_entity.attr_def_list = pt_pop(this_parser); >>
	  }
	  { method_definition_list
		<< if(qc)qc->info.create_entity.method_def_list= pt_pop(this_parser); >>
	  }
	  { method_files
		<< if(qc)qc->info.create_entity.method_file_list=pt_pop(this_parser); >>
	  }
	  { inherit_resolution_list
		<< if(qc)qc->info.create_entity.resolution_list= pt_pop(this_parser); >>
	  }
	  { partition_clause
		<< if(qc)qc->info.create_entity.partition_info= pt_pop(this_parser); >>
	  }
		<< /* At this point the attribute subtrees may have
		    * synthesized class constraints hanging off of them.
		    * Those constraints must be migrated up to the
		    * constraint_list member of the CREATE_ENTITY node.
		    * If they aren't, parser_print_tree() will print out
		    * syntactically incorrect SQL.
		    *
		    * At the same time, we need to gather any explicit
		    * class constraints that showed up in the attribute
		    * definition list and put those on the
		    * constraint_list member.
		    */
		   if (qc)
		     pt_gather_constraints(this_parser, qc);
		   pt_push(this_parser, qc);
		>>
	;

create_vclass_statement "create vclass statement"
	: 	<< PT_NODE *qc, *e;
                   qc = parser_new_node(this_parser, PT_CREATE_ENTITY);
		>>
	  (VIEW | VCLASS)
	  class__name
		<< e = pt_pop(this_parser);
                   if(qc)
		   {
                     qc->info.create_entity.entity_name = e;
                     qc->info.create_entity.entity_type= PT_VCLASS;
		   }
		   pt_push(this_parser, qc);
		>>
	  ( vclass_definition )
	  ;

vclass_definition "vclass definition"
	:   << PT_NODE *qc;
		PT_MISC_TYPE t = PT_CASCADED;
                   qc = pt_pop(this_parser);
	     >>

	  { subtable_clause
		<< if(qc) qc->info.create_entity.supclass_list =
			pt_pop(this_parser); >>
	  }
	  { class_attribute_definition_list
	  	<<  if(qc)qc->info.create_entity.class_attr_def_list =
	  		pt_pop(this_parser); >>
	  }
	  { Left_Paren view_attribute_definition_list Right_Paren
		<< if(qc)qc->info.create_entity.attr_def_list =
			pt_pop(this_parser); >>
	  }
	  { method_definition_list
		<< if(qc)qc->info.create_entity.method_def_list=
			pt_pop(this_parser); >>
	  }
	  { method_files
		<< if(qc)qc->info.create_entity.method_file_list=
			pt_pop(this_parser); >>
	  }
	  { inherit_resolution_list
		<< if(qc)qc->info.create_entity.resolution_list=
			pt_pop(this_parser); >>
	  }
	  { AS query_list
		<< if (qc)
		     qc->info.create_entity.as_query_list = pt_pop(this_parser);
		>>
	  }
	  { WITH { levels_clause > [t] } CHECK OPTION
		<< if (qc)
		     qc->info.create_entity.with_check_option = t;
		>>
	  }
          ( SEMI_COLON   /* the only place a semi-colon is REQUIRED */
	  | eof	  )

	   	<<
		   pt_gather_constraints(this_parser, qc);
		   pt_push(this_parser, qc);
		>>
	;

levels_clause > [PT_MISC_TYPE t]
	:
          LOCAL    << $t=PT_LOCAL; >>
        | CASCADED << $t=PT_CASCADED; >>
	;

query_specification "query specification"
	: << PT_NODE * query, *root_spec, *spec, *subquery; >>
	csql_query
	  <<	/* must resolve ambiguity with subquery derived tables.
		 * These will always be parsed as derived tables.
		 * However, if they do not have a correlation variable,
		 * the parse assumption is incorrect, and must be corrected
		 * here by removing the derived table from a from list,
		 * and making it into another query in this query list.
		 */
		query = pt_pop(this_parser);

		if (query && query->node_type == PT_SELECT
		    && !query->info.query.q.select.where
		    && !query->info.query.q.select.group_by
		    && !query->info.query.q.select.having
		    && !query->info.query.order_by
		    && (root_spec = query->info.query.q.select.from)
		    && (spec = root_spec->next)) {
		    /* could be the ambiguous case. Look for it */
		    	while (spec) {
			    if ((subquery = spec->info.spec.derived_table)
			    && (PT_IS_QUERY_NODE_TYPE(subquery->node_type))
			    && !spec->info.spec.range_var) {
			    	/* This is the case to patch up */
				/* detach spec. Only important first time. */
			    	root_spec->next = NULL;
			    	parser_append_node(subquery, query);
			    }
			    root_spec = spec;
			    spec = spec->next;
			}
		    }
		pt_push(this_parser, query);		>>
		;

query_list "query list"
	: query_specification
		("," query_specification << PLIST; >>)*
	;

/* Create class  and vclass component clauses */

inherit_resolution_list "inherit resolution list"
	: INHERIT inherit_resolution
	  ( "," inherit_resolution << PLIST; >> )*
	;

inherit_resolution "inheritance conflict resolution"
	: 	<< PT_MISC_TYPE t=PT_NORMAL;
                   PT_NODE *node=parser_new_node(this_parser, PT_RESOLUTION);
		>>
	  { CLASS  << t=PT_META_ATTR; >> }
          attribute_name OF identifier
		<<
                   if(node)
		   {
                     node->info.resolution.of_sup_class_name=pt_pop(this_parser);
                     node->info.resolution.attr_mthd_name=pt_pop(this_parser);
                     node->info.resolution.attr_type = t;
                   }
		>>
	  { AS attribute_name
		<< if(node)node->info.resolution.as_attr_mthd_name=pt_pop(this_parser);>>
          }
		<< pt_push(this_parser, node); >>
	;


subtable_clause "subtable clause"
	: (UNDER | AS SUBCLASS OF) superclass_list
	;

class_constraint "class constraint"
	: 	<< PT_NODE *name = NULL, *constraint;
		>>
	  { CONSTRAINT identifier << name = pt_pop(this_parser); >> }
	  (   unique_constraint
	    | foreign_key_constraint
	    | check_constraint
	  )
		<< constraint = pt_pop(this_parser);
		   if (constraint)
		     constraint->info.constraint.name = name;
		   pt_push(this_parser, constraint);
		>>
	  ( constraint_attribute )*
	;

unique_constraint "unique constraint"
	: 	<< PT_NODE *node, *attr_list;
		   long unique = 0;
		>>
	  (   PRIMARY KEY
		<< unique = 0; >>
	    | UNIQUE
		<< unique = 1; >>
	  )
	  Left_Paren attribute_list
		<< attr_list = pt_pop(this_parser); >>
	  Right_Paren
		<< node = parser_new_node(this_parser, PT_CONSTRAINT);
		   if (node)
		   {
		     node->info.constraint.type
		       = unique ? PT_CONSTRAIN_UNIQUE : PT_CONSTRAIN_PRIMARY_KEY;
		     node->info.constraint.un.unique.attrs = attr_list;
		   }
		   pt_push(this_parser, node);
		>>
	;

foreign_key_constraint "foreign key constraint"
	:	<< PT_NODE *attrs, *node;
		   node = parser_new_node(this_parser, PT_CONSTRAINT);
		   if (node) {
		     node->info.constraint.type = PT_CONSTRAIN_FOREIGN_KEY;
		   }
		   pt_push(this_parser, node);
		>>
	  FOREIGN KEY
	  Left_Paren attribute_list
		<< attrs = pt_pop(this_parser);
		   if (node) {
		      node->info.constraint.un.foreign_key.attrs = attrs;
		   }
		>>
	  Right_Paren
	  references_spec[node]
	;

references_spec < [PT_NODE *node] "references specification"
	:	<< PT_NODE *name, *attrs = NULL, *cache_attr = NULL;
		   PT_MISC_TYPE match_type = PT_MATCH_REGULAR;
		   PT_MISC_TYPE delete_action = PT_RULE_RESTRICT;
		   PT_MISC_TYPE update_action = PT_RULE_RESTRICT;
		>>
	  REFERENCES class__name
		<< name = pt_pop(this_parser);
		   if (node)
		     node->info.constraint.un.foreign_key.referenced_class = name;
		>>
	  { Left_Paren attribute_list
		<< attrs = pt_pop(this_parser); >>
	    Right_Paren
	  }
	  { ( delete_rule > [delete_action]
              {( update_rule > [update_action] { cache_rule > [cache_attr] })
             | ( cache_rule > [cache_attr]  { update_rule > [update_action] })
              })
	  | ( update_rule > [update_action]
              {( delete_rule > [delete_action] { cache_rule > [cache_attr] })
             | ( cache_rule > [cache_attr] { delete_rule > [delete_action] })
              })
	  | ( cache_rule > [cache_attr]
              {( update_rule > [update_action] { delete_rule > [delete_action] })
             | ( delete_rule > [delete_action] { update_rule > [update_action] })
              })
	  }
		<< if (node) {
		     node->info.constraint.un.foreign_key.referenced_attrs = attrs;
		     node->info.constraint.un.foreign_key.match_type = match_type;
		     node->info.constraint.un.foreign_key.delete_action = delete_action;
		     node->info.constraint.un.foreign_key.update_action = update_action;
		     node->info.constraint.un.foreign_key.cache_attr = cache_attr;
		   }
		>>
	;

delete_rule > [PT_MISC_TYPE action] "delete rule"
	: ON_ DELETE delete_action > [$action]
	;

update_rule > [PT_MISC_TYPE action] "update rule"
	: ON_ UPDATE update_action > [$action]
	;

cache_rule > [PT_NODE *attr] "object cache rule"
	: ON_ CACHE OBJECT attribute_name
                << $attr = pt_pop(this_parser); >>
	;

delete_action > [PT_MISC_TYPE action] "delete action"
	: CASCADE     << $action = PT_RULE_CASCADE; >>
	| NO ACTION   << $action = PT_RULE_NO_ACTION; >>
	| RESTRICT    << $action = PT_RULE_RESTRICT; >>
	;

update_action > [PT_MISC_TYPE action] "update action"
	: NO ACTION   << $action = PT_RULE_NO_ACTION; >>
	| RESTRICT    << $action = PT_RULE_RESTRICT; >>
	;
check_constraint "check constraint"
	:	<< PT_NODE *expr, *node; >>
	  CHECK Left_Paren search_condition
		<< expr = pt_pop(this_parser); >>
	  Right_Paren
		<< node = parser_new_node(this_parser, PT_CONSTRAINT);
		   if (node)
		   {
		     node->info.constraint.type = PT_CONSTRAIN_CHECK;
		     node->info.constraint.un.check.expr = expr;
		   }
		   pt_push(this_parser, node);
		>>
	;

constraint_attribute "constraint attribute"
	:	<< long deferrable = 1, initially_deferred = 0;
		   PT_NODE *constraint;
		   constraint = pt_pop(this_parser);
		>>
	(
	  { NOT << deferrable = 0; >> } DEFERRABLE
		<< if (constraint)
		     constraint->info.constraint.deferrable = deferrable;
		>>
	| INITIALLY ( DEFERRED << initially_deferred = 1; >>
		    | IMMEDIATE )
		<< if (constraint)
		     constraint->info.constraint.initially_deferred
		       = initially_deferred;
		>>
	)
		<< pt_push(this_parser, constraint);
		>>
	;

/* Method definition productions */

method_definition_list "method definition list"
	: METHOD method_definition ( "," method_definition << PLIST; >> )*
	;

method_definition "method definition clause"
	:	<< PT_NODE *dt, *node=parser_new_node(this_parser, PT_METHOD_DEF);
		   PT_MISC_TYPE t=PT_NORMAL; PT_TYPE_ENUM typ;
		>>
	  { CLASS  << t=PT_META_ATTR; >> }
	  method__name
		<<
                   if(node)
		   {
                     node->info.method_def.method_name = pt_pop(this_parser);
                     node->info.method_def.mthd_type = t;
		   }
		>>
	  { Left_Paren
	    { arg_type_list
                <<if(node) node->info.method_def.method_args_list = pt_pop(this_parser);>>
	    }
	    Right_Paren
          }
          { data__type >[typ, dt]
                <<if(node) { node->type_enum = typ; node->data_type = dt; }>>
          }
          { FUNCTION identifier
                <<if(node) node->info.method_def.function_name = pt_pop(this_parser);>>
          }
		<< pt_push(this_parser, node); >>
	;
arg_type_list "method argument list"
	: 	<< PT_TYPE_ENUM typ; PT_NODE *dt, *at; PT_MISC_TYPE inout; >>
	   in_out > [inout]
	   data__type >[typ, dt]
		<< at = parser_new_node(this_parser, PT_DATA_TYPE);
		   if (at) {
		   	at->type_enum = typ;
		   	at->data_type = dt;
		   	at->info.data_type.inout = inout;
		   }
		   pt_push(this_parser, at);
		>>
	  ("," in_out > [ inout ]
	       data__type >[typ, dt]
		<< at = parser_new_node(this_parser, PT_DATA_TYPE);
		   if (at) {
		   	at->type_enum = typ;
		   	at->data_type = dt;
		   	at->info.data_type.inout = inout;
		   }
		   pt_push(this_parser, at); PLIST;
		>>
	  )*
	;

method_files "method implementation file list"
	: File file_path_name ( "," file_path_name << PLIST; >> )*
	;


/* Attribute definition productions */

class_attribute_definition_list "class attribute definition list"
	:  CLASS ATTRIBUTE Left_Paren
		attribute_definition[PT_META_ATTR]
	   ("," attribute_definition[PT_META_ATTR] << PLIST; >> )*
		Right_Paren
	;

class_or_normal_attribute_definition_list "attribute definition list"
	:  (attribute_definition[PT_NORMAL] | class_attribute_definition)
      ("," (attribute_definition[PT_NORMAL] | class_attribute_definition)
		 << PLIST; >> )*
	;

view_attribute_definition_list "attribute definition list"
	:  (attribute_definition[PT_NORMAL] | view_attribute_definition)
      ("," (attribute_definition[PT_NORMAL] | view_attribute_definition)
		 << PLIST; >> )*
	;

attribute_definition_list < [PT_MISC_TYPE normal_or_class] "attribute definition list"
	:  (attribute_definition[normal_or_class] )
      ("," (attribute_definition[normal_or_class] )
		 << PLIST; >> )*
	;

attribute_definition < [PT_MISC_TYPE normal_or_class] "attribute definition clause"
	:	class_constraint
	|	attr_definition [normal_or_class]
	;

attr_definition < [PT_MISC_TYPE normal_or_class] "attribute definition clause"
	:	<< PT_NODE *data_default, *auto_increment_node, *node, *dt;
		   PT_TYPE_ENUM typ;
                   PT_NODE *auto_increment_start_val,
                           *auto_increment_inc_val;
                   node = parser_new_node(this_parser, PT_ATTR_DEF);
                   auto_increment_node = auto_increment_start_val = auto_increment_inc_val = 0;
		>>
          attribute_name data__type >[typ, dt]
		<<
                   if(node)
		   { node->type_enum=typ; node->data_type=dt;
                     node->info.attr_def.attr_name=pt_pop(this_parser);
                     node->info.attr_def.attr_type=normal_or_class;
		     if (typ==PT_TYPE_CHAR && dt)
		       node->info.attr_def.size_constraint=
			 dt->info.data_type.precision;
                     /* case of TEXT typed attribute */
                     if (typ==PT_TYPE_OBJECT &&
                           dt && dt->type_enum==PT_TYPE_VARCHAR) {
                       node->type_enum=dt->type_enum;
                       PT_NAME_INFO_SET_FLAG(node->info.attr_def.attr_name,
                         PT_NAME_INFO_EXTERNAL);
                     }
                   }
		>>
          << #if 0 /* to disable TEXT */ >>
          {EXTERNAL
                <<
                   if (node) {
                     if (node->type_enum == PT_TYPE_VARCHAR) {
                       PT_NAME_INFO_SET_FLAG(node->info.attr_def.attr_name,
                         PT_NAME_INFO_EXTERNAL);
                     }
                   }
                >>
          }
         << #endif /* 0 */ >>
	  {shared_or_default_clause
                << data_default = pt_pop(this_parser);
		   if(node && data_default) {
		     node->info.attr_def.data_default=data_default;
		     if (data_default->info.data_default.shared == PT_SHARED)
		       node->info.attr_def.attr_type = PT_SHARED;
		   }
		>>
           | AUTO_INCREMENT
               <<
                   if (normal_or_class == PT_META_ATTR) {
                     PT_ERRORm(this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
                             MSGCAT_SEMANTIC_CLASS_ATT_CANT_BE_AUTOINC);
                   }
               >>
           { Left_Paren serial_integer_literal>[auto_increment_start_val]
                    "," serial_integer_literal>[auto_increment_inc_val]
             Right_Paren
           }
               <<
                   auto_increment_node = parser_new_node(this_parser,
                           PT_AUTO_INCREMENT);
                   if (node && auto_increment_node) {
                     auto_increment_node->info.auto_increment.start_val = auto_increment_start_val;
                     auto_increment_node->info.auto_increment.increment_val = auto_increment_inc_val;
                     node->info.attr_def.auto_increment = auto_increment_node;
                   }
                >>
          }
		<< pt_push(this_parser, node);
		>>
	  ( column_constraint_definition[node]
		<< PLIST;
		>>
	  )*
	;

column_constraint_definition < [PT_NODE *column] "column constraint definition"
	: << PT_NODE *name;
	     name = NULL;
	  >>
	  { CONSTRAINT identifier << name = pt_pop(this_parser); >> }
	  column_constraint[column, name]
	  ( constraint_attribute )*
	;

column_constraint < [PT_NODE *column, PT_NODE *constraint_name] "column constraint"
    :   << PT_NODE *expr, *constraint;
           constraint = parser_new_node(this_parser, PT_CONSTRAINT);
        >>
    ( UNIQUE
        <<
           if (column && constraint)
           {
             constraint->info.constraint.type = PT_CONSTRAIN_UNIQUE;
             constraint->info.constraint.un.unique.attrs
               = parser_copy_tree(this_parser, column->info.attr_def.attr_name);
             constraint->info.constraint.un.unique.attrs->info.name.meta_class
               = column->info.attr_def.attr_type;
           }
        >>
    | PRIMARY KEY
        <<
           if (column && constraint)
           {
             constraint->info.constraint.type = PT_CONSTRAIN_PRIMARY_KEY;
             constraint->info.constraint.un.unique.attrs
               = parser_copy_tree(this_parser, column->info.attr_def.attr_name);
           }
        >>
    | Null
        <<
	   /* to support null in ODBC-DDL, ignore it. */
	   ;
        >>
    | NOT Null
        <<
           if (column && constraint)
           {
             constraint->info.constraint.type = PT_CONSTRAIN_NOT_NULL;
             constraint->info.constraint.un.not_null.attr
               = parser_copy_tree(this_parser, column->info.attr_def.attr_name);
             /*
              * This should probably be deferred until semantic
              * analysis time; leave it this way for now.
              */
             column->info.attr_def.constrain_not_null = 1;
           }
        >>
    | CHECK Left_Paren search_condition Right_Paren
        <<
           expr = pt_pop(this_parser);
           if (column && constraint)
           {
             constraint->info.constraint.type = PT_CONSTRAIN_CHECK;
             constraint->info.constraint.un.check.expr = expr;
           }
        >>
    | FOREIGN KEY references_spec[constraint]
        <<
           if (column && constraint)
           {
             constraint->info.constraint.type = PT_CONSTRAIN_FOREIGN_KEY;
             constraint->info.constraint.un.foreign_key.attrs
               = parser_copy_tree(this_parser, column->info.attr_def.attr_name);
           }
        >>
    )
        << if (constraint)
             constraint->info.constraint.name = constraint_name;
           pt_push(this_parser, constraint);
        >>
    ;

class_attribute_definition "class attribute definition clause"
	:
	  CLASS attribute_definition[PT_META_ATTR]
	;

view_attribute_definition "view attribute definition clause"
	:	<< PT_NODE *vad = parser_new_node(this_parser, PT_ATTR_DEF);
		>>
          attribute_name
		<<
                   if(vad)
		   { vad->data_type=NULL;
                     vad->info.attr_def.attr_name=pt_pop(this_parser);
                     vad->info.attr_def.attr_type=PT_NORMAL;
                   }
                   pt_push(this_parser, vad);
		>>
	;

shared_or_default_clause "shared or default value clause"
	: 	<< PT_MISC_TYPE shared=PT_DEFAULT;
		   PT_NODE *node=parser_new_node(this_parser, PT_DATA_DEFAULT);
		>>
	  ( SHARED  (default__value | no_default_value) <<shared=PT_SHARED;>>
	  | DEFAULT (default__value | no_default_value)
	  )
		<<
                   if (node)
		   {
		     node->info.data_default.default_value=pt_pop(this_parser);
		     node->info.data_default.shared=shared;
		   }
		   pt_push(this_parser, node);
		>>
	;

default__value "default value"
	: expression_
	;

no_default_value
	:	<< PT_NODE *node = parser_new_node(this_parser, PT_VALUE);
                   if (node) node->type_enum = PT_TYPE_NULL;
                   pt_push(this_parser, node);
		>>
	;

/* Driver Cache parameter get/set statements */

driver_arg_spec
	:	<< PT_NODE *val = parser_new_node(this_parser, PT_VALUE); >>
	(
	  unsigned_integer >[val]
	| parameter_       >[val]
	| host_parameter[&input_host_index] >[val]
	)
	 	<< pt_push(this_parser, val); >>
	;

/* Transaction-related statements */
set_transaction_statement "set transaction statement"
	: 	<< PT_NODE *modes, *st = parser_new_node(this_parser, PT_SET_XACTION); >>
	  SET TRANSACTION transaction_mode
	  ("," transaction_mode <<PLIST;>>)*
		<< modes = pt_pop(this_parser);
		   if (st) st->info.set_xaction.xaction_modes = modes;
		   pt_push(this_parser, st);
		>>
	;

transaction_mode "transaction mode"
	: 	<< PT_NODE *tm, *is; >>
	(	<< tm = parser_new_node(this_parser, PT_ISOLATION_LVL); >>
	  ISOLATION LEVEL isolation_level_spec[tm]
                          { << is = parser_new_node(this_parser, PT_ISOLATION_LVL); >>
                            "," isolation_level_spec[is]
                 << if (tm && is) {
                      if (tm->info.isolation_lvl.async_ws) {
                        if (is->info.isolation_lvl.async_ws) {
                          /* async_ws, async_ws */
                        } else {
                          /* async_ws, iso_lvl */
                          tm->info.isolation_lvl.schema    = is->info.isolation_lvl.schema;
                          tm->info.isolation_lvl.instances = is->info.isolation_lvl.instances;
                          tm->info.isolation_lvl.level     = is->info.isolation_lvl.level;
                        }
                      } else {
                        if (is->info.isolation_lvl.async_ws) {
                          /* iso_lvl, async_ws */
                          tm->info.isolation_lvl.async_ws = 1;
                        } else {
                          /* iso_lvl, iso_lvl */
                          if (tm->info.isolation_lvl.level != NULL ||
                              is->info.isolation_lvl.level != NULL)
                            PT_ERRORm(this_parser, tm,
                            		  MSGCAT_SET_PARSER_SEMANTIC,
                                      MSGCAT_SEMANTIC_GT_1_ISOLATION_LVL);
                          else
                          if (tm->info.isolation_lvl.schema    != is->info.isolation_lvl.schema ||
                              tm->info.isolation_lvl.instances != is->info.isolation_lvl.instances)
                            PT_ERRORm(this_parser, tm,
                            		  MSGCAT_SET_PARSER_SEMANTIC,
                                      MSGCAT_SEMANTIC_GT_1_ISOLATION_LVL);
                        }
                      }
                    }
                    if (is) {
                      is->info.isolation_lvl.level = NULL;
                      parser_free_node(this_parser, is);
                    }
                 >>
                          }
	| 	<< tm = parser_new_node(this_parser, PT_TIMEOUT); >>
	  LOCK TIMEOUT timeout_spec[tm]
	)	<< pt_push(this_parser, tm); >>
	;

isolation_level_spec < [PT_NODE *isol] "isolation level specification"
	: expression_
		<< if (isol) isol->info.isolation_lvl.level = pt_pop(this_parser); >>
	|
	  (	<< if (isol) {
                     isol->info.isolation_lvl.schema=PT_NO_ISOLATION_LEVEL;
                     isol->info.isolation_lvl.instances=PT_NO_ISOLATION_LEVEL;
		   }
		>>
	    ASYNC WORKSPACE
		<< if (isol) isol->info.isolation_lvl.async_ws = 1; >>
	  | SERIALIZABLE
		<<
		   if (isol)
                   {
                     isol->info.isolation_lvl.schema=PT_SERIALIZABLE;
		     isol->info.isolation_lvl.instances=PT_SERIALIZABLE;
		   }
		>>
	  | CURSOR STABILITY
		<< if (isol) isol->info.isolation_lvl.instances=PT_READ_COMMITTED; >>
	  | 	<< PT_MISC_TYPE lvl; >>
	    isolation_level_name >[lvl]
		<< switch(lvl)
		   { default:
		     case PT_REPEATABLE_READ:
		       break;
		     case PT_READ_COMMITTED:
		     case PT_READ_UNCOMMITTED:
		       if (isol) isol->info.isolation_lvl.instances=lvl;
		       break;
		   }
		>>
	    { (SCHEMA | CLASS)
		<< if (isol)
		   { isol->info.isolation_lvl.schema=lvl;
		     if (lvl == PT_READ_UNCOMMITTED)
		     { isol->info.isolation_lvl.schema=PT_READ_COMMITTED;
		       PT_ERRORm(this_parser, isol, MSGCAT_SET_PARSER_SYNTAX,
		                 MSGCAT_SYNTAX_READ_UNCOMMIT);
		     }
		     isol->info.isolation_lvl.instances=PT_NO_ISOLATION_LEVEL;
		   }
		>>
	      { "," isolation_level_name >[lvl] INSTANCES
		<< if (isol) isol->info.isolation_lvl.instances=lvl; >>
	      }
	    | INSTANCES
		<< if (isol)
		   { isol->info.isolation_lvl.instances=lvl;
		     isol->info.isolation_lvl.schema=PT_NO_ISOLATION_LEVEL;
		   }
		>>
	      { "," isolation_level_name >[lvl] (SCHEMA | CLASS)
		<< if (isol)
		   { isol->info.isolation_lvl.schema=lvl;
		     if (lvl == PT_READ_UNCOMMITTED)
		     { isol->info.isolation_lvl.schema=PT_READ_COMMITTED;
		       PT_ERRORm(this_parser, isol, MSGCAT_SET_PARSER_SYNTAX,
			             MSGCAT_SYNTAX_READ_UNCOMMIT);
		     }
		   }
		>>
	      }
	    }
	  )
	;

isolation_level_name > [PT_MISC_TYPE lvl] "isolation level name"
	: REPEATABLE READ  << $lvl = PT_REPEATABLE_READ;  >>
	| READ COMMITTED   << $lvl = PT_READ_COMMITTED;   >>
	| READ UNCOMMITTED << $lvl = PT_READ_UNCOMMITTED; >>
	;

timeout_spec < [PT_NODE *tim] "timeout specification"
	: 	<< PT_NODE *val; long i; >>
	( (	<< val = parser_new_node(this_parser, PT_VALUE);
		   if (val) val->type_enum = PT_TYPE_INTEGER;
		>>
	    ( INFINITE << i = -1; >>
	    | OFF_      << i =  0; >>
	    )	<< if (val) val->info.value.data_value.i = i; >>
	  )
	| unsigned_integer >[val]
	| unsigned_real >[val]
	| parameter_ >[val]
	| host_parameter[&input_host_index] >[val]
	) 	<< if (tim) tim->info.timeout.val = val; >>
	;

get_transaction_statement "get transaction statement"
	:	<< PT_NODE *parm; PT_MISC_TYPE opt;
		   PT_NODE *gt = parser_new_node(this_parser, PT_GET_XACTION);
		>>
	  GET TRANSACTION get_transaction_option >[opt]
	  { into_clause >[parm]
		<< if (gt) gt->info.get_xaction.into_var = parm; >>
	  }	<< if (gt) gt->info.get_xaction.option = opt;
		   pt_push(this_parser, gt);
		>>
	;

get_transaction_option > [PT_MISC_TYPE opt] "get transaction option"
	: ISOLATION LEVEL << $opt=PT_ISOLATION_LEVEL; >>
	| LOCK TIMEOUT	  << $opt=PT_LOCK_TIMEOUT;    >>
	;

commit_statement "commit statement"
	: << PT_NODE *comm = parser_new_node(this_parser, PT_COMMIT_WORK); >>
	  COMMIT {WORK} { RETAIN LOCK
                          << if (comm)
                               comm->info.commit_work.retain_lock = 1;
                          >>
                        }
          << pt_push(this_parser, comm); >>
	;

rollback_statement "rollback statement"
        :   << PT_NODE *roll = parser_new_node(this_parser, PT_ROLLBACK_WORK); >>
          ROLLBACK {WORK}
          { TO {SAVEPOINT} expression_
            << if (roll)
                 roll->info.rollback_work.save_name = pt_pop(this_parser);
            >>
          }
          << pt_push(this_parser, roll); >>
	;

savepoint_statement "savepoint statement"
        :   << PT_NODE *svpt = parser_new_node(this_parser, PT_SAVEPOINT); >>
          SAVEPOINT expression_
            << if (svpt)
                    svpt->info.savepoint.save_name = pt_pop(this_parser);
            >>
          << pt_push(this_parser, svpt); >>
	;

prepare_to_commit_statement "prepare to commit statement"
	: 	<< PT_NODE *tid, *prep=parser_new_node(this_parser, PT_PREPARE_TO_COMMIT); >>
	  PREPARE {TO} COMMIT unsigned_integer > [tid]
		<< if (prep && tid)
		     prep->info.prepare_to_commit.trans_id = tid->info.value.data_value.i;
		   pt_push(this_parser, prep);
		>>
	;

attach_statement "attach statement"
	: 	<< PT_NODE *tid, *att=parser_new_node(this_parser, PT_ATTACH); >>
	  ATTACH unsigned_integer > [tid]
		<< if (att && tid)
		     att->info.attach.trans_id = tid->info.value.data_value.i;
		   pt_push(this_parser, att);
		>>
	;

/* Trigger-related statements */
scope_statement "scope statement"
	:	<< PT_NODE *from, *stmt,
                   *scope=parser_new_node(this_parser, PT_SCOPE);
                   from=stmt=(PT_NODE *)0; >>
	  SCOPE trigger__action > [stmt]
          {
          FROM table_specification_list << from=pt_pop(this_parser); >>
          }
		<< if (scope) {
		     scope->info.scope.stmt = stmt;
                     scope->info.scope.from = from;
                   }
		   pt_push(this_parser, scope);
		>>
	;

evaluate_statement "evaluate statement"
	:	<< PT_NODE *expr, *eval=parser_new_node(this_parser, PT_EVALUATE);
		   PT_NODE *param = NULL;
		>>
	  EVALUATE expression_ << expr=pt_pop(this_parser); >>
		<< if (eval && expr)
		     eval->info.evaluate.expression = expr;
		   pt_push(this_parser, eval);
		>>
	  { into_clause > [param]
	  	<< if(eval) eval->info.evaluate.into_var = param;
	  	>>
	  }
	;

create_trigger_statement "create trigger statement"
	: 	<< PT_NODE *trig, *nam, *prio, *event, *ref, *cond, *act;
		   PT_MISC_TYPE status, ctim, atim;
		   trig=parser_new_node(this_parser, PT_CREATE_TRIGGER);
		   status=ctim=atim=PT_MISC_DUMMY; prio=ref=cond=NULL;
		>>
	  TRIGGER identifier << nam=pt_pop(this_parser); >>
	  { STATUS trigger__status >[status] }
	  { PRIORITY unsigned_real >[prio] }
	  trigger_time >[ctim] event_specification << event=pt_pop(this_parser); >>
	  { IF trigger__condition << cond=pt_pop(this_parser); >> }
	  EXECUTE { trigger_action_time >[atim] }
	  trigger__action >[act]
		<< if (trig)
		   { trig->info.create_trigger.trigger_name=nam;
		     trig->info.create_trigger.trigger_status=status;
		     trig->info.create_trigger.trigger_priority=prio;
		     trig->info.create_trigger.condition_time=ctim;
		     trig->info.create_trigger.trigger_event=event;
		     trig->info.create_trigger.trigger_reference=ref;
		     trig->info.create_trigger.trigger_condition=cond;
		     trig->info.create_trigger.action_time=atim;
		     trig->info.create_trigger.trigger_action=act;
		   }
		   pt_push(this_parser, trig);
		>>
	;

trigger__status > [PT_MISC_TYPE status] "trigger status"
	: ACTIVE   << $status=PT_ACTIVE;   >>
	| INACTIVE << $status=PT_INACTIVE; >>
	;

trigger_time > [PT_MISC_TYPE t] "trigger time"
	: BEFORE	<< $t=PT_BEFORE;  >>
	| AFTER		<< $t=PT_AFTER;   >>
	| DEFERRED	<< $t=PT_DEFERRED;>>
	;

trigger_action_time > [PT_MISC_TYPE t] "trigger action time"
	: AFTER		<< $t=PT_AFTER;   >>
	| DEFERRED	<< $t=PT_DEFERRED;>>
	;

event_specification "event specification"
	: 	<< PT_EVENT_TYPE typ; PT_NODE *ev, *tgt=NULL;
		   ev=parser_new_node(this_parser, PT_EVENT_SPEC);
		>>
	  event__type >[typ] { event__target >[tgt] }
		<< if (ev)
		   { ev->info.event_spec.event_type=typ;
		     ev->info.event_spec.event_target=tgt;
		   }
		   pt_push(this_parser, ev);
		>>
	;

/*
   ALTER, DROP, ABORT, & TIMEOUT have been (perhaps temporarily removed from
   the grammar although PT_EV_ constants still exist for them.
 */
event__type > [PT_EVENT_TYPE t] "event type"
	: INSERT		<< $t=PT_EV_INSERT; 	 >>
	| STATEMENT INSERT	<< $t=PT_EV_STMT_INSERT; >>
	| DELETE		<< $t=PT_EV_DELETE;	 >>
	| STATEMENT DELETE	<< $t=PT_EV_STMT_DELETE; >>
	| UPDATE		<< $t=PT_EV_UPDATE;	 >>
	| STATEMENT UPDATE	<< $t=PT_EV_STMT_UPDATE; >>
	| COMMIT		<< $t=PT_EV_COMMIT;	 >>
	| ROLLBACK		<< $t=PT_EV_ROLLBACK;	 >>
	;

event__target > [PT_NODE *tgt] "event target"
	:	<< PT_NODE *cls=NULL, *att=NULL;
		   $tgt=parser_new_node(this_parser, PT_EVENT_TARGET);
		>>
	ON_ identifier << cls=pt_pop(this_parser); >>
	{ Left_Paren
	  attribute_name << att=pt_pop(this_parser); >>
	  Right_Paren
	}
		<< if ($tgt)
		   { $tgt->info.event_target.class_name=cls;
		     $tgt->info.event_target.attribute=att;
		   }
		>>
	;

trigger__condition "trigger condition"
	: search_condition
	| call_statement
	;

trigger__action > [PT_NODE *act] "trigger action"
	:	<< PT_NODE *str; $act=parser_new_node(this_parser, PT_TRIGGER_ACTION); >>
	( REJECT
		<< if($act) $act->info.trigger_action.action_type=PT_REJECT; >>
	| INVALIDATE TRANSACTION
		<< if($act) $act->info.trigger_action.action_type=PT_INVALIDATE_XACTION; >>
	| PRINT char_string_literal > [str]
		<< if($act) {
		     $act->info.trigger_action.action_type = PT_PRINT;
   	      	     $act->info.trigger_action.string = str;
		   }
		>>
	| evaluate_statement
		<< if($act) {
		     $act->info.trigger_action.action_type=PT_EXPRESSION;
		     $act->info.trigger_action.expression=pt_pop(this_parser);
		   }
		>>
	| insert_statement
		<< if($act) {
		     $act->info.trigger_action.action_type=PT_EXPRESSION;
		     $act->info.trigger_action.expression=pt_pop(this_parser);
		   }
		>>
	| update_statement
		<< if($act) {
		     $act->info.trigger_action.action_type=PT_EXPRESSION;
		     $act->info.trigger_action.expression=pt_pop(this_parser);
		   }
		>>
	| delete_statement
		<< if($act) {
		     $act->info.trigger_action.action_type=PT_EXPRESSION;
		     $act->info.trigger_action.expression=pt_pop(this_parser);
		   }
		>>
	| call_statement
		<< if($act) {
		     $act->info.trigger_action.action_type=PT_EXPRESSION;
		     $act->info.trigger_action.expression=pt_pop(this_parser);
		   }
		>>
	)
	;

alter_trigger_statement "alter trigger statement"
	: 	<< PT_NODE *alt, *spec, *pri; PT_MISC_TYPE status;
		   alt=parser_new_node(this_parser, PT_ALTER_TRIGGER);
		>>
	  TRIGGER trigger_name_list >[spec]
	  trigger_status_or_priority >[status,pri]
		<< if (alt)
		   { alt->info.alter_trigger.trigger_spec_list=spec;
		     alt->info.alter_trigger.trigger_status=status;
		     alt->info.alter_trigger.trigger_priority=pri;
		   }
		   pt_push(this_parser, alt);
		>>
	;

trigger_name_list > [PT_NODE *s] "trigger name list"
	:	<< $s=parser_new_node(this_parser, PT_TRIGGER_SPEC_LIST); >>
	identifier ("," identifier <<PLIST;>>)*
		<< if($s) $s->info.trigger_spec_list.trigger_name_list=pt_pop(this_parser); >>
	;

trigger_specification_list > [PT_NODE *s] "trigger specification_list"
	:	<< $s=parser_new_node(this_parser, PT_TRIGGER_SPEC_LIST); >>
	( identifier ("," identifier <<PLIST;>>)*
		<< if($s) $s->info.trigger_spec_list.trigger_name_list=pt_pop(this_parser); >>
	| ALL TRIGGERS
		<< if($s) $s->info.trigger_spec_list.all_triggers=1; >>
	)
	;

trigger_status_or_priority > [PT_MISC_TYPE s, PT_NODE *p] "trigger status or priority"
	: STATUS trigger__status >[$s] <<$p=NULL;>>
	| PRIORITY unsigned_real >[$p] <<$s=PT_MISC_DUMMY;>>
	;

drop_trigger_statement "drop trigger statement"
	: 	<< PT_NODE *s, *drp=parser_new_node(this_parser, PT_DROP_TRIGGER); >>
	TRIGGER trigger_name_list >[s]
		<< if (drp) drp->info.drop_trigger.trigger_spec_list=s;
		   pt_push(this_parser, drp);
		>>
	;

/* Formerly REMOVE, changed to DROP DEFERRED so we don't have
   to reserve yet another keyword */
drop_deferred_statement "drop deferred trigger statement"
	: 	<< PT_NODE *s, *r=parser_new_node(this_parser, PT_REMOVE_TRIGGER); >>
	DEFERRED TRIGGER trigger_specification_list >[s]
		<< if (r) r->info.remove_trigger.trigger_spec_list=s;
		   pt_push(this_parser, r);
		>>
	;

execute_deferred_trigger_statement "execute deferred trigger statement"
	: 	<< PT_NODE *s, *ex=parser_new_node(this_parser, PT_EXECUTE_TRIGGER); >>
	  EXECUTE DEFERRED TRIGGER trigger_specification_list >[s]
		<< if (ex) ex->info.execute_trigger.trigger_spec_list=s;
		   pt_push(this_parser, ex);
		>>
	;

rename_trigger_statement "rename trigger statement"
	:	<< PT_NODE *node=parser_new_node(this_parser, PT_RENAME_TRIGGER);
		>>
	  TRIGGER class__name
	  AS class__name
		<<
                    if (node)
		    {
			node->info.rename_trigger.new_name = pt_pop(this_parser);
                    	node->info.rename_trigger.old_name = pt_pop(this_parser);
                    }
                    pt_push(this_parser, node);
                >>
	;

get_trigger_statement "get trigger statement"
	:	<< PT_NODE *parm; PT_MISC_TYPE opt;
		   PT_NODE *gt = parser_new_node(this_parser, PT_GET_TRIGGER);
		>>
	  GET TRIGGER get_trigger_option >[opt]
	  { into_clause >[parm]
		<< if (gt) gt->info.get_trigger.into_var = parm; >>
	  }	<< if (gt) gt->info.get_trigger.option = opt;
		   pt_push(this_parser, gt);
		>>
	;

get_trigger_option > [PT_MISC_TYPE opt] "get trigger option"
	: TRACE << $opt=PT_TRIGGER_TRACE; >>
	| {MAXIMUM} DEPTH  << $opt=PT_TRIGGER_DEPTH;    >>
	;

set_trigger_statement "set trigger statement"
	: 	<< PT_NODE *st = parser_new_node(this_parser, PT_SET_TRIGGER); >>
	  SET TRIGGER
	  ( TRACE trace_spec[st]
		<< if (st) st->info.set_trigger.option = PT_TRIGGER_TRACE; >>
	    | {MAXIMUM} DEPTH depth_spec[st]
		<< if (st) st->info.set_trigger.option = PT_TRIGGER_DEPTH; >>
	  )
		<< pt_push(this_parser, st); >>
	;

trace_spec < [PT_NODE *st] "trigger trace specification"
	:	<< PT_NODE *val; >>
	( 	<< val = parser_new_node(this_parser, PT_VALUE); >>
	  ( ON_	<< if (val) val->info.value.data_value.i = -1; >>
	  | OFF_	<< if (val) val->info.value.data_value.i =  0; >>
	  )	<< if (val) val->type_enum = PT_TYPE_INTEGER;  >>
	| unsigned_integer >[val]
	| parameter_	   >[val]
	| host_parameter[&input_host_index] >[val]
	)	<< if (st) st->info.set_trigger.val = val; >>
	;

depth_spec < [PT_NODE *st] "trigger depth specification"
	:	<< PT_NODE *val; >>
	( 	<< val = parser_new_node(this_parser, PT_VALUE); >>
          INFINITE << if (val) { val->info.value.data_value.i = -1;
  			     val->type_enum = PT_TYPE_INTEGER; }  >>
	| unsigned_integer >[val]
	| parameter_	   >[val]
	| host_parameter[&input_host_index] >[val]
	)	<< if (st) st->info.set_trigger.val = val; >>
	;

/* SERIAL */

create_serial_statement "create serial statement"
	:	<< 	PT_NODE *stmt, *serial_name, *increment_val, *start_val;
			PT_NODE *max_val, *min_val;
			int cyclic = 0, no_cyclic = 0, no_max = 0, no_min = 0;
			stmt = serial_name = increment_val = start_val = max_val = min_val = 0;
		>>
		SERIAL identifier << serial_name = pt_pop(this_parser); >>
		{ START_ WITH serial_integer_literal > [start_val] }
		{ INCREMENT BY serial_integer_literal > [increment_val] }

		{
		 ( (MINVALUE serial_integer_literal > [min_val]
				| NOMINVALUE << no_min = 1; >> )
		   {(MAXVALUE serial_integer_literal > [max_val]
				| NOMAXVALUE << no_max = 1; >> )} )
		|
		 ( (MAXVALUE serial_integer_literal > [max_val]
				| NOMAXVALUE << no_max = 1; >> )
		   {(MINVALUE serial_integer_literal > [min_val]
				| NOMINVALUE << no_min = 1; >> )} )
		}

		{ (CYCLE <<cyclic = 1;>> | NOCYCLE << no_cyclic=1;>> ) }
		<<
			stmt = parser_new_node( this_parser, PT_CREATE_SERIAL );
			if ( stmt )
			{
				stmt->info.serial.serial_name 	= serial_name;
				stmt->info.serial.increment_val = increment_val;
				stmt->info.serial.start_val		= start_val;
				stmt->info.serial.max_val		= max_val;
				stmt->info.serial.min_val		= min_val;
				stmt->info.serial.cyclic		= cyclic;
				stmt->info.serial.no_max		= no_max;
				stmt->info.serial.no_min		= no_min;
				stmt->info.serial.no_cyclic		= no_cyclic;
			}
			pt_push(this_parser, stmt );
		>>
	;

serial_integer_literal > [PT_NODE *val] "serial integer literal"
	: 	<< char *minus=0; >>
		( {PLUS} | MINUS << minus = pt_append_string(this_parser, NULL, "-"); >>
		) UNSIGNED_INTEGER
		<<	$val = parser_new_node(this_parser, PT_VALUE);
			if ( $val )
			{
				$val->info.value.text =
					pt_append_string(this_parser, minus, $2.text);
				$val->type_enum = PT_TYPE_NUMERIC;
			}
		>>
	;

alter_serial_statement  "alter serial statement"
	:	<< 	PT_NODE *stmt, *serial_name, *increment_val;
			PT_NODE *max_val, *min_val;
			int cyclic = 0, no_cyclic = 0, no_max = 0, no_min = 0;
			stmt = serial_name = increment_val = max_val = min_val = 0;
		>>
		SERIAL identifier << serial_name = pt_pop(this_parser); >>
		{ INCREMENT BY serial_integer_literal > [increment_val] }

		{
		 ( (MINVALUE serial_integer_literal > [min_val]
				| NOMINVALUE << no_min = 1; >> )
		   {(MAXVALUE serial_integer_literal > [max_val]
				| NOMAXVALUE << no_max = 1; >> )} )
		|
		 ( (MAXVALUE serial_integer_literal > [max_val]
				| NOMAXVALUE << no_max = 1; >> )
		   {(MINVALUE serial_integer_literal > [min_val]
				| NOMINVALUE << no_min = 1; >> )} )
		}

		{ (CYCLE <<cyclic = 1;>>
					| NOCYCLE << no_cyclic = 1; >> ) }
		<<
			stmt = parser_new_node( this_parser, PT_ALTER_SERIAL );
			if ( stmt )
			{
				stmt->info.serial.serial_name 	= serial_name;
				stmt->info.serial.increment_val = increment_val;
				stmt->info.serial.start_val		= 0;
				stmt->info.serial.max_val		= max_val;
				stmt->info.serial.min_val		= min_val;
				stmt->info.serial.cyclic		= cyclic;
				stmt->info.serial.no_max		= no_max;
				stmt->info.serial.no_min		= no_min;
				stmt->info.serial.no_cyclic		= no_cyclic;

			}
			pt_push(this_parser, stmt );

			if ( !increment_val && !max_val && !min_val && cyclic == 0
					&& no_max == 0 && no_min == 0 && no_cyclic == 0 )
			{
				PT_ERRORmf(this_parser, stmt, MSGCAT_SET_PARSER_SEMANTIC,
						   MSGCAT_SEMANTIC_SERIAL_ALTER_NO_OPTION, 0 );
			}
		>>
	;


drop_serial_statement "drop_serial_statement"
	:	<<	PT_NODE *stmt=0, *serial_name=0; >>
		SERIAL identifier
		<<	serial_name = pt_pop(this_parser);
			stmt = parser_new_node( this_parser, PT_DROP_SERIAL );
			if ( stmt )
			{
				stmt->info.serial.serial_name 	= serial_name;
			}
			pt_push(this_parser, stmt);
		>>
	;

/* Stored Procedure statements */
create_stored_procedure_statement "create stored procedure statement"
        :       << PT_NODE *sp, *name, *param_list = NULL, *java_method, *tmp;
                   PT_TYPE_ENUM typ, ret_type;
                   PT_MISC_TYPE type = PT_SP_PROCEDURE;
                   ret_type = PT_TYPE_NONE;
                   sp = parser_new_node(this_parser, PT_CREATE_STORED_PROCEDURE);
                >>
         (( PROCEDURE << type = PT_SP_PROCEDURE; >>
                identifier << name = pt_pop(this_parser); >>
                Left_Paren {
                        sp_parameter_list << param_list = pt_pop(this_parser); >>
                } Right_Paren
          )
         |( FUNCTION << type = PT_SP_FUNCTION; >>
                identifier << name = pt_pop(this_parser); >>
                Left_Paren {
                        sp_parameter_list << param_list = pt_pop(this_parser); >>
                } Right_Paren
                RETURN
                ( data__type >[typ, tmp] << ret_type = typ; >>
                  | CURSOR << ret_type = PT_TYPE_RESULTSET; >> )
          )
         )
         { IS | AS } LANGUAGE << ; >>
         JAVA << ; >>
         NAME << ; >>
         char_string_literal > [java_method]
                << if (sp) {
                        sp->info.sp.name = name;
                        sp->info.sp.type = type;
                        sp->info.sp.param_list = param_list;
                        sp->info.sp.ret_type = ret_type;
                        sp->info.sp.java_method = java_method;
                   }
                   pt_push(this_parser, sp);
                >>
        ;

sp_parameter_list "parameter list"
        :
                sp_parameter_definition
                ("," sp_parameter_definition << PLIST; >> )*
        ;

sp_parameter_definition "parameter definition"
        :       << PT_NODE *node, *dt = NULL, *param_name;
                   PT_TYPE_ENUM typ;
                   PT_MISC_TYPE mode;
                   node = parser_new_node(this_parser, PT_SP_PARAMETERS);
                >>
        attribute_name <<  param_name = pt_pop(this_parser);
                           /*printf("%s\n", parser_print_tree(this_parser, * param_name));*/>>
        sp_in_out > [mode]
        ( data__type > [typ, dt] << /*printf("%s\n", parser_print_tree(this_parser, dt));*/ >>
          | CURSOR << typ = PT_TYPE_RESULTSET; >> )
                <<
                  if (node) {
                        node->type_enum = typ;
                        node->data_type = dt;
                        node->info.sp_param.name = param_name;
                        node->info.sp_param.mode = mode;
                  }
                  pt_push(this_parser, node);
                >>
        ;

sp_in_out > [PT_MISC_TYPE inout] "input output clause"
        : << $inout = PT_NOPUT; >>
        { (IN << $inout = PT_INPUT; >> { OUT_ << $inout = PT_INPUTOUTPUT; >> })
        | OUT_ << $inout = PT_OUTPUT; >>
        | INOUT << $inout = PT_INPUTOUTPUT; >>
        }
        ;

drop_stored_procedure_statement "drop_stored_procedure_statement"
        :       <<      PT_NODE *sp, *name_list;
                        PT_MISC_TYPE type;
                >>
                ( PROCEDURE << type = PT_SP_PROCEDURE; >>
                | FUNCTION << type = PT_SP_FUNCTION; >>
                ) identifier ("," identifier <<PLIST;>>)*
                <<      name_list = pt_pop(this_parser);
                        sp = parser_new_node(this_parser, PT_DROP_STORED_PROCEDURE);
                        if (sp)
                        {
                                sp->info.sp.name = name_list;
                                sp->info.sp.type = type;
                        }
                        pt_push(this_parser, sp);
                >>
        ;

/* Queries */

esql_query_statement "csql query"
	: 	<<PT_NODE *stmt;
                  select_level++;>>
	  csql_query
		<< stmt = pt_pop(this_parser); >>
		/* for_update_clause is used only in embedded
		 * sql and only in declare cursor statements  */
	  { for_update_of_clause
		<< if (stmt) stmt->info.query.for_update = pt_pop(this_parser);
		>>
	  }	<<pt_push(this_parser, stmt);
                  select_level--;>>
	;

csql_query "csql query"
	: << PT_NODE *node;
             bool saved_cannot_cache;

             saved_cannot_cache = cannot_cache;
             cannot_cache = false;
          >>
          select_expression orderby_clause
          << node = pt_pop(this_parser);
             if (node) {
                if (cannot_cache) {
                    node->info.query.reexecute = 1;
                    node->info.query.do_cache = 0;
                    node->info.query.do_not_cache = 1;
                }
                pt_push(this_parser, node);
             }
             cannot_cache = saved_cannot_cache;
          >>
	;

select_expression "select expression"
	: 	<< PT_NODE *stmt, *opd2; >>
	  select_or_subquery
	  (table_op select_or_subquery
		<< opd2 = pt_pop(this_parser); stmt = pt_pop(this_parser);
		   if (stmt)
		   {
                         /* set query id # */
                         stmt->info.query.id = (long) stmt;
		         stmt->info.query.q.union_.arg2 = opd2;
		   }
		   pt_push(this_parser, stmt);
		>>
	  )*
	;

table_op "select expression operator"
	:	<<PT_NODE *stmt; PT_MISC_TYPE isAll = PT_DISTINCT;>>
	  ( 	<<stmt = parser_new_node(this_parser, PT_UNION);       >>
	    Union
	  |	<<stmt = parser_new_node(this_parser, PT_DIFFERENCE);  >>
	    DIFFERENCE
	  |	<<stmt = parser_new_node(this_parser, PT_DIFFERENCE);  >>
	    EXCEPT
	  |	<<stmt = parser_new_node(this_parser, PT_INTERSECTION);>>
	    INTERSECTION
	  |	<<stmt = parser_new_node(this_parser, PT_INTERSECTION);>>
	    INTERSECT
	  ) all_distinct[PT_DISTINCT] > [isAll]
		<<if (stmt)
		      {
			      stmt->info.query.q.union_.arg1 = pt_pop(this_parser);
			      stmt->info.query.all_distinct = isAll;
		      }
		  pt_push(this_parser, stmt);
		>>
	;

select_or_subquery "select statement or subquery"
	: select_statement
	| subquery
	;

select_statement "select statement"
	:   << PT_NODE *q = parser_new_node(this_parser,  PT_SELECT);
	       PT_NODE *h_var = NULL, *n;
               PT_MISC_TYPE is_all;
	       char *hint_comment;
               bool is_dummy_select = false; /* init */
               bool found_ANSI_join;
	       bool saved_fo = found_Oracle_outer; /* save */
               if (select_level >= 0) select_level++;
               hidden_incr_list = NULL;
	    >>
	  SELECT
	    << if (q) {
	         q->info.query.q.select.flavor = PT_USER_SELECT;
		 q->info.query.q.select.hint   = PT_HINT_NONE; /* init */
               }
            >>
	  { ( CPP_STYLE_HINT
	      << if (q) {
		   hint_comment = pt_makename($1.text);
                   (void) pt_get_hint(hint_comment, hint_table, q);
	         }
	      >>
	    | SQL_STYLE_HINT
	      << if (q) {
                   hint_comment = pt_makename($1.text);
                   (void) pt_get_hint(hint_comment, hint_table, q);
	         }
	      >>
	    | C_STYLE_HINT
	      << if (q) {
                   hint_comment = pt_makename($1.text);
                   (void) pt_get_hint(hint_comment, hint_table, q);
	         }
	      >>
	    )+
	  }
	  all_distinct[PT_ALL] > [is_all]
	  select__list  /* may be restricted to single column */
	    << if (q) {
                 q->info.query.q.select.list = n = pt_pop(this_parser);
                 if (hidden_incr_list) {
                     (void)parser_append_node(hidden_incr_list, q->info.query.q.select.list);
                     hidden_incr_list = NULL;
                 }
                 if (n &&
                     n->node_type == PT_VALUE &&
                     n->type_enum == PT_TYPE_STAR) { /* select * from ... */
                   is_dummy_select = true;  /* Here, guess as TRUE */
		 } else if (n && n->next == NULL &&
		            n->node_type == PT_NAME &&
			    n->type_enum == PT_TYPE_STAR) { /* select A.* from */
                   is_dummy_select = true;  /* Here, guess as TRUE */
                 } else {
                   is_dummy_select = false; /* not dummy */
                 }
               }
            >>
	  { (INTO | TO)
	    to_parameter_list > [h_var]
	    << if (q) {
                 q->info.query.into_list = h_var;
                 is_dummy_select = false; /* not dummy */
               }
            >>
	  }
          FROM extended_table_specification_list[&found_ANSI_join]
            << if (q) {
                 q->info.query.q.select.from = n = pt_pop(this_parser);
                 if (n && n->next) is_dummy_select = false; /* not dummy */
                 if (found_ANSI_join == true)
                   PT_SELECT_INFO_SET_FLAG(q, PT_SELECT_INFO_ANSI_JOIN);
               }
               found_Oracle_outer = false; /* init */
            >>
	  { where_clause
	    << if (q) {
                 q->info.query.q.select.where = n = pt_pop(this_parser);
                 if (n) is_dummy_select = false; /* not dummy */
                 if (found_Oracle_outer == true)
                   PT_SELECT_INFO_SET_FLAG(q, PT_SELECT_INFO_ORACLE_OUTER);
               }
            >>
	  }
	  { groupby_clause
            << if (q) {
                 q->info.query.q.select.group_by = n = pt_pop(this_parser);
                 if (n) is_dummy_select = false; /* not dummy */
               }
            >>
	  }
	  { having_clause
            << if (q) {
                 q->info.query.q.select.having = n = pt_pop(this_parser);
                 if (n) is_dummy_select = false; /* not dummy */
               }
            >>
	  }
          { using_index_clause
            << if (q) q->info.query.q.select.using_index = pt_pop(this_parser); >>
          }
          { with_increment_clause
            << if (q) q->info.query.q.select.with_increment = pt_pop(this_parser); >>
          }
            <<
               if (q) {
                 /* set query id # */
                 q->info.query.id = (long) q;
                 q->info.query.all_distinct = is_all;
               }
               if (is_all != PT_ALL) is_dummy_select = false; /* not dummy */
               if (is_dummy_select == true) {
                 /* mark as dummy */
                 PT_SELECT_INFO_SET_FLAG(q, PT_SELECT_INFO_DUMMY);
               }
               if (hidden_incr_list) {
                 /* if not handle hidden expressions, raise an error */
                 PT_ERRORf(this_parser, q, "%s can be used at select or with increment clause only.",
                   pt_short_print(this_parser, hidden_incr_list));
               }
               pt_push(this_parser, q);

               found_Oracle_outer = saved_fo; /* restore */
               if (select_level >= 0) select_level--;
            >>
	;

all_distinct < [PT_MISC_TYPE defaultAll] > [PT_MISC_TYPE isAll]
	:	<<$isAll = defaultAll;>>
	  { ALL			  <<$isAll = PT_ALL;>>
	  | ( DISTINCT | UNIQUE ) <<$isAll = PT_DISTINCT;>>
	  }
	;

select__list "select list"
        : << int saved_ic, saved_gc, saved_oc; >>
        ( << PT_NODE *node_=parser_new_node(this_parser, PT_VALUE); >>
          STAR
                << if(node_) node_->type_enum = PT_TYPE_STAR;
                   pt_push(this_parser, node_);
                >>
        )
        | << saved_ic = instnum_check;
             saved_gc = groupbynum_check;
             saved_oc = orderbynum_check;
             instnum_check = groupbynum_check = orderbynum_check = 2; >>
          alias_enabled_expression_list
          << instnum_check = saved_ic;
             groupbynum_check = saved_gc;
             orderbynum_check = saved_oc; >>
        ;

alias_enabled_expression_list "alias enabled expression list"
        : alias_enabled_expression_
          ( "," alias_enabled_expression_ <<PLIST;>> )*
        ;

alias_enabled_expression_ "alias enabled expression"
        : << PT_NODE *node = NULL, *subq = NULL, *id = NULL; >>
          expression_
          << node = pt_pop(this_parser);
             /* at select list, can not use parentheses set expr value */
	     if (node) {
                 if (node->node_type == PT_VALUE &&
                     node->type_enum == PT_TYPE_EXPR_SET) {
                     node->type_enum = PT_TYPE_SEQUENCE; /* for print out */
                     PT_ERRORf(this_parser, node, "check syntax at %s, illegal parentheses set expression.", pt_short_print(this_parser, node));
                 }
		 else if (PT_IS_QUERY_NODE_TYPE(node->node_type)) {
		     /* mark as single tuple query */
                     node->info.query.single_tuple = 1;

		     if ((subq = pt_get_subquery_list(node)) && subq->next) {
		         /* illegal multi-column subquery */
			 PT_ERRORm(this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			           MSGCAT_SEMANTIC_NOT_SINGLE_COL);
		     }
		 }
	     }
             pt_push(this_parser, node);
          >>
          { {AS} identifier
             << id = pt_pop(this_parser);
                if (id && id->node_type == PT_NAME) {
                    node = pt_pop(this_parser);
                    if (node) {
			if (node->type_enum == PT_TYPE_STAR) {
			    PT_ERROR(this_parser, id, "please check syntax after '*', expecting ',' or FROM in select statement.");
			}
                        else {
			    node->alias_print = pt_makename(id->info.name.original);
                            pt_push(this_parser, node);
                            parser_free_node(this_parser, id);
			}
                    }
                }
             >>
          }
        ;

expression_list "expression list"
	: expression_
	  ( "," expression_ <<PLIST;>> )*
	;

to_parameter_list > [PT_NODE *list] "to parameter list"
	:	<<PT_NODE *var; $list = NULL;>>
	  to_parameter > [$list]
	  ( "," to_parameter > [var]
		<<if (var && $list) parser_append_node(var, $list);
		>>
	  )*
	;

to_parameter > [PT_NODE *val] "to parameter clause"
	: host_parameter[&output_host_index] > [$val]
		<< if ($val) $val->info.host_var.var_type = PT_HOST_OUT; >>
	| parameter_ > [$val]
		<< if ($val)
		   { $val->info.name.meta_class = PT_PARAMETER;
		     $val->info.name.spec_id = (long) $val;
		     $val->info.name.resolved = pt_makename("out parameter");
		   }
		>>
	| identifier
		<< $val = pt_pop(this_parser);
		   if ($val)
		   { $val->info.name.meta_class = PT_PARAMETER;
		     $val->info.name.spec_id = (long) $val;
		     $val->info.name.resolved = pt_makename("out parameter");
		   }
		>>
	;

from_parameter > [PT_NODE *val] "from parameter clause"
	: << PT_MISC_TYPE cls = PT_PARAMETER; >>

	host_parameter[&input_host_index] > [$val]
	| parameter_ > [$val]
	| { CLASS << cls = PT_META_CLASS; >> }  identifier
	<< $val = pt_pop(this_parser);
	if ($val) {
	    $val->info.name.meta_class = cls;
	    $val->data_type = parser_new_node(this_parser, PT_DATA_TYPE);
	}
	>>
	;

host_parameter < [long *index] > [PT_NODE *var] "host parameter clause"
	: 	<< $var = parser_new_node(this_parser, PT_HOST_VAR); >>
	  "?"	<< if ($var)
		   { $var->info.host_var.var_type = PT_HOST_IN;
		     $var->info.host_var.str   = pt_makename($1.text);
		     $var->info.host_var.index = (*$index)++;
		   }
		>>
           |
	  "?" COLON_ UNSIGNED_INTEGER
                << if (parent_parser == NULL) {
                     /* if it isn't doing internal statement parsing,
                        ignore the trailing host var index specifier
                        (colon+number) */
                    $var->info.host_var.var_type = PT_HOST_IN;
                    $var->info.host_var.str   = pt_makename($1.text);
                    $var->info.host_var.index = (*$index)++;
                   } else if ($var) {
                     /* in the case of internal statement parsing,
                        take the the host var's index */
                     $var->info.host_var.var_type = PT_HOST_IN;
                     $var->info.host_var.str   = pt_makename($1.text);
                     $var->info.host_var.index = atol($3.text);
                   }
		>>
	;

parameter_ > [PT_NODE *name] "interpreter parameter"
	: COLON_ identifier
		<< $name = pt_pop(this_parser);
		   if ($name) $name->info.name.meta_class = PT_PARAMETER;
		>>
	;

where_clause "where clause"
        : << int saved_ic; >>
          WHERE
          << saved_ic = instnum_check; instnum_check = 1; >>
          search_condition
          << instnum_check = saved_ic; >>
	;

groupby_clause "group by clause"
    : GROUP BY group_spec_list
    ;

group_spec_list "group by specification list"
    : group_spec ("," group_spec << PLIST; >>)*
    ;

group_spec "group by specification"
    :   << PT_NODE *node = parser_new_node(this_parser, PT_SORT_SPEC); >>
      groupby_expression_
        << if (node) {
             node->info.sort_spec.asc_or_desc = PT_ASC;
             node->info.sort_spec.expr = pt_pop(this_parser);
           }
           pt_push(this_parser, node);
        >>
    ;

groupby_expression_ "group by expression"
    : groupby_term
      ( PLUS  << PFOP(PT_PLUS);  >>
        groupby_term  << PSOP; >>
      |
        MINUS << PFOP(PT_MINUS); >>
        groupby_term  << PSOP; >>
      | "\|\|"  << PFOP(PT_STRCAT); >>
        groupby_term  << PSOP; >>
      )*
    ;

groupby_term "group by expression"
    : groupby_factor
      ( STAR   << PFOP(PT_TIMES);  >>
        groupby_factor << PSOP; >>
        |
        SLASH  << PFOP(PT_DIVIDE); >>
        groupby_factor << PSOP; >>
      )*
    ;

groupby_factor "group by expression"
    : PLUS  groupby_primary
    | MINUS groupby_primary << PFOP(PT_UNARY_MINUS); >>
    | groupby_primary
    ;

/*
 * groupby_primary is a subset of primary
 * the followings are excluded :
 *      aggregate_func
 *      class_func
 *      instnum_func
 *      orderbynum_func
 *      insert_expression
 *      Left_Paren search_condition Right_Paren
 *      subquery
 */
groupby_primary "group by expression"
    : arith_func
    | char_func
    | scal_func
    | cast_func
    | case_expr
    | extract_expr
    | literal_w_o_param
/* function_call being subsumed by path_expression--see path_link */
    | path_expression
/* permit () in GROUP BY list
   example) SELECT ... GROUP BY a, (b), c + d, (e + f) + g, ((h + i) + j) */
    | << PT_NODE *exp; >>
      Left_Paren
      groupby_expression_
        << exp = pt_pop(this_parser);
           if (exp && exp->node_type == PT_EXPR)
               exp->info.expr.paren_type = 1;
           pt_push(this_parser, exp);
        >>
      Right_Paren
    ;

having_clause "having clause"
        : << int saved_gc; >>
          HAVING
          << saved_gc = groupbynum_check; groupbynum_check = 1; >>
          search_condition
          << groupbynum_check = saved_gc; >>
	;

index__name "index name"
        :   << PT_NODE *class, *index; >>
        ( identifier "\." identifier
            << index = pt_pop(this_parser);
               class = pt_pop(this_parser);
               index->info.name.resolved = class->info.name.original;
               index->info.name.meta_class = PT_INDEX_NAME;
               pt_push(this_parser, index);
            >>
        | identifier
            << index = pt_pop(this_parser);
               index->info.name.meta_class = PT_INDEX_NAME;
               pt_push(this_parser, index);
            >>
        )
        { Left_Paren
            ( PLUS /* force to use */
              << index = pt_pop(this_parser);
                 index->etc = (void *) 1;
                 pt_push(this_parser, index);
              >>
            | MINUS /* force to not use */
              << index = pt_pop(this_parser);
                 index->etc = (void *) -1;
                 pt_push(this_parser, index);
              >>
            )
          Right_Paren
        }
        ;

using_index_clause "using index clause"
        : << PT_NODE *all, *none, *node, *list; >>
          USING INDEX
            ( index__name ("," index__name << PLIST; >>)*
            | NONE
              << none = parser_new_node(this_parser, PT_NAME);
                 none->info.name.original = NULL;
                 none->info.name.meta_class = PT_INDEX_NAME;
                 pt_push(this_parser, none);
              >>
            | ALL EXCEPT
              << all = parser_new_node(this_parser, PT_NAME);
                 all->info.name.original = NULL;
                 all->info.name.resolved = "*";
                 all->info.name.meta_class = PT_INDEX_NAME;
                 all->etc = (void *) -2;
                 pt_push(this_parser, all);
              >>
              index__name << PLIST; >> ("," index__name << PLIST; >>)*
              << list = pt_pop(this_parser);
                 for (node = list; node; node = node->next)
                    node->etc = (void *) -2;
                 pt_push(this_parser, list);
              >>
            )
        ;

with_increment_clause "with increment clause"
        : << PT_OP_TYPE t; >>
         WITH ( INCREMENT << t=PT_INCR; >> |
                DECREMENT << t=PT_DECR; >> )
         For
            incr_arg_name[t]("," incr_arg_name[t] << PLIST; >>)*
        ;
incr_arg_name < [PT_OP_TYPE t] "increment argument name"
        : << PT_NODE *expr; PT_OP_TYPE t2; >>
         (
            path_expression << PFOP($t); >>
          | ( INCR << t2=PT_INCR; >> |
              DECR << t2=PT_DECR; >> )
             l_paren path_expression
              << PFOP(t2);
                 if ($t != t2) {
                     expr = pt_pop(this_parser);
                     PT_ERRORf2(this_parser, expr, "%s can be used at 'with %s for'.",
                         pt_short_print(this_parser, expr),
                         ($t == PT_INCR ? "increment" : "decrement"));
                     pt_push(this_parser, expr);
                 }
              >>
             Right_Paren
         )
          <<
             if (select_level != 1) {
                 expr = pt_pop(this_parser);
                 PT_ERRORf(this_parser, expr, "%s can be used at top select statement only.",
                     pt_short_print(this_parser, expr));
                 pt_push(this_parser, expr);
             }
             expr = pt_pop(this_parser);
             SET_AS_HIDDEN_COLUMN(expr);
             pt_push(this_parser, expr);
          >>
        ;

orderby_clause "order by clause"
        :   << PT_NODE *stmt = pt_pop(this_parser);
               PT_NODE *n, *order, *list = NULL, *temp, *col;
               char *n_str, *c_str;
               bool found_star;
               int saved_oc, index_of_col; >>
          { ORDER BY sort_spec_list
            << if (stmt) {
                  stmt->info.query.order_by = order = pt_pop(this_parser);
                  if (order) {/* not dummy */
                     PT_SELECT_INFO_CLEAR_FLAG(stmt, PT_SELECT_INFO_DUMMY);
                     if (pt_is_query(stmt)) {
                        /* UNION, INTERSECT, DIFFERENCE, SELECT */
                        temp = stmt;
                        while (temp) {
                           switch (temp->node_type) {
                              case PT_SELECT :
                                 goto start_check;
                                 break;
                              case PT_UNION :
                              case PT_INTERSECTION:
                              case PT_DIFFERENCE:
                                 temp = temp->info.query.q.union_.arg1;
                                 break;
                              default:
                                 temp = NULL;
                                 break;
                           }
                        }
start_check:
                        if (temp) list = temp->info.query.q.select.list;
                        found_star = false;

                        if (list && list->node_type == PT_VALUE && list->type_enum == PT_TYPE_STAR) {
                           /* found "*" */
                           found_star = true;
                        }
                        else {
                           for (col = list ; col; col = col->next) {
                              if (col->node_type == PT_NAME && col->type_enum == PT_TYPE_STAR) {
                                 /* found "classname.*" */
                                 found_star = true;
                                 break;
                              }
                           }
                        }

                        for (; order; order = order->next) {
                           if (!(n = order->info.sort_spec.expr)) break;

                           if (n->node_type == PT_VALUE) continue;

                           n_str = parser_print_tree(this_parser, n);
                           if (!n_str) continue;

                           for ( col = list, index_of_col = 1; col;
                                 col = col->next, index_of_col++) {
                              c_str = parser_print_tree(this_parser, col);
                              if (!c_str) continue;

                              if ((col->alias_print &&
                                   intl_mbs_casecmp(n_str, col->alias_print)==0)
                                 ||intl_mbs_casecmp(n_str, c_str) == 0) {
                                 if (found_star) {
                                    temp = parser_copy_tree(this_parser, col);
                                    temp->next = NULL;
                                 }
                                 else {
                                    temp = parser_new_node(this_parser, PT_VALUE);
                                    if (!temp) break;
                                    temp->type_enum = PT_TYPE_INTEGER;
                                    temp->info.value.data_value.i = index_of_col;
                                 }
                                 parser_free_node(this_parser, n);
                                 order->info.sort_spec.expr = temp;
                                 break;
                              }
                           }
                        }
                     }
                  }
               }
                >>
              { For
                << saved_oc = orderbynum_check; orderbynum_check = 1; >>
                search_condition
                << orderbynum_check = saved_oc; >>
                    << if (stmt)
                        stmt->info.query.orderby_for = pt_pop(this_parser);
                    >>
              }
          }     << pt_push(this_parser, stmt); >>
        ;

for_update_of_clause "for update of clause"
	: For UPDATE OF sort_spec_list
	;

sort_spec_list "sort specification list"
	: sort_by ("," sort_by << PLIST; >>)*
	;

sort_by "sort specification"
	: 	<< PT_MISC_TYPE isdescending=PT_ASC;
                   PT_NODE *ord, *node=parser_new_node(this_parser, PT_SORT_SPEC);
		>>
	  sort_column > [ord]
          { ASC | DESC<< isdescending=PT_DESC; >> }
		<< if(node)
		   {
                     node->info.sort_spec.asc_or_desc=isdescending;
                     node->info.sort_spec.expr=ord;
                   }
                   pt_push(this_parser, node);
		>>
	;

sort_column > [PT_NODE *ord] "sort column"
        : expression_ << $ord=pt_pop(this_parser); >>
	;

set_expression "set expression"
	: expression_
	;

expression_ "expression"
	: term
	  ( PLUS  << PFOP(PT_PLUS);  >>
	    term  << PSOP; >>
          | MINUS << PFOP(PT_MINUS); >>
	    term  << PSOP; >>
	  | "\|\|"  << PFOP(PT_STRCAT); >>
	    term  << PSOP; >>
          )*
	;

term "expression"
	: factor
	  ( STAR   << PFOP(PT_TIMES);  >>
	    factor << PSOP; >>
          | SLASH  << PFOP(PT_DIVIDE); >>
	    factor << PSOP; >>
          )*
	;

factor "expression"
	: PLUS  primary
        | MINUS primary << PFOP(PT_UNARY_MINUS); >>
        |       primary
	;

class_func "class function"
	:
	    CLASS Left_Paren identifier Right_Paren
            << { PT_NODE *name = NULL;
		 name = pt_pop(this_parser);
		 if (name) { name->info.name.meta_class = PT_OID_ATTR;
			     pt_push(this_parser, name); }
	       }
            >>
        ;

primary "expression"
	: aggregate_func
        | arith_func
	| char_func
	| scal_func
	| class_func
	| cast_func
	| case_expr
	| extract_expr
        | instnum_func
        | orderbynum_func
/* parameters are subsumed by path_expression */
	| literal_w_o_param
/*
   insert expression in parens becomes ambigous with
   search conditon expression. However we always want it resolved
   as insert expression, so it is put first.
*/
	| insert_expression
		<< { PT_NODE *ins=pt_pop(this_parser);
		   if(ins)
		     ins->info.insert.is_subinsert = PT_IS_SUBINSERT;
		   pt_push(this_parser, ins);
		   }
		>>
/* function_call being subsumed by path_expression--see path_link */
	| path_expression
	| 	<< PT_NODE *exp, *val, *tmp;
                   bool paren_set_expr = false; >>
          l_paren
          (
/* this is what it should be expression_ */
            search_condition
/* this is subquery part ( see the next 'csql_query') subquery */
            { (table_op select_or_subquery
                << tmp = pt_pop(this_parser); exp = pt_pop(this_parser);
                   if (exp) {
                       exp->info.query.q.union_.arg2 = tmp;
                   }
                   pt_push(this_parser, exp);
                >>
              )+
              orderby_clause
            }
            ("," expression_ << paren_set_expr = true; PLIST; >>)*
                <<
                   exp = pt_pop(this_parser);

                   if (paren_set_expr == true) {
                       /* create parentheses set expr value */
                       val = parser_new_node(this_parser, PT_VALUE);
                       if (val) {
                           /* for each elements, convert parentheses set
                            * expr value into sequence value */
                           for (tmp = exp; tmp; tmp = tmp->next) {
                               if (tmp->node_type == PT_VALUE &&
                                   tmp->type_enum == PT_TYPE_EXPR_SET) {
                                   tmp->type_enum = PT_TYPE_SEQUENCE;
                               }
                           } /* for (tmp = exp; tmp; tmp = tmp->next) */

                           val->info.value.data_value.set = exp;
                           val->type_enum = PT_TYPE_EXPR_SET;
                       }
                       exp = val;
                   } else {
                       if (exp && exp->node_type == PT_EXPR) {
                           exp->info.expr.paren_type = 1;
                       }
                   }

                   pt_push(this_parser, exp);
                >>
          |
/* this is what it should be
   ( see previous 'search_condition' for tailing part) subquery */
            csql_query
                <<
                   /* check for subquey */
                   exp = pt_pop(this_parser);
                   if (within_join_condition) {
                       PT_ERRORm(this_parser, exp, MSGCAT_SET_PARSER_SYNTAX,
                                 MSGCAT_SYNTAX_JOIN_COND_SUBQ);
                   }
                   /* this does not do anything for union, difference, etc
                    * it only exists to force parens around
                    * subqueries that are select statements.
                    */
                   if (exp) exp->info.query.is_subquery = PT_IS_SUBQUERY;
                   pt_push(this_parser, exp);
                >>
          )
          Right_Paren
	;


aggregate_func "expression"
	: ( 	<< PT_NODE *rrr=parser_new_node(this_parser, PT_FUNCTION);
		   PT_MISC_TYPE t = PT_ALL;
		>>
	    COUNT
            Left_Paren
	    (
	      STAR
		<<
                   if(rrr) {
                   	rrr->info.function.arg_list=NULL;
			rrr->info.function.function_type=PT_COUNT_STAR;
		   }
		>>
	      |
              ( (DISTINCT | UNIQUE) << t=PT_DISTINCT;>>
		expression_
              | ALL
	    	expression_
              | expression_
	      )
		<< if(rrr) {
			rrr->info.function.all_or_distinct = t;
			rrr->info.function.function_type=PT_COUNT;
			rrr->info.function.arg_list=pt_pop(this_parser);
		   }
		>>
	    )
	    Right_Paren
		<< pt_push(this_parser, rrr); >>
	  )
	| other_agg_func
	;

other_agg_func "expression"
	: 	<< PT_NODE*node=parser_new_node(this_parser, PT_FUNCTION);
		   FUNC_TYPE t; PT_MISC_TYPE ad=PT_ALL;
		>>
	  ( AVG << t=PT_AVG;>>
	  | Max << t=PT_MAX;>>
	  | Min << t=PT_MIN;>>
	  | SUM << t=PT_SUM;>>
	  | STDDEV << t=PT_STDDEV; >>
	  | VARIANCE << t=PT_VARIANCE; >>
	  )	<< if(node)node->info.function.function_type=t; >>
          l_paren
          ( (DISTINCT | UNIQUE) << ad=PT_DISTINCT;>>
	    path_expression
          | ALL << ad=PT_ALL;>>
	    expression_
          | expression_  << ad=PT_ALL;>>
	  )
	  Right_Paren
		<<
                   if(node)
		   {
			/* don't bother to sort an uniqueify min and max ! */
			if (t == PT_MAX || t == PT_MIN) ad = PT_ALL;
		   	node->info.function.all_or_distinct = ad;
                     	node->info.function.arg_list = pt_pop(this_parser);
                   }
                   pt_push(this_parser, node);
		>>
          | GROUPBY_NUM Left_Paren Right_Paren
            <<
                if (node) {
                    node->info.function.function_type = PT_GROUPBY_NUM;
                    node->info.function.arg_list = NULL;
                    node->info.function.all_or_distinct = PT_ALL;
                }
                pt_push(this_parser, node);
                if (groupbynum_check == 0)
                    PT_ERRORmf2(this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
                                MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR,
                                "GROUPBY_NUM()", "GROUPBY_NUM()");
            >>
	;

arith_func "expression"
        : MODULUS l_paren expression_ << PFOP(PT_MODULUS); >>
                  "," expression_ Right_Paren << PSOP; >>
        | << PT_NODE *expr; >>
          RAND Left_Paren Right_Paren
          << expr = parser_new_node(this_parser, PT_EXPR);
             expr->info.expr.op = PT_RAND;
             pt_push(this_parser, expr);

             cannot_cache = true;
          >>
        | << PT_NODE *expr; >>
          DRAND Left_Paren Right_Paren
          << expr = parser_new_node(this_parser, PT_EXPR);
             expr->info.expr.op = PT_DRAND;
             pt_push(this_parser, expr);

             cannot_cache = true;
          >>
        | << PT_NODE *expr; >>
          RANDOM Left_Paren Right_Paren
          << expr = parser_new_node(this_parser, PT_EXPR);
             expr->info.expr.op = PT_RANDOM;
             pt_push(this_parser, expr);

             cannot_cache = true;
          >>
        | << PT_NODE *expr; >>
          DRANDOM Left_Paren Right_Paren
          << expr = parser_new_node(this_parser, PT_EXPR);
             expr->info.expr.op = PT_DRANDOM;
             pt_push(this_parser, expr);

             cannot_cache = true;
          >>
	| FLOOR l_paren
	        expression_ << PFOP(PT_FLOOR); >>
		Right_Paren
	| CEIL  l_paren
	        expression_ << PFOP(PT_CEIL); >>
		Right_Paren
	| SIGN  l_paren
	        expression_ << PFOP(PT_SIGN); >>
		Right_Paren
	| ABS   l_paren
	        expression_ << PFOP(PT_ABS); >>
		Right_Paren
	| POWER l_paren
	        expression_ << PFOP(PT_POWER); >>
		"," expression_ << PSOP; >>
		Right_Paren
	| LOG   l_paren
	        expression_ << PFOP(PT_LOG); >>
		"," expression_ << PSOP; >>
		Right_Paren
	| EXP   l_paren
	        expression_ << PFOP(PT_EXP); >>
		Right_Paren
	| SQRT  l_paren
	        expression_ << PFOP(PT_SQRT); >>
		Right_Paren
	| << int flag = 0; >>
	  ROUND l_paren
	        expression_ << PFOP(PT_ROUND); >>
		{ "," expression_ << PSOP; flag = 1;>> }
		Right_Paren
		<<
		   if (flag == 0) { /* default is 0 */
		     PT_NODE *val = parser_new_node(this_parser, PT_VALUE);
		     if (val) {
		       val->type_enum = PT_TYPE_INTEGER;
		       val->info.value.data_value.i = 0;
		       pt_push(this_parser, val);
		       PSOP;
		     }
		   }
		>>
	| << int flag = 0; >>
	  TRUNC l_paren
	        expression_ << PFOP(PT_TRUNC); >>
		{ "," expression_ << PSOP; flag = 1;>> }
		Right_Paren
		<<
		   if (flag == 0) { /* default is 0 */
		     PT_NODE *val = parser_new_node(this_parser, PT_VALUE);
		     if (val) {
		       val->type_enum = PT_TYPE_INTEGER;
		       val->info.value.data_value.i = 0;
		       pt_push(this_parser, val);
		       PSOP;
		     }
		   }
		>>
        | << PT_NODE *expr, *arg1; PT_OP_TYPE t; >>
          ( INCR << t=PT_INCR; >>
            | DECR << t=PT_DECR; >> )
               l_paren
               path_expression << PFOP(t); >>
               Right_Paren
               <<
                   if (select_level != 1) {
                       expr = pt_pop(this_parser);
                       PT_ERRORf(this_parser, expr, "%s can be used at top select statement only.",
                           pt_short_print(this_parser, expr));
                       pt_push(this_parser, expr);
                   }
                   /* in case of inner increment expression, replace the expr with it's argument,
                      and the expr add to select list as hidden expr like that.
                        "SELECT A, INCR(B)+1, C FROM X" => "SELECT A, B+1, C, HIDDEN(INCR(B))" */
                   expr = pt_pop(this_parser);
                   SET_AS_HIDDEN_COLUMN(expr);
                   hidden_incr_list = parser_append_node(expr, hidden_incr_list);
                   if ((arg1=parser_copy_tree(this_parser, expr->info.expr.arg1)) == NULL)
                       /* OUTOFMEMORY error */;
                   else pt_push(this_parser, arg1);

                   cannot_cache = true;
               >>
        ;

scal_func "expression"
	: ADD_MONTHS l_paren expression_ << PFOP(PT_ADD_MONTHS); >>
		"," expression_ Right_Paren	<< PSOP; >>
	|
		LAST_DAY l_paren expression_ << PFOP(PT_LAST_DAY); >>
		Right_Paren
	|
		MONTHS_BETWEEN l_paren expression_ << PFOP(PT_MONTHS_BETWEEN); >>
		"," expression_ Right_Paren << PSOP; >>
	|
		<< 	PT_NODE *p; >>
		( SYS_DATE | CURRENT_DATE | SYSDATE )
		<<
			p = parser_new_node(this_parser, PT_EXPR);
			p->info.expr.op = PT_SYS_DATE;
			pt_push(this_parser, p);

                        si_timestamp = true;
                        cannot_cache = true;
		>>
	|
		<< 	PT_NODE *p; >>
		( SYS_TIME_ | CURRENT_TIME | SYSTIME )
		<<
			p = parser_new_node(this_parser, PT_EXPR);
			p->info.expr.op = PT_SYS_TIME;
			pt_push(this_parser, p);

                        si_timestamp = true;
                        cannot_cache = true;
		>>
	|
		<< 	PT_NODE *p; >>
		( SYS_TIMESTAMP | CURRENT_TIMESTAMP | SYSTIMESTAMP )
		<<
			p = parser_new_node(this_parser, PT_EXPR);
			p->info.expr.op = PT_SYS_TIMESTAMP;
			pt_push(this_parser, p);

                        si_timestamp = true;
                        cannot_cache = true;
		>>
	|
		<< 	PT_NODE *p; >>
		( USER | CURRENT_USER )
		<<
			p = parser_new_node(this_parser, PT_EXPR);
			p->info.expr.op = PT_CURRENT_USER;
			pt_push(this_parser, p);

                        cannot_cache = true;
		>>
	|
		<< 	PT_NODE *p; >>
		( LOCAL_TRANSACTION_ID )
		<<
			p = parser_new_node(this_parser, PT_EXPR);
			p->info.expr.op = PT_LOCAL_TRANSACTION_ID;
			pt_push(this_parser, p);

                        si_tran_id = true;
                        cannot_cache = true;
		>>
	|       << int arg_cnt = 1; PT_NODE *arg3 = NULL; >>
		TO_CHAR l_paren expression_ << PFOP(PT_TO_CHAR); >>
		{ "," expression_ << PSOP; arg_cnt = 2; >>
		  { "," char_string_literal > [arg3]
		    << arg_cnt = 3; >> }
		}
		Right_Paren
		<<
		  if (arg_cnt == 1) { /* 2nd op */
		    PT_NODE *format = parser_new_node(this_parser, PT_VALUE);
		    if (format) {
		      format->type_enum = PT_TYPE_NULL;
		      pt_push(this_parser, format);
		      PSOP;
		    }
		  }
		  if (arg_cnt == 1 || arg_cnt == 2) {
		    PT_NODE *date_lang = parser_new_node(this_parser, PT_VALUE);
		    if (date_lang) {
		      char *lang_str;
		      date_lang->type_enum = PT_TYPE_INTEGER;
		      lang_str = envvar_get("DATE_LANG");
		      if (lang_str && strcasecmp(lang_str, LANG_NAME_KOREAN) == 0)
		        date_lang->info.value.data_value.i = 4;
		      else
		        date_lang->info.value.data_value.i = 2;
		    }
		    if (arg_cnt == 1) { /* default format */
		      date_lang->info.value.data_value.i |= 1;
		    }
		    pt_push(this_parser, date_lang);
		    PTOP;
		  }
		  else if (arg_cnt == 3) { /* valid check */
		    char *lang_str;
		    PT_NODE *date_lang = parser_new_node(this_parser, PT_VALUE);
		    PT_NODE *expr;
		    expr = pt_pop(this_parser);
		    if (date_lang && expr) {
		      date_lang->type_enum = PT_TYPE_INTEGER;
		      if (arg3->info.value.data_value.str != NULL) {
		        lang_str = (char *) arg3->info.value.data_value.str->bytes;
		        if (strcasecmp(lang_str, LANG_NAME_KOREAN) == 0) {
		          date_lang->info.value.data_value.i = 4;
		        }
		        else if (strcasecmp(lang_str, LANG_NAME_ENGLISH) == 0) {
		          date_lang->info.value.data_value.i = 2;
		        }
		        else { /* unknown */
		          PT_ERROR(this_parser, arg3, "check syntax at 'date_lang'");
		        }
		      }
		    }
		    parser_free_node(this_parser, arg3);
		    if (expr) expr->info.expr.arg3 = date_lang;
		    pt_push(this_parser, expr);
		  }
		>>
	|       << int arg_cnt = 1; PT_NODE *arg3 = NULL; >>
		TO_DATE l_paren expression_ << PFOP(PT_TO_DATE); >>
		{ "," expression_ << PSOP; arg_cnt = 2; >>
		  { "," char_string_literal > [arg3]
		    << arg_cnt = 3; >> }
		}
		Right_Paren
		<<
		  if (arg_cnt == 1) { /* 2nd op */
		    PT_NODE *format = parser_new_node(this_parser, PT_VALUE);
		    if (format) {
		      format->type_enum = PT_TYPE_NULL;
		      pt_push(this_parser, format);
		      PSOP;
		    }
		  }
		  if (arg_cnt == 1 || arg_cnt == 2) {
		    PT_NODE *date_lang = parser_new_node(this_parser, PT_VALUE);
		    if (date_lang) {
		      char *lang_str;
		      date_lang->type_enum = PT_TYPE_INTEGER;
		      lang_str = envvar_get("DATE_LANG");
		      if (lang_str && strcasecmp(lang_str, LANG_NAME_KOREAN) == 0)
		        date_lang->info.value.data_value.i = 4;
		      else
		        date_lang->info.value.data_value.i = 2;
		    }
		    if (arg_cnt == 1) { /* default format */
		      date_lang->info.value.data_value.i |= 1;
		    }
		    pt_push(this_parser, date_lang);
		    PTOP;
		  }
		  else if (arg_cnt == 3) { /* valid check */
		    char *lang_str;
		    PT_NODE *date_lang = parser_new_node(this_parser, PT_VALUE);
		    PT_NODE *expr;
		    expr = pt_pop(this_parser);
		    if (date_lang && expr) {
		      date_lang->type_enum = PT_TYPE_INTEGER;
		      if (arg3->info.value.data_value.str != NULL) {
		        lang_str = (char *) arg3->info.value.data_value.str->bytes;
		        if (strcasecmp(lang_str, LANG_NAME_KOREAN) == 0) {
		          date_lang->info.value.data_value.i = 4;
		        }
		        else if (strcasecmp(lang_str, LANG_NAME_ENGLISH) == 0) {
		          date_lang->info.value.data_value.i = 2;
		        }
		        else { /* unknown */
		          PT_ERROR(this_parser, arg3, "check syntax at 'date_lang'");
		        }
		      }
		    }
		    parser_free_node(this_parser, arg3);
		    if (expr) expr->info.expr.arg3 = date_lang;
		    pt_push(this_parser, expr);
		  }
		>>
	|       << int arg_cnt = 1; PT_NODE *arg3 = NULL; >>
		TO_TIME l_paren expression_ << PFOP(PT_TO_TIME); >>
		{ "," expression_ << PSOP; arg_cnt = 2; >>
		  { "," char_string_literal > [arg3]
		    << arg_cnt = 3; >> }
		}
		Right_Paren
		<<
		  if (arg_cnt == 1) { /* 2nd op */
		    PT_NODE *format = parser_new_node(this_parser, PT_VALUE);
		    if (format) {
		      format->type_enum = PT_TYPE_NULL;
		      pt_push(this_parser, format);
		      PSOP;
		    }
		  }
		  if (arg_cnt == 1 || arg_cnt == 2) {
		    PT_NODE *date_lang = parser_new_node(this_parser, PT_VALUE);
		    if (date_lang) {
		      char *lang_str;
		      date_lang->type_enum = PT_TYPE_INTEGER;
		      lang_str = envvar_get("DATE_LANG");
		      if (lang_str && strcasecmp(lang_str, LANG_NAME_KOREAN) == 0)
		        date_lang->info.value.data_value.i = 4;
		      else
		        date_lang->info.value.data_value.i = 2;
		    }
		    if (arg_cnt == 1) { /* default format */
		      date_lang->info.value.data_value.i |= 1;
		    }
		    pt_push(this_parser, date_lang);
		    PTOP;
		  }
		  else if (arg_cnt == 3) { /* valid check */
		    char *lang_str;
		    PT_NODE *date_lang = parser_new_node(this_parser, PT_VALUE);
		    PT_NODE *expr;
		    expr = pt_pop(this_parser);
		    if (date_lang && expr) {
		      date_lang->type_enum = PT_TYPE_INTEGER;
		      if (arg3->info.value.data_value.str != NULL) {
		        lang_str = (char *) arg3->info.value.data_value.str->bytes;
		        if (strcasecmp(lang_str, LANG_NAME_KOREAN) == 0) {
		          date_lang->info.value.data_value.i = 4;
		        }
		        else if (strcasecmp(lang_str, LANG_NAME_ENGLISH) == 0) {
		          date_lang->info.value.data_value.i = 2;
		        }
		        else { /* unknown */
		          PT_ERROR(this_parser, arg3, "check syntax at 'date_lang'");
		        }
		      }
		    }
		    parser_free_node(this_parser, arg3);
		    if (expr) expr->info.expr.arg3 = date_lang;
		    pt_push(this_parser, expr);
		  }
		>>
	|       << int arg_cnt = 1; PT_NODE *arg3 = NULL; >>
		TO_TIMESTAMP l_paren expression_ << PFOP(PT_TO_TIMESTAMP); >>
		{ "," expression_ << PSOP; arg_cnt = 2; >>
		  { "," char_string_literal > [arg3]
		    << arg_cnt = 3; >> }
		}
		Right_Paren
		<<
		  if (arg_cnt == 1) { /* 2nd op */
		    PT_NODE *format = parser_new_node(this_parser, PT_VALUE);
		    if (format) {
		      format->type_enum = PT_TYPE_NULL;
		      pt_push(this_parser, format);
		      PSOP;
		    }
		  }
		  if (arg_cnt == 1 || arg_cnt == 2) {
		    PT_NODE *date_lang = parser_new_node(this_parser, PT_VALUE);
		    if (date_lang) {
		      char *lang_str;
		      date_lang->type_enum = PT_TYPE_INTEGER;
		      lang_str = envvar_get("DATE_LANG");
		      if (lang_str && strcasecmp(lang_str, LANG_NAME_KOREAN) == 0)
		        date_lang->info.value.data_value.i = 4;
		      else
		        date_lang->info.value.data_value.i = 2;
		    }
		    if (arg_cnt == 1) { /* default format */
		      date_lang->info.value.data_value.i |= 1;
		    }
		    pt_push(this_parser, date_lang);
		    PTOP;
		  }
		  else if (arg_cnt == 3) { /* valid check */
		    char *lang_str;
		    PT_NODE *date_lang = parser_new_node(this_parser, PT_VALUE);
		    PT_NODE *expr;
		    expr = pt_pop(this_parser);
		    if (date_lang && expr) {
		      date_lang->type_enum = PT_TYPE_INTEGER;
		      if (arg3->info.value.data_value.str != NULL) {
		        lang_str = (char *) arg3->info.value.data_value.str->bytes;
		        if (strcasecmp(lang_str, LANG_NAME_KOREAN) == 0) {
		          date_lang->info.value.data_value.i = 4;
		        }
		        else if (strcasecmp(lang_str, LANG_NAME_ENGLISH) == 0) {
		          date_lang->info.value.data_value.i = 2;
		        }
		        else { /* unknown */
		          PT_ERROR(this_parser, arg3, "check syntax at 'date_lang'");
		        }
		      }
		    }
		    parser_free_node(this_parser, arg3);
		    if (expr) expr->info.expr.arg3 = date_lang;
		    pt_push(this_parser, expr);
		  }
		>>
	|
		<< PT_NODE *val; >>
		TO_NUMBER l_paren expression_ << PFOP(PT_TO_NUMBER); >>
		{ ","
		  char_string_literal > [val] << pt_push(this_parser, val); >>
		  << PSOP; >>
		}
		Right_Paren
	|
		<< PT_NODE *expr, *prev, *arg;
		   int expr_count = 0; >>
	        LEAST l_paren expression_
		<< expr = parser_new_node(this_parser, PT_EXPR);
		   if (expr) {
		       arg = pt_pop(this_parser);
		       expr->info.expr.op = PT_LEAST;
		       expr->info.expr.arg1 = arg;
		       expr->info.expr.arg2 = expr->info.expr.arg3 = NULL;
		       expr->info.expr.continued_case = 1;
		       expr_count = 1;
		   }
		   PICE(expr);
		   prev = expr;
		>>
		(
		  "," expression_
		  <<
		      if (expr_count == 1) {
		          arg = pt_pop(this_parser);
		          if (prev) prev->info.expr.arg2 = arg;
		          PICE(prev);
			  expr_count = 0;
		      }
		      else {
		          arg = pt_pop(this_parser);
			  expr = parser_new_node(this_parser, PT_EXPR);
			  if (expr) {
			      expr->info.expr.op = PT_LEAST;
			      expr->info.expr.arg1 = prev;
			      expr->info.expr.arg2 = arg;
			      expr->info.expr.arg3 = NULL;
			      expr->info.expr.continued_case = 1;
			  }
			  if (prev && prev->info.expr.continued_case >= 1)
			      prev->info.expr.continued_case++;
			  PICE(expr);
			  prev = expr;
		      }
		  >>
		)*
		Right_Paren
		<< if (expr->info.expr.arg2 == NULL) {
		    expr->info.expr.arg2 =
		        parser_copy_tree_list(this_parser, expr->info.expr.arg1);
		    expr->info.expr.arg2->column_number =
		        -expr->info.expr.arg2->column_number;
		   }
		   pt_push(this_parser, expr);
		>>
	|
		<< PT_NODE *expr, *prev, *arg;
		   int expr_count = 0; >>
	        GREATEST l_paren expression_
		<< expr = parser_new_node(this_parser, PT_EXPR);
		   if (expr) {
		       arg = pt_pop(this_parser);
		       expr->info.expr.op = PT_GREATEST;
		       expr->info.expr.arg1 = arg;
		       expr->info.expr.arg2 = expr->info.expr.arg3 = NULL;
		       expr->info.expr.continued_case = 1;
		       expr_count = 1;
		   }
		   PICE(expr);
		   prev = expr;
		>>
		(
		  "," expression_
		  <<
		      if (expr_count == 1) {
		          arg = pt_pop(this_parser);
		          if (prev) prev->info.expr.arg2 = arg;
		          PICE(prev);
			  expr_count = 0;
		      }
		      else {
		          arg = pt_pop(this_parser);
			  expr = parser_new_node(this_parser, PT_EXPR);
			  if (expr) {
			      expr->info.expr.op = PT_GREATEST;
			      expr->info.expr.arg1 = prev;
			      expr->info.expr.arg2 = arg;
			      expr->info.expr.arg3 = NULL;
			      expr->info.expr.continued_case = 1;
			  }
			  if (prev && prev->info.expr.continued_case >= 1)
			      prev->info.expr.continued_case++;
			  PICE(expr);
			  prev = expr;
		      }
		  >>
		)*
		Right_Paren
		<< if (expr->info.expr.arg2 == NULL) {
		    expr->info.expr.arg2 =
		        parser_copy_tree_list(this_parser, expr->info.expr.arg1);
		    expr->info.expr.arg2->column_number =
		        -expr->info.expr.arg2->column_number;
		   }
		   pt_push(this_parser, expr);
		>>
	;

char_func "expression"
	:
	    POSITION l_paren expression_ << PFOP(PT_POSITION); >>
            IN expression_ Right_Paren << PSOP; >>
	  | << int flag = 0; >>
	    ( INSTR | INSTRB )
	    l_paren expression_ << PFOP(PT_INSTR); >>
            "," expression_ << PSOP; >>
	    { "," expression_ << PTOP; flag = 1; >>
	    }
	    Right_Paren
	    << if (flag == 0) { /* default is 1 */
		   PT_NODE *val = parser_new_node(this_parser, PT_VALUE);
		   if (val) {
		       val->type_enum = PT_TYPE_INTEGER;
		       val->info.value.data_value.i = 1;
		       pt_push(this_parser, val);
		       PTOP;
		   }
	       }
	    >>
	  | << 	PT_NODE *tmp; >>
	    ( SUBSTR | SUBSTRB )
	    l_paren expression_
	    << PFOP(PT_SUBSTRING);
	       tmp = pt_top(this_parser);
	       tmp->info.expr.qualifier = PT_SUBSTR;
	       PICE(tmp);
	    >>
            "," expression_ << PSOP; >>
	      {"," expression_ << PTOP; >>
	      }
	    Right_Paren
	  | << PT_NODE *tmp; >>
	    SUBSTRING l_paren expression_
	    << PFOP(PT_SUBSTRING);
	       tmp = pt_top(this_parser);
	       tmp->info.expr.qualifier = PT_SUBSTR_ORG;
	       PICE(tmp);
	    >>
            FROM expression_ << PSOP; >>
	      {For expression_ << PTOP; >>
	      }
	    Right_Paren
	  |
	    OCTET_LENGTH l_paren expression_ Right_Paren
            << PFOP(PT_OCTET_LENGTH); >>
	  |
	    BIT_LENGTH l_paren expression_ Right_Paren
            << PFOP(PT_BIT_LENGTH); >>
	  |
	    ( CHAR_LENGTH | LENGTH | LENGTHB )
	    l_paren expression_ Right_Paren
            << PFOP(PT_CHAR_LENGTH); >>
	  |
	    LOWER l_paren expression_ Right_Paren
            << PFOP(PT_LOWER); >>
	  |
	    UPPER l_paren expression_ Right_Paren
            << PFOP(PT_UPPER); >>
	  |
	  	TRANSLATE l_paren expression_  << PFOP(PT_TRANSLATE); >>
			"," expression_ << PSOP; >>
			"," expression_ << PTOP; >>
			Right_Paren
	  |
	  	REPLACE l_paren expression_  << PFOP(PT_REPLACE); >>
			"," expression_  << PSOP; >>
			{ "," expression_ << PTOP; >> }
			Right_Paren
	  |
	  	LPAD l_paren expression_  << PFOP(PT_LPAD); >>
			"," expression_  << PSOP; >>
			{ "," expression_ << PTOP; >> }
			Right_Paren
	  |
	  	RPAD l_paren expression_  << PFOP(PT_RPAD); >>
			"," expression_  << PSOP; >>
			{ "," expression_ << PTOP; >> }
			Right_Paren
	  | 	<< 	PT_NODE *arg2 = NULL, *tmp;
	  			PT_MISC_TYPE qualifier = PT_LEADING; >>
	  	LTRIM l_paren expression_
		{ "," expression_ << arg2 = pt_pop(this_parser); >> }
		Right_Paren
		<< 	PFOP(PT_LTRIM);
			tmp = pt_top(this_parser);
			tmp->info.expr.qualifier = qualifier;
			tmp->info.expr.arg2  = arg2;
                        PICE(tmp);
		>>
	  | 	<< 	PT_NODE *arg2 = NULL, *tmp;
	  			PT_MISC_TYPE qualifier = PT_TRAILING; >>
	  	RTRIM l_paren expression_
		{ "," expression_ << arg2 = pt_pop(this_parser); >> }
		Right_Paren
		<< 	PFOP(PT_RTRIM);
			tmp = pt_top(this_parser);
			tmp->info.expr.qualifier = qualifier;
			tmp->info.expr.arg2  = arg2;
                        PICE(tmp);
		>>
	  |
            << PT_NODE *arg2 = NULL, *tmp; PT_MISC_TYPE qualifier = PT_BOTH; >>
	    TRIM  l_paren
	    /* we are going to break up trim into two unambiguous cases */
	    ( ( LEADING << qualifier = PT_LEADING; >>
	      | TRAILING << qualifier = PT_TRAILING; >>
	      | BOTH << qualifier = PT_BOTH; >>
	      )
              { expression_ << arg2 = pt_pop(this_parser); >>
	      }
	      FROM
	      expression_
	    |
              expression_
	      { FROM  << arg2 = pt_pop(this_parser); >>
                expression_
	      }
            )
	    Right_Paren
            << PFOP(PT_TRIM);
	       tmp = pt_top(this_parser);
	       tmp->info.expr.qualifier = qualifier;
	       tmp->info.expr.arg2 = arg2;
               PICE(tmp);
	    >>
	  |
	    CHR l_paren expression_ Right_Paren
            << PFOP(PT_CHR); >>
	;

instnum_func "expression"
    :   << PT_NODE *node; bool rownum; >>
        ( INST_NUM Left_Paren Right_Paren
          << rownum = false; >>
        | ROWNUM
          << rownum = true; >>
        )
        <<
            node = parser_new_node(this_parser, PT_EXPR);
            if (node) {
                node->info.expr.op = (rownum) ? PT_ROWNUM : PT_INST_NUM;
                PT_EXPR_INFO_SET_FLAG(node, PT_EXPR_INFO_INSTNUM_C);
            }
            pt_push(this_parser, node);
            if (instnum_check == 0)
                PT_ERRORmf2(this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
                            MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR,
                            "INST_NUM() or ROWNUM", "INST_NUM() or ROWNUM");
        >>
        ;

orderbynum_func "expression"
    :   << PT_NODE *node; >>
        ORDERBY_NUM Left_Paren Right_Paren
        <<
            node = parser_new_node(this_parser, PT_EXPR);
            if (node) {
                node->info.expr.op = PT_ORDERBY_NUM;
                PT_EXPR_INFO_SET_FLAG(node, PT_EXPR_INFO_ORDERBYNUM_C);
            }
            pt_push(this_parser, node);
            if (orderbynum_check == 0)
                PT_ERRORmf2(this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
                            MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR,
                            "ORDERBY_NUM()", "ORDERBY_NUM()");
        >>
        ;

cast_func "expression"
	:   << PT_NODE *dt, *set_dt, *tmp; PT_TYPE_ENUM typ;
            >>
	    CAST l_paren expression_ << PFOP(PT_CAST); >>
            AS data__type > [typ, dt]
	    << if (! dt) {
		  dt=parser_new_node(this_parser, PT_DATA_TYPE);
  		  if (dt) {
		     dt->type_enum = typ;
		     dt->data_type = NULL;
		  }
	       } else if ((typ == PT_TYPE_SET) ||
 			  (typ == PT_TYPE_MULTISET) ||
 		      	  (typ == PT_TYPE_SEQUENCE)) {
 	           set_dt = parser_new_node(this_parser, PT_DATA_TYPE);
 		   if (set_dt) {
		       set_dt->type_enum = typ;
		       set_dt->data_type = dt;
		       dt = set_dt;
 		   }
	       }
               tmp = pt_top(this_parser);
	       if (tmp)
		  tmp->info.expr.cast_type = dt;
	    >>
	    Right_Paren
	;

case_expr "case expression"
	:   << PT_NODE *node, *case_oper, *prev, *curr, *ppp, *qqq, *rrr;
	       PT_NODE *nodep;
	       int expr_cnt = 0; >>
	    ( NULLIF l_paren expression_ << PFOP(PT_NULLIF); >>
		"," expression_ << PSOP; >>
		Right_Paren
	    |
		<< PT_NODE *expr, *prev, *arg;
		   int expr_count = 0; >>
	        COALESCE l_paren expression_
		<< expr = parser_new_node(this_parser, PT_EXPR);
		   if (expr) {
		       arg = pt_pop(this_parser);
		       expr->info.expr.op = PT_COALESCE;
		       expr->info.expr.arg1 = arg;
		       expr->info.expr.arg2 = expr->info.expr.arg3 = NULL;
		       expr->info.expr.continued_case = 1;
		       expr_count = 1;
		   }
		   PICE(expr);
		   prev = expr;
		>>
		(
		  "," expression_
		  <<
		      if (expr_count == 1) {
		          arg = pt_pop(this_parser);
		          if (prev) prev->info.expr.arg2 = arg;
		          PICE(prev);
			  expr_count = 0;
		      }
		      else {
		          arg = pt_pop(this_parser);
			  expr = parser_new_node(this_parser, PT_EXPR);
			  if (expr) {
			      expr->info.expr.op = PT_COALESCE;
			      expr->info.expr.arg1 = prev;
			      expr->info.expr.arg2 = arg;
			      expr->info.expr.arg3 = NULL;
			      expr->info.expr.continued_case = 1;
			  }
			  if (prev && prev->info.expr.continued_case >= 1)
			      prev->info.expr.continued_case++;
			  PICE(expr);
			  prev = expr;
		      }
		  >>
		)*
		Right_Paren
		<< if (expr->info.expr.arg2 == NULL) {
		    expr->info.expr.arg2 =
		        parser_new_node(this_parser, PT_VALUE);
		    if (expr->info.expr.arg2) {
			expr->info.expr.arg2->type_enum = PT_TYPE_NULL;
		        expr->info.expr.arg2->column_number =
		            -expr->info.expr.arg2->column_number;
		    }
		   }
		   pt_push(this_parser, expr);
		>>

	    | CASE expression_
	      << case_oper = pt_pop(this_parser); >>
	      simple_when_clause [case_oper]
	      << node = prev = pt_pop(this_parser);
		 if (node) node->info.expr.continued_case = 0;
	      >>
	      ( simple_when_clause [case_oper]
		<< curr = pt_pop(this_parser);
		   if (curr) curr->info.expr.continued_case = 1;
		   if (prev) prev->info.expr.arg2 = curr; /* else res */
                   PICE(prev);
		   prev = curr;
		>>
	      )*
	      { ELSE expression_
		<< ppp = pt_pop(this_parser);
		   if (prev) prev->info.expr.arg2 = ppp;
                   PICE(prev);
		>>
	      }
	      END
	      /* if no else clause, assume ELSE NULL */
	      <<  if (prev && !prev->info.expr.arg2) {
		      ppp = parser_new_node(this_parser, PT_VALUE);
                      if (ppp) ppp->type_enum = PT_TYPE_NULL;
		      prev->info.expr.arg2 = ppp;
                      PICE(prev);
		 }
		 if (case_oper) parser_free_node(this_parser, case_oper);
	      >>
	      << pt_push(this_parser, node); >>
	    | CASE searched_when_clause
	      << node = prev = pt_pop(this_parser);
		 if (node) node->info.expr.continued_case = 0;
	      >>
	      ( searched_when_clause
		<< curr = pt_pop(this_parser);
		   if (curr) curr->info.expr.continued_case = 1;
		   if (prev) prev->info.expr.arg2 = curr; /* else res */
                   PICE(prev);
		   prev = curr;
		>>
	      )*
	      { ELSE expression_
		<< ppp = pt_pop(this_parser);
		   if (prev) prev->info.expr.arg2 = ppp;
                   PICE(prev);
		>>
	      }
	      END
	      /* if no else clause, assume ELSE NULL */
	      << if (prev && !prev->info.expr.arg2) {
		     ppp = parser_new_node(this_parser, PT_VALUE);
                     if (ppp) ppp->type_enum = PT_TYPE_NULL;
		     prev->info.expr.arg2 = ppp;
                     PICE(prev);
		 }
	      >>
	      << pt_push(this_parser, node); >>
	    | DECODE_ 	/* comes from CASE simple */
	      l_paren expression_
	      << case_oper = pt_pop(this_parser); >>
	      decode_comma_clause [case_oper]
	      << node = prev = pt_pop(this_parser);
		 if (node) node->info.expr.continued_case = 0;
	      >>
	      (
	      "," expression_	/* search, result, default */
	      <<
	        expr_cnt++;
		if (expr_cnt == 2) { /* new pair */
		  rrr = pt_pop(this_parser);
		  ppp = pt_pop(this_parser);
		  nodep = parser_new_node(this_parser, PT_EXPR);
		  if (nodep) {
		    nodep->info.expr.op = PT_DECODE;
		    qqq = parser_new_node(this_parser, PT_EXPR);
		    if (qqq) {
		      qqq->info.expr.op = PT_EQ;
		      qqq->info.expr.qualifier = PT_EQ_TORDER;
		      qqq->info.expr.arg1 = parser_copy_tree_list(this_parser, case_oper);
		      qqq->info.expr.arg2 = ppp;
		      nodep->info.expr.arg3 = qqq;
		      PICE(qqq);
		    }
		    nodep->info.expr.arg1 = rrr;
		  }
		  PICE(nodep);
		  pt_push(this_parser, nodep);

		  curr = pt_pop(this_parser);
		  if (curr) curr->info.expr.continued_case = 1;
		  if (prev) prev->info.expr.arg2 = curr; /* else res */
                  PICE(prev);
		  prev = curr;

		  expr_cnt = 0;
		}
	      >>
	      )*
	      Right_Paren
	      <<
	        if (expr_cnt == 1) { /* default */
		   ppp = pt_pop(this_parser);
		   if (prev) prev->info.expr.arg2 = ppp;
                   PICE(prev);
		}
	        /* if no else clause, assume ELSE NULL */
	        if (prev && !prev->info.expr.arg2) {
		   ppp = parser_new_node(this_parser, PT_VALUE);
                   if (ppp) ppp->type_enum = PT_TYPE_NULL;
		   prev->info.expr.arg2 = ppp;
                   PICE(prev);
		}
		if (case_oper) parser_free_node(this_parser, case_oper);
	      >>
	      << pt_push(this_parser, node); >>
	    | NVL l_paren expression_ << PFOP(PT_NVL); >>
		"," expression_ << PSOP; >>
		Right_Paren
	    | NVL2 l_paren expression_ << PFOP(PT_NVL2); >>
		"," expression_ << PSOP; >>
		"," expression_ << PTOP; >>
		Right_Paren
	    )
	;

decode_comma_clause < [PT_NODE *oper] "case expression"
	:   << PT_NODE *node, *ppp, *qqq; >>
	    "," expression_
	    << ppp = pt_pop(this_parser);
	       node = parser_new_node(this_parser, PT_EXPR);
	       if (node) {
		  node->info.expr.op = PT_DECODE;
		  qqq = parser_new_node(this_parser, PT_EXPR);
		  if (qqq) {
		     qqq->info.expr.op = PT_EQ;
		     qqq->info.expr.qualifier = PT_EQ_TORDER;
		     qqq->info.expr.arg1 = parser_copy_tree_list(this_parser, $oper);
		     qqq->info.expr.arg2 = ppp;
		     node->info.expr.arg3 = qqq;
                     PICE(qqq);
		  }
	       }
	    >>
	    "," expression_
	    << ppp = pt_pop(this_parser);
	       if (node) node->info.expr.arg1 = ppp;
               PICE(node);
	       pt_push(this_parser, node);
	    >>
	;

simple_when_clause < [PT_NODE *oper] "case expression"
	:   << PT_NODE *node, *ppp, *qqq; >>
	    WHEN expression_
	    << ppp = pt_pop(this_parser);
	       node = parser_new_node(this_parser, PT_EXPR);
	       if (node) {
		  node->info.expr.op = PT_CASE;
		  node->info.expr.qualifier = PT_SIMPLE_CASE;
		  qqq = parser_new_node(this_parser, PT_EXPR);
		  if (qqq) {
		     qqq->info.expr.op = PT_EQ;
		     qqq->info.expr.arg1 =
			 parser_copy_tree_list(this_parser, $oper);
		     qqq->info.expr.arg2 = ppp;
		     node->info.expr.arg3 = qqq;
                     PICE(qqq);
		  }
	       }
	    >>
	    THEN expression_
	    << ppp = pt_pop(this_parser);
	       if (node) node->info.expr.arg1 = ppp;
               PICE(node);
	       pt_push(this_parser, node);
	    >>
	;

searched_when_clause "case expression"
	:   << PT_NODE *node, *ppp;
	       node = parser_new_node(this_parser, PT_EXPR);
	       if (node) {
		  node->info.expr.op = PT_CASE;
		  node->info.expr.qualifier = PT_SEARCHED_CASE;
	       }
	    >>
	    WHEN search_condition
	    << ppp = pt_pop(this_parser);
	       if (node) node->info.expr.arg3 = ppp;
               PICE(node);
	    >>
	    THEN expression_
	    << ppp = pt_pop(this_parser);
	       if (node) node->info.expr.arg1 = ppp;
               PICE(node);
	       pt_push(this_parser, node);
	    >>
	;

extract_expr "extract expression"
	:   << PT_NODE *tmp; PT_MISC_TYPE datetime_component; >>
	    EXTRACT Left_Paren
       	    extract_field > [datetime_component]
       	    FROM
       	    extract_source
       	    Right_Paren
       	    << PFOP(PT_EXTRACT);
	       tmp = pt_top(this_parser);
     	       if (tmp) tmp->info.expr.qualifier = datetime_component;
       	    >>
	;

extract_field > [PT_MISC_TYPE component] "extract field"
	:   datetime_field > [$component]
	;

datetime_field > [PT_MISC_TYPE component] "datetime field"
	:   YEAR	 << $component = PT_YEAR; >>
  	    | MONTH	 << $component = PT_MONTH; >>
  	    | DAY    	 << $component = PT_DAY; >>
 	    | HOUR  	 << $component = PT_HOUR; >>
 	    | MINUTE  	 << $component = PT_MINUTE; >>
 	    | SECOND	 << $component = PT_SECOND; >>
	;

extract_source "extract source"
	:   expression_
	;

generic_function_call_body < [const char *callee] "expression"
	:	<< PT_NODE *name, *func_node, *meth_node;
		   func_node = parser_new_node(this_parser, PT_FUNCTION);
	           meth_node = parser_new_node(this_parser, PT_METHOD_CALL);
		   if (func_node) func_node->info.function.generic_name = callee;
		>>
	  Left_Paren
	    { expression_ ("," expression_ << PLIST; >>)*
		<< if(func_node) func_node->info.function.arg_list=pt_pop(this_parser); >>
	    }
		<< if(func_node) func_node->info.function.function_type=PT_GENERIC; >>
	  Right_Paren
  	    { ON_ call_target
	        /*
		 * if there is an ON, it must be a method call.  Create a new
		 * node and fill in the fields from the function node.
		 */
		<< if (meth_node)
	           { meth_node->info.method_call.method_name = name =
			parser_new_node(this_parser, PT_NAME);
		     if (func_node && name) name->info.name.original =
			func_node->info.function.generic_name;
		     if (func_node) meth_node->info.method_call.arg_list =
			func_node->info.function.arg_list;
		     meth_node->info.method_call.call_or_expr =	PT_IS_MTHD_EXPR;
		     meth_node->info.method_call.on_call_target=pt_pop(this_parser);
		   }
		   /* replace the function node with the method node */
		   func_node = meth_node;
		>>
	    }
	    	<< pt_push(this_parser, func_node);
	    	   cannot_prepare = true;
	    	   cannot_cache = true;
	    	>>
	;

table_set_function_call "expression"
	:	<< PT_NODE *func_node; long ftype;
		   func_node=parser_new_node(this_parser, PT_FUNCTION);
		>>
	  ( SET 	<< ftype = F_TABLE_SET; >>
	  | SEQUENCE 	<< ftype = F_TABLE_SEQUENCE; >>
	  | LIST 	<< ftype = F_TABLE_SEQUENCE; >>
	  | MULTISET	<< ftype = F_TABLE_MULTISET; >>
	  )
	  subquery
	  	<< if(func_node)
		   { func_node->info.function.arg_list=pt_pop(this_parser);
	    	     func_node->info.function.function_type=ftype;
	    	   }
		   pt_push(this_parser, func_node);
		>>
	;

search_condition "search condition"
	: boolean_term
          ( OR 		 << PFOP(PT_OR); >>
	    boolean_term << PSOP; >>
          )*
	;

boolean_term "expression"
	: boolean_factor
          ( AND 	   << PFOP(PT_AND); >>
	    boolean_factor << PSOP; >>
          )*
	;

boolean_factor "expression"
	: NOT boolean_primary << PFOP(PT_NOT); >>
	| boolean_primary
	;

boolean_primary "expression"
	: predicate
	;

predicate "predicate"
	: << PT_NODE *e, *attr;
             PT_JOIN_TYPE join_type = PT_JOIN_NONE;
             int saved_wjc = 0;
          >>
          exists_predicate
	| expression_ {
                       { Left_Paren PLUS Right_Paren
                         << join_type = PT_JOIN_RIGHT_OUTER;
                            saved_wjc = within_join_condition;
                            within_join_condition = 1;
                         >>
                       }
                       ( comparison_pred
                       | between_pred
                       | in_pred
                       | range_pred
                       | like_pred
                       | null_pred
                       | set_pred)
                         << if (join_type == PT_JOIN_RIGHT_OUTER)
                                within_join_condition = saved_wjc;
                         >>
                       { Left_Paren PLUS Right_Paren
                         << if (join_type == PT_JOIN_RIGHT_OUTER)
                                join_type = PT_JOIN_FULL_OUTER; /* error */
                            else
                                join_type = PT_JOIN_LEFT_OUTER;
                         >>
                       }
                      }
          <<
             /*
              * Oracle style outer join support: convert to ANSI standard style
              * only permit the following predicate
              *
              * 'single_column(+) op expression_'
              * 'expression_      op single_column(+)'
              *
              */
             /* marking Oracle style left/right outer join operator */
             if (join_type != PT_JOIN_NONE) {
                 e = pt_pop(this_parser);
                 if (e && e->node_type == PT_EXPR) {
                     switch (join_type) {
                       case PT_JOIN_LEFT_OUTER:
                         attr = e->info.expr.arg2;
                         break;
                       case PT_JOIN_RIGHT_OUTER:
                         attr = e->info.expr.arg1;
                         break;
                       case PT_JOIN_FULL_OUTER:
                         PT_ERROR(this_parser, e, "a predicate may reference only one outer-joined table");
                         attr = NULL;
                         break;
                       default:
                         PT_ERROR(this_parser, e, "check syntax at '(+)'");
                         attr = NULL;
                         break;
                     }

                     if (attr) {
                         while (attr->node_type == PT_DOT_)
                             attr = attr->info.dot.arg2;

                         if (attr->node_type == PT_NAME) {
                             switch (join_type) {
                               case PT_JOIN_LEFT_OUTER:
                                 PT_EXPR_INFO_SET_FLAG(e,
				                       PT_EXPR_INFO_LEFT_OUTER);
                                 found_Oracle_outer = true;
                                 break;
                               case PT_JOIN_RIGHT_OUTER:
                                 PT_EXPR_INFO_SET_FLAG(e,
				                       PT_EXPR_INFO_RIGHT_OUTER);
                                 found_Oracle_outer = true;
                                 break;
                               default:
                                 break;
                             }
                     } else {
                             PT_ERROR(this_parser, e, "'(+)' operator can be applied only to a column, not to an arbitary expression");
                         }
                     }
                 }
                 pt_push(this_parser, e);
             } /* if (join_type != PT_JOIN_INNER) */
          >>
/*
	this is what it should be, instead of optinal predicates above
	| l_paren search_condition Right_Paren
*/
	;


/* predicates */

comparison_pred "comparison predicate"  /*includes quantified */
        : << PT_NODE *e, *opd1, *opd2, *subq;
             PT_OP_TYPE op;
             bool found_paren_set_expr = false;
          >>
          comp_op expression_
          <<
             opd2 = pt_pop(this_parser);  e = pt_pop(this_parser);
             if (e && this_parser->error_msgs == NULL) {
               e->info.expr.arg2 = opd2;
               opd1 = e->info.expr.arg1;
               op = e->info.expr.op;
               /* convert parentheses set expr value into sequence */
               if (opd1) {
                 if (opd1->node_type == PT_VALUE &&
                     opd1->type_enum == PT_TYPE_EXPR_SET) {
                   opd1->type_enum = PT_TYPE_SEQUENCE;
                   found_paren_set_expr = true;
                 } else if (PT_IS_QUERY_NODE_TYPE(opd1->node_type)) {
                   if ((subq = pt_get_subquery_list(opd1)) &&
                       subq->next == NULL) {
                     /* single-column subquery */
                   } else {
                     found_paren_set_expr = true;
                   }
                 }
               }
               if (opd2) {
                 if (opd2->node_type == PT_VALUE &&
                     opd2->type_enum == PT_TYPE_EXPR_SET) {
                   opd2->type_enum = PT_TYPE_SEQUENCE;
                   found_paren_set_expr = true;
                 } else if (PT_IS_QUERY_NODE_TYPE(opd2->node_type)) {
                   if ((subq = pt_get_subquery_list(opd2)) &&
                       subq->next == NULL) {
                     /* single-column subquery */
	           } else {
                     found_paren_set_expr = true;
                   }
                 }
               }
               if (op == PT_EQ || op == PT_NE) {
                   /* expression number check */
                 if (found_paren_set_expr == true &&
                     pt_check_set_count_set(this_parser, opd1, opd2)) {
                   if (PT_IS_QUERY_NODE_TYPE(opd1->node_type)) {
                     pt_select_list_to_one_col(this_parser, opd1, true);
                   }
                   if (PT_IS_QUERY_NODE_TYPE(opd2->node_type)) {
                     pt_select_list_to_one_col(this_parser, opd2, true);
                   }
                   /* rewrite parentheses set expr equi-comparions predicate
                    * as equi-comparison predicates tree of each elements.
                    * for example, (a, b) = (x, y) -> a = x and b = y
                    */
                   if (op == PT_EQ &&
                       pt_is_set_type(opd1) && pt_is_set_type(opd2)) {
                     e = pt_rewrite_set_eq_set(this_parser, e);
                   }
                 }
                 /* mark as single tuple list */
                 if (PT_IS_QUERY_NODE_TYPE(opd1->node_type)) {
                   opd1->info.query.single_tuple = 1;
                 }
                 if (PT_IS_QUERY_NODE_TYPE(opd2->node_type)) {
                   opd2->info.query.single_tuple = 1;
                 }
               } else {
                 if (found_paren_set_expr == true) { /* operator check */
                    PT_ERRORf(this_parser, e, "check syntax at %s, illegal operator.", pt_show_binopcode(op));
                 }
               }
             } /* if (e) */
             PICE(e);
             pt_push(this_parser, e);
          >>
	;

comp_op "comparison operation"
	:	<< PT_NODE *expr; PT_OP_TYPE t = 0; PFOP(t); >>
	  ( "="  ( ALL  <<t=PT_EQ_ALL; >>
		 | SOME <<t=PT_EQ_SOME;>>
		 | ANY  <<t=PT_EQ_SOME;>>
		 |      <<t=PT_EQ;     >>
		 )
	  | "<>" ( ALL  <<t=PT_NE_ALL; >>
		 | SOME <<t=PT_NE_SOME;>>
		 | ANY  <<t=PT_NE_SOME;>>
		 |	<<t=PT_NE;     >>
		 )
	  | ">"  ( ALL  <<t=PT_GT_ALL; >>
		 | SOME <<t=PT_GT_SOME;>>
		 | ANY  <<t=PT_GT_SOME;>>
		 |	<<t=PT_GT;     >>
		 )
	  | ">=" ( ALL  <<t=PT_GE_ALL; >>
		 | SOME <<t=PT_GE_SOME;>>
		 | ANY  <<t=PT_GE_SOME;>>
		 |	<<t=PT_GE;     >>
		 )
	  | "<"  ( ALL	<<t=PT_LT_ALL; >>
		 | SOME <<t=PT_LT_SOME;>>
		 | ANY	<<t=PT_LT_SOME;>>
		 |	<<t=PT_LT;     >>
		 )
	  | "<=" ( ALL	<<t=PT_LE_ALL; >>
		 | SOME <<t=PT_LE_SOME;>>
		 | ANY	<<t=PT_LE_SOME;>>
		 |	<<t=PT_LE;     >>
		 )
	  )	 << expr = pt_pop(this_parser);
                    expr->info.expr.op = t;
                    pt_push(this_parser, expr); >>
	;

like_pred "like predicate"
	: 	<< PT_NODE *esc, *lit; >>
	  ( NOT LIKE << PFOP(PT_NOT_LIKE); >>
	  | LIKE     << PFOP(PT_LIKE); >>
	  ) pattern
	  { ESCAPE << PFOP(PT_LIKE_ESCAPE); >>
	    char_string_literal >[lit]
		<<if ((esc=pt_top(this_parser))) esc->info.expr.arg2=lit; PICE(esc); >>
	  }	<< PSOP; >>
	;

pattern	"pattern"
	: expression_
	;

null_pred "null predicate"
	: IS
	  ( NOT Null << PFOP(PT_IS_NOT_NULL); >>
	  | Null     << PFOP(PT_IS_NULL); >>
	  )
	;

exists_predicate "exists predicate"
	: EXISTS expression_ << PFOP(PT_EXISTS); >>
	;

set_pred "set predicate"
	: set_op expression_ << PSOP;>>
	;

between_pred "between predicate"
	: 	<< PT_OP_TYPE t; >>
	  ( NOT BETWEEN  << t=PT_NOT_BETWEEN; >>
	  | BETWEEN      << t=PT_BETWEEN; >>
	  )  		 << PFOP(t); >>
	  expression_ AND << PFOP(PT_BETWEEN_AND); >>
	  expression_     << PSOP; PSOP; >>
	;

in_pred "in predicate"
	: 	<< PT_NODE *node, *t, *v, *lhs, *rhs, *subq;
                   bool is_paren = false;
                   bool found_paren_set_expr = false;
                   bool found_match = false;
                   int lhs_cnt, rhs_cnt = 0;
                >>
          ( NOT IN << PFOP(PT_IS_NOT_IN);>>
          | IN     << PFOP(PT_IS_IN); >>
	  )
	  ( Left_Paren
            expression_list << is_paren = true; >>
            Right_Paren
          | expression_
          )
          <<
             t = pt_pop(this_parser); node = pt_pop(this_parser);
             if (node) {
               lhs = node->info.expr.arg1;
               /* convert lhs parentheses set expr value into
                * sequence value */
               if (lhs) {
                 if (lhs->node_type == PT_VALUE &&
                     lhs->type_enum == PT_TYPE_EXPR_SET) {
                   lhs->type_enum = PT_TYPE_SEQUENCE;
                   found_paren_set_expr = true;
                 } else if (PT_IS_QUERY_NODE_TYPE(lhs->node_type)) {
                   if ((subq = pt_get_subquery_list(lhs)) &&
                        subq->next == NULL) {
		         /* single column subquery */
		       } else {
                     found_paren_set_expr = true;
		       }
                 }
               }

               if (is_paren == true) { /* convert to multi-set */
                 v = parser_new_node(this_parser, PT_VALUE);
                 if (v) {
                   v->info.value.data_value.set = t;
                   v->type_enum = PT_TYPE_MULTISET;
                 } /* if (v) */
                 node->info.expr.arg2 = v;
               } else {
                 /* convert subquery-starting parentheses set expr
                  * ( i.e., {subquery, x, y, ...} ) into multi-set */
                 if (t->node_type == PT_VALUE &&
                     t->type_enum == PT_TYPE_EXPR_SET) {
                   is_paren = true; /* mark as parentheses set expr */
                   t->type_enum = PT_TYPE_MULTISET;
                 }
                 node->info.expr.arg2 = t;
               }

               rhs = node->info.expr.arg2;
               if (is_paren == true) {
                 rhs = rhs->info.value.data_value.set;
               }
               /* for each rhs elements, convert parentheses
                * set expr value into sequence value */
               for (t = rhs; t; t = t->next) {
                 if (t->node_type == PT_VALUE &&
                     t->type_enum == PT_TYPE_EXPR_SET) {
                     t->type_enum = PT_TYPE_SEQUENCE;
                     found_paren_set_expr = true;
                 } else if (PT_IS_QUERY_NODE_TYPE(t->node_type)) {
                   if ((subq = pt_get_subquery_list(t)) &&
                        subq->next == NULL) {
		         /* single column subquery */
		       } else {
                     found_paren_set_expr = true;
		       }
                 }
               }

               if (found_paren_set_expr == true) {
                 /* expression number check */
                 if ((lhs_cnt = pt_get_expression_count(lhs)) < 0) {
		       found_match = true;
		     } else {
                   for (t = rhs; t; t = t->next) {
                     rhs_cnt = pt_get_expression_count(t);
                     if ((rhs_cnt < 0) || (lhs_cnt == rhs_cnt)) {
                       /* can not check negative rhs_cnt. go ahead */
                       found_match = true;
                       break;
                     }
                   }
                 }

		     if (found_match == true) {
                   /* convert select list of parentheses set expr
                    * into that of sequence value */
                   if (pt_is_query(lhs)) {
                     pt_select_list_to_one_col(this_parser, lhs, true);
                   }
                   for (t = rhs; t; t = t->next) {
                     if (pt_is_query(t)) {
                         pt_select_list_to_one_col(this_parser, t, true);
                     }
                   }
		     } else {
                   PT_ERRORmf2(this_parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		                       MSGCAT_SEMANTIC_ATT_CNT_COL_CNT_NE,
		                       lhs_cnt, rhs_cnt);
                 }
               }
             }
             pt_push(this_parser, node);
          >>
	;

range_pred "range predicate"
        : RANGE << PFOP(PT_RANGE); >>
          Left_Paren range_list Right_Paren << PSOP; >>
        ;

range_list "range list"
        : range_
          ( OR range_ <<PLISTOR;>> )*
        ;

range_ "range"
        : << PT_OP_TYPE t; >>
          ( expression_ ( ( GE_LE  << t = PT_BETWEEN_GE_LE; >>
                          | GE_LT  << t = PT_BETWEEN_GE_LT; >>
                          | GT_LE  << t = PT_BETWEEN_GT_LE; >>
                          | GT_LT  << t = PT_BETWEEN_GT_LT; >>
                          )           << PFOP(t); >>
                          expression_ << PSOP;    >>
                        | "="      << t = PT_BETWEEN_EQ_NA;  PFOP(t); >>
                        | ( GE_INF << t = PT_BETWEEN_GE_INF; >>
                          | GT_INF << t = PT_BETWEEN_GT_INF; >>
                          )           << PFOP(t); >>
                          Max
                        )
          | Min ( INF_LE << t = PT_BETWEEN_INF_LE; >>
                | INF_LT << t = PT_BETWEEN_INF_LT; >>
                )
                expression_ << PFOP(t); >>
          )
          ;

set_op "set comparison"
	: 	<< PT_OP_TYPE t; >>
	  ( SETEQ      << t=PT_SETEQ; >>
          | SETNEQ     << t=PT_SETNEQ; >>
          | SUBSET     << t=PT_SUBSET; >>
          | SUBSETEQ   << t=PT_SUBSETEQ; >>
          | SUPERSETEQ << t=PT_SUPERSETEQ; >>
          | SUPERSET   << t=PT_SUPERSET; >>
          ) 	<< PFOP(t); >>
	;

subquery "subquery"
	: 	<< PT_NODE *stmt; >>
	  LEFT_PAREN csql_query Right_Paren
	  	<< stmt = pt_pop(this_parser);
                    if (within_join_condition) {
                        PT_ERRORm(this_parser, stmt, MSGCAT_SET_PARSER_SYNTAX,
                                  MSGCAT_SYNTAX_JOIN_COND_SUBQ);
                    }
 	  	   /* this does not do anything for union, difference, etc
 	  	    * it only exists to force parens around
 	  	    * subqueries that are select statements.
 	  	    */
 		   if(stmt) stmt->info.query.is_subquery=PT_IS_SUBQUERY;
                   pt_push(this_parser, stmt);
		>>
	;

/*
  We have to combine several things as path_expr:
      name.name.name...
      name.name(arg_list)  -- not allowed per sheng 4-1-92
      name[x].name[y]...
  Parser treats 'dot' as a binary operator.
*/

path_expression "path expression"
	: 	<< PT_NODE *dot=NULL, *lnk=NULL; PT_MISC_TYPE cls = PT_NORMAL; >>
		( parameter_ > [lnk] <<pt_push(this_parser, lnk);>>
	 |
	  	{ CLASS << cls = PT_META_CLASS; >> }
	  	path_link << lnk = pt_pop(this_parser);
		       if (lnk) lnk->info.name.meta_class = cls;
		       pt_push(this_parser, lnk);
		    >>
	 	)
	  	(
	   	  (
		  	(<< /* this is before the token match so the
	               dot node gets the right line number */
	        dot = parser_new_node(this_parser, PT_DOT_);>>
	     "\."|"\->"
		 	)
			<<
				if (dot)
				{
				  dot->info.dot.arg1 = pt_pop(this_parser);
				}
				pt_push(this_parser, dot);
			>>
          	path_link <<PSOP;>>
          )*
          |
          "\." IDENTITY
		  << /* should tag the name as an oid
          		     Name resolution should be modified
          		     to resolve this as only a correlation name. */
		  >>
          |
          "\." OBJECT
          <<
              /* internally used,
               * if it is attr.object and the attr is TEXT,
               * the attr is not replaced with attr.tdata */
              lnk = pt_pop(this_parser);
	      if (lnk && lnk->node_type == PT_NAME) {
                  PT_NAME_INFO_SET_FLAG(lnk, PT_NAME_INFO_EXTERNAL);
	      }
              pt_push(this_parser, lnk);
          >>
          |
          "\." STAR
          <<
              lnk = pt_pop(this_parser);
	      if (lnk && lnk->node_type == PT_NAME &&
		  lnk->info.name.meta_class == PT_META_CLASS) {
		  /* don't allow "class class_variable.*" */
		  PT_ERROR(this_parser, lnk, "check syntax at '*'");
	      }
	      else {
                  if (lnk) lnk->type_enum = PT_TYPE_STAR;
	      }
              pt_push(this_parser, lnk);
          >>
		)
	  <<
	  	if ( dot
			&& dot->node_type == PT_DOT_
			&& dot->info.dot.arg2
			&& dot->info.dot.arg2->node_type == PT_NAME )
		{
			PT_NODE * serial_value = NULL;
			PT_NODE * name = dot->info.dot.arg2;
			PT_NODE * name_str=NULL;
			unsigned long save_custom;

			if ( intl_mbs_casecmp( name->info.name.original,
			                  "current_value" ) == 0 ||
			     intl_mbs_casecmp( name->info.name.original,
			                  "currval" ) == 0 )
			{
				dot = pt_pop(this_parser);
				serial_value = parser_new_node(this_parser, PT_EXPR);
				serial_value->info.expr.op = PT_CURRENT_VALUE;
				name_str = parser_new_node(this_parser, PT_VALUE );

				save_custom = this_parser->custom_print;
				this_parser->custom_print |= PT_SUPPRESS_QUOTES;

				name_str->info.value.data_value.str =
						pt_print_bytes( this_parser, dot->info.dot.arg1 );
				this_parser->custom_print = save_custom;

				name_str->info.value.data_value.str->length =
						strlen( (char *)name_str->info.value.data_value.str->bytes );
				name_str->info.value.text = (char *)
								name_str->info.value.data_value.str->bytes;
				name_str->type_enum = PT_TYPE_CHAR;
				name_str->info.value.string_type = ' ';
				serial_value->info.expr.arg1 = name_str;
				serial_value->info.expr.arg2 = NULL;
				PICE(serial_value);
				pt_push(this_parser, serial_value);
				parser_free_node(this_parser, dot);

				cannot_prepare = true;
				cannot_cache = true;
			}
			else
			if ( intl_mbs_casecmp( name->info.name.original,
			                  "next_value" ) == 0 ||
			     intl_mbs_casecmp( name->info.name.original,
			                  "nextval" ) == 0 )
			{
				dot = pt_pop(this_parser);
				serial_value = parser_new_node(this_parser, PT_EXPR);
				serial_value->info.expr.op = PT_NEXT_VALUE;
				name_str = parser_new_node(this_parser, PT_VALUE );

				save_custom = this_parser->custom_print;
				this_parser->custom_print |= PT_SUPPRESS_QUOTES;

				name_str->info.value.data_value.str =
						pt_print_bytes( this_parser, dot->info.dot.arg1 );
				this_parser->custom_print = save_custom;

				name_str->info.value.data_value.str->length =
						strlen( (char *)name_str->info.value.data_value.str->bytes );
				name_str->info.value.text = (char *)
								name_str->info.value.data_value.str->bytes;
				name_str->type_enum = PT_TYPE_CHAR;
				name_str->info.value.string_type = ' ';
				serial_value->info.expr.arg1 = name_str;
				serial_value->info.expr.arg2 = NULL;
				PICE(serial_value);
				pt_push(this_parser, serial_value);
				parser_free_node(this_parser, dot);

				cannot_prepare = true;
				cannot_cache = true;
			}
		}
	  >>
	;

path_link "path link"
	: 	<< PT_NODE *name, *corr; const char *callee; >>
	  identifier
	  ( {path__correlation
		<< corr = pt_pop(this_parser);
		   name = pt_pop(this_parser);
		   if (name) name->info.name.path_correlation = corr;
                   pt_push(this_parser, name);
		>>
            }
	  | 	<< name = pt_pop(this_parser); callee = (name ? name->info.name.original : ""); >>
	    generic_function_call_body[callee]
	  )
	| table_set_function_call
	;

path__correlation "path correlation"
/*	: Left_Bracket identifier Right_Bracket*/
        : Left_Brace identifier Right_Brace
	;

data_type_statement "data_type statement"
	:	<< PT_NODE *dt, *set_dt; PT_TYPE_ENUM typ;
                >>
	  DATA_TYPE data__type > [typ, dt]
		<< if (! dt)
		   {
			dt=parser_new_node(this_parser, PT_DATA_TYPE);
			if (dt)
			{
			     dt->type_enum = typ;
			     dt->data_type = NULL;
			}
                   }
		   else {
			if ((typ == PT_TYPE_SET) ||
			    (typ == PT_TYPE_MULTISET) ||
			    (typ == PT_TYPE_SEQUENCE))
			{
			    set_dt = parser_new_node(this_parser, PT_DATA_TYPE);
			    if (set_dt)
			    {
				set_dt->type_enum = typ;
				set_dt->data_type = dt;
				dt = set_dt;
			    }
			}
		   }
		pt_push(this_parser, dt);
		>>
	;
in_out > [PT_MISC_TYPE inout] "input output clause"
	: << $inout = PT_NOPUT; >>
	{ IN << $inout = PT_INPUT; >>
	| OUT_ << $inout = PT_OUTPUT; >>
	| INOUT << $inout = PT_INPUTOUTPUT; >>
	}
	;

data__type > [PT_TYPE_ENUM typ, PT_NODE *dt] "type declaration clause"
	: << $dt = NULL; $typ = 0; >>
	(
	  primitive_type >[$typ, $dt]
	| set_type >[$typ]
	  ( 	<< PT_TYPE_ENUM e; PT_NODE *elt; >>
	    data__type >[e, $dt]
		<< if (!$dt) {
			$dt = parser_new_node(this_parser, PT_DATA_TYPE);
	                if ($dt)
			{
			     $dt->type_enum = e;
			     $dt->data_type = NULL;
			}
		   }
		>>
	  | 	<< $dt=NULL; >>
	    { Left_Paren
	      { data__type >[e, elt]
		<< if (!elt) {
			elt = parser_new_node(this_parser, PT_DATA_TYPE);
			   if (elt)
			   { elt->type_enum = e;
			     elt->data_type = NULL;
			   }
		}
	        $dt = elt;
		>>
	      ("," data__type >[e, elt]
		<< if (!elt) {
			elt = parser_new_node(this_parser, PT_DATA_TYPE);
			   if (elt)
			   { elt->type_enum = e;
			     elt->data_type = NULL;
			   }
		}
		$dt = parser_append_node(elt, $dt);
		>>
	      )*
	      }
	      Right_Paren
	    }
	  )
	)
	;

char_bit_type > [PT_TYPE_ENUM typ] "char type"
	:
	  ( Char | CHARACTER )		<< $typ = PT_TYPE_CHAR; >>
	    { VARYING			<< $typ = PT_TYPE_VARCHAR; >> }
	| VARCHAR			<< $typ = PT_TYPE_VARCHAR; >>
	| NATIONAL ( Char | CHARACTER )	<< $typ = PT_TYPE_NCHAR; >>
	    { VARYING			<< $typ = PT_TYPE_VARNCHAR; >> }
	| NCHAR				<< $typ = PT_TYPE_NCHAR; >>
	    { VARYING			<< $typ = PT_TYPE_VARNCHAR; >> }
	| BIT				<< $typ = PT_TYPE_BIT; >>
	    { VARYING			<< $typ = PT_TYPE_VARBIT; >> }
	;

primitive_type > [PT_TYPE_ENUM typ, PT_NODE *dt] "type declaration clause"
	:	<< long identity = 0; $dt=NULL; >>
	( Integer  << $typ=PT_TYPE_INTEGER;  >>
        | SmallInt << $typ=PT_TYPE_SMALLINT; >>
        | Int      << $typ=PT_TYPE_INTEGER;  >>
        | Double {PRECISION} << $typ=PT_TYPE_DOUBLE; >>
        | Date     << $typ=PT_TYPE_DATE;     >>
        | Time     << $typ=PT_TYPE_TIME;     >>
	| Utime    << $typ=PT_TYPE_TIMESTAMP;    >>
	| TIMESTAMP << $typ=PT_TYPE_TIMESTAMP;    >>
        | Monetary << $typ=PT_TYPE_MONETARY; >>
	| OBJECT   << $typ=PT_TYPE_OBJECT;   >>
        | String
		<< $typ=PT_TYPE_VARCHAR;
		   /* set the default precision to max string length */
		   $dt=parser_new_node(this_parser, PT_DATA_TYPE);
		   if ($dt)
		   { $dt->type_enum=$typ;
		     $dt->info.data_type.precision=DB_MAX_VARCHAR_PRECISION;
		   }
		>>
        << #if 0 /* to disable TEXT */ >>
        | TEXT
		<<
                   /* at parsing, the data type of TEXT attr looks like varchar,
                      but the real data type is object */
                   $typ=PT_TYPE_OBJECT;
		   /* set the default precision to max string length */
		   $dt=parser_new_node(this_parser, PT_DATA_TYPE);
		   if ($dt)
		   { $dt->type_enum=PT_TYPE_VARCHAR;
		     $dt->info.data_type.precision=DB_MAX_VARCHAR_PRECISION;
		   }
		>>
        << #endif /* 0 */ >>
	| class__name {IDENTITY << identity = 1; >> }
		<<
		   $typ=PT_TYPE_OBJECT;
		   $dt=parser_new_node(this_parser, PT_DATA_TYPE);
		   if ($dt)
		   {
		   	$dt->type_enum = $typ;
			$dt->info.data_type.entity=pt_pop(this_parser);
			/* a convenient place to pass back
			 * wether or not identity was explicitly specified.
			 * This can change name resolution when classes and adt's
			 * have the same name.
			 */
			$dt->info.data_type.units = identity;
		   }
		>>
        | 	<< PT_NODE *len = NULL, *prec = NULL, *scale = NULL;
	           long l=1;>>
	  ( char_bit_type > [$typ] << $dt=len=NULL; >>
	    { Left_Paren unsigned_integer > [len] Right_Paren }
		<< if (!len) {
		     switch ($typ) {
			case PT_TYPE_CHAR:
			case PT_TYPE_NCHAR:
			case PT_TYPE_BIT:
			l = 1;
			break;
			case PT_TYPE_VARCHAR:
			l = DB_MAX_VARCHAR_PRECISION;
			break;
			case PT_TYPE_VARNCHAR:
			l = DB_MAX_VARNCHAR_PRECISION;
			break;
			case PT_TYPE_VARBIT:
			l = DB_MAX_VARBIT_PRECISION;
			break;
			default:
			break;
		     }
		   }
		   else
		   { long maxlen=DB_MAX_VARCHAR_PRECISION;
		     l = len->info.value.data_value.i;
		     switch ($typ) {
			case PT_TYPE_CHAR:
			maxlen = DB_MAX_CHAR_PRECISION;
			break;
			case PT_TYPE_VARCHAR:
			maxlen = DB_MAX_VARCHAR_PRECISION;
			break;
			case PT_TYPE_NCHAR:
			maxlen = DB_MAX_NCHAR_PRECISION;
			break;
			case PT_TYPE_VARNCHAR:
			maxlen = DB_MAX_VARNCHAR_PRECISION;
			break;
			case PT_TYPE_BIT:
			maxlen = DB_MAX_BIT_PRECISION;
			break;
			case PT_TYPE_VARBIT:
			maxlen = DB_MAX_VARBIT_PRECISION;
			break;
			default:
			break;
		     }
                     if (l > maxlen)
		     { PT_ERRORmf(this_parser, len, MSGCAT_SET_PARSER_SYNTAX,
			              MSGCAT_SYNTAX_MAX_BITLEN, maxlen);
		     }
		     l = (l > maxlen ? maxlen : l);
		   }
		   $dt=parser_new_node(this_parser, PT_DATA_TYPE);
		   if ($dt)
		   { $dt->type_enum=$typ;
		     $dt->info.data_type.precision=l;
                      switch ($typ) {
                         case PT_TYPE_CHAR:
                         case PT_TYPE_VARCHAR:
                         $dt->info.data_type.units = INTL_CODESET_ISO88591;
                         break;

                         case PT_TYPE_NCHAR:
                         case PT_TYPE_VARNCHAR:
                         $dt->info.data_type.units = (int) lang_charset();
                         break;

                         case PT_TYPE_BIT:
                         case PT_TYPE_VARBIT:
                         $dt->info.data_type.units = INTL_CODESET_RAW_BITS;
                         break;

 			default:
 			break;
 		    }
		   }
		>>
	  | (NUMERIC) << prec=scale=NULL; >>
	    { Left_Paren
	      unsigned_integer >[prec] {"," unsigned_integer >[scale]}
	      Right_Paren
	    }	<< /*
		    * Squirrel away the precision and scale info in a
		    * PT_DATA_TYPE node for later semantic checking.
		    */
		   $dt = parser_new_node(this_parser, PT_DATA_TYPE);
		   $typ = PT_TYPE_NUMERIC;
		   if ($dt)
		   {
		       $dt->type_enum = $typ;
		       $dt->info.data_type.precision = prec ? prec->info.value.data_value.i : 15;
		       $dt->info.data_type.dec_precision = scale ? scale->info.value.data_value.i : 0;
                       if (scale && prec)
                           if (scale->info.value.data_value.i > prec->info.value.data_value.i)
                           {
                               PT_ERRORmf2(this_parser, $dt,
                                           MSGCAT_SET_PARSER_SEMANTIC,
                                           MSGCAT_SEMANTIC_INV_PREC_SCALE,
                                           prec->info.value.data_value.i,
                                           scale->info.value.data_value.i);
                           }
                       if (prec)
                           if (prec->info.value.data_value.i > DB_MAX_NUMERIC_PRECISION)
                           {
                               PT_ERRORmf2(this_parser, $dt,
                                           MSGCAT_SET_PARSER_SEMANTIC,
                                           MSGCAT_SEMANTIC_PREC_TOO_BIG,
                                           prec->info.value.data_value.i,
                                           DB_MAX_NUMERIC_PRECISION);
                           }
		   }
		>>
   	  | (Float|Real)    << prec=NULL; >>
	    { Left_Paren
	      unsigned_integer >[prec]
	      Right_Paren
	    }	<<
		   if (prec &&
		       prec->info.value.data_value.i >= 8 &&
		       prec->info.value.data_value.i <= DB_MAX_NUMERIC_PRECISION) {
		       /* convert float to double precision */
		       $typ = PT_TYPE_DOUBLE;
		   }
		   else {
		       /*
		        * Squirrel away the precision and scale info in a
		        * PT_DATA_TYPE node for later semantic checking.
		        */
		       $dt = parser_new_node(this_parser, PT_DATA_TYPE);
		       $typ = PT_TYPE_FLOAT;
		       if ($dt)
		       {
		           $dt->type_enum = $typ;
		           $dt->info.data_type.precision =
			       prec ? prec->info.value.data_value.i : 7;
		           $dt->info.data_type.dec_precision = 0;
                           if (prec)
                               if (prec->info.value.data_value.i >
			           DB_MAX_NUMERIC_PRECISION) {
                                   PT_ERRORmf2(this_parser, $dt,
                                               MSGCAT_SET_PARSER_SEMANTIC,
                                               MSGCAT_SEMANTIC_PREC_TOO_BIG,
                                               prec->info.value.data_value.i,
                                               DB_MAX_NUMERIC_PRECISION);
                               }
		       }
		   }
		>>
	  )
	  <<
	  if (len) parser_free_node(this_parser, len);
	  if (prec) parser_free_node(this_parser, prec);
	  if (scale) parser_free_node(this_parser, scale);
	  >>
        )
	;

set_type > [PT_TYPE_ENUM typ] "set type declaration"
	: SET_OF       << $typ=PT_TYPE_SET; >>
	| SET {OF}     << $typ=PT_TYPE_SET; >>
        | MULTISET_OF  << $typ=PT_TYPE_MULTISET; >>
        | MULTISET {OF}<< $typ=PT_TYPE_MULTISET; >>
        | SEQUENCE_OF  << $typ=PT_TYPE_SEQUENCE; >>
        | SEQUENCE {OF}<< $typ=PT_TYPE_SEQUENCE; >>
        | LIST {OF}    << $typ=PT_TYPE_SEQUENCE; >>
	;

/* the problem with signs is that the parser wants to treat
     -1  the same way it treats -X:  as a unary minus.
     Thus, all our constants are unsigned. The only places we have
     to explicitly look for the sign is in argument values,
     insert values, and constant sets. This avoids ambiguity */

numeric_literal > [PT_NODE *val] "numeric literal"
	: unsigned_integer > [$val]
	| unsigned_real    > [$val]
	| monetary_literal > [$val]
	;

literal_ "literal or parameter"
	: 	<< PT_NODE *val; >>
	  literal_w_o_param
	| parameter_ > [val] <<pt_push(this_parser, val);>>
	;

literal_w_o_param "literal without parameters"
	: 	<< PT_NODE *val; >>
	  numeric_literal      > [val] <<pt_push(this_parser, val);>>
	| char_string_literal  > [val] <<pt_push(this_parser, val);>>
	| bit_string_literal   > [val] <<pt_push(this_parser, val);>>
	| host_parameter[&input_host_index] > [val] <<pt_push(this_parser, val);>>
	| null_value
	| constant_set
	| 	<< val = parser_new_node(this_parser, PT_VALUE); >>
	    NA	<< if (val) val->type_enum=PT_TYPE_NA;
                   pt_push(this_parser, val);
		>>
	| date_or_time_literal > [val] <<pt_push(this_parser, val);>>
	;

constant_set "constant set"
	: 	<< PT_NODE *node=parser_new_node(this_parser, PT_VALUE);
		   PT_TYPE_ENUM typ=PT_TYPE_SEQUENCE;
                   PT_NODE *e;
		>>
	  { SET		<< typ=PT_TYPE_SET; >>
	  | MULTISET	<< typ=PT_TYPE_MULTISET; >>
	  | SEQUENCE 	<< typ=PT_TYPE_SEQUENCE; >>
	  | LIST 	<< typ=PT_TYPE_SEQUENCE; >>
	  }
	  Left_Brace
	  ( expression_list
		<< if(node)
		   { node->info.value.data_value.set=pt_pop(this_parser);
                     node->type_enum=typ;
                     for (e = node->info.value.data_value.set;
		          e;
			  e = e->next) {
			 if (e->type_enum == PT_TYPE_STAR) {
		             /* illegal grammar
			      *
			      * e.g., select {*} from ...
			      *       select {a, b, x.*, c} from x ...
			      */
			     PT_ERRORf(this_parser, e, "check syntax at %s, illegal '*' expression.", pt_short_print(this_parser, e));
			     break;
			 }
                     }
		   }
		>>
	  |	<< if(node)
		   { node->info.value.data_value.set=0;
                     node->type_enum=typ;
		   }
		>>
	  )
	  Right_Brace	<< pt_push(this_parser, node); >>
	;

null_value "null value clause"
	: 	<< PT_NODE *node=parser_new_node(this_parser, PT_VALUE);
                   if(node) node->type_enum=PT_TYPE_NULL;
                   pt_push(this_parser, node);
		>>
	  Null
	;

attribute_name
	: identifier
	;

db_name_at_host > [PT_NODE *nam]
	: char_string_literal > [$nam]
	;

file_path_name "method_implementation_file_path"
	: 	<< PT_NODE *str, *node = parser_new_node(this_parser, PT_FILE_PATH); >>
	  char_string_literal > [str]
         	<< if(node) node->info.file_path.string = str;
		   pt_push(this_parser, node);
		>>
	;

identifier "identifier"
	: 	<< char *q = NULL; PT_NODE * p = parser_new_node(this_parser, PT_NAME); >>
	  (  IdName	 	<< q=pt_makename($1.text); >>
	  |  BracketDelimitedIdName 	<< q=pt_makename($1.text); >>
	  |  DelimitedIdName 	<<
			q=pt_makename($1.text);
				>>
		/*
           parser thinks the following are keywords, but
           they really are legal as identifiers in any situation
           where an identifier can appear.

		   Do NOT add anything to this list unless:
			1) it is not a reserved word according to SQL2 or SQL3  AND
			2) it is marked as unreserved in the sqlm_keywords[]
	           table in pt_keywd.c

		   The only exceptions to the above rule is OLD and NEW
		   which are marked as reserved in the sqlm_keywords[] table
	 	   but which are allowed here for trigger support.
        */
	  | ABORT	<< q = pt_makename($1.text); >>
	  | ABS	        << q = pt_makename($1.text); >>
	  | ACTIVE	<< q = pt_makename($1.text); >>
	  | ANALYZE     << q = pt_makename($1.text); >>
	  | AUTHORIZATION << q = pt_makename($1.text); >>
          | AUTO_INCREMENT << q = pt_makename($1.text); >>
          | CACHE       << q = pt_makename($1.text); >>
	  | CEIL	<< q = pt_makename($1.text); >>
	  | CHAR_LENGTH	<< q = pt_makename($1.text); >>
	  | CHR	        << q = pt_makename($1.text); >>
	  | COMMITTED	<< q = pt_makename($1.text); >>
	  | COST	<< q = pt_makename($1.text); >>
	  | DECAY_CONSTANT << q = pt_makename($1.text); >>
	  | DECODE_	<< q = pt_makename($1.text); >>
	  | DECR	<< q = pt_makename($1.text); >>
	  | DECREMENT	<< q = pt_makename($1.text); >>
	  | DECRYPT	<< q = pt_makename($1.text); >>
	  | DEFINED	<< q = pt_makename($1.text); >>
	  | DIRECTORY	<< q = pt_makename($1.text); >>
          | DRAND       << q = pt_makename($1.text); >>
          | DRANDOM     << q = pt_makename($1.text); >>
	  | ENCRYPT	<< q = pt_makename($1.text); >>
	  | EVENT	<< q = pt_makename($1.text); >>
	  | EXP		<< q = pt_makename($1.text); >>
	  | FLOOR	<< q = pt_makename($1.text); >>
	  | GDB		<< q = pt_makename($1.text); >>
	  | GE_INF	<< q = pt_makename($1.text); >>
	  | GE_LE	<< q = pt_makename($1.text); >>
	  | GE_LT	<< q = pt_makename($1.text); >>
	  | GREATEST	<< q = pt_makename($1.text); >>
          | GROUPBY_NUM << q = pt_makename($1.text); >>
          | GROUPS      << q = pt_makename($1.text); >>
          | GT_INF      << q = pt_makename($1.text); >>
          | GT_LE       << q = pt_makename($1.text); >>
          | GT_LT       << q = pt_makename($1.text); >>
	  | HASH	<< q = pt_makename($1.text); >>
	  | HOST	<< q = pt_makename($1.text); >>
	  | IDENTIFIED	<< q = pt_makename($1.text); >>
	  | INACTIVE	<< q = pt_makename($1.text); >>
          | INCR        << q = pt_makename($1.text); >>
	  | INCREMENT	<< q = pt_makename($1.text); >>
	  | INFINITE	<< q = pt_makename($1.text); >>
	  | INFO	<< q = pt_makename($1.text); >>
	  | INF_LE	<< q = pt_makename($1.text); >>
	  | INF_LT	<< q = pt_makename($1.text); >>
	  | INSENSITIVE	<< q = pt_makename($1.text); >>
	  | INSTANCES	<< q = pt_makename($1.text); >>
	  | INSTR	<< q = pt_makename($1.text); >>
	  | INSTRB	<< q = pt_makename($1.text); >>
	  | INST_NUM	<< q = pt_makename($1.text); >>
	  | INTRINSIC	<< q = pt_makename($1.text); >>
	  | INVALIDATE	<< q = pt_makename($1.text); >>
	  | JAVA	<< q = pt_makename($1.text); >>
	  | LAST_DAY	<< q = pt_makename($1.text); >>
	  | LEAST	<< q = pt_makename($1.text); >>
	  | LENGTH	<< q = pt_makename($1.text); >>
	  | LENGTHB	<< q = pt_makename($1.text); >>
	  | LOCK	<< q = pt_makename($1.text); >>
	  | LOG 	<< q = pt_makename($1.text); >>
	  | LPAD	<< q = pt_makename($1.text); >>
	  | LTRIM	<< q = pt_makename($1.text); >>
	  | MAXIMUM	<< q = pt_makename($1.text); >>
	  | MAXVALUE	<< q = pt_makename($1.text); >>
	  | MAX_ACTIVE	<< q = pt_makename($1.text); >>
          | MEMBERS     << q = pt_makename($1.text); >>
	  | MINVALUE	<< q = pt_makename($1.text); >>
	  | MIN_ACTIVE	<< q = pt_makename($1.text); >>
          | MODULUS     << q = pt_makename($1.text); >>
          | MONTHS_BETWEEN << q = pt_makename($1.text); >>
	  | NAME	<< q = pt_makename($1.text); >>
	  | NEW		<< q = pt_makename($1.text); >>	/* allowed for triggers */
	  | NOCYCLE	<< q = pt_makename($1.text); >>
	  | NOMAXVALUE	<< q = pt_makename($1.text); >>
	  | NOMINVALUE	<< q = pt_makename($1.text); >>
	  | NVL	        << q = pt_makename($1.text); >>
	  | NVL2	<< q = pt_makename($1.text); >>
	  | OBJECT_ID	<< q = pt_makename($1.text); >>
          | OLD		<< q = pt_makename($1.text); >>	/* allowed for triggers */
	  | ORDERBY_NUM	<< q = pt_makename($1.text); >>
	  | PARTITION   << q = pt_makename($1.text); >>
	  | PARTITIONING << q = pt_makename($1.text); >>
	  | PARTITIONS  << q = pt_makename($1.text); >>
	  | PASSWORD	<< q = pt_makename($1.text); >>
	  | POWER	<< q = pt_makename($1.text); >>
	  | PRINT	<< q = pt_makename($1.text); >>
	  | PRIORITY	<< q = pt_makename($1.text); >>
          | RAND        << q = pt_makename($1.text); >>
          | RANDOM      << q = pt_makename($1.text); >>
          | RANGE       << q = pt_makename($1.text); >>
          | REBUILD     << q = pt_makename($1.text); >>
	  | REJECT	<< q = pt_makename($1.text); >>
	  | REMOVE    	<< q = pt_makename($1.text); >>
	  | REORGANIZE	<< q = pt_makename($1.text); >>
	  | REPEATABLE	<< q = pt_makename($1.text); >>
	  | RESET	<< q = pt_makename($1.text); >>
	  | RETAIN      << q = pt_makename($1.text); >>
	  | REVERSE	<< q = pt_makename($1.text); >>
	  | ROUND	<< q = pt_makename($1.text); >>
	  | RPAD	<< q = pt_makename($1.text); >>
	  | RTRIM	<< q = pt_makename($1.text); >>
	  | SERIAL	<< q = pt_makename($1.text); >>
	  | SIGN	<< q = pt_makename($1.text); >>
	  | SQRT	<< q = pt_makename($1.text); >>
	  | STABILITY	<< q = pt_makename($1.text); >>
	  | START_	<< q = pt_makename($1.text); >>
	  | STATEMENT	<< q = pt_makename($1.text); >>
	  | STATUS	<< q = pt_makename($1.text); >>
	  | STDDEV	<< q = pt_makename($1.text); >>
	  | STOP	<< q = pt_makename($1.text); >>
	  | SUBSTR	<< q = pt_makename($1.text); >>
	  | SUBSTRB	<< q = pt_makename($1.text); >>
	  | SWITCH	<< q = pt_makename($1.text); >>
	  | SYSTEM	<< q = pt_makename($1.text); >>
<< #if 0 /* disable TEXT */ >>
          | TEXT        << q = pt_makename($1.text); >>
<< #endif /* 0 */ >>
	  | THAN   	<< q = pt_makename($1.text); >>
	  | TIMEOUT	<< q = pt_makename($1.text); >>
	  | TO_CHAR	<< q = pt_makename($1.text); >>
	  | TO_DATE	<< q = pt_makename($1.text); >>
	  | TO_NUMBER	<< q = pt_makename($1.text); >>
	  | TO_TIME	<< q = pt_makename($1.text); >>
	  | TO_TIMESTAMP << q = pt_makename($1.text); >>
	  | TRACE	<< q = pt_makename($1.text); >>
	  | TRIGGERS	<< q = pt_makename($1.text); >>
	  | TRUNC	<< q = pt_makename($1.text); >>
	  | UNCOMMITTED	<< q = pt_makename($1.text); >>
	  | VARIANCE	<< q = pt_makename($1.text); >>
	  | WORKSPACE   << q = pt_makename($1.text); >>
	  )
		<<
		    if (p) p->info.name.original = q;
                    pt_push(this_parser, p);
		>>
	;

method__name
	: identifier
	;

char_string_literal > [PT_NODE *str]
	: 	<< PT_TYPE_ENUM type_enum;
		   char string_type;
		   $str=parser_new_node(this_parser, PT_VALUE);
		>>
	( Quote
		<< string_type = ' ';
		   type_enum = PT_TYPE_CHAR;
		>>
	| NQuote
		<< string_type = 'N';
		   type_enum = PT_TYPE_NCHAR;
		>>
	)
	String_Body
		<< if ($str) {
		     $str->type_enum = type_enum;
             $str->info.value.string_type = string_type;
             $str->info.value.data_value.str =
             pt_append_bytes(this_parser, NULL, zzlextext,
		     			zzlextextend-zzlextext);
		     $str->info.value.text = (char *)
		     	$str->info.value.data_value.str->bytes;
		   }
		>>
	string_continuation[$str]
	;

bit_string_literal > [PT_NODE *str]
	: 	<< PT_TYPE_ENUM type_enum;
		   char string_type;
		   $str=parser_new_node(this_parser, PT_VALUE);
		   type_enum = PT_TYPE_BIT;
		>>
	( BQuote
		<< string_type = 'B';
		>>
	| XQuote
		<< string_type = 'X';
		>>
	)
	String_Body
		<< if ($str) {
		     $str->type_enum = type_enum;
		     $str->info.value.string_type = string_type;
		     $str->info.value.data_value.str =
		     pt_append_bytes(this_parser, NULL, zzlextext,
		     			zzlextextend-zzlextext);
		     $str->info.value.text = (char *)
		     	$str->info.value.data_value.str->bytes;
		   }
		>>
	string_continuation[$str]
	;

string_continuation < [PT_NODE *str] "string continuation"
	:
	( Quote String_Body
		<< if (str) {
		     str->info.value.data_value.str =
                  pt_append_bytes(this_parser, str->info.value.data_value.str,
			     	zzlextext, zzlextextend-zzlextext);
		     str->info.value.text = (char *)
		     	str->info.value.data_value.str->bytes;
		   }
		>>
	)*
	;

superclass_list
	: {ONLY} class__name ("," {ONLY} class__name << PLIST; >> )*
	;

cursor
	: identifier
	;

user__name "user name"
	: identifier
	;

unsigned_integer > [PT_NODE *val] "numeric literal"
	: 	<< $val = parser_new_node(this_parser, PT_VALUE); >>
	  UNSIGNED_INTEGER
		<< if ($val)
		   {
		     $val->info.value.text = pt_makename($1.text);

		     /* this will not fit for numerics > 32 bits worth */
	             $val->info.value.data_value.i = atol($1.text);

		     /* test against the maximum length of digits guranteed
		      * to fit in a 32 bit integer */
		     if ((strlen($val->info.value.text) <= 9) ||
                         (strlen($val->info.value.text) == 10 &&
                          ($val->info.value.text[0] == '0' ||
                           $val->info.value.text[0] == '1')))
		     {
                       $val->type_enum = PT_TYPE_INTEGER;
                     }
                     else
                     {
                       $val->type_enum = PT_TYPE_NUMERIC;
		     }
		   }
		>>
	;

unsigned_real > [PT_NODE *val] "numeric literal"
	: 	<< $val = parser_new_node(this_parser, PT_VALUE); >>
	  UNSIGNED_REAL
		<< if ($val)
		   {
		     /* Scientific Notation */
		     if (strchr($1.text, 'E') != NULL  ||
		         strchr($1.text, 'e') != NULL) {
		         $val->info.value.text = pt_makename($1.text);
			 $val->type_enum = PT_TYPE_DOUBLE;
			 $val->info.value.data_value.d = atof($1.text);
		     }
		     /* float notation */
		     else if (strchr($1.text, 'F') != NULL ||
			      strchr($1.text, 'f') != NULL) {
		         $val->info.value.text = pt_makename($1.text);
			 $val->type_enum = PT_TYPE_FLOAT;
			 $val->info.value.data_value.f = atof($1.text);
		     }
		     /* regular numeric */
		     else {
			 $val->info.value.text = pt_makename($1.text);
			 $val->type_enum = PT_TYPE_NUMERIC;
                     }
		 }
		>>
	;

monetary_literal > [PT_NODE *val] "monetary literal"
	: 	<< char *str, *txt, *minus = NULL; $val = parser_new_node(this_parser, PT_VALUE); >>
	  ( YEN_SIGN
		<< str = pt_append_string(this_parser, NULL, $1.text);
		   if ($val)
			$val->info.value.data_value.money.type = PT_CURRENCY_YEN;
		>>
	  | DOLLAR_SIGN
		<< str = pt_append_string(this_parser, NULL, $1.text);
		   if ($val)
			$val->info.value.data_value.money.type = PT_CURRENCY_DOLLAR;
		>>
	  | WON_SIGN
                << str = pt_append_string(this_parser, NULL, $1.text);
                   if ($val)
                        $val->info.value.data_value.money.type = PT_CURRENCY_WON;
                >>
	  )
	  { PLUS | MINUS << minus = pt_append_string(this_parser, NULL, "-"); >> }
	  ( UNSIGNED_INTEGER << txt = pt_append_string(this_parser, minus, pt_makename($1.text)); >>
	  | UNSIGNED_REAL    << txt = pt_append_string(this_parser, minus, pt_makename($1.text)); >>
	  )	<< if ($val)
		   { $val->info.value.text =
		   	pt_append_string(this_parser, str, txt);
		     $val->type_enum = PT_TYPE_MONETARY;
		     $val->info.value.data_value.money.amount = atof(txt);
		   }
		>>
	;

query_number "query number"
	: 	<<PT_NODE *qno;>>
	  unsigned_integer > [qno] <<pt_push(this_parser, qno);>>
	;

date_or_time_literal > [PT_NODE *val] "SQL2 date or time literal"
	:	<< PT_TYPE_ENUM style;
		>>
	( Date	<< style = PT_TYPE_DATE; >>
	| Time	<< style = PT_TYPE_TIME; >>
	| TIMESTAMP << style = PT_TYPE_TIMESTAMP; >>
	) char_string_literal > [$val]
		<<
		   if ($val) {
		     $val->type_enum = style;
		   }
		>>
	;

partition_clause "partition clause"
    : <<
    PT_NODE *qc, *h = NULL;
    PT_PARTITION_TYPE t = PT_PARTITION_HASH;
    qc = parser_new_node(this_parser, PT_PARTITION);
    >>
    PARTITION {BY}
    (HASH << t=PT_PARTITION_HASH; >>
         l_paren
         expression_
         Right_Paren << if (qc) qc->info.partition.expr=pt_pop(this_parser); >>
         PARTITIONS unsigned_integer > [h]
    | RANGE << t=PT_PARTITION_RANGE; >>
        l_paren
        expression_
        Right_Paren << if (qc) qc->info.partition.expr=pt_pop(this_parser); >>
        l_paren
        partition_definition_list
        Right_Paren << if (qc) qc->info.partition.parts=pt_pop(this_parser); >>
    | LIST << t=PT_PARTITION_LIST; >>
        l_paren
        expression_
        Right_Paren << if (qc) qc->info.partition.expr=pt_pop(this_parser); >>
        l_paren
        partition_definition_list
        Right_Paren << if (qc) qc->info.partition.parts=pt_pop(this_parser); >>
    )
    <<
    if (qc) {
        qc->info.partition.type=t;
        qc->info.partition.hashsize=h;
    }
    pt_push(this_parser, qc);
    >>
    ;

partition_definition_list "partition definition list"
    : partition_definition ( "," partition_definition << PLIST; >> )*
    ;

partition_definition "partition definition clause"
    : << PT_NODE *node=parser_new_node(this_parser, PT_PARTS);
    >>
    PARTITION partition_name
    << if(node) node->info.parts.name = pt_pop(this_parser); >>
    ( VALUES LESS THAN << if (node) node->info.parts.type=PT_PARTITION_RANGE; >>
      ( MAXVALUE << if (node) node->info.parts.values=NULL; >>
        | l_paren
        literal_
        Right_Paren << if (node) node->info.parts.values=pt_pop(this_parser); >>
        )
    | VALUES IN << if (node) node->info.parts.type=PT_PARTITION_LIST; >>
      l_paren
      literal_list
      Right_Paren << if (node) node->info.parts.values=pt_pop(this_parser); >> )
    <<
    pt_push(this_parser, node);
    >>
    ;

partition_name
    : identifier
    ;

alter_partition_clause "alter partition clause"
    : << PT_NODE *alt = pt_pop(this_parser), *h; >>
    ( apply_partition_clause[alt]
    | REMOVE PARTITIONING <<
        if(alt) alt->info.alter.code = PT_REMOVE_PARTITION;
        >>
    | REORGANIZE PARTITION
        alter_partition_name_list <<
        if(alt) alt->info.alter.code = PT_REORG_PARTITION;
        if(alt) alt->info.alter.alter_clause.partition.name_list =
        pt_pop(this_parser);
        >>
    INTO
        l_paren
        partition_definition_list
        Right_Paren <<
        if (alt) alt->info.alter.alter_clause.partition.parts =
        pt_pop(this_parser);
        >>
    | ANALYZE PARTITION <<
        if(alt) alt->info.alter.code = PT_ANALYZE_PARTITION;
        if(alt) alt->info.alter.alter_clause.partition.name_list = NULL;
        >>
    ({ ALL }
    | alter_partition_name_list <<
        if(alt) alt->info.alter.alter_clause.partition.name_list =
        pt_pop(this_parser);
        >> )
    | COALESCE PARTITION
        unsigned_integer > [h]
        <<
        if(alt) alt->info.alter.code = PT_COALESCE_PARTITION;
        if(alt) alt->info.alter.alter_clause.partition.size = h;
        >> )
    << pt_push(this_parser, alt); >>
    ;

apply_partition_clause < [PT_NODE *alt] "apply partition clause"
    : << PT_NODE *part;
    if(alt) alt->info.alter.code = PT_APPLY_PARTITION;
    >>
    partition_clause << part = pt_pop(this_parser);
    if(alt) alt->info.alter.alter_clause.partition.info = part;
    >>
    ;

alter_partition_name_list "partition name list"
    : identifier ( "," identifier << PLIST; >> )*
    ;

literal_list "literal list"
    : literal_ ( "," literal_ << PLIST; >> )*
    ;

/*
               #errclass definitions

  These affect the way syntax errors are reported by grouping
  certain tokens and rules and associating them with a string.
  They have no effect on the syntax or the grammar.
*/
#errclass "set operator" { set_op }
#errclass "table operator" { table_op }
#errclass "comparison"  {  comp_op }
#errclass "addition operator" { STAR SLASH }
#errclass "multiplication operator" { STAR SLASH }
#errclass "arithmetic operator" { PLUS MINUS STAR SLASH }
#errclass "operator" 	{ "arithmetic operator" "set operator" "table operator" "comparison" }
#errclass "char literal" 	{ char_string_literal }
#errclass "numeric literal" 	{ numeric_literal }
#errclass "bit literal" 	{ bit_string_literal }
#errclass "set literal" 	{ constant_set }
#errclass "date/time literal" 	{ date_or_time_literal }
#errclass "literal" 	{ NA Null USER PLUS MINUS Left_Brace bit_string_literal numeric_literal date_or_time_literal char_string_literal USER }
#errclass "function" 	{ aggregate_func arith_func char_func scal_func CAST instnum_func orderbynum_func}
#errclass "data type"   { data__type }
#errclass ")" 		{ Right_Paren }
#errclass "search condition" { search_condition }
#errclass "predicate" { predicate }
#errclass "expression" 	{ Left_Paren }
#errclass "expression "	{ Left_Paren INSERT extract_expr case_expr }
#errclass "subquery"    { LEFT_PAREN }
#errclass "statement"   { statement_ }
#errclass "parameter " 	{ parameter_ }
#errclass "variable" 	{ host_parameter }
#errclass "parameter" 	{ parameter_ host_parameter }
#errclass "qualified name" { path_expression }
#errclass "name" 	{ identifier }
#errclass "null"        { NA Null }
#errclass "ON"        { ON_ }
#errclass "OFF"        { OFF_ }
#errclass "OUT"        { OUT_ }


/* other scanners */
#lexclass COMMENT  /* C-style comments */
#token "[\*]+/"    << zzmode(START);
                      if (hint != PT_HINT_NONE) {       /* C-style hint */
                          /* convert to hint token */
                          int i, l;
                          strcpy(hint_str, "/*+ ");
                          l = strlen(hint_str);
                          for (i = 0; hint_table[i].tokens; i++) {
                            if ((hint & hint_table[i].hint) && l < JP_MAXNAME) {
                                l += strlen(hint_table[i].tokens) + 1;
                                if (l < JP_MAXNAME)
                                    strcat(strcat(hint_str, hint_table[i].tokens), " ");
                            }
                          }
                          strcat(hint_str, "*/");
                          zzreplstr(hint_str);
                          LA(1) = C_STYLE_HINT;
                      } else {
                          zzskip();                     /* C-style comment */
                      }
                   >>
#token "[\*]+~[/]"
       << if (IS_WHITE_CHAR(zzlextext[strlen(zzlextext) - 1])) {
              prev_is_white_char = true;
          } else {
              prev_is_white_char = false;
          }

          zzskip(); >>
#token "~[\*]+"
       << if (zzlextext[0] == '+') {
              is_hint_comment = true;
              prev_is_white_char = false;
          }

          if (is_hint_comment == true) { /* read hint info */
              (void) pt_check_hint(zzlextext, hint_table, &hint, prev_is_white_char);

              prev_is_white_char = false;
          }

          zzskip();
       >>

#lexclass SQS  /* single quoted strings */
#token "\'\'"      << zzreplchar( '\'' ); zzmore(); >>   /* embedded ''     */
#token "~[\']"     << zzmore(); >>
#token String_Body "\'" << zzreplchar( 0 ); zzmode(START); >>

#lexclass DQS  /* double quoted strings */
#token "\"\""      << zzreplchar( '\"' ); zzmore(); >>   /* embedded ""     */
#token "~[\"]"     << zzmore(); >>
#token DelimitedIdName "\"" << zzreplchar( 0 ); zzmode(START); >>

#lexclass BQS  /* bracket quoted strings */
#token "~[\]]"     << zzmore(); >>
#token BracketDelimitedIdName "\]" << zzreplchar( 0 ); zzmode(START); >>
