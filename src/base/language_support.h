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
 * language_support.h : Multi-language and character set support
 *
 */

#ifndef _LANGUAGE_SUPPORT_H_
#define _LANGUAGE_SUPPORT_H_

#ident "$Id$"

#include <stddef.h>

#include "intl_support.h"
#include "locale_support.h"

/*
 * currently recognized language names.
 */
#define LANG_NAME_KOREAN	"ko_KR"
#define LANG_NAME_ENGLISH	"en_US"
#define LANG_NAME_TURKISH	"tr_TR"
#define LANG_CHARSET_UTF8       "utf8"
#define LANG_CHARSET_EUCKR      "euckr"
#define LANG_NAME_DEFAULT 	LANG_NAME_ENGLISH

/* number of characters in the (extended) alphabet per language */
#define LANG_CHAR_COUNT_EN 256
#define LANG_CHAR_COUNT_TR 352
/*
 * message for fundamental error that occur before any messages catalogs
 * can be accessed or opened.
 */
#define LANG_ERR_NO_CUBRID "The `%s' environment variable is not set.\n"

#define LANG_MAX_LANGNAME       256

#define LANG_VARIABLE_CHARSET(x) ((x) != INTL_CODESET_ASCII     && \
				  (x) != INTL_CODESET_RAW_BITS  && \
				  (x) != INTL_CODESET_RAW_BYTES && \
				  (x) != INTL_CODESET_ISO88591)


#if !defined (SERVER_MODE)
typedef struct db_charset DB_CHARSET;
struct db_charset
{
  const char *charset_name;
  const char *charset_desc;
  const char *space_char;
  INTL_CODESET charset_id;
  int default_collation;
  int space_size;
};
#endif /* !SERVER_MODE */

/* Language locale data */
typedef struct lang_locale_data LANG_LOCALE_DATA;
struct lang_locale_data
{
  const char *lang_name;
  INTL_LANG lang_id;
  INTL_CODESET codeset;

  ALPHABET_DATA alphabet;	/* data for lower / uppper */
  ALPHABET_DATA ident_alphabet;	/* data for lower / uppper for identifiers */

  COLL_DATA coll;		/* collation data */

  TEXT_CONVERSION *txt_conv;	/* console text conversion */


  bool is_initialized;		/* init status */

  const char *time_format;	/* default time format */
  const char *date_format;	/* default date format */
  const char *datetime_format;	/* default datetime format */
  const char *timestamp_format;	/* default timestamp format */

  const char *day_short_name[CAL_DAY_COUNT];
  const char *day_name[CAL_DAY_COUNT];
  const char *month_short_name[CAL_MONTH_COUNT];
  const char *month_name[CAL_MONTH_COUNT];
  const char *am_pm[CAL_AM_PM_COUNT];

  const char *day_short_parse_order;
  const char *day_parse_order;
  const char *month_short_parse_order;
  const char *month_parse_order;
  const char *am_pm_parse_order;

  char number_decimal_sym;
  char number_group_sym;
  DB_CURRENCY default_currency_code;

  void (*initloc) (LANG_LOCALE_DATA * ld);	/* locale data init function */
  int (*fastcmp) (const unsigned char *string1, const int size1,	/* fast string compare */
		  const unsigned char *string2, const int size2);
  /* function to get collatable character sequence (in sort order) */
  int (*next_coll_seq) (const unsigned char *seq, const int size,
			unsigned char *next_seq, int *len_next);
  void *user_data;		/* pointer to user locale data */
};

#ifdef __cplusplus
extern "C"
{
#endif

  extern bool lang_init (void);
  extern bool lang_init_full (void);
  extern void lang_final (void);
  extern bool lang_check_init (void);
  extern const char *lang_get_Loc_name (void);
  extern const char *lang_get_Lang_name (void);
  extern INTL_LANG lang_id (void);
  extern INTL_CODESET lang_charset (void);
  extern int lang_loc_bytes_per_char (void);
  extern DB_CURRENCY lang_currency (void);
  extern const char *lang_currency_symbol (DB_CURRENCY curr);
#if defined(ENABLE_UNUSED_FUNCTION)
  extern int lang_char_mem_size (const char *p);
  extern int lang_char_screen_size (const char *p);
  extern int lang_wchar_mem_size (const wchar_t * p);
  extern int lang_wchar_screen_size (const wchar_t * p);
#endif
  extern bool lang_check_identifier (const char *name, int length);
  extern const LANG_LOCALE_DATA *lang_locale (void);
  extern const LANG_LOCALE_DATA *lang_get_specific_locale
    (const INTL_LANG lang, const INTL_CODESET codeset);
  extern const char *lang_get_lang_name_from_id (const INTL_LANG lang_id);
  extern int lang_set_flag_from_lang (const char *lang_str, bool user_format,
				      int *flag);
  extern int lang_set_flag_from_lang_id (const INTL_LANG lang,
					 bool user_format, int *flag);
  extern INTL_LANG lang_get_lang_id_from_flag (const int flag,
					       bool * user_format);
  extern const char *lang_date_format (const INTL_LANG lang_id,
				       const DB_TYPE type);
  extern char lang_digit_grouping_symbol (const INTL_LANG lang_id);
  extern char lang_digit_fractional_symbol (const INTL_LANG lang_id);
  extern TEXT_CONVERSION *lang_get_txt_conv (void);

#if !defined (SERVER_MODE)
  extern void lang_server_charset_init (void);
  extern INTL_CODESET lang_server_charset_id (void);
  extern bool lang_check_server_env (void);
#if defined(ENABLE_UNUSED_FUNCTION)
  extern DB_CHARSET lang_server_db_charset (void);
  extern void lang_server_space_char (char *space, int *size);
  extern void lang_server_charset_name (char *name);
  extern void lang_server_charset_desc (char *desc);
#endif				/* ENABLE_UNUSED_FUNCTION */
  extern int lang_charset_name_to_id (const char *name,
				      INTL_CODESET * codeset);
  extern int lang_set_national_charset (const char *codeset_name);
  extern int lang_charset_space_char (INTL_CODESET codeset, char *space_char,
				      int *space_size);
#endif				/* !SERVER_MODE */

#ifdef __cplusplus
}
#endif

#endif				/* _LANGUAGE_SUPPORT_H_ */
