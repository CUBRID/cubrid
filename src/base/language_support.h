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
 * currently built-in language names.
 */
#define LANG_NAME_KOREAN	"ko_KR"
#define LANG_NAME_ENGLISH	"en_US"
#define LANG_NAME_TURKISH	"tr_TR"
#define LANG_CHARSET_UTF8       "utf8"
#define LANG_CHARSET_EUCKR      "euckr"
#define LANG_CHARSET_ISO88591   "iso88591"
#define LANG_NAME_DEFAULT 	LANG_NAME_ENGLISH

#define LANG_MAX_COLLATIONS  32
#define LANG_MAX_LOADED_LOCALES  32

#define LANG_COERCIBLE_COLL LANG_SYS_COLLATION
#define LANG_COERCIBLE_CODESET LANG_SYS_CODESET

#define LANG_GET_BINARY_COLLATION(c) (((c) == INTL_CODESET_UTF8) \
  ? LANG_COLL_UTF8_BINARY :					 \
  (((c) == INTL_CODESET_KSC5601_EUC) ? LANG_COLL_EUCKR_BINARY :  \
    LANG_COLL_ISO_BINARY))

/* collation and charset do be used by system : */
#define LANG_SYS_COLLATION  (LANG_GET_BINARY_COLLATION(lang_charset()))

#define LANG_SYS_CODESET  lang_charset()

#define LANG_IS_COERCIBLE_COLL(c)	\
  ((c) == LANG_COLL_ISO_BINARY || (c) == LANG_COLL_UTF8_BINARY	\
   || (c) == LANG_COLL_EUCKR_BINARY)

/* common collation to be used at runtime */
#define LANG_RT_COMMON_COLL(c1, c2, coll)     \
  do {					      \
    if ((c1) == (c2))			      \
      {					      \
	coll = (c1);			      \
      }					      \
    else if (LANG_IS_COERCIBLE_COLL (c1))     \
      {					      \
	coll = (c2);			      \
      }					      \
    else				      \
      {					      \
	assert (LANG_IS_COERCIBLE_COLL (c2)); \
	coll = (c1);			      \
      }					      \
  } while (0)

#define LANG_COLL_ISO_BINARY	0
#define LANG_COLL_UTF8_BINARY	1
#define LANG_COLL_ISO_EN_CS	2
#define LANG_COLL_ISO_EN_CI	3
#define LANG_COLL_UTF8_EN_CS	4
#define LANG_COLL_UTF8_EN_CI	5
#define LANG_COLL_UTF8_TR_CS	6
#define LANG_COLL_ISO_KO_CS	7
#define LANG_COLL_UTF8_KO_CS	8
#define LANG_COLL_EUCKR_BINARY	9
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

/* collation optimizations */
typedef struct coll_opt COLL_OPT;
struct coll_opt
{
  /* enabled by default; disabled for case insensitive collations and
   * collations with expansions */
  bool allow_like_rewrite;

  /* enabled by default; disabled for collations having identical sort key
   * for different strings (case insensitive collations).
   * In order to produce specific sort keys, an UCA collation should be 
   * configured with maximum sorting level. But, even in this case there
   * are some acceptatable codepoints which have the same weight. These 
   * codepoints ussually represent the same graphic symbol. */
  bool allow_index_cov;

  /* enabled by default; disabled for collations with expansions */
  bool allow_prefix_index;
};

typedef struct lang_locale_data LANG_LOCALE_DATA;

typedef struct lang_collation LANG_COLLATION;
struct lang_collation
{
  INTL_CODESET codeset;
  int built_in;
  bool need_init;
  COLL_OPT options;		/* collation options */

  /* default language to use for this collation (for casing functions) */
  LANG_LOCALE_DATA *default_lang;

  COLL_DATA coll;		/* collation data */
  /* string compare */
  int (*fastcmp) (const LANG_COLLATION * lang_coll,
		  const unsigned char *string1, const int size1,
		  const unsigned char *string2, const int size2);
  /* function to get collatable character sequence (in sort order) */
  int (*next_coll_seq) (const LANG_COLLATION * lang_coll,
			const unsigned char *seq, const int size,
			unsigned char *next_seq, int *len_next);
  /* find position where strings are different (BTREE string prefix) */
  int (*split_point) (const LANG_COLLATION * lang_coll,
		      const unsigned char *str1, const int size1,
		      const unsigned char *str2, const int size2,
		      int *char_pos, int *byte_pos);
  /* collation data init function */
  void (*init_coll) (LANG_COLLATION * lang_coll);
};

/* Language locale data */
struct lang_locale_data
{
  /* next locale with same lang id, but diferrent codeset */
  LANG_LOCALE_DATA *next_lld;

  const char *lang_name;
  INTL_LANG lang_id;
  INTL_CODESET codeset;

  ALPHABET_DATA alphabet;	/* data for lower / uppper */
  ALPHABET_DATA ident_alphabet;	/* data for lower / uppper for identifiers */

  LANG_COLLATION *default_lang_coll;	/* default collation for this locale */

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

  UNICODE_NORMALIZATION unicode_norm;

  char *checksum;

  void (*initloc) (LANG_LOCALE_DATA * ld);	/* locale data init function */
  bool is_user_data;		/* TRUE  if lang data is loaded from DLL/so
				 * FALSE if built-in
				 */
};

typedef struct lang_coll_compat LANG_COLL_COMPAT;
struct lang_coll_compat
{
  int coll_id;
  char coll_name[COLL_NAME_SIZE];
  INTL_CODESET codeset;
  char checksum[32 + 1];
};

typedef struct lang_locale_compat LANG_LOCALE_COMPAT;
struct lang_locale_compat
{
  char lang_name[LANG_MAX_LANGNAME];
  INTL_CODESET codeset;
  char checksum[32 + 1];
};

#ifdef __cplusplus
extern "C"
{
#endif

  extern bool lang_init (void);
  extern bool lang_init_full (void);
  extern void lang_init_console_txt_conv (void);
  extern void lang_final (void);
  extern bool lang_check_init (void);
  extern int lang_locales_count (bool check_codeset);
  extern const char *lang_get_Loc_name (void);
  extern const char *lang_get_user_loc_name (void);
  extern const char *lang_get_Lang_name (void);
  extern INTL_LANG lang_id (void);
  extern INTL_CODESET lang_charset (void);
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
  extern const LANG_LOCALE_DATA *lang_get_first_locale_for_lang
    (const INTL_LANG lang);
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
  extern bool lang_is_coll_name_allowed (const char *name);
  extern LANG_COLLATION *lang_get_collation (const int coll_id);
  extern const char *lang_get_collation_name (const int coll_id);
  extern LANG_COLLATION *lang_get_collation_by_name (const char *coll_name);
  extern int lang_collation_count (void);
  extern const char *lang_get_codeset_name (int codeset_id);
  extern const ALPHABET_DATA *lang_user_alphabet_w_coll
    (const int collation_id);
  extern TEXT_CONVERSION *lang_get_txt_conv (void);

  extern int lang_strcmp_utf8_uca_w_coll_data (const COLL_DATA * coll_data,
					       const unsigned char *str1,
					       const int size1,
					       const unsigned char *str2,
					       const int size2);
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
  extern void lang_set_client_charset_coll (const INTL_CODESET codeset,
					    const int collation_id);
  extern INTL_CODESET lang_get_client_charset (void);
  extern int lang_get_client_collation (void);
  extern void lang_set_parser_use_client_charset (bool use);
  extern bool lang_get_parser_use_client_charset (void);
#endif				/* !SERVER_MODE */

  extern int lang_load_library (const char *lib_file, void **handle);
  extern int lang_locale_data_load_from_lib (LANG_LOCALE_DATA * lld,
					     void *lib_handle,
					     const LOCALE_FILE * lf,
					     bool is_load_for_dump);
  extern int lang_load_count_coll_from_lib (int *count_coll, void *lib_handle,
					    const LOCALE_FILE * lf);
  extern int lang_load_get_coll_name_from_lib (const int coll_pos,
					       char **coll_name,
					       void *lib_handle,
					       const LOCALE_FILE * lf);
  extern int lang_load_coll_from_lib (COLL_DATA * cd, void *lib_handle,
				      const LOCALE_FILE * lf);

  extern UNICODE_NORMALIZATION *lang_get_generic_unicode_norm (void);
  extern void lang_set_generic_unicode_norm (UNICODE_NORMALIZATION * norm);
  extern int lang_check_coll_compat (const LANG_COLL_COMPAT * coll_array,
				     const int coll_cnt,
				     const char *client_text,
				     const char *server_text);
  extern int lang_check_locale_compat (const LANG_LOCALE_COMPAT * loc_array,
				       const int loc_cnt,
				       const char *client_text,
				       const char *server_text);
#ifdef __cplusplus
}
#endif

#endif				/* _LANGUAGE_SUPPORT_H_ */
