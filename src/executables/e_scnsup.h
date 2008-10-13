/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 * 
 * e_scnsup.h - 
 * 
 * note : antlr complains with "action/parameter buffer overflow; size 4000"
 * if a long structure initialization list is placed in the antlr ".g" 
 * (input grammar) file.  To keep antlr happy, we place all lengthy 
 * action code sequences in .h files such as this one.
 */

#ifndef _E_SCNSUP_H_
#define _E_SCNSUP_H_

#ident "$Id$"

#define CHECK_LINENO \
    do {             \
      if (need_line_directive) \
        emit_line_directive(); \
    } while (0)

#if defined(YYDEBUG)
#define YY_FLUSH_ON_DEBUG \
    do {                  \
      if (yydebug)        \
        fflush(yyout);    \
    } while (0)
#else
#define YY_FLUSH_ON_DEBUG
#endif

#define PP_STR_ECHO      \
    do {                 \
      if (current_buf)   \
        pp_translate_string(current_buf, zzlextext, true); \
    } while (0)

typedef struct scanner_mode_record SCANNER_MODE_RECORD;
typedef struct keyword_rec KEYWORD_REC;

struct scanner_mode_record
{
  enum scanner_mode previous_mode;
  bool recognize_keywords;
  bool suppress_echo;
  struct scanner_mode_record *previous_record;
};

enum scansup_msg
{
  MSG_EMPTY_STACK = 1,
  MSG_NOT_PERMITTED = 2
};

struct keyword_rec
{
  const char *keyword;
  short value;
  short suppress_echo;		/* Ignored for C keywords */
};

typedef struct keyword_table
{
  KEYWORD_REC *keyword_array;
  int size;
} KEYWORD_TABLE;

YYSTYPE yylval;			/* the semantic value of the lookahead symbol  */
FILE *yyin, *yyout;
char *yyfilename;
int yylineno = 1;
int errors = 0;
varstring *current_buf;		/* remain PUBLIC for debugging ease */
ECHO_FN echo_fn = &echo_stream;

static SCANNER_MODE_RECORD *mode_stack;
static enum scanner_mode mode;
static bool need_line_directive;
static bool recognize_keywords;
static bool suppress_echo = false;

static KEYWORD_REC c_keywords[] = {
  {"auto", AUTO_, 0},
  {"BIT", BIT_, 1},
  {"bit", BIT_, 1},
  {"break", GENERIC_TOKEN, 0},
  {"case", GENERIC_TOKEN, 0},
  {"char", CHAR_, 0},
  {"const", CONST_, 0},
  {"continue", GENERIC_TOKEN, 0},
  {"default", GENERIC_TOKEN, 0},
  {"do", GENERIC_TOKEN, 0},
  {"double", DOUBLE_, 0},
  {"else", GENERIC_TOKEN, 0},
  {"enum", ENUM, 0},
  {"extern", EXTERN_, 0},
  {"float", FLOAT_, 0},
  {"for", GENERIC_TOKEN, 0},
  {"go", GENERIC_TOKEN, 0},
  {"goto", GENERIC_TOKEN, 0},
  {"if", GENERIC_TOKEN, 0},
  {"int", INT_, 0},
  {"long", LONG_, 0},
  {"register", REGISTER_, 0},
  {"return", GENERIC_TOKEN, 0},
  {"short", SHORT_, 0},
  {"signed", SIGNED_, 0},
  {"sizeof", GENERIC_TOKEN, 0},
  {"static", STATIC_, 0},
  {"struct", STRUCT_, 0},
  {"switch", GENERIC_TOKEN, 0},
  {"typedef", TYPEDEF_, 0},
  {"union", UNION_, 0},
  {"unsigned", UNSIGNED_, 0},
  {"VARCHAR", VARCHAR_, 1},
  {"varchar", VARCHAR_, 1},
  {"VARYING", VARYING, 0},
  {"varying", VARYING, 0},
  {"void", VOID_, 0},
  {"volatile", VOLATILE_, 0},
  {"while", GENERIC_TOKEN, 0},
};

static KEYWORD_REC sqlx_keywords[] = {
  /* Make sure that they are in alphabetical order */
  {"ADD", ADD, 0},
  {"ALL", ALL, 0},
  {"ALTER", ALTER, 0},
  {"AND", AND, 0},
  {"AS", AS, 0},
  {"ASC", ASC, 0},
  {"ATTACH", ATTACH, 0},
  {"ATTRIBUTE", ATTRIBUTE, 0},
  {"AVG", AVG, 0},
  {"BEGIN", BEGIN, 1},
  {"BETWEEN", BETWEEN, 0},
  {"BY", BY, 0},
  {"CALL", CALL_, 0},
  {"CHANGE", CHANGE, 0},
  {"CHAR", CHAR_, 0},
  {"CHARACTER", CHARACTER, 0},
  {"CHECK", CHECK_, 0},
  {"CLASS", CLASS, 0},
  {"CLOSE", CLOSE, 0},
  {"COMMIT", COMMIT, 0},
  {"CONNECT", CONNECT, 1},
  {"CONTINUE", CONTINUE_, 1},
  {"COUNT", COUNT, 0},
  {"CREATE", CREATE, 0},
  {"CURRENT", CURRENT, 1},
  {"CURSOR", CURSOR_, 1},
  {"DATE", DATE, 0},
  {"DEC", NUMERIC, 0},
  {"DECIMAL", NUMERIC, 0},
  {"DECLARE", DECLARE, 1},
  {"DEFAULT", DEFAULT, 0},
  {"DELETE", DELETE, 0},
  {"DESC", DESC, 0},
  {"DESCRIBE", DESCRIBE, 1},
  {"DESCRIPTOR", DESCRIPTOR, 1},
  {"DIFFERENCE", DIFFERENCE, 0},
  {"DISCONNECT", DISCONNECT, 1},
  {"DISTINCT", DISTINCT, 0},
  {"DOUBLE", DOUBLE_, 0},
  {"DROP", DROP, 0},
  {"END", END, 1},
  {"ESCAPE", ESCAPE, 0},
  {"EVALUATE", EVALUATE, 0},
  {"EXCEPT", EXCEPT, 0},
  {"EXCLUDE", EXCLUDE, 0},
  {"EXECUTE", EXECUTE, 1},
  {"EXISTS", EXISTS, 0},
  {"FETCH", FETCH, 1},
  {"FILE", FILE_, 0},
  {"FLOAT", FLOAT_, 0},
  {"FOR", FOR, 0},
  {"FOUND", FOUND, 0},
  {"FROM", FROM, 0},
  {"FUNCTION", FUNCTION_, 0},
  {"GET", GET, 0},
  {"GO", GO, 1},
  {"GOTO", GOTO_, 1},
  {"GRANT", GRANT, 0},
  {"GROUP", GROUP, 0},
  {"HAVING", HAVING, 0},
  {"IDENTIFIED", IDENTIFIED, 1},
  {"IMMEDIATE", IMMEDIATE, 1},
  {"IN", IN, 0},
  {"INCLUDE", INCLUDE, 1},
  {"INDEX", INDEX, 0},
  {"INDICATOR", INDICATOR, 1},
  {"INHERIT", INHERIT, 0},
  {"INSERT", INSERT, 0},
  {"INT", INT_, 0},
  {"INTEGER", INTEGER, 0},
  {"INTERSECTION", INTERSECTION, 0},
  {"INTO", INTO, 0},
  {"IS", IS, 0},
  {"LIKE", LIKE, 0},
  {"MAX", MAX_, 0},
  {"METHOD", METHOD, 0},
  {"MIN", MIN_, 0},
  {"MULTISET_OF", MULTISET_OF, 0},
  {"NOT", NOT, 0},
  {"NULL", NULL_, 0},
  {"NUMBER", NUMERIC, 0},
  {"NUMERIC", NUMERIC, 0},
  {"OBJECT", OBJECT, 0},
  {"OF", OF, 0},
  {"OID", OID, 0},
  {"ON", ON_, 0},
  {"ONLY", ONLY, 0},
  {"OPEN", OPEN, 0},
  {"OPTION", OPTION, 0},
  {"OR", OR, 0},
  {"ORDER", ORDER, 0},
  {"PRECISION", PRECISION, 0},
  {"PREPARE", PREPARE, 1},
  {"PRIVILEGES", PRIVILEGES, 0},
  {"PUBLIC", PUBLIC_, 0},
  {"READ", READ, 0},
  {"REAL", REAL, 0},
  {"REGISTER", REGISTER_, 0},
  {"RENAME", RENAME, 0},
  {"REPEATED", REPEATED, 1},
  {"REVOKE", REVOKE, 0},
  {"ROLLBACK", ROLLBACK, 0},
  {"SECTION", SECTION, 1},
  {"SELECT", SELECT, 0},
  {"SEQUENCE_OF", SEQUENCE_OF, 0},
  {"SET", SET, 0},
  {"SETEQ", SETEQ, 0},
  {"SETNEQ", SETNEQ, 0},
  {"SET_OF", SET_OF, 0},
  {"SHARED", SHARED, 0},
  {"SMALLINT", SMALLINT, 0},
  {"SOME", SOME, 0},
  {"SQLM", SQLX, 1},
  {"SQLCA", SQLCA, 1},
  {"SQLDA", SQLDA, 1},
  {"SQLERROR", SQLERROR_, 1},
  {"SQLWARNING", SQLWARNING_, 1},
  {"STATISTICS", STATISTICS, 0},
  {"STOP", STOP_, 1},
  {"STRING", STRING, 0},
  {"SUBCLASS", SUBCLASS, 0},
  {"SUBSET", SUBSET, 0},
  {"SUBSETEQ", SUBSETEQ, 0},
  {"SUM", SUM, 0},
  {"SUPERCLASS", SUPERCLASS, 0},
  {"SUPERSET", SUPERSET, 0},
  {"SUPERSETEQ", SUPERSETEQ, 0},
  {"TABLE", TABLE, 0},
  {"TIME", TIME, 0},
  {"TIMESTAMP", TIMESTAMP, 0},
  {"TO", TO, 0},
  {"TRIGGER", TRIGGER, 0},
  {"UNION", UNION_, 0},
  {"UNIQUE", UNIQUE, 0},
  {"UPDATE", UPDATE, 0},
  {"USE", USE, 0},
  {"USER", USER, 0},
  {"USING", USING, 0},
  {"UTIME", UTIME, 0},
  {"VALUES", VALUES, 0},
  {"VIEW", VIEW, 0},
  {"WHENEVER", WHENEVER, 1},
  {"WHERE", WHERE, 0},
  {"WITH", WITH, 0},
  {"WORK", WORK, 0},
};

static KEYWORD_REC preprocessor_keywords[] = {
  /* Make sure that they are in alphabetical order */
  {"FROM", FROM, 0},
  {"IDENTIFIED", IDENTIFIED, 0},
  {"INDICATOR", INDICATOR, 1},
  {"INTO", INTO, 0},
  {"ON", ON_, 0},
  {"SELECT", SELECT, 0},
  {"TO", TO, 0},
  {"VALUES", VALUES, 0},
  {"WITH", WITH, 0},
};

static KEYWORD_TABLE sqlx_table = { sqlx_keywords, DIM (sqlx_keywords) };
static KEYWORD_TABLE preprocessor_table = { preprocessor_keywords,
  DIM (preprocessor_keywords)
};

#if YYDEBUG != 0
static int yydebug = 1;
#endif

static int check_c_identifier (void);
static void ignore_token (void);
static void count_embedded_newlines (void);
static void echo_string_constant (const char *, int);

/*
 * pp_suppress_echo() - 
 * return : void
 * suppress(in) :
 */
void
pp_suppress_echo (int suppress)
{
  suppress_echo = suppress;
}

/*
 * echo_stream() - 
 * return: void
 * str(in) :
 * length(in) :
 */
void
echo_stream (const char *str, int length)
{
  if (!suppress_echo)
    {
      if (exec_echo[0])
	{
	  fputs (exec_echo, yyout);
	  exec_echo[0] = 0;
	}
      fwrite ((char *) str, length, 1, yyout);
      YY_FLUSH_ON_DEBUG;
    }
}

/*
 * echo_vstr() - 
 * return : void
 * str(in) :
 * length(in) :
 */
void
echo_vstr (const char *str, int length)
{
  if (!suppress_echo)
    {
      if (current_buf)
	{
	  vs_strcatn (current_buf, str, length);
	}
    }
}

/*
 * echo_devnull() - 
 * return : void
 * str(in) :
 * length(in) :
 */
void
echo_devnull (const char *str, int length)
{
  /* Do nothing */
}

/*
 * pp_set_echo() - 
 * return :
 * new_echo(in) :
 */
ECHO_FN
pp_set_echo (ECHO_FN new_echo)
{
  ECHO_FN old_echo;

  old_echo = echo_fn;
  echo_fn = new_echo;

  return old_echo;
}

/*
 * yyvmsg() - 
 * return : void
 * fmt(in) :
 * args(in) :
 */
static void
yyvmsg (const char *fmt, va_list args)
{
  /* Flush yyout just in case stderr and yyout are the same. */
  fflush (yyout);

  if (yyfilename)
    {
      fprintf (stderr, "%s:", yyfilename);
    }

  fprintf (stderr, "%ld: ", zzline);
  vfprintf (stderr, fmt, args);
  fputs ("\n", stderr);
  fflush (stderr);
}

/*
 * yyerror() - 
 * return : void
 * s(in) :
 */
void
yyerror (const char *s)
{
  yyverror ("%s before \"%s\"", s, zzlextext);
}

/*
 * yyverror() - 
 * return : void
 * fmt(in) :
 */
void
yyverror (const char *fmt, ...)
{
  va_list args;

  errors++;

  va_start (args, fmt);
  yyvmsg (fmt, args);
  va_end (args);
}

/*
 * yyvwarn() - 
 * return : void
 * fmt(in) :
 */
void
yyvwarn (const char *fmt, ...)
{
  va_list args;

  va_start (args, fmt);
  yyvmsg (fmt, args);
  va_end (args);
}

/*
 * yyredef() - 
 * return : void
 * name(in) :
 */
void
yyredef (char *name)
{
  /* Eventually we will probably want to turn this off, but it's
     interesting to find out about perceived redefinitions right now. */
  yyverror ("redefinition of symbol \"%s\"", name);
}

/*
 * keyword_cmp() - 
 * return :
 * a(in) :
 * b(in) :
 */
static int
keyword_cmp (const void *a, const void *b)
{
  return strcmp (((const KEYWORD_REC *) a)->keyword,
		 ((const KEYWORD_REC *) b)->keyword);
}

/*
 * keyword_case_cmp() - 
 * return :
 * a(in) :
 * b(in) :
 */
static int
keyword_case_cmp (const void *a, const void *b)
{
  return intl_mbs_casecmp (((const KEYWORD_REC *) a)->keyword,
			   ((const KEYWORD_REC *) b)->keyword);
}

/*
 * check_c_identifier() - 
 * return :
 */
static int
check_c_identifier (void)
{
  KEYWORD_REC *p;
  KEYWORD_REC dummy;
  SYMBOL *sym;

  /*
   * Check first to see if we have a keyword.
   */
  dummy.keyword = zzlextext;
  p = (KEYWORD_REC *) bsearch (&dummy,
			       c_keywords,
			       sizeof (c_keywords) / sizeof (c_keywords[0]),
			       sizeof (KEYWORD_REC), &keyword_cmp);

  if (p)
    {
      /*
       * Notic that this is "sticky", unlike in check_identifier().
       * Once we've seen a keyword that initiates suppression, we don't
       * want anything echoed until the upper level grammar productions
       * say so.  In particular, we don't want echoing to be re-enabled
       * the very next time we see an ordinary C keyword.
       */
      suppress_echo = suppress_echo || p->suppress_echo;
      return p->value;
    }

  /*
   * If not, see if this is an already-encountered identifier.
   */
  sym = yylval.p_sym = pp_findsym (pp_Symbol_table, zzlextext);
  if (sym)
    {
      /*
       * Something by this name lives in the symbol table.
       *
       * This needs to be made sensitive to whether typedef names are
       * being recognized or not.
       */
      return sym->type->tdef && pp_recognizing_typedef_names ? TYPEDEF_NAME :
	IS_INT_CONSTANT (sym->type) ? ENUMERATION_CONSTANT : IDENTIFIER;
    }
  else
    {
      /*
       * Symbol wasn't recognized in the symbol table, so this must be
       * a new identifier.
       */
      return IDENTIFIER;
    }
}


/*
 * check_identifier() - 
 * return :
 * keywords(in) :
 */
static int
check_identifier (KEYWORD_TABLE * keywords)
{
  KEYWORD_REC *p;
  KEYWORD_REC dummy;

  /*
   * Check first to see if we have a keyword.
   */
  dummy.keyword = zzlextext;
  p = (KEYWORD_REC *) bsearch (&dummy,
			       keywords->keyword_array,
			       keywords->size,
			       sizeof (KEYWORD_REC), &keyword_case_cmp);

  if (p)
    {
      suppress_echo = p->suppress_echo;
      return p->value;
    }
  else
    {
      suppress_echo = false;
      return IDENTIFIER;
    }
}

/*
 * yy_push_mode() - 
 * return : void
 * new_mode(in) :
 */
void
yy_push_mode (enum scanner_mode new_mode)
{
  SCANNER_MODE_RECORD *next = malloc (sizeof (SCANNER_MODE_RECORD));

  next->previous_mode = mode;
  next->recognize_keywords = recognize_keywords;
  next->previous_record = mode_stack;
  next->suppress_echo = suppress_echo;
  mode_stack = next;

  /*
   * Set up so that the first id-like token that we see in VAR_MODE
   * will be reported as an IDENTIFIER, regardless of whether it
   * strcasecmps the same as some SQL/X keyword.
   */
  recognize_keywords = (new_mode != VAR_MODE);

  yy_enter (new_mode);
}

/*
 * yy_check_mode() - 
 * return : void
 */
void
yy_check_mode (void)
{
  if (mode_stack != NULL)
    {
      yyverror ("(yy_check_mode) internal error: mode stack still full");
      exit (1);
    }
}

/*
 * yy_pop_mode() - 
 * return : void
 */
void
yy_pop_mode (void)
{
  SCANNER_MODE_RECORD *prev;
  enum scanner_mode new_mode;

  if (mode_stack == NULL)
    {
      yyverror (pp_get_msg (EX_ESQLMSCANSUPPORT_SET, MSG_EMPTY_STACK));
      exit (1);
    }

  prev = mode_stack->previous_record;
  new_mode = mode_stack->previous_mode;
  recognize_keywords = mode_stack->recognize_keywords;
  suppress_echo = mode_stack->suppress_echo;

  free_and_init (mode_stack);
  mode_stack = prev;

  yy_enter (new_mode);
}

/*
 * yy_mode() - 
 * return :
 */
enum scanner_mode
yy_mode (void)
{
  return mode;
}

/*
 * yy_enter() - 
 * return : void
 * new_mode(in) :
 */
void
yy_enter (enum scanner_mode new_mode)
{
  static struct
  {
    const char *name;
    int mode;
    void (*echo_fn) (const char *, int);
  }
  mode_info[] =
  {
    {
    "echo", START, &echo_stream},	/* ECHO_MODE 0  */
    {
    "sqlx", SQLX_mode, &echo_vstr},	/* SQLX_MODE 1  */
    {
    "C", C_mode, &echo_stream},	/* C_MODE    2  */
    {
    "expr", EXPR_mode, &echo_stream},	/* EXPR_MODE 3  */
    {
    "var", VAR_mode, &echo_vstr},	/* VAR_MODE  4  */
    {
    "hostvar", HV_mode, &echo_vstr},	/* HV_MODE   5  */
    {
    "buffer", BUFFER_mode, &echo_vstr},	/* BUFFER_MODE  */
    {
    "comment", COMMENT_mode, NULL}	/* COMMENT      */
  };

#if defined(YYDEBUG)
  if (yydebug)
    {
      fprintf (stdout, "Entering %s mode\n", mode_info[new_mode].name);
      fflush (stdout);
    }
#endif

  mode = new_mode;
  zzmode (mode_info[new_mode].mode);
  if (mode_info[new_mode].echo_fn)
    echo_fn = mode_info[new_mode].echo_fn;
}

/*
 * emit_line_directive() - 
 * return : void
 */
void
emit_line_directive (void)
{
  if (pp_emit_line_directives)
    {
      if (yyfilename)
	fprintf (yyout, "#line %ld \"%s\"\n", zzline, yyfilename);
      else
	fprintf (yyout, "#line %ld\n", zzline);
    }

  need_line_directive = false;
}

/*
 * ignore_token() - 
 * return : void
 */
static void
ignore_token (void)
{
  yyverror (pp_get_msg (EX_ESQLMSCANSUPPORT_SET, MSG_NOT_PERMITTED),
	    zzlextext);
}

/*
 * count_embedded_newlines() - 
 * return : void
 */
static void
count_embedded_newlines (void)
{
  const char *p = zzlextext;
  for (; *p; ++p)
    {
      if (*p == '\n')
	{
	  ++zzline;
	  zzendcol = 0;
	}
      ++zzendcol;
    }
}


/*
 *  */

/*
 * echo_string_constant() - 
 * return : void
 * str(in) :
 * length(in) :
 *
 * TODO(M2) : This may be incorrect now!
 * according to ansi, there is no escape syntax for newlines!
 */
static void
echo_string_constant (const char *str, int length)
{
  const char *p;
  char *q, *result;
  int charsLeft = length;

  p = memchr (str, '\n', length);

  if (p == NULL)
    {
      ECHO_STR (str, length);
      return;
    }

  /*
   * Bad luck; there's at least one embedded newline, so we have to do
   * something to get rid of it (if it's escaped).  If it's unescaped
   * then this is a pretty weird string, but we have to let it go.
   *
   * Because we come in here with the opening quote mark still intact,
   * we're always guaranteed that there is a valid character in front
   * of the result of strchr().
   */

  result = q = malloc (length + 1);

  /*
   * Copy the spans between escaped newlines to the new buffer.  If we
   * encounter an unescaped newline we just include it.
   */
  while (p)
    {
      /*
       * Let # stand for the newline character; then memchr() leaves us
       * set up like this:
       *
       *      abcdefg#hijklm
       *      ^      ^
       *     str     p
       *
       * If g is really a backslash (i.e., (*(p-1) == '\\')), we only
       * want to copy up to g (p-1), and start the next span at h
       * (p+1).  If g isn't a backslash, want to leave everything alone
       * and search for the next newline.
       */
      if (*(p - 1) == '\\')
	{
	  int span;

	  span = p - str - 1;	/* exclude size of escape char */
	  memcpy (q, str, span);
	  q += span;
	  str = p + 1;
	  length -= 2;
	  /* Exclude size of skipped escape and newline chars from string */
	  charsLeft -= (span + 2);
	}

      p = memchr (p + 1, '\n', charsLeft);
    }

  memcpy (q, str, charsLeft);

  ECHO_STR (result, length);
  free_and_init (result);
}

/*
 * yy_sync_lineno() - 
 * return : void
 */
void
yy_sync_lineno (void)
{
  need_line_directive = true;
}

/*
 * yy_set_buf() - 
 * return : void
 * vstr(in) :
 */
void
yy_set_buf (varstring * vstr)
{
  current_buf = vstr;
}

/*
 * yy_get_buf() - 
 * return : void
 */
varstring *
yy_get_buf (void)
{
  return current_buf;
}

/*
 * yy_erase_last_token() - 
 * return : void
 */
void
yy_erase_last_token (void)
{
  if (current_buf && zzlextext)
    current_buf->end -= strlen (zzlextext);
}

/*
 * yyinit() - 
 * return : void
 */
void
yyinit (void)
{
  /*
   * We've been burned too many times by people who can't alphabetize;
   * just sort the bloody thing once to protect ourselves from bozos.
   */
  qsort (c_keywords,
	 sizeof (c_keywords) / sizeof (c_keywords[0]),
	 sizeof (c_keywords[0]), &keyword_cmp);
  qsort (sqlx_keywords,
	 sizeof (sqlx_keywords) / sizeof (sqlx_keywords[0]),
	 sizeof (sqlx_keywords[0]), &keyword_case_cmp);

  zzline = 1;
  mode_stack = NULL;
  recognize_keywords = true;
  yy_enter (START);
}
#endif /* _E_SCNSUP_H_ */
