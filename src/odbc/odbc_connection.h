/*
* Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
*
* Redistribution and use in source and binary forms, with or without modification, 
* are permitted provided that the following conditions are met: 
*
* - Redistributions of source code must retain the above copyright notice, 
*   this list of conditions and the following disclaimer. 
*
* - Redistributions in binary form must reproduce the above copyright notice, 
*   this list of conditions and the following disclaimer in the documentation 
*   and/or other materials provided with the distribution. 
*
* - Neither the name of the <ORGANIZATION> nor the names of its contributors 
*   may be used to endorse or promote products derived from this software without 
*   specific prior written permission. 
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
* IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
* INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
* OF SUCH DAMAGE. 
*
*/

#ifndef	__ODBC_CONN_HEADER	/* to avoid multiple inclusion */
#define	__ODBC_CONN_HEADER

#include		"odbc_portable.h"
#include		"odbc_diag_record.h"
#include		"odbc_env.h"
#include		"odbc_statement.h"

#define			KEYWORD_DSN			"DSN"
#define			KEYWORD_FILEDSN		"FILEDSN"
#define			KEYWORD_DBNAME		"DB_NAME"
#define			KEYWORD_USER		"UID"
#define			KEYWORD_PASSWORD	"PWD"
#define			KEYWORD_SERVER		"SERVER"
#define			KEYWORD_PORT		"PORT"
#define			KEYWORD_FETCH_SIZE	"FETCH_SIZE"
#define			KEYWORD_DESCRIPTION	"DESCRIPTION"
#define			KEYWORD_SAVEFILE	"SAVEFILE"
#define			KEYWORD_DRIVER		"DRIVER"
#define			KEYWORD_CHARSET		"CHARSET"

#define		TRAN_REP_CLASS_COMMIT_INSTANCE		TRAN_READ_COMMITTED
#define		TRAN_READ_COMMITTED			4
#define		TRAN_REP_CLASS_REP_INSTANCE		TRAN_REPEATABLE_READ
#define		TRAN_REPEATABLE_READ			5

typedef struct stCUBRIDDSNItem
{
  char driver[ITEMBUFLEN];
  char dsn[ITEMBUFLEN];
  char db_name[ITEMBUFLEN];
  char user[ITEMBUFLEN];
  char password[ITEMBUFLEN];
  char server[ITEMBUFLEN];
  char port[ITEMBUFLEN];
  char fetch_size[ITEMBUFLEN];
  char save_file[_MAX_PATH];
  char description[2 * ITEMBUFLEN];
  char charset[ITEMBUFLEN];
} CUBRIDDSNItem;

typedef struct st_odbc_connection
{
  unsigned short handle_type;
  struct st_diag *diag;
  int connhd;
  struct st_odbc_env *env;
  struct st_odbc_connection *next;
  //void                                  *statements;
  //void                                  *descriptors;
  struct st_odbc_statement *statements;
  struct st_odbc_desc *descriptors;	/* external descriptor */

  char *data_source;		/* data source name */
  unsigned char *server;	/* odbc server address */
  long port;			/* odbc server port number */
  char *db_name;		/* CUBRID db name */
  char *user;			/* CUBRID db user */
  char *password;		/* CUBRID db password */
  int fetch_size;		/* fetch size */
  char *charset;
  char db_ver[16];

  unsigned long old_txn_isolation;	/* for read-only mode */

  // Maximum length of the string data type from UniCAS
  long max_string_length;

  /* ODBC connection attributes */

  unsigned long attr_access_mode;	/* CORE */
  unsigned long attr_autocommit;	/* LEVEL 1 */
  //unsigned long         attr_connection_dead;   /* LEVEL 1 */
  //      attr_connection_dead는 connhd로 부터 알아 낼 수 있다.
  // if connhd > 0, alive.

  void *attr_quiet_mode;	/* CORE */
  unsigned long attr_metadata_id;	/* CORE */
  unsigned long attr_odbc_cursors;	/* CORE, DM */
  unsigned long attr_trace;	/* CORE, DM */
  char *attr_tracefile;		/* CORE, DM */
  unsigned long attr_txn_isolation;	/* LEVEL 1 */
  unsigned long attr_async_enable;	/* LEVEL 1 */

  /* Not supported */
  unsigned long attr_auto_ipd;	/* LEVEL2, RDONLY */
  unsigned long attr_connection_timeout;	/* LEVEL 2 */
  char *attr_current_catalog;	/* LEVEL 2 */
  unsigned long attr_login_timeout;	/* LEVEL 2 */
  unsigned long attr_packet_size;	/* LEVEL 2 */
  char *attr_translate_lib;	/* CORE */
  unsigned long attr_translate_option;	/* CORE */

  /* stmt attributes */
  unsigned long attr_max_rows;	// 1
  unsigned long attr_query_timeout;	// 2
} ODBC_CONNECTION;

PUBLIC RETCODE odbc_alloc_connection (ODBC_ENV * env,
				      ODBC_CONNECTION ** connptr);
PUBLIC RETCODE odbc_free_connection (ODBC_CONNECTION * conn);
PUBLIC RETCODE odbc_connect_new (ODBC_CONNECTION * conn,
				 const char *data_source,
				 const char *db_name,
				 const char *user,
				 const char *password,
				 const char *server,
				 int port, int fetch_size,
				 const char *charset);
PUBLIC RETCODE odbc_disconnect (ODBC_CONNECTION * conn);
PUBLIC RETCODE odbc_set_connect_attr (ODBC_CONNECTION * conn,
				      long attribute,
				      void *valueptr, long stringlength);
PUBLIC RETCODE odbc_get_connect_attr (ODBC_CONNECTION * conn,
				      SQLINTEGER attribute,
				      SQLPOINTER value_ptr,
				      SQLINTEGER buffer_length,
				      SQLINTEGER * string_length_ptr);
PUBLIC RETCODE odbc_auto_commit (ODBC_CONNECTION * conn);
PUBLIC RETCODE odbc_native_sql (ODBC_CONNECTION * conn,
				SQLCHAR * in_stmt_text,
				SQLCHAR * out_stmt_text,
				SQLINTEGER buffer_length,
				SQLINTEGER * out_stmt_length);
PUBLIC RETCODE odbc_get_functions (ODBC_CONNECTION * conn,
				   unsigned short function_id,
				   unsigned short *supported_ptr);
PUBLIC RETCODE odbc_get_info (ODBC_CONNECTION * conn, SQLUSMALLINT info_type,
			      SQLPOINTER info_value_ptr,
			      SQLSMALLINT buffer_length,
			      SQLLEN * string_length_ptr);
PUBLIC int get_dsn_info (const char *dsn, char *db_name, int db_name_len,
			 char *user, int user_len, char *pwd, int pwd_len,
			 char *server, int server_len, int *port,
			 int *fetch_size, char *charset, int *charset_len);

#endif /* ! __ODBC_CONN_HEADER */
