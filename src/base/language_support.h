/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * language_support.h : Multi-language and character set support
 *
 */

#ifndef _LANGUAGE_SUPPORT_H_
#define _LANGUAGE_SUPPORT_H_

#ident "$Id$"

#include <stddef.h>

#include "intl.h"

/*
 * currently recognized language names.
 */
#define LANG_NAME_KOREAN	"ko_KR"
#define LANG_NAME_ENGLISH	"en_US"
#define LANG_NAME_DEFAULT 	LANG_NAME_ENGLISH
#define LANG_CHARSET_UTF8       "utf8"
#define LANG_CHARSET_EUCKR      "euckr"

/*
 * message for fundamental error that occur before any messages catalogs
 * can be accessed or opened.
 */
#define LANG_ERR_NO_CUBRID "The `%s' environment variable is not set.\n"

#define LANG_MAX_LANGNAME       256
#define LANG_MAX_BYTES_PER_CHAR	2

#define LANG_VARIABLE_CHARSET(x) ((x) != INTL_CODESET_ASCII     && \
				  (x) != INTL_CODESET_RAW_BITS  && \
				  (x) != INTL_CODESET_RAW_BYTES && \
				  (x) != INTL_CODESET_ISO88591)


#if !defined (SERVER_MODE)
typedef struct db_charset DB_CHARSET;
struct db_charset
{
  INTL_CODESET charset_id;
  const char *charset_name;
  const char *charset_desc;
  int default_collation;
  const char *space_char;
  int space_size;
};
#endif /* !SERVER_MODE */

#ifdef __cplusplus
extern "C"
{
#endif

  extern bool lang_init (void);
  extern void lang_final (void);
  extern const char *lang_name (void);
  extern INTL_LANG lang_id (void);
  extern INTL_CODESET lang_charset (void);
  extern int lang_loc_bytes_per_char (void);
  extern DB_CURRENCY lang_currency (void);
  extern const char *lang_currency_symbol (DB_CURRENCY curr);
  extern int lang_char_mem_size (const char *p);
  extern int lang_char_screen_size (const char *p);
  extern int lang_wchar_mem_size (const wchar_t * p);
  extern int lang_wchar_screen_size (const wchar_t * p);
  extern bool lang_check_identifier (const char *name, int length);

#if !defined (SERVER_MODE)
  extern void lang_server_charset_init (void);
  extern DB_CHARSET lang_server_db_charset (void);
  extern INTL_CODESET lang_server_charset_id (void);
  extern void lang_server_space_char (char *space, int *size);
  extern void lang_server_charset_name (char *name);
  extern void lang_server_charset_desc (char *desc);
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
