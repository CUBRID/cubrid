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
#if !defined(WINDOWS)
#include <langinfo.h>
#endif

#include "chartype.h"
#include "misc_string.h"
#include "language_support.h"
#include "authenticate.h"
#include "environment_variable.h"
#include "db.h"
#include "locale_support.h"
/* this must be the last header file included! */
#include "dbval.h"

static INTL_LANG lang_Loc_id = INTL_LANG_ENGLISH;
static INTL_CODESET lang_Loc_charset = INTL_CODESET_ISO88591;
static int lang_Loc_bytes_per_char = 1;
static char lang_Loc_name[LANG_MAX_LANGNAME] = LANG_NAME_DEFAULT;
static char lang_Lang_name[LANG_MAX_LANGNAME] = LANG_NAME_DEFAULT;
static DB_CURRENCY lang_Loc_currency = DB_CURRENCY_DOLLAR;

/* locale data */
static LANG_LOCALE_DATA lc_English_iso88591;
static LANG_LOCALE_DATA lc_English_utf8;
static LANG_LOCALE_DATA lc_Turkish_iso88591;
static LANG_LOCALE_DATA lc_Turkish_utf8;
static LANG_LOCALE_DATA lc_Korean_iso88591;
static LANG_LOCALE_DATA lc_Korean_utf8;
static LANG_LOCALE_DATA *lang_Loc_data = &lc_English_iso88591;

static bool lang_Initialized = false;
static bool lang_Fully_Initialized = false;
static bool lang_Init_w_error = false;

typedef struct lang_defaults LANG_DEFAULTS;
struct lang_defaults
{
  const char *lang_name;
  const INTL_LANG lang;
  const INTL_CODESET codeset;
};

LANG_DEFAULTS builtin_langs[] = {
  /* English - ISO-8859-1 - default lang and charset */
  {LANG_NAME_ENGLISH, INTL_LANG_ENGLISH, INTL_CODESET_ISO88591},
  /* English - UTF-8 */
  {LANG_NAME_ENGLISH, INTL_LANG_ENGLISH, INTL_CODESET_UTF8},
  /* Korean - EUC-KR */
  {LANG_NAME_KOREAN, INTL_LANG_KOREAN, INTL_CODESET_KSC5601_EUC},
  /* Turkish - UTF-8 */
  {LANG_NAME_TURKISH, INTL_LANG_TURKISH, INTL_CODESET_UTF8}
};


static TEXT_CONVERSION *console_conv = NULL;
extern TEXT_CONVERSION con_iso_8859_1_conv;
extern TEXT_CONVERSION con_iso_8859_9_conv;

/* all loaded locales */
#define MAX_LOADED_LOCALES  32
static LANG_LOCALE_DATA *lang_loaded_locales[MAX_LOADED_LOCALES] = { NULL };
static int lang_count_locales = 0;

static void set_current_locale (bool is_full_init);
static void set_lang_from_env (void);
static void set_default_lang (void);
static int init_user_locales (void);
static void register_lang_locale_data (LANG_LOCALE_DATA * lld);

static bool lang_is_codeset_allowed (const INTL_LANG intl_id,
				     const INTL_CODESET codeset);
static int lang_get_lang_id_from_name (const char *lang_name,
				       INTL_LANG * lang);
static int lang_get_builtin_lang_id_from_name (const char *lang_name,
					       INTL_LANG * lang_id);
static INTL_CODESET lang_get_default_codeset (const INTL_LANG intl_id);

/*
 * lang_init - Initializes any global state required by the multi-language
 *             module
 *   return: true if success
 *
 *  Note : this is a "light" language initialization. User defined locales
 *	   are not loaded during this process and if environment cannot be
 *	   resolved, the default language is set.
 */
bool
lang_init (void)
{
  if (lang_Initialized)
    {
      return lang_Initialized;
    }

  set_lang_from_env ();

  /* register all built-in locales allowed in current charset 
   * Support for multiple locales is required for switching function context
   * string - data/time , string - number conversions */
  if (lang_Loc_charset == INTL_CODESET_UTF8)
    {
      register_lang_locale_data (&lc_English_utf8);
      register_lang_locale_data (&lc_Korean_utf8);
      register_lang_locale_data (&lc_Turkish_utf8);
    }
  else
    {
      register_lang_locale_data (&lc_English_iso88591);
      register_lang_locale_data (&lc_Korean_iso88591);
      register_lang_locale_data (&lc_Turkish_iso88591);
    }

  /* set current locale */
  set_current_locale (false);

  lang_Initialized = true;

  return (lang_Initialized);
}

/*
 * lang_init_full - Initializes the language according to environment
 *		    including user defined language
 *
 *   return: true if success
 */
bool
lang_init_full (void)
{
  (void) lang_init ();

  if (lang_Fully_Initialized)
    {
      return lang_Fully_Initialized;
    }

  assert (lang_Initialized == true);

  /* re-get variables from environment */
  set_lang_from_env ();

  /* load & register user locales (only in UTF-8 codeset) */
  if (lang_Loc_charset == INTL_CODESET_UTF8)
    {
      if (init_user_locales () != NO_ERROR)
	{
	  set_default_lang ();
	  lang_Init_w_error = true;
	  lang_Fully_Initialized = true;
	  return lang_Fully_Initialized;
	}
    }

  set_current_locale (true);

  if (lang_Loc_data->txt_conv != NULL)
    {
      char *sys_id = NULL;
      char *conv_sys_ids = NULL;
#if defined(WINDOWS)
      UINT cp;
      char win_codepage_str[32];

      cp = GetConsoleCP ();
      snprintf (win_codepage_str, sizeof (win_codepage_str) - 1, "%d", cp);

      sys_id = win_codepage_str;
      conv_sys_ids = lang_Loc_data->txt_conv->win_codepages;
#else
      if (setlocale (LC_CTYPE, "") != NULL)
	{
	  sys_id = nl_langinfo (CODESET);
	  conv_sys_ids = lang_Loc_data->txt_conv->nl_lang_str;
	}
#endif

      if (sys_id != NULL && conv_sys_ids != NULL)
	{
	  char *conv_sys_end = conv_sys_ids + strlen (conv_sys_ids);
	  char *found_token;

	  /* supported system identifiers for conversion are separated by
	   * comma */
	  do
	    {
	      found_token = strstr (conv_sys_ids, sys_id);
	      if (found_token == NULL)
		{
		  break;
		}

	      if (found_token + strlen (sys_id) >= conv_sys_end
		  || *(found_token + strlen (sys_id)) == ','
		  || *(found_token + strlen (sys_id)) == ' ')
		{
		  if (lang_Loc_data->txt_conv->init_conv_func != NULL)
		    {
		      lang_Loc_data->txt_conv->init_conv_func ();
		    }
		  console_conv = lang_Loc_data->txt_conv;
		  break;
		}
	      else
		{
		  conv_sys_ids = conv_sys_ids + strlen (sys_id);
		}
	    }
	  while (conv_sys_ids < conv_sys_end);
	}
    }

  lang_Fully_Initialized = true;

  return (lang_Fully_Initialized);
}

/*
 * set_current_locale - sets the current locale according to 'lang_Lang_name'
 *			and 'lang_Loc_charset'
 *
 *  is_full_init(in) : true if this is a full language initialization
 */
static void
set_current_locale (bool is_full_init)
{
  lang_get_lang_id_from_name (lang_Lang_name, &lang_Loc_id);

  lang_Loc_data = lang_loaded_locales[lang_Loc_id];

  if (lang_Loc_data->codeset != lang_Loc_charset
      || strcmp (lang_Lang_name,
		 lang_get_lang_name_from_id (lang_Loc_id)) != 0)
    {
      /* when charset is not UTF-8, full init will not be required */
      if (is_full_init || lang_Loc_charset != INTL_CODESET_UTF8)
	{
	  lang_Init_w_error = true;
	}
      set_default_lang ();
    }

  lang_Loc_currency = lang_Loc_data->default_currency_code;

  /* static globals in db_date.c should also be initialized with the current
   * locale (for parsing local am/pm strings for times) */
  db_date_locale_init ();
}

/*
 * set_lang_from_env - Initializes language variables from environment
 *
 *   return: true if success
 *
 *  Note : This function sets the following variables according to
 *	   $CUBRID_LANG environment variable :
 *	    lang_Loc_name : locale string: <lang>.<charset>; en_US.utf8
 *	    lang_Lang_name : <lang> string part (without <charset>
 *	    lang_Loc_charset : charset id : ISO-8859-1 or UTF-8 
 *	    lang_Loc_bytes_per_char : maximum number of bytes per character
 */
static void
set_lang_from_env (void)
{
  const char *env, *s;
  /*
   * Determine the locale by examining environment variables.
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

  /* allow environment to override the character set settings */
  s = strchr (lang_Loc_name, '.');
  if (s != NULL)
    {
      strncpy (lang_Lang_name, lang_Loc_name, s - lang_Loc_name);
      lang_Lang_name[s - lang_Loc_name] = '\0';

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

      /* for UTF-8 charset we allow any user defined lang name */
      if (lang_Loc_charset != INTL_CODESET_UTF8)
	{
	  (void) lang_get_builtin_lang_id_from_name (lang_Loc_name,
						     &lang_Loc_id);
	  if (!lang_is_codeset_allowed (lang_Loc_id, lang_Loc_charset))
	    {
	      lang_Loc_charset = lang_get_default_codeset (lang_Loc_id);
	    }
	}
    }
  else
    {
      /* no charset provided in $CUBRID_LANG */
      (void) lang_get_builtin_lang_id_from_name (lang_Loc_name, &lang_Loc_id);
      strcpy (lang_Lang_name, lang_Loc_name);
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
}

/*
 * set_default_lang -
 *   return:
 *
 */
static void
set_default_lang (void)
{
  lang_Loc_id = INTL_LANG_ENGLISH;
  strncpy (lang_Loc_name, LANG_NAME_DEFAULT, sizeof (lang_Loc_name));
  strncpy (lang_Lang_name, LANG_NAME_DEFAULT, sizeof (lang_Lang_name));
  lang_Loc_charset = lang_get_default_codeset (lang_Loc_id);
  lang_Loc_bytes_per_char = 1;
  lang_Loc_data = &lc_English_iso88591;
  lang_Loc_currency = lang_Loc_data->default_currency_code;
}

/*
 * lang_check_init -
 *   return: error code if language initialization flag is set
 *
 */
bool
lang_check_init (void)
{
  return (!lang_Init_w_error);
}

/* if is a new locale, always assign the user defined string
 * if locale already exists, assign the user defined string when there is no
 * existing format, or the user defined string is not empty */
#define SET_LOCALE_STRING(is_new,lf,uf)	\
   do { \
      if (is_new) \
	{ \
	  lf = uf; \
	} \
      else \
	{ \
	  lf = (lf == NULL) ? (uf) : ((*(uf) == '\0') ? (lf) : (uf)); \
	} \
   } while (0);

/*
 * init_user_locales -
 *   return: error code
 *
 */
static int
init_user_locales (void)
{
  LOCALE_FILE *user_lf = NULL;
  LOCALE_DATA *usr_loc = NULL;
  int num_user_loc = 0, i;
  int er_status = NO_ERROR;

  er_status = locale_get_cfg_locales (&user_lf, &num_user_loc, true);
  if (er_status != NO_ERROR)
    {
      goto error;
    }

  for (i = 0; i < num_user_loc; i++)
    {
      /* load user locale */
      LANG_LOCALE_DATA *lld = NULL;
      INTL_LANG l_id;
      bool is_new_lang = false;
      int j;

      er_status = locale_check_and_set_default_files (&(user_lf[i]), true);
      if (er_status != NO_ERROR)
	{
	  goto error;
	}

      usr_loc = malloc (sizeof (LOCALE_DATA));
      if (usr_loc == NULL)
	{
	  er_status = ER_LOC_INIT;
	  LOG_LOCALE_ERROR ("memory allocation failed", er_status, true);
	  goto error;
	}

      memset (usr_loc, 0, sizeof (LOCALE_DATA));

      er_status = locale_load_from_bin (&(user_lf[i]), usr_loc);
      if (er_status != NO_ERROR)
	{
	  goto error;
	}

      if (lang_get_lang_id_from_name (user_lf[i].locale_name, &l_id) == 0)
	{
	  /* language already found, overwrite with user defined */
	  lld = lang_loaded_locales[(int) l_id];

	  /* double user customization : remove previous one */
	  if (lld->user_data != NULL)
	    {
	      locale_destroy_data (lld->user_data);
	    }
	}
      else
	{
	  /* new language */
	  l_id = lang_count_locales;

	  assert (l_id >= INTL_LANG_USER_DEF_START);

	  if (l_id >= MAX_LOADED_LOCALES)
	    {
	      er_status = ER_LOC_INIT;
	      LOG_LOCALE_ERROR ("too many locales", er_status, true);
	      goto error;
	    }

	  lld = malloc (sizeof (LANG_LOCALE_DATA));
	  if (lld == NULL)
	    {
	      er_status = ER_LOC_INIT;
	      LOG_LOCALE_ERROR ("memory allocation failed", er_status, true);
	      goto error;
	    }

	  memset (lld, 0, sizeof (LANG_LOCALE_DATA));

	  is_new_lang = true;
	  register_lang_locale_data (lld);
	}

      /* initialize Language locale */
      lld->lang_name = usr_loc->locale_name;
      lld->lang_id = l_id;

      /* user defined language is supported only with UTF-8 codeset */
      lld->codeset = (is_new_lang) ? INTL_CODESET_UTF8 : lld->codeset;

      /* calendar data */
      SET_LOCALE_STRING (is_new_lang, lld->date_format, usr_loc->dateFormat);
      SET_LOCALE_STRING (is_new_lang, lld->time_format, usr_loc->timeFormat);
      SET_LOCALE_STRING (is_new_lang, lld->datetime_format,
			 usr_loc->datetimeFormat);
      SET_LOCALE_STRING (is_new_lang, lld->timestamp_format,
			 usr_loc->timestampFormat);

      for (j = 0; j < CAL_DAY_COUNT; j++)
	{
	  SET_LOCALE_STRING (is_new_lang, lld->day_short_name[j],
			     usr_loc->day_names_abbreviated[j]);
	  SET_LOCALE_STRING (is_new_lang, lld->day_name[j],
			     usr_loc->day_names_wide[j]);
	}

      for (j = 0; j < CAL_MONTH_COUNT; j++)
	{
	  SET_LOCALE_STRING (is_new_lang, lld->month_short_name[j],
			     usr_loc->month_names_abbreviated[j]);
	  SET_LOCALE_STRING (is_new_lang, lld->month_name[j],
			     usr_loc->month_names_wide[j]);
	}

      lld->day_short_parse_order = usr_loc->day_names_abbr_parse_order;
      lld->day_parse_order = usr_loc->day_names_wide_parse_order;
      lld->month_parse_order = usr_loc->month_names_wide_parse_order;
      lld->month_short_parse_order = usr_loc->month_names_abbr_parse_order;
      lld->am_pm_parse_order = usr_loc->am_pm_parse_order;

      for (j = 0; j < CAL_AM_PM_COUNT; j++)
	{
	  SET_LOCALE_STRING (is_new_lang, lld->am_pm[j], usr_loc->am_pm[j]);
	}

      lld->number_decimal_sym = usr_loc->number_decimal_sym;
      lld->number_group_sym = usr_loc->number_group_sym;
      lld->default_currency_code = usr_loc->default_currency_code;

      /* user alphabet */
      memcpy (&(lld->alphabet), &(usr_loc->alphabet), sizeof (ALPHABET_DATA));
      /* identifier alphabet */
      memcpy (&(lld->ident_alphabet), &(usr_loc->identif_alphabet),
	      sizeof (ALPHABET_DATA));

      /* collation data */
      memcpy (&(lld->coll), &(usr_loc->opt_coll), sizeof (COLL_DATA));

      if (lld->coll.uca_exp_num > 1)
	{
	  lld->fastcmp = intl_strcmp_utf8_uca;
	  lld->next_coll_seq = intl_next_coll_seq_utf8_w_contr;
	}
      else if (lld->coll.count_contr > 0)
	{
	  lld->fastcmp = intl_strcmp_utf8_w_contr;
	  lld->next_coll_seq = intl_next_coll_seq_utf8_w_contr;
	}
      else
	{
	  lld->fastcmp = intl_strcmp_utf8;
	  lld->next_coll_seq = intl_next_coll_char_utf8;
	}

      if (usr_loc->txt_conv.conv_type == TEXT_CONV_GENERIC_2BYTE)
	{
	  lld->txt_conv = &(usr_loc->txt_conv);
	  lld->txt_conv->init_conv_func = NULL;
	  lld->txt_conv->text_to_utf8_func = intl_text_dbcs_to_utf8;
	  lld->txt_conv->utf8_to_text_func = intl_text_utf8_to_dbcs;
	}
      else if (usr_loc->txt_conv.conv_type == TEXT_CONV_GENERIC_1BYTE)
	{
	  lld->txt_conv = &(usr_loc->txt_conv);
	  lld->txt_conv->init_conv_func = NULL;
	  lld->txt_conv->text_to_utf8_func = intl_text_single_byte_to_utf8;
	  lld->txt_conv->utf8_to_text_func = intl_text_utf8_to_single_byte;
	}
      else if (usr_loc->txt_conv.conv_type == TEXT_CONV_ISO_88591_BUILTIN)
	{
	  lld->txt_conv = &con_iso_8859_1_conv;
	}
      else if (usr_loc->txt_conv.conv_type == TEXT_CONV_ISO_88599_BUILTIN)
	{
	  lld->txt_conv = &con_iso_8859_9_conv;
	}
      else
	{
	  assert (usr_loc->txt_conv.conv_type == TEXT_CONV_NO_CONVERSION);
	  lld->txt_conv = NULL;
	}

      lld->user_data = usr_loc;
      lld->is_initialized = true;

      usr_loc = NULL;
    }

  /* free user defined locale files struct */
  for (i = 0; i < num_user_loc; i++)
    {
      free_and_init (user_lf[i].locale_name);
      free_and_init (user_lf[i].ldml_file);
      free_and_init (user_lf[i].bin_file);
    }

  if (user_lf != NULL)
    {
      free (user_lf);
    }

  return er_status;

error:

  if (usr_loc != NULL)
    {
      locale_destroy_data (usr_loc);
      free (usr_loc);
      usr_loc = NULL;
    }

  /* free user defined locale files struct */
  for (i = 0; i < num_user_loc; i++)
    {
      free_and_init (user_lf[i].locale_name);
      free_and_init (user_lf[i].ldml_file);
      free_and_init (user_lf[i].bin_file);
    }

  if (user_lf != NULL)
    {
      free (user_lf);
    }

  return er_status;
}

/*
 * register_lang_locale_data - registers a language locale data to the
 *   return:
 *   lld(in): language locale data
 */
static void
register_lang_locale_data (LANG_LOCALE_DATA * lld)
{
  assert (lld != NULL);
  assert (lang_count_locales < MAX_LOADED_LOCALES);

  lang_loaded_locales[lang_count_locales++] = lld;

  if (!(lld->is_initialized) && lld->initloc != NULL)
    {
      assert (lld->lang_id < (INTL_LANG) INTL_LANG_USER_DEF_START);
      init_builtin_calendar_names (lld);
      lld->initloc (lld);
    }
}

/*
 * lang_get_Loc_name - returns full locale name (language_name.codeset)
 *		       according to environment
 *   return: locale string
 */
const char *
lang_get_Loc_name (void)
{
  if (!lang_Initialized)
    {
      lang_init ();
    }
  return lang_Loc_name;
}

/*
 * lang_get_Lang_name - returns the language name according to environment
 *   return: language name string
 */
const char *
lang_get_Lang_name (void)
{
  if (!lang_Initialized)
    {
      lang_init ();
    }
  return lang_Lang_name;
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
  int i;

  for (i = 0; i < lang_count_locales; i++)
    {
      assert (lang_loaded_locales[i] != NULL);
      if (lang_loaded_locales[i]->user_data != NULL)
	{
	  locale_destroy_data (lang_loaded_locales[i]->user_data);
	}

      lang_loaded_locales[i]->is_initialized = false;
    }

  locale_destroy_shared_data ();

  lang_count_locales = 0;
  lang_Initialized = false;
  lang_Fully_Initialized = false;
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

  if (lang_charset () == codeset && (int) lang < lang_count_locales)
    {
      return lang_loaded_locales[lang];
    }

  return lang_Loc_data;
}

/*
 * lang_get_builtin_lang_id_from_name - returns the builtin language id from a
 *					language name
 *
 *   return: 0, if language name is accepted, non-zero otherwise
 *   lang_name(in):
 *   lang_id(out): language identifier
 *
 *  Note : INTL_LANG_ENGLISH is returned if name is not a valid language name
 */
static int
lang_get_builtin_lang_id_from_name (const char *lang_name,
				    INTL_LANG * lang_id)
{
  int i;

  assert (lang_id != NULL);

  *lang_id = INTL_LANG_ENGLISH;

  for (i = 0; i < sizeof (builtin_langs) / sizeof (LANG_DEFAULTS); i++)
    {
      if (strncasecmp (lang_name, builtin_langs[i].lang_name,
		       strlen (builtin_langs[i].lang_name)) == 0)
	{
	  *lang_id = builtin_langs[i].lang;
	  return 0;
	}
    }

  assert (*lang_id < INTL_LANG_USER_DEF_START);

  return 1;
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
  int i;

  assert (lang_id != NULL);

  *lang_id = INTL_LANG_ENGLISH;

  for (i = 0; i < lang_count_locales; i++)
    {
      assert (lang_loaded_locales[i] != NULL);

      if (strcasecmp (lang_name, lang_loaded_locales[i]->lang_name) == 0)
	{
	  assert (i == (int) lang_loaded_locales[i]->lang_id);
	  *lang_id = lang_loaded_locales[i]->lang_id;
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
  if ((int) lang_id < lang_count_locales)
    {
      assert (lang_loaded_locales[lang_id] != NULL);
      return lang_loaded_locales[lang_id]->lang_name;
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
 *		Bit 1 - 31 : INTL_LANG
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
 *		Bit 1 - 31 : INTL_LANG
 *		Consider change this flag to store the language as value
 *		instead of as bit map
 *
 *  Note : function is used in context of some date-string functions.
 */
int
lang_set_flag_from_lang_id (const INTL_LANG lang, bool user_format, int *flag)
{
  int lang_val = (int) lang;

  if (user_format)
    {
      *flag = 0;
    }
  else
    {
      *flag = 0x1;
    }

  if (lang_val >= lang_count_locales)
    {
      lang_val = (int) INTL_LANG_ENGLISH;
      *flag |= lang_val << 1;
      return 1;
    }

  *flag |= lang_val << 1;

  return 0;
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
  int lang_val = (int) flag;

  if (flag & 0x1)
    {
      *user_format = false;
    }
  else
    {
      *user_format = true;
    }

  lang_val = flag >> 1;

  if (lang_val >= 0 && lang_val < lang_count_locales)
    {
      return (INTL_LANG) lang_val;
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

  for (i = 0; i < sizeof (builtin_langs) / sizeof (LANG_DEFAULTS); i++)
    {
      if (intl_id == builtin_langs[i].lang)
	{
	  codeset = builtin_langs[i].codeset;
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

  for (i = 0; i < sizeof (builtin_langs) / sizeof (LANG_DEFAULTS); i++)
    {
      if (intl_id == builtin_langs[i].lang &&
	  codeset == builtin_langs[i].codeset)
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
  const LANG_LOCALE_DATA *lld =
    lang_get_specific_locale (lang_id, lang_Loc_charset);

  assert (lld != NULL);

  if (PRM_USE_LOCALE_NUMBER_FORMAT)
    {
      return lld->number_group_sym;
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
  const LANG_LOCALE_DATA *lld =
    lang_get_specific_locale (lang_id, lang_Loc_charset);

  assert (lld != NULL);

  if (PRM_USE_LOCALE_NUMBER_FORMAT)
    {
      return lld->number_decimal_sym;
    }

  return '.';
}

/*
 * lang_get_txt_conv - Returns the information required for console text
 *		       conversion
 */
TEXT_CONVERSION *
lang_get_txt_conv (void)
{
  return console_conv;
}

#if !defined (SERVER_MODE)
static DB_CHARSET lang_Server_charset;
static INTL_LANG lang_Server_lang_id;
static char lang_Server_lang_name[LANG_MAX_LANGNAME + 1];

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

      if (db_get (Au_root, "lang", &value) != NO_ERROR)
	{
	  lang_Server_lang_id = lang_id ();
	  strcpy (lang_Server_lang_name,
		  lang_get_lang_name_from_id (lang_Server_lang_id));
	}
      else
	{
	  char *db_lang;

	  db_lang = db_get_string (&value);

	  if (db_lang != NULL)
	    {
	      int lang_len = MIN (strlen (db_lang), LANG_MAX_LANGNAME);

	      strncpy (lang_Server_lang_name, db_lang, lang_len);
	      lang_Server_lang_name[lang_len] = '\0';

	      lang_get_lang_id_from_name (lang_Server_lang_name,
					  &lang_Server_lang_id);
	    }
	  else
	    {
	      lang_Server_lang_id = lang_id ();
	      strcpy (lang_Server_lang_name,
		      lang_get_lang_name_from_id (lang_Server_lang_id));
	    }
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
  db_make_string (&value, lang_get_lang_name_from_id (server_lang));
  if (db_put_internal (Au_root, "lang", &value) != NO_ERROR)
    {
      /* Error Setting the language */
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
lang_initloc_en_iso88591 (LANG_LOCALE_DATA * ld)
{
  assert (ld != NULL);

  ld->is_initialized = true;
}

/*
 * lang_initloc_en () - init locale data for English language
 *   return:
 */
static void
lang_initloc_en_utf8 (LANG_LOCALE_DATA * ld)
{
  int i;

  assert (ld != NULL);

  /* init alphabet */
  for (i = 0; i < LANG_CHAR_COUNT_EN; i++)
    {
      lang_upper_EN[i] = i;
      lang_lower_EN[i] = i;

      lang_weight_EN[i] = i;

      lang_next_alpha_char_EN[i] = i + 1;
    }

  lang_weight_EN[32] = 0;
  lang_next_alpha_char_EN[32] = 1;

  for (i = (int) 'a'; i <= (int) 'z'; i++)
    {
      lang_upper_EN[i] = i - ('a' - 'A');
      lang_lower_EN[i - ('a' - 'A')] = i;
    }

  /* other initializations to follow here */

  ld->is_initialized = true;
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
      cmp = lang_Loc_data->coll.weights[*string1++] -
	lang_Loc_data->coll.weights[*string2++];
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
	  if (lang_Loc_data->coll.weights[*string2++])
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
	  if (lang_Loc_data->coll.weights[*string1++])
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
 *   seq(in): pointer to current char
 *   size(in): size in bytes for seq
 *   next_seq(in/out): buffer to return next alphabetical char
 *   len_next(in/out): length in chars for nex_seq
 *
 */
static int
lang_next_alpha_char_iso88591 (const unsigned char *seq, const int size,
			       unsigned char *next_seq, int *len_next)
{
  assert (seq != NULL);
  assert (next_seq != NULL);
  assert (len_next != NULL);
  assert (size > 0);

  *next_seq = *seq + 1;
  *len_next = 1;
  return 1;
}

static LANG_LOCALE_DATA lc_English_iso88591 = {
  LANG_NAME_ENGLISH,
  INTL_LANG_ENGLISH,
  INTL_CODESET_ISO88591,
  {ALPHABET_TAILORED, 0, 0, NULL, 0, NULL, false},	/* alphabet */
  {ALPHABET_TAILORED, 0, 0, NULL, 0, NULL, false},	/* alphabet identifiers */
  {{TAILOR_UNDEFINED, false, false, 0, false, CONTR_IGNORE},
   NULL, NULL, 0, 0, NULL, NULL, NULL, NULL, 0, 0, NULL, 0, 0},	/* collation */
  NULL,				/* console text conversion */
  false,
  NULL,				/* time, date, date-time, timestamp format */
  NULL,
  NULL,
  NULL,
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  '.',
  ',',
  DB_CURRENCY_DOLLAR,
  lang_initloc_en_iso88591,
  lang_fastcmp_iso_88591,
  lang_next_alpha_char_iso88591,
  NULL
};

static LANG_LOCALE_DATA lc_English_utf8 = {
  LANG_NAME_ENGLISH,
  INTL_LANG_ENGLISH,
  INTL_CODESET_UTF8,
  {ALPHABET_ASCII, LANG_CHAR_COUNT_EN, 1, lang_lower_EN, 1, lang_upper_EN,
   false},
  {ALPHABET_ASCII, LANG_CHAR_COUNT_EN, 1, lang_lower_EN, 1, lang_upper_EN,
   false},
  {{TAILOR_UNDEFINED, false, false, 0, false, CONTR_IGNORE},
   lang_weight_EN, lang_next_alpha_char_EN, LANG_CHAR_COUNT_EN,
   0, NULL, NULL, NULL,
   NULL, 0, 0, NULL, 0, 0},
  &con_iso_8859_1_conv,		/* text conversion */
  false,
  NULL,				/* time, date, date-time, timestamp format */
  NULL,
  NULL,
  NULL,
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  '.',
  ',',
  DB_CURRENCY_DOLLAR,
  lang_initloc_en_utf8,
  lang_fastcmp_en_utf8,
  intl_next_coll_char_utf8,
  NULL
};


/*
 * Turkish Locale Data
 */

/* Turkish collation */
static unsigned int lang_upper_TR[LANG_CHAR_COUNT_TR];
static unsigned int lang_lower_TR[LANG_CHAR_COUNT_TR];
static unsigned int lang_upper_i_TR[LANG_CHAR_COUNT_TR];
static unsigned int lang_lower_i_TR[LANG_CHAR_COUNT_TR];
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
lang_initloc_tr (LANG_LOCALE_DATA * ld)
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

  assert (ld != NULL);

  /* init alphabet */
  for (i = 0; i < LANG_CHAR_COUNT_TR; i++)
    {
      lang_upper_TR[i] = i;
      lang_lower_TR[i] = i;

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
    }

  assert (DIM (special_lower_cp) == DIM (special_upper_cp));
  /* specific turkish letters: */
  for (i = 0; i < (int) DIM (special_lower_cp); i++)
    {
      lang_lower_TR[special_lower_cp[i]] = special_lower_cp[i];
      lang_upper_TR[special_lower_cp[i]] = special_upper_cp[i];

      lang_lower_TR[special_upper_cp[i]] = special_lower_cp[i];
      lang_upper_TR[special_upper_cp[i]] = special_upper_cp[i];
    }

  memcpy (lang_upper_i_TR, lang_upper_TR,
	  LANG_CHAR_COUNT_TR * sizeof (lang_upper_TR[0]));
  memcpy (lang_lower_i_TR, lang_lower_TR,
	  LANG_CHAR_COUNT_TR * sizeof (lang_lower_TR[0]));

  /* identifiers alphabet : same as Unicode data */
  lang_upper_i_TR[0x131] = 'I';	/* small letter dotless i */
  lang_lower_i_TR[0x130] = 'i';	/* capital letter I with dot above */

  /* exceptions in TR casing for user alphabet : 
   */
  lang_upper_TR[0x131] = 'I';	/* small letter dotless i */
  lang_lower_TR[0x131] = 0x131;	/* small letter dotless i */
  lang_upper_TR['i'] = 0x130;	/* capital letter I with dot above */
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

  ld->is_initialized = true;
}

/* Turkish in ISO-8859-1 charset : limited support (only date - formats) */
static LANG_LOCALE_DATA lc_Turkish_iso88591 = {
  LANG_NAME_TURKISH,
  INTL_LANG_TURKISH,
  INTL_CODESET_ISO88591,
  {ALPHABET_TAILORED, 0, 0, NULL, 0, NULL, false},	/* alphabet : same as English ISO */
  {ALPHABET_TAILORED, 0, 0, NULL, 0, NULL, false},	/* identifiers alphabet : same as English ISO */
  {{TAILOR_UNDEFINED, false, false, 0, false, CONTR_IGNORE},
   NULL, NULL, 0, 0, NULL, NULL, NULL, NULL, 0, 0, NULL, 0, 0},	/* collation : same as English ISO */
  NULL,				/* console text conversion */
  false,
  lang_time_format_TR,
  lang_date_format_TR,
  lang_datetime_format_TR,
  lang_timestamp_format_TR,
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  ',',
  '.',
  DB_CURRENCY_TL,
  lang_initloc_en_iso88591,	/* same as English with ISO charset */
  lang_fastcmp_iso_88591,
  lang_next_alpha_char_iso88591,
  NULL
};

extern TEXT_CONVERSION con_iso_8859_9_conv;
static LANG_LOCALE_DATA lc_Turkish_utf8 = {
  LANG_NAME_TURKISH,
  INTL_LANG_TURKISH,
  INTL_CODESET_UTF8,
  {ALPHABET_ASCII, LANG_CHAR_COUNT_TR, 1, lang_lower_TR, 1, lang_upper_TR,
   false},
  {ALPHABET_TAILORED, LANG_CHAR_COUNT_TR, 1, lang_lower_i_TR, 1,
   lang_upper_i_TR, false},
  {{TAILOR_UNDEFINED, false, false, 0, false, CONTR_IGNORE},
   lang_weight_TR, lang_next_alpha_char_TR, LANG_CHAR_COUNT_TR,
   0, NULL, NULL, NULL,
   NULL, 0, 0, NULL, 0, 0},
  &con_iso_8859_9_conv,		/* console text conversion */
  false,
  lang_time_format_TR,
  lang_date_format_TR,
  lang_datetime_format_TR,
  lang_timestamp_format_TR,
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  ',',
  '.',
  DB_CURRENCY_TL,
  lang_initloc_tr,
  intl_strcmp_utf8,
  intl_next_coll_char_utf8,
  NULL
};

/*
 * Korean Locale Data
 */

/*
 * lang_initloc_ko () - init locale data for Korean language
 *   return:
 */
static void
lang_initloc_ko (LANG_LOCALE_DATA * ld)
{
  assert (ld != NULL);

  /* TODO: update this if EUC-KR is fully supported as standalone charset */

  ld->is_initialized = true;
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
 *   seq(in): pointer to current char
 *   size(in): size in bytes for seq
 *   next_seq(in/out): buffer to return next alphabetical char
 *   len_next(in/out): length in chars for nex_seq
 */
static int
lang_next_alpha_char_ko (const unsigned char *seq, const int size,
			 unsigned char *next_seq, int *len_next)
{
  /* TODO: update this if EUC-KR is fully supported as standalone charset */
  assert (seq != NULL);
  assert (next_seq != NULL);
  assert (len_next != NULL);
  assert (size > 0);

  *next_seq = *seq + 1;
  *len_next = 1;
  return 1;
}


static LANG_LOCALE_DATA lc_Korean_iso88591 = {
  LANG_NAME_KOREAN,
  INTL_LANG_KOREAN,
  INTL_CODESET_ISO88591,
  {ALPHABET_TAILORED, 0, 0, NULL, 0, NULL, false},	/* alphabet : same as English ISO */
  {ALPHABET_TAILORED, 0, 0, NULL, 0, NULL, false},	/* identifiers alphabet : same as English ISO */
  {{TAILOR_UNDEFINED, false, false, 0, false, CONTR_IGNORE},
   NULL, NULL, 0,
   0, NULL, NULL, NULL,
   NULL, 0, 0, NULL, 0, 0},	/* collation : same as English ISO */
  NULL,				/* console text conversion */
  false,
  NULL,				/* time, date, date-time, timestamp format */
  NULL,
  NULL,
  NULL,
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  ',',
  '.',
  DB_CURRENCY_WON,
  lang_initloc_ko,
  lang_fastcmp_ko,
  lang_next_alpha_char_ko,
  NULL
};

/* built-in support of Korean in UTF-8 : date-time conversions as in English
 * collation : by codepoints
 * this needs to be overriden by user defined locale */
static LANG_LOCALE_DATA lc_Korean_utf8 = {
  LANG_NAME_KOREAN,
  INTL_LANG_KOREAN,
  INTL_CODESET_UTF8,
  {ALPHABET_ASCII, LANG_CHAR_COUNT_EN, 1, lang_lower_EN, 1, lang_upper_EN,
   false},
  {ALPHABET_ASCII, LANG_CHAR_COUNT_EN, 1, lang_lower_EN, 1, lang_upper_EN,
   false},
  {{TAILOR_UNDEFINED, false, false, 0, false, CONTR_IGNORE},
   lang_weight_EN, lang_next_alpha_char_EN, LANG_CHAR_COUNT_EN,
   0, NULL, NULL, NULL,
   NULL, 0, 0, NULL, 0, 0},
  NULL,				/* console text conversion */
  false,
  NULL,				/* time, date, date-time, timestamp format */
  NULL,
  NULL,
  NULL,
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  {NULL},
  '.',
  ',',
  DB_CURRENCY_WON,
  lang_initloc_ko,
  intl_strcmp_utf8,
  intl_next_coll_char_utf8,
  NULL
};
