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

/*
 * language_support.c : Multi-language and character set support
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include "chartype.h"
#include "misc_string.h"
#include "language_support.h"
#include "authenticate.h"
#include "environment_variable.h"
#include "db.h"
/* this must be the last header file included! */
#include "dbval.h"

static INTL_LANG lang_Loc_id = INTL_LANG_ENGLISH;
static INTL_CODESET lang_Loc_charset = INTL_CODESET_ISO88591;
static int lang_Loc_bytes_per_char = 1;
static char lang_Loc_name[LANG_MAX_LANGNAME] = LANG_NAME_DEFAULT;
static DB_CURRENCY lang_Loc_currency = DB_CURRENCY_DOLLAR;

/* locale data */
static LANG_LOCALE_DATA lc_English_iso88591;
static LANG_LOCALE_DATA lc_English_utf8;
static LANG_LOCALE_DATA lc_Turkish;
static LANG_LOCALE_DATA lc_Korean;
static LANG_LOCALE_DATA *lang_Loc_data = &lc_English_iso88591;

static bool lang_Initialized = false;

typedef struct lang_defaults LANG_DEFAULTS;
struct lang_defaults
{
  const char *lang_name;
  const INTL_LANG lang;
  const INTL_CODESET codeset;
  const DB_CURRENCY currency;
};

LANG_DEFAULTS lang_defaults[] = {
  {LANG_NAME_ENGLISH, INTL_LANG_ENGLISH, INTL_CODESET_ISO88591,
   DB_CURRENCY_DOLLAR},		/* English - ISO-8859-1 - default lang and charset */
  {LANG_NAME_ENGLISH, INTL_LANG_ENGLISH, INTL_CODESET_UTF8,
   DB_CURRENCY_DOLLAR},		/* English - UTF-8 */
  {LANG_NAME_KOREAN, INTL_LANG_KOREAN, INTL_CODESET_KSC5601_EUC,
   DB_CURRENCY_WON},		/* Korean - EUC-KR */
  {LANG_NAME_TURKISH, INTL_LANG_TURKISH, INTL_CODESET_UTF8, DB_CURRENCY_TL}
};

static bool lang_is_codeset_allowed (const INTL_LANG intl_id,
				     const INTL_CODESET codeset);
static int lang_get_lang_id_from_name (const char *lang_name,
				       INTL_LANG * lang);
static INTL_CODESET lang_get_default_currency (const INTL_LANG intl_id);
static INTL_CODESET lang_get_default_codeset (const INTL_LANG intl_id);

/*
 * lang_init - Initializes any global state required by the multi-language
 *             module
 *   return: true if success
 */
bool
lang_init (void)
{
  const char *env, *s;

  if (lang_Initialized)
    {
      return lang_Initialized;
    }

  /* if the language setting is unset or unrecognized, assume English settings
   * and character sets. Allow an unrecognized language setting to be
   * treated as an 8bit ascii language, but still keep lang_name set
   */
  strncpy (lang_Loc_name, LANG_NAME_ENGLISH, sizeof (lang_Loc_name));
  lang_Loc_id = INTL_LANG_ENGLISH;
  lang_Loc_bytes_per_char = 1;
  lang_Loc_currency = DB_CURRENCY_DOLLAR;
  lang_Loc_charset = lang_get_default_codeset (lang_Loc_id);

  /*
   * Determine the locale by examining environment varialbes.
   * First we check our internal variable CUBRID_LANG to allow CUBRID
   * to operate in a different locale than is set with LANG.  This is
   * necessary when lang must be set to something other than one of the
   * recognized settings used to select message catalogs in
   * $CUBRID/admin/msg.
   */
  env = envvar_get ("LANG");
  if (env != NULL)
    {
      strncpy (lang_Loc_name, env, sizeof (lang_Loc_name));
    }
  else
    {
      env = getenv ("LANG");
      if (env != NULL)
	{
	  strncpy (lang_Loc_name, env, sizeof (lang_Loc_name));
	}
      else
	{
	  strncpy (lang_Loc_name, LANG_NAME_DEFAULT, sizeof (lang_Loc_name));
	}
    }

  /* Set up some internal constants based on the locale name */
  (void) lang_get_lang_id_from_name (lang_Loc_name, &lang_Loc_id);
  lang_Loc_currency = lang_get_default_currency (lang_Loc_id);

  /* allow environment to override the character set settings */
  s = strchr (lang_Loc_name, '.');
  if (s != NULL)
    {
      s++;
      if (strcasecmp (s, LANG_CHARSET_EUCKR) == 0)
	{
	  lang_Loc_charset = INTL_CODESET_KSC5601_EUC;
	}
      else if (strcasecmp (s, LANG_CHARSET_UTF8) == 0)
	{
	  lang_Loc_charset = INTL_CODESET_UTF8;
	}
      else
	{
	  lang_Loc_charset = INTL_CODESET_ISO88591;
	}

      if (!lang_is_codeset_allowed (lang_Loc_id, lang_Loc_charset))
	{
	  lang_Loc_charset = lang_get_default_codeset (lang_Loc_id);
	}
    }
  else
    {
      lang_Loc_charset = lang_get_default_codeset (lang_Loc_id);
    }

  switch (lang_Loc_charset)
    {
    case INTL_CODESET_KSC5601_EUC:
      lang_Loc_bytes_per_char = 2;
      break;
    case INTL_CODESET_UTF8:
      lang_Loc_bytes_per_char = INTL_UTF8_MAX_CHAR_SIZE;
      break;
    default:
      lang_Loc_bytes_per_char = 1;
    }

  /* init locale */
  lang_Loc_data = &lc_English_iso88591;
  lang_Loc_data->initloc ();

  if (lang_Loc_charset != INTL_CODESET_ISO88591)
    {
      /* if legacy charset : do not need these initilization */
      lang_Loc_data = &lc_English_utf8;
      lang_Loc_data->initloc ();

      lang_Loc_data = &lc_Turkish;
      lang_Loc_data->initloc ();

      lang_Loc_data = &lc_Korean;
      lang_Loc_data->initloc ();
    }

  switch (lang_Loc_id)
    {
    case INTL_LANG_ENGLISH:
      if (lang_Loc_charset == INTL_CODESET_ISO88591)
	{
	  lang_Loc_data = &lc_English_iso88591;
	}
      else
	{
	  lang_Loc_data = &lc_English_utf8;
	}
      break;

    case INTL_LANG_KOREAN:
      lang_Loc_data = &lc_Korean;
      break;

    case INTL_LANG_TURKISH:
      lang_Loc_data = &lc_Turkish;
      break;

    default:
      /* unsupported locale, set to default (English) */
      strncpy (lang_Loc_name, LANG_NAME_DEFAULT, sizeof (lang_Loc_name));
      lang_Loc_id = INTL_LANG_ENGLISH;
      lang_Loc_currency = DB_CURRENCY_DOLLAR;
      lang_Loc_data = &lc_English_iso88591;
      lang_Loc_charset = lang_get_default_codeset (lang_Loc_id);
    }

  /* static globals in db_date.c should also be initialized with the current
   * locale (for parsing local am/pm strings for times) */
  db_date_locale_init ();

  lang_Initialized = true;
  return (lang_Initialized);
}

/*
 * lang_name - returns language name per env settings.
 *   return: language name string
 */
const char *
lang_name (void)
{
  if (!lang_Initialized)
    {
      lang_init ();
    }
  return lang_Loc_name;
}

/*
 * lang_id - Returns language id per env settings
 *   return: language identifier
 */
INTL_LANG
lang_id (void)
{
  if (!lang_Initialized)
    {
      lang_init ();
    }
  return lang_Loc_id;
}

/*
 * lang_currency - Returns language currency per env settings
 *   return: language currency identifier
 */
DB_CURRENCY
lang_currency ()
{
  if (!lang_Initialized)
    {
      lang_init ();
    }
  return lang_Loc_currency;
}

/*
 * lang_charset - Returns language charset per env settings
 *   return: language charset
 */
INTL_CODESET
lang_charset (void)
{
  if (!lang_Initialized)
    {
      lang_init ();
    }
  return lang_Loc_charset;
}

/*
 * lang_loc_bytes_per_char - Returns language charset maximum bytes per char
 *   return: charset maximum bytes per char per
 */
int
lang_loc_bytes_per_char (void)
{
  if (!lang_Initialized)
    {
      lang_init ();
    }
  return lang_Loc_bytes_per_char;
}

/*
 * lang_final - Releases any resources held by this module
 *   return: none
 */
void
lang_final (void)
{
  lang_Initialized = false;
}

/*
 * lang_currency_symbol - Computes an appropriate printed representation for
 *                        a currency identifier
 *   return: currency string
 *   curr(in): currency constant
 */
const char *
lang_currency_symbol (DB_CURRENCY curr)
{
  return intl_get_money_symbol_console (curr);
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * lang_char_mem_size - Returns the character memory size for the given
 *                      pointer to a character
 *   return: memory size for the first character
 *   p(in)
 */
int
lang_char_mem_size (const char *p)
{
  if (LANG_VARIABLE_CHARSET (lang_charset ()))
    {
      if (0x80 & (p[0]))
	{
	  return 2;
	}
    }
  return 1;
}

/*
 * lang_char_screen_size - Returns the screen size for the given pointer
 *                         to a character
 *   return: screen size for the first character
 *   p(in)
 */
int
lang_char_screen_size (const char *p)
{
  if (LANG_VARIABLE_CHARSET (lang_charset ()))
    {
      return (0x80 & (p[0]) ? 2 : 1);
    }
  return 1;
}

/*
 * lang_wchar_mem_size - Returns the memory size for the given pointer
 *                       to a wide character
 *   return: memory size for the first character
 *   p(in)
 */
int
lang_wchar_mem_size (const wchar_t * p)
{
  if (LANG_VARIABLE_CHARSET (lang_charset ()))
    {
      if (0x8000 & (p[0]))
	{
	  return 2;
	}
    }
  return 1;
}

/*
 * lang_wchar_screen_size - Returns the screen size for the given pointer
 *                          to a wide character
 *   return: screen size for the first character
 *   p(in)
 */
int
lang_wchar_screen_size (const wchar_t * p)
{
  if (LANG_VARIABLE_CHARSET (lang_charset ()))
    {
      return (0x8000 & (p[0]) ? 2 : 1);
    }
  return 1;
}
#endif

/*
 * lang_check_identifier - Tests an identifier for possibility
 *   return: true if the name is suitable for identifier,
 *           false otherwise.
 *   name(in): identifier name
 *   length(in): identifier name length
 */
bool
lang_check_identifier (const char *name, int length)
{
  bool ok = false;
  int i;

  if (name == NULL)
    {
      return false;
    }

  if (char_isalpha (name[0]))
    {
      ok = true;
      for (i = 0; i < length && ok; i++)
	{
	  if (!char_isalnum (name[i]) && name[i] != '_')
	    {
	      ok = false;
	    }
	}
    }

  return (ok);
}

/*
 * lang_locale - returns language locale per env settings.
 *   return: language locale data
 */
const LANG_LOCALE_DATA *
lang_locale (void)
{
  if (!lang_Initialized)
    {
      lang_init ();
    }
  return lang_Loc_data;
}

/*
 * lang_get_specific_locale - returns language locale of a specific language
 *   return: language locale data
 *  lang(in): 
 *  codeset(in): 
 */
const LANG_LOCALE_DATA *
lang_get_specific_locale (const INTL_LANG lang, const INTL_CODESET codeset)
{
  if (!lang_Initialized)
    {
      lang_init ();
    }
  switch (lang)
    {
    case INTL_LANG_ENGLISH:
      if (codeset == INTL_CODESET_ISO88591)
	{
	  return &lc_English_iso88591;
	}
      else
	{
	  assert (codeset == INTL_CODESET_UTF8);
	  return &lc_English_utf8;
	}
    case INTL_LANG_KOREAN:
      assert (codeset == INTL_CODESET_KSC5601_EUC);
      return &lc_Korean;
    case INTL_LANG_TURKISH:
      assert (codeset == INTL_CODESET_UTF8);
      return &lc_Turkish;
    default:
      return lang_Loc_data;
    }
  return lang_Loc_data;
}

/*
 * lang_get_lang_id_from_name - returns the language id from a language name
 *
 *   return: 0, if language name is accepted, non-zero otherwise
 *   lang_name(in):
 *   lang_id(out): language identifier
 *
 *  Note : INTL_LANG_ENGLISH is returned if name is not a valid language name
 */
static int
lang_get_lang_id_from_name (const char *lang_name, INTL_LANG * lang_id)
{
  unsigned int i;

  assert (lang_id != NULL);

  *lang_id = INTL_LANG_ENGLISH;

  for (i = 0; i < sizeof (lang_defaults) / sizeof (LANG_DEFAULTS); i++)
    {
      if (strncasecmp (lang_name, lang_defaults[i].lang_name,
		       strlen (lang_defaults[i].lang_name)) == 0)
	{
	  *lang_id = lang_defaults[i].lang;
	  return 0;
	}
    }

  return 1;
}

/*
 * lang_get_lang_name_from_id - returns the language name from a language id
 *
 *   return: language name (NULL if lang_id is not valid)
 *   lang_id(in):
 *
 */
const char *
lang_get_lang_name_from_id (const INTL_LANG lang_id)
{
  switch (lang_id)
    {
    case INTL_LANG_ENGLISH:
      return LANG_NAME_ENGLISH;
    case INTL_LANG_KOREAN:
      return LANG_NAME_KOREAN;
    case INTL_LANG_TURKISH:
      return LANG_NAME_TURKISH;
    default:
      return NULL;
    }

  return NULL;
}

/*
 * lang_set_flag_from_lang - set a flag according to language string
 *
 *   return: 0 if language string OK and flag was set, non-zero otherwise
 *   lang_str(in): language string identier
 *   user_format(in): true if user has given a format, false if default format
 *   flag(out): bit flag : bit 0 is the user flag, bits 1 - 31 are for
 *		language identification
 *		Bit 0 : if set, the language was given by user
 *		Bit 1 : English
 *		Bit 2 : Koreean
 *		Bit 3 : Turkish
 *		Consider change this flag to store the language as value
 *		instead of as bit map
 *
 *  Note : function is used in context of some date-string functions.
 *	   If lang_str cannot be solved, the language is assumed English.
 */
int
lang_set_flag_from_lang (const char *lang_str, bool user_format, int *flag)
{
  INTL_LANG lang = INTL_LANG_ENGLISH;
  int status = 0;

  if (lang_str != NULL)
    {
      status = lang_get_lang_id_from_name (lang_str, &lang);
    }

  if (lang_set_flag_from_lang_id (lang, user_format, flag) == 0)
    {
      return status;
    }

  assert (lang == INTL_LANG_ENGLISH);

  return 1;
}

/*
 * lang_set_flag_from_lang - set a flag according to language identifier
 *
 *   return: 0 if language string OK and flag was set, non-zero otherwise
 *   lang(in): language identier
 *   user_format(in): true if user has given a format, false if default format
 *   flag(out): bit flag : bit 0 is the user flag, bits 1 - 31 are for
 *		language identification
 *		Bit 0 : if set, the language was given by user
 *		Bit 1 : English
 *		Bit 2 : Koreean
 *		Bit 3 : Turkish
 *		Consider change this flag to store the language as value
 *		instead of as bit map
 *
 *  Note : function is used in context of some date-string functions.
 */
int
lang_set_flag_from_lang_id (const INTL_LANG lang, bool user_format, int *flag)
{
  if (user_format)
    {
      *flag = 0;
    }
  else
    {
      *flag = 0x1;
    }

  if (lang == INTL_LANG_ENGLISH)
    {
      *flag |= 0x2;
      return 0;
    }
  else if (lang == INTL_LANG_KOREAN)
    {
      *flag |= 0x4;
      return 0;
    }
  else if (lang == INTL_LANG_TURKISH)
    {
      *flag |= 0x8;
      return 0;
    }

  /* default en_US */
  *flag |= 0x2;

  return 1;
}

/*
 * lang_get_lang_id_from_flag - get lang id from flag
 *
 *   return: id of language, current language is returned when flag value is
 *	     invalid
 *   flag(in): bit flag : bit 0 is the user flag, bits 1 - 31 are for
 *	       language identification
 *
 *  Note : function is used in context of some date-string functions.
 */
INTL_LANG
lang_get_lang_id_from_flag (const int flag, bool * user_format)
{
  if (flag & 0x1)
    {
      *user_format = false;
    }
  else
    {
      *user_format = true;
    }

  if (flag & 0x4)
    {
      return INTL_LANG_KOREAN;
    }
  else if (flag & 0x8)
    {
      return INTL_LANG_TURKISH;
    }

  return lang_id ();
}

/*
 * lang_date_format - Returns the default format of date for the required
 *		      language or NULL if a the default format is not
 *		      available
 *   lang_id (in):
 *   type (in): DB type for format
 */
const char *
lang_date_format (const INTL_LANG lang_id, const DB_TYPE type)
{
  if (PRM_USE_LOCALE_DATE_FORMAT)
    {
      if (!lang_Initialized)
	{
	  lang_init ();
	}

      switch (type)
	{
	case DB_TYPE_TIME:
	  return lang_locale ()->time_format;
	case DB_TYPE_DATE:
	  return lang_locale ()->date_format;
	case DB_TYPE_DATETIME:
	  return lang_locale ()->datetime_format;
	case DB_TYPE_TIMESTAMP:
	  return lang_locale ()->timestamp_format;
	default:
	  break;
	}
    }

  return NULL;
}

/*
 * lang_get_default_currency - returns the default codeset to be used for a
 *			      given language identifier
 *   return: currency
 *   intl_id(in):
 *
 *  Note : DB_CURRENCY_DOLLAR is returned if language is invalid
 */
static INTL_CODESET
lang_get_default_currency (const INTL_LANG intl_id)
{
  unsigned int i;
  DB_CURRENCY currency = DB_CURRENCY_DOLLAR;

  for (i = 0; i < sizeof (lang_defaults) / sizeof (LANG_DEFAULTS); i++)
    {
      if (intl_id == lang_defaults[i].lang)
	{
	  currency = lang_defaults[i].currency;
	  break;
	}
    }
  return currency;
}

/*
 * lang_get_default_codeset - returns the default codeset to be used for a
 *			      given language identifier
 *   return: codeset
 *   intl_id(in):
 */
static INTL_CODESET
lang_get_default_codeset (const INTL_LANG intl_id)
{
  unsigned int i;
  INTL_CODESET codeset = INTL_CODESET_UTF8;

  for (i = 0; i < sizeof (lang_defaults) / sizeof (LANG_DEFAULTS); i++)
    {
      if (intl_id == lang_defaults[i].lang)
	{
	  codeset = lang_defaults[i].codeset;
	  break;
	}
    }
  return codeset;
}

/*
 * lang_is_codeset_allowed - checks if a combination of language and codeset
 *			     is allowed
 *   return: true if combination is allowed, false otherwise
 *   intl_id(in):
 *   codeset(in):
 */
static bool
lang_is_codeset_allowed (const INTL_LANG intl_id, const INTL_CODESET codeset)
{
  unsigned int i;

  for (i = 0; i < sizeof (lang_defaults) / sizeof (LANG_DEFAULTS); i++)
    {
      if (intl_id == lang_defaults[i].lang &&
	  codeset == lang_defaults[i].codeset)
	{
	  return true;
	}
    }
  return false;
}

/*
 * lang_digit_grouping_symbol - Returns symbol used for grouping digits in
 *				numbers
 *   lang_id (in):
 */
char
lang_digit_grouping_symbol (const INTL_LANG lang_id)
{
  if (PRM_USE_LOCALE_NUMBER_FORMAT)
    {
      switch (lang_id)
	{
	case INTL_LANG_TURKISH:
	  return '.';
	default:
	  return ',';
	}
    }
  return ',';
}

/*
 * lang_digit_fractional_symbol - Returns symbol used for fractional part of
 *				  numbers
 *   lang_id (in):
 */
char
lang_digit_fractional_symbol (const INTL_LANG lang_id)
{
  if (PRM_USE_LOCALE_NUMBER_FORMAT)
    {
      switch (lang_id)
	{
	case INTL_LANG_TURKISH:
	  return ',';
	default:
	  return '.';
	}
    }
  return '.';
}

/*
 * lang_get_unicode_case_ex_cp() - checks if a character has an exception
 *				   from the locale casing for lower case
 *
 *   return: codepoint of case character or 0xffffffff if the character is
 *	     not an case exception
 *   cp(in): unicode codepoint of character
 *
 */
unsigned int
lang_unicode_lower_case_ex_cp (const unsigned int cp)
{
  int i;
  const unsigned int array_ex[][2] = {
    {0x49, 0x69},		/* I -> i */
    {0x130, 0x130}		/* capital I with dot -> unchanged */
  };

  for (i = 0; i < sizeof (array_ex) / sizeof (array_ex[0]); i++)
    {
      if (array_ex[i][0] == cp)
	{
	  return array_ex[i][1];
	}
    }

  return 0xffffffff;
}

/*
 * lang_unicode_upper_case_ex_cp() - checks if a character has an exception
 *				     from the locale casing for upper case
 *
 *   return: codepoint of case character or 0xffffffff if the character is
 *	     not an case exception
 *   cp(in): unicode codepoint of character
 *
 */
unsigned int
lang_unicode_upper_case_ex_cp (const unsigned int cp)
{
  int i;
  const unsigned int array_ex[][2] = {
    {0x69, 0x49},		/* i -> I */
    {0x131, 0x131}		/* small dotless i -> unchanged */
  };

  for (i = 0; i < sizeof (array_ex) / sizeof (array_ex[0]); i++)
    {
      if (array_ex[i][0] == cp)
	{
	  return array_ex[i][1];
	}
    }

  return 0xffffffff;
}

#if !defined (SERVER_MODE)
static DB_CHARSET lang_Server_charset;
static INTL_LANG lang_Server_lang_id;

static const DB_CHARSET lang_Db_charsets[] = {
  {"ascii", "US English charset - ASCII encoding", " ", INTL_CODESET_ASCII, 0,
   1},
  {"raw-bits", "Uninterpreted bits - Raw encoding", "", INTL_CODESET_RAW_BITS,
   0, 1},
  {"raw-bytes", "Uninterpreted bytes - Raw encoding", "",
   INTL_CODESET_RAW_BYTES, 0, 1},
  {"iso8859-1", "Latin 1 charset - ISO 8859 encoding", " ",
   INTL_CODESET_ISO88591, 0, 1},
  {"ksc-euc", "KSC 5601 1990 charset - EUC encoding", "\241\241",
   INTL_CODESET_KSC5601_EUC, 0, 2},
  {"utf-8", "UNICODE charset - UTF-8 encoding", " ",
   INTL_CODESET_UTF8, 0, 1},
  {"", "", "", INTL_CODESET_NONE, 0, 0}
};

static int lang_Server_charset_Initialized = 0;

/*
 * lang_server_charset_init - Initializes the global value of the server's
 *                            charset
 *   return: none
 *
 * Note: This is the charset that is bound to the database at the time
 *       of creation.
 */
void
lang_server_charset_init (void)
{
  DB_VALUE value;
  INTL_CODESET srvr_codeset;
  int i;

  /* Determine the Server's charset */

  /* Currently can't read the db_root table while on the server.
   * Temporarily just get the server's codeset from the locale if
   * on the server.
   *
   * The following is safe since this is a client only function. If this
   * needs to move to the server, the db_get must be pre-processed out
   * or something ...
   */
  if (Au_root)
    {
      /* Can't find the server's codeset.  This should only happen if using
       * a database prior to NCHAR implementation, or prior to completely
       * logging in.  If so, set the server codeset to be ASCII until
       * the the db_get can work correctly.  All string handling prior
       * to that time will be done without conversion.
       */
      if (db_get (Au_root, "charset", &value) != NO_ERROR)
	{
	  srvr_codeset = lang_charset ();
	}
      else
	{
	  /* Set the initialized flag */
	  lang_Server_charset_Initialized = 1;
	  srvr_codeset = (INTL_CODESET) db_get_int (&value);
	}

      if (db_get (Au_root, "lang_id", &value) != NO_ERROR)
	{
	  lang_Server_lang_id = lang_id ();
	}
      else
	{
	  lang_Server_lang_id = (INTL_LANG) db_get_int (&value);
	}
    }
  else
    {
      srvr_codeset = lang_charset ();
    }

  /* Find the charset in the Db_Charsets array */
  for (i = 0; lang_Db_charsets[i].charset_id != INTL_CODESET_NONE; i++)
    {
      if (lang_Db_charsets[i].charset_id == srvr_codeset)
	{
	  lang_Server_charset = lang_Db_charsets[i];
	  return;
	}
    }

  /* Server's codeset not found in the list; Initialize to ASCII */
  lang_Server_charset = lang_Db_charsets[0];
  return;
}

/*
 * lang_set_national_charset - Changes the charset definition for NCHAR
 *   return: error code
 *   charset_name(in): desired charset name
 *
 * Note: This should be called only by the DBA.
 */
int
lang_set_national_charset (const char *charset_name)
{
  INTL_CODESET server_codeset;
  INTL_LANG server_lang;
  DB_VALUE value;

  if (charset_name == NULL)
    {
      server_codeset = lang_charset ();
    }
  else
    {
      if (lang_charset_name_to_id (charset_name, &server_codeset) != NO_ERROR)
	{
	  server_codeset = lang_charset ();
	}
    }

  server_lang = lang_id ();
  db_make_int (&value, (int) server_lang);
  if (db_put_internal (Au_root, "lang_id", &value) != NO_ERROR)
    {
      /* Error Setting the nchar codeset */
    }

  db_make_int (&value, (int) server_codeset);
  if (db_put_internal (Au_root, "charset", &value) != NO_ERROR)
    {
      /* Error Setting the nchar codeset */
    }

  return NO_ERROR;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * lang_server_db_charset - Initializes if necessary, then return server's
 *                          charset
 *   return: DB_CHARSET structure associated with the server
 */
DB_CHARSET
lang_server_db_charset (void)
{
  if (!lang_Server_charset_Initialized)
    {
      lang_server_charset_init ();
    }
  return lang_Server_charset;
}
#endif

/*
 * lang_server_charset_id - Initializes if necessary, then return server's
 *                          charset_id
 *   return: INTL_CODESET of the server's charset
 */
INTL_CODESET
lang_server_charset_id (void)
{
  if (!lang_Server_charset_Initialized)
    {
      lang_server_charset_init ();
    }
  return lang_Server_charset.charset_id;
}

/*
 * lang_check_server_env - checks if server settings match with existing
 *			   environment
 *                          
 *   return: true if server settings match
 */
bool
lang_check_server_env ()
{
  if (!lang_Server_charset_Initialized)
    {
      lang_server_charset_init ();
    }

  if (lang_Server_charset.charset_id != lang_charset ()
      || lang_Server_lang_id != lang_id ())
    {
      return false;
    }

  return true;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * lang_server_space_char - Initializes if necessary, then return server's
 *                          space character
 *   return: none
 *   space(out): string containing the space character for the server's charset
 *   size(out): number of bytes in the space char
 */
void
lang_server_space_char (char *space, int *size)
{
  if (!lang_Server_charset_Initialized)
    {
      lang_server_charset_init ();
    }
  (void) strcpy (space, lang_Server_charset.space_char);
  *size = lang_Server_charset.space_size;
}

/*
 * lang_server_charset_name - Initializes if necessary, then return server's
 *                            charset name
 *   return: none
 *   name(out): the name of the server's charset
 */
void
lang_server_charset_name (char *name)
{
  if (!lang_Server_charset_Initialized)
    {
      lang_server_charset_init ();
    }
  (void) strcpy (name, lang_Server_charset.charset_name);
}

/*
 * lang_server_charset_desc - Initializes if necessary, then return server's
 *                            charset desc
 *   return: none
 *   desc(out): the description of the server's charset
 */
void
lang_server_charset_desc (char *desc)
{
  if (!lang_Server_charset_Initialized)
    {
      lang_server_charset_init ();
    }
  (void) strcpy (desc, lang_Server_charset.charset_desc);
}
#endif

/*
 * lang_charset_name_to_id - Returns the INTL_CODESET of the specified charset
 *   return: NO_ERROR or error code if the specified name can't be found in
 *           the lang_Db_charsets array
 *   name(in): the name of the desired charset
 *   codeset(out): INTL_CODESET of the desired charset
 */
int
lang_charset_name_to_id (const char *name, INTL_CODESET * codeset)
{
  int i;

  /* Find the charset in the lang_Db_charsets array */
  for (i = 0; lang_Db_charsets[i].charset_id != INTL_CODESET_NONE; i++)
    {
      if (strcmp (lang_Db_charsets[i].charset_name, name) == 0)
	{
	  *codeset = lang_Db_charsets[i].charset_id;
	  return NO_ERROR;
	}
    }

  return ER_FAILED;
}

/*
 * lang_charset_space_char - Returns the space character and its size in bytes
 *                           for a codeset
 *   return: NO_ERROR or error code
 *   codeset(in): INTL_CODESET of the desired charset
 *   space_char(out): character string holding the space character
 *   space_size(out): the number of bytes in the space character
 *
 * Note: This routine assumes that the calling routine has allocated
 *       enough space for space_char, which will use 3 bytes at a maximum.
 */
int
lang_charset_space_char (INTL_CODESET codeset, char *space_char,
			 int *space_size)
{
  int i;

  /* Find the charset in the Db_Charsets array */
  for (i = 0; lang_Db_charsets[i].charset_id != INTL_CODESET_NONE; i++)
    {
      if (lang_Db_charsets[i].charset_id == codeset)
	{
	  *space_size = lang_Db_charsets[i].space_size;
	  (void) memcpy (space_char, lang_Db_charsets[i].space_char,
			 (*space_size));
	  return NO_ERROR;
	}
    }

  *space_size = 0;
  space_char[0] = '\0';
  return ER_FAILED;
}
#endif /* !SERVER_MODE */


/*
 * English Locale Data
 */

/* English collation */
static unsigned int lang_upper_EN[LANG_CHAR_COUNT_EN];
static unsigned int lang_lower_EN[LANG_CHAR_COUNT_EN];
static unsigned char lang_is_letter_EN[LANG_CHAR_COUNT_EN];
static unsigned int lang_weight_EN[LANG_CHAR_COUNT_EN];
static unsigned int lang_next_alpha_char_EN[LANG_CHAR_COUNT_EN];

#if !defined(LANG_W_MAP_COUNT_EN)
#define	LANG_W_MAP_COUNT_EN 256
#endif
static int lang_w_map_EN[LANG_W_MAP_COUNT_EN];

/*
 * lang_initloc_en () - init locale data for English language
 *   return:
 */
static void
lang_initloc_en_iso88591 (void)
{
  lang_Loc_data->is_initialized = true;
}

/*
 * lang_initloc_en () - init locale data for English language
 *   return:
 */
static void
lang_initloc_en_utf8 (void)
{
  int i;

  assert (lang_Loc_data != NULL);

  /* init alphabet */
  for (i = 0; i < LANG_CHAR_COUNT_EN; i++)
    {
      lang_upper_EN[i] = i;
      lang_lower_EN[i] = i;

      lang_is_letter_EN[i] = 0;

      lang_weight_EN[i] = i;

      lang_next_alpha_char_EN[i] = i + 1;
    }

  lang_weight_EN[32] = 0;
  lang_next_alpha_char_EN[32] = 1;

  for (i = (int) 'a'; i <= (int) 'z'; i++)
    {
      lang_upper_EN[i] = i - ('a' - 'A');
      lang_lower_EN[i - ('a' - 'A')] = i;

      lang_is_letter_EN[i] = 1;
      lang_is_letter_EN[i - ('a' - 'A')] = 1;
    }

  /* other initializations to follow here */

  lang_Loc_data->is_initialized = true;
}

/*
 * lang_fastcmp_iso_88591 () - compare two character strings of ISO-8859-1
 *			       codeset
 *
 * Arguments:
 *      string1: 1st character string
 *        size1: size of 1st string
 *      string2: 2nd character string
 *        size2: size of 2nd string
 *
 * Returns:
 *   Greater than 0 if string1 > string2
 *   Equal to 0     if string1 = string2
 *   Less than 0    if string1 < string2
 *
 * Errors:
 *
 * Note:
 *   This function is similar to strcmp(3) or bcmp(3). It is designed to
 *   follow SQL_TEXT character set collation. Padding character(space ' ') is
 *   the smallest character in the set. (e.g.) "ab z" < "ab\t1"
 *
 */

static int
lang_fastcmp_iso_88591 (const unsigned char *string1, const int size1,
			const unsigned char *string2, const int size2)
{
  int n, i, cmp;
  unsigned char c1, c2;

#define PAD ' '			/* str_pad_char(INTL_CODESET_ISO88591, pad, &pad_size) */
#define SPACE PAD		/* smallest character in the collation sequence */
#define ZERO '\0'		/* space is treated as zero */

  n = size1 < size2 ? size1 : size2;
  for (i = 0, cmp = 0; i < n && cmp == 0; i++)
    {
      c1 = *string1++;
      if (c1 == SPACE)
	{
	  c1 = ZERO;
	}
      c2 = *string2++;
      if (c2 == SPACE)
	{
	  c2 = ZERO;
	}
      cmp = c1 - c2;
    }
  if (cmp != 0)
    {
      return cmp;
    }
  if (size1 == size2)
    {
      return cmp;
    }

  c1 = c2 = ZERO;
  if (size1 < size2)
    {
      n = size2 - size1;
      for (i = 0; i < n && cmp == 0; i++)
	{
	  c2 = *string2++;
	  if (c2 == PAD)
	    {
	      c2 = ZERO;
	    }
	  cmp = c1 - c2;
	}
    }
  else
    {
      n = size1 - size2;
      for (i = 0; i < n && cmp == 0; i++)
	{
	  c1 = *string1++;
	  if (c1 == PAD)
	    {
	      c1 = ZERO;
	    }
	  cmp = c1 - c2;
	}
    }
  return cmp;

#undef PAD
#undef SPACE
#undef ZERO
}

/*
 * lang_fastcmp_en_utf8 () - string compare for English language in UTF-8
 *   return:
 *   string1(in):
 *   size1(in):
 *   string2(in):
 *   size2(in):
 *
 * Note: This string comparison ignores trailing white spaces.
 */
static int
lang_fastcmp_en_utf8 (const unsigned char *string1, const int size1,
		      const unsigned char *string2, const int size2)
{
  int cmp, i, size;

  size = size1 < size2 ? size1 : size2;
  for (cmp = 0, i = 0; cmp == 0 && i < size; i++)
    {
      /* compare weights of the two chars */
      cmp =
	lang_Loc_data->alpha_weight[*string1++] -
	lang_Loc_data->alpha_weight[*string2++];
    }
  if (cmp != 0 || size1 == size2)
    {
      return cmp;
    }

  if (size1 < size2)
    {
      size = size2 - size1;
      for (i = 0; i < size && cmp == 0; i++)
	{
	  /* ignore tailing white spaces */
	  if (lang_Loc_data->alpha_weight[*string2++])
	    {
	      return -1;
	    }
	}
    }
  else
    {
      size = size1 - size2;
      for (i = 0; i < size && cmp == 0; i++)
	{
	  /* ignore trailing white spaces */
	  if (lang_Loc_data->alpha_weight[*string1++])
	    {
	      return 1;
	    }
	}
    }

  return cmp;
}

/*
 * lang_next_alpha_char_iso88591() - computes the next alphabetical char
 *   return: size in bytes of the next alphabetical char
 *   cur_char(in): pointer to current char
 *   next_char(in/out): buffer to return next alphabetical char
 *
 */
static int
lang_next_alpha_char_iso88591 (const unsigned char *cur_char,
			       unsigned char *next_char)
{
  *next_char = *cur_char + 1;
  return 1;
}

static LANG_LOCALE_DATA lc_English_iso88591 = {
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  0,
  false,
  NULL,				/* time, date, date-time, timestamp format */
  NULL,
  NULL,
  NULL,
  lang_initloc_en_iso88591,
  lang_fastcmp_iso_88591,
  lang_next_alpha_char_iso88591,
};

static LANG_LOCALE_DATA lc_English_utf8 = {
  lang_upper_EN,
  lang_lower_EN,
  lang_is_letter_EN,
  lang_weight_EN,
  lang_next_alpha_char_EN,
  LANG_CHAR_COUNT_EN,
  false,
  NULL,				/* time, date, date-time, timestamp format */
  NULL,
  NULL,
  NULL,
  lang_initloc_en_utf8,
  lang_fastcmp_en_utf8,
  intl_next_alpha_char_utf8
};


/*
 * Turkish Locale Data
 */

/* Turkish collation */
static unsigned int lang_upper_TR[LANG_CHAR_COUNT_TR];
static unsigned int lang_lower_TR[LANG_CHAR_COUNT_TR];
static unsigned char lang_is_letter_TR[LANG_CHAR_COUNT_TR];
static unsigned int lang_weight_TR[LANG_CHAR_COUNT_TR];
static unsigned int lang_next_alpha_char_TR[LANG_CHAR_COUNT_TR];

static char lang_time_format_TR[] = "HH24:MI:SS";
static char lang_date_format_TR[] = "DD.MM.YYYY";
static char lang_datetime_format_TR[] = "HH24:MI:SS.FF DD.MM.YYYY";
static char lang_timestamp_format_TR[] = "HH24:MI:SS DD.MM.YYYY";

/*
 * lang_initloc_tr () - init locale data for Turkish language
 *   return:
 *   string1(in):
 *   size1(in):
 *   string2(in):
 *   size2(in):
 */
static void
lang_initloc_tr (void)
{
  int i;

  const unsigned int special_upper_cp[] = {
    0xc7,			/* capital C with cedilla */
    0x11e,			/* capital letter G with breve */
    0x130,			/* capital letter I with dot above */
    0xd6,			/* capital letter O with diaeresis */
    0x15e,			/* capital letter S with cedilla */
    0xdc			/* capital letter U with diaeresis */
  };

  const unsigned int special_prev_upper_cp[] =
    { 'C', 'G', 'I', 'O', 'S', 'U' };

  const unsigned int special_lower_cp[] = {
    0xe7,			/* small c with cedilla */
    0x11f,			/* small letter g with breve */
    0x131,			/* small letter dotless i */
    0xf6,			/* small letter o with diaeresis */
    0x15f,			/* small letter s with cedilla */
    0xfc			/* small letter u with diaeresis */
  };

  const unsigned int special_prev_lower_cp[] =
    { 'c', 'g', 'h', 'o', 's', 'u' };

  assert (lang_Loc_data != NULL);

  /* init alphabet */
  for (i = 0; i < LANG_CHAR_COUNT_TR; i++)
    {
      lang_upper_TR[i] = i;
      lang_lower_TR[i] = i;

      lang_is_letter_TR[i] = 0;

      lang_weight_TR[i] = i;

      lang_next_alpha_char_TR[i] = i + 1;
    }
  lang_weight_TR[32] = 0;
  lang_next_alpha_char_TR[32] = 1;

  for (i = (int) 'a'; i <= (int) 'z'; i++)
    {
      lang_upper_TR[i] = i - ('a' - 'A');
      lang_lower_TR[i - ('a' - 'A')] = i;

      lang_lower_TR[i] = i;
      lang_upper_TR[i - ('a' - 'A')] = i - ('a' - 'A');

      lang_is_letter_TR[i] = 1;
      lang_is_letter_TR[i - ('a' - 'A')] = 1;
    }

  assert (DIM (special_lower_cp) == DIM (special_upper_cp));
  /* specific turkish letters: */
  for (i = 0; i < (int) DIM (special_lower_cp); i++)
    {
      lang_lower_TR[special_lower_cp[i]] = special_lower_cp[i];
      lang_upper_TR[special_lower_cp[i]] = special_upper_cp[i];

      lang_lower_TR[special_upper_cp[i]] = special_lower_cp[i];
      lang_upper_TR[special_upper_cp[i]] = special_upper_cp[i];

      lang_is_letter_TR[special_upper_cp[i]] = 1;
      lang_is_letter_TR[special_lower_cp[i]] = 1;
    }
  /* exceptions in TR casing : 
   */
  lang_upper_TR[0x131] = 'I';	/* small letter dotless i */
  lang_lower_TR[0x131] = 0x131;	/* small letter dotless i */
  lang_upper_TR['i'] = 0x130;	/* = capital letter I with dot above */
  lang_lower_TR['i'] = 'i';

  lang_lower_TR[0x130] = 'i';	/* capital letter I with dot above */
  lang_upper_TR[0x130] = 0x130;	/* capital letter I with dot above */
  lang_upper_TR['I'] = 'I';
  lang_lower_TR['I'] = 0x131;	/* small letter dotless i */

  /* weighting for string compare */
  for (i = 0; i < (int) DIM (special_upper_cp); i++)
    {
      unsigned int j;
      unsigned int cp = special_upper_cp[i];
      unsigned cp_repl = 1 + special_prev_upper_cp[i];
      unsigned int w_repl = lang_weight_TR[cp_repl];

      lang_weight_TR[cp] = w_repl;

      assert (cp_repl < cp);
      for (j = cp_repl; j < cp; j++)
	{
	  if (lang_weight_TR[j] >= w_repl)
	    {
	      (lang_weight_TR[j])++;
	    }
	}
    }

  for (i = 0; i < (int) DIM (special_lower_cp); i++)
    {
      unsigned int j;
      unsigned int cp = special_lower_cp[i];
      unsigned cp_repl = 1 + special_prev_lower_cp[i];
      unsigned int w_repl = lang_weight_TR[cp_repl];

      lang_weight_TR[cp] = w_repl;

      assert (cp_repl < cp);
      for (j = cp_repl; j < cp; j++)
	{
	  if (lang_weight_TR[j] >= w_repl)
	    {
	      (lang_weight_TR[j])++;
	    }
	}
    }

  /* next letter in alphabet (for pattern searching) */
  for (i = 0; i < (int) DIM (special_upper_cp); i++)
    {
      unsigned int cp_special = special_upper_cp[i];
      unsigned int cp_prev = special_prev_upper_cp[i];
      unsigned int cp_next = cp_prev + 1;

      lang_next_alpha_char_TR[cp_prev] = cp_special;
      lang_next_alpha_char_TR[cp_special] = cp_next;
    }

  for (i = 0; i < (int) DIM (special_lower_cp); i++)
    {
      unsigned int cp_special = special_lower_cp[i];
      unsigned int cp_prev = special_prev_lower_cp[i];
      unsigned int cp_next = cp_prev + 1;

      lang_next_alpha_char_TR[cp_prev] = cp_special;
      lang_next_alpha_char_TR[cp_special] = cp_next;
    }

  /* other initializations to follow here */

  lang_Loc_data->is_initialized = true;
}

static LANG_LOCALE_DATA lc_Turkish = {
  lang_upper_TR,
  lang_lower_TR,
  lang_is_letter_TR,
  lang_weight_TR,
  lang_next_alpha_char_TR,
  LANG_CHAR_COUNT_TR,
  false,
  lang_time_format_TR,
  lang_date_format_TR,
  lang_datetime_format_TR,
  lang_timestamp_format_TR,
  lang_initloc_tr,
  intl_strcmp_utf8,
  intl_next_alpha_char_utf8
};


/*
 * Korean Locale Data
 */

/*
 * lang_initloc_ko () - init locale data for Korean language
 *   return:
 */
static void
lang_initloc_ko (void)
{
  assert (lang_Loc_data != NULL);

  /* TODO: update this if EUC-KR is fully supported as standalone charset */

  lang_Loc_data->is_initialized = true;
}

/*
 * lang_fastcmp_ko () - compare two EUC-KR character strings
 *
 * Arguments:
 *      string1: 1st character string
 *        size1: size of 1st string
 *      string2: 2nd character string
 *        size2: size of 2nd string
 *
 * Returns:
 *   Greater than 0 if string1 > string2
 *   Equal to 0     if string1 = string2
 *   Less than 0    if string1 < string2
 *
 */
static int
lang_fastcmp_ko (const unsigned char *string1, const int size1,
		 const unsigned char *string2, const int size2)
{
  int n, i, cmp, pad_size = 0;
  unsigned char c1, c2, pad[2];

  assert (size1 >= 0 && size2 >= 0);

  pad[0] = pad[1] = '\241';
  pad_size = 2;

#define PAD pad[i % pad_size]
#define SPACE PAD		/* smallest character in the collation sequence */
#define ZERO '\0'		/* space is treated as zero */
  n = size1 < size2 ? size1 : size2;
  for (i = 0, cmp = 0; i < n && cmp == 0; i++)
    {
      c1 = *string1++;
      if (c1 == SPACE)
	{
	  c1 = ZERO;
	}
      c2 = *string2++;
      if (c2 == SPACE)
	{
	  c2 = ZERO;
	}
      cmp = c1 - c2;
    }
  if (cmp != 0)
    {
      return cmp;
    }
  if (size1 == size2)
    {
      return cmp;
    }

  c1 = c2 = ZERO;
  if (size1 < size2)
    {
      n = size2 - size1;
      for (i = 0; i < n && cmp == 0; i++)
	{
	  c2 = *string2++;
	  if (c2 == PAD)
	    {
	      c2 = ZERO;
	    }
	  cmp = c1 - c2;
	}
    }
  else
    {
      n = size1 - size2;
      for (i = 0; i < n && cmp == 0; i++)
	{
	  c1 = *string1++;
	  if (c1 == PAD)
	    {
	      c1 = ZERO;
	    }
	  cmp = c1 - c2;
	}
    }
  return cmp;
#undef SPACE
#undef ZERO
#undef PAD
}

/*
 * lang_next_alpha_char_ko() - computes the next alphabetical char
 *   return: size in bytes of the next alphabetical char
 *   cur_char(in): pointer to current char
 *   next_char(in/out): buffer to return next alphabetical char
 *
 */
static int
lang_next_alpha_char_ko (const unsigned char *cur_char,
			 unsigned char *next_char)
{
  /* TODO: update this if EUC-KR is fully supported as standalone charset */
  *next_char = *cur_char + 1;
  return 1;
}


static LANG_LOCALE_DATA lc_Korean = {
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  0,
  false,
  NULL,				/* time, date, date-time, timestamp format */
  NULL,
  NULL,
  NULL,
  lang_initloc_ko,
  lang_fastcmp_ko,
  lang_next_alpha_char_ko
};
