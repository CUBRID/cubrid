/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */


/*
 * broker_tester.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>

#if defined(WINDOWS)
#include <process.h>
#include "porting.h"
#include <sys/timeb.h>
#else /* WINDOWS */
#include <sys/time.h>
#endif /* WINDOWS */

#include "broker_config.h"
#include "broker_util.h"
#include "broker_config.h"
#include "broker_shm.h"
#include "broker_filename.h"
#include "cas_protocol.h"
#include "cas_common.h"
#include "cubrid_getopt.h"
#include "cas_cci.h"

#if defined(WINDOWS)
#include "broker_wsa_init.h"
#endif /* WINDOWS */

#include "ini_parser.h"
#include "porting.h"
#include "log_top_string.h"

#define TESTER_ERR_MSG_SIZE            1024
#define TIME_BUF_SIZE                  50
#define	MAX_DISPLAY_LENGTH             20

/* Appended to every prepared/batched statement so the queries issued by this
 * tool can be told apart from application traffic in sql.log. */
#define BR_TESTER_HINT                 " /* broker_tester */"

/* execute_test_with_query () returns this positive sentinel when the query
 * contains bind parameters, which it cannot run. The caller reports the
 * context-specific usage message (-c command vs -i file). */
#define BR_TESTER_BIND_REJECTED        1

/* Upper bound for the repeat count N of @execute(N)/@array(N)/@call(N). */
#define BR_MAX_EXEC_COUNT              10000

/* Line kinds returned by classify_line (). */
#define BR_LINE_QUERY                  0
#define BR_LINE_DIRECTIVE              1
#define BR_LINE_BIND                   2

#define DEFAULT_EMPTY_STRING           "\0"
#define DEFAULT_CUB_USER_NAME          "PUBLIC"

#define RESULT_FORMAT            "%-15s"
#define SHARD_ID_FORMAT          "%-10d"
#define STR_SHARD_ID_FORMAT      "%-10s"
#define ROWCOUNT_FORMAT          "%-15d"
#define QUERY_FORMAT             "%s"
#define STR_ROWCOUNT_FORMAT      "%-15s"
#define TIME_FORMAT              "%-20s"

#define PRINT_CCI_ERROR(...)                         \
        do {                                         \
          if (br_tester_info.verbose_mode)           \
           {                                         \
            if (out_file_fp != NULL)                 \
            {                                        \
             fprintf (out_file_fp , "<Error>\n");\
             fprintf (out_file_fp , __VA_ARGS__);    \
            }                                        \
            fprintf (stderr, "<Error>\n");       \
            fprintf (stderr, __VA_ARGS__);           \
           }                                         \
        } while(0)

#define PRINT_RESULT(...)                                                   \
        do {                                                                 \
          _Pragma("GCC diagnostic push")                                     \
          _Pragma("GCC diagnostic ignored \"-Wformat-security\"")            \
          if (out_file_fp != NULL)                                           \
          {                                                                  \
            fprintf (out_file_fp ,__VA_ARGS__);                              \
          }                                                                  \
          fprintf (stdout, __VA_ARGS__);                                     \
          _Pragma("GCC diagnostic pop")                                      \
        } while (0)

#define PRINT_TITLE(n, ...)                          \
        do {                                         \
          if (out_file_fp != NULL)                   \
          {                                          \
            fprintf (out_file_fp ,__VA_ARGS__);      \
          }                                          \
          n += fprintf (stdout, __VA_ARGS__);        \
        } while (0)

static const char SECTION_NAME[] = "broker";

static FILE *out_file_fp;

static char tester_err_msg[TESTER_ERR_MSG_SIZE];

typedef struct
{
  char *db_name;
  char *db_user;
  char *db_passwd;
  char *command;
  char *input_file_name;
  char *output_file_name;
  int broker_port;
  bool verbose_mode;
  bool single_shard;
  bool shard_flag;
  int num_shard;
} TESTER_INFO;

TESTER_INFO br_tester_info;

typedef enum
{
  BR_PM_IN = 0,
  BR_PM_OUT,
  BR_PM_INOUT
} T_BR_PARAM_MODE;

typedef enum
{
  BR_EXEC_SINGLE = 0,
  BR_EXEC_MULTI,
  BR_EXEC_ARRAY,
  BR_EXEC_CALL,
  BR_EXEC_BATCH
} T_BR_EXEC_MODE;

/* One parsed bind parameter. */
typedef struct
{
  int idx;			/* 1-based marker index */
  T_CCI_U_TYPE u_type;		/* CCI type mapped from the TYPE token */
  char *type_tok;		/* original TYPE token, upper-cased (used for CALL out display) */
  char *value;			/* value string; server converts. NULL is bound as "" */
  int size;			/* byte length for BIT/VARBIT, 0 otherwise */
  bool is_null;
  T_BR_PARAM_MODE param_mode;	/* OUT/INOUT only allowed under @call */
  T_CCI_BIT bit;		/* filled at bind time for BIT/VARBIT; must outlive cci_execute */
} T_BR_BIND;

/* One statement pending flush. A directive plus its query and bind sets, or a
 * collected @batch block. */
typedef struct
{
  char *query;
  T_BR_EXEC_MODE mode;
  int exec_count;		/* directive repeat count N; defaults to 1 */
  T_BR_BIND ***sets;		/* sets[set][col]; one set is one array row */
  int *set_nbind;		/* bind count of each set */
  int num_sets;			/* number of sets M */
  char **batch_sql;		/* BATCH only: array of plain SQL (no binds) */
  int batch_count;		/* number of batch statements K */
  int batch_capacity;		/* allocated slots in batch_sql */
} T_BR_STMT;

static int init_tester_info (char *broker_name);
static void init_default_conn_info (int appl_server_type);

static int get_master_shm_id (void);
static void get_time (struct timeval *start_time, char *time, int buf_len);

static int parse_conn_target (const char *arg, char **host_out, int *port_out);

static int execute_test_with_query (int conn_handle, char *query, int shard_flag);
static int execute_test (int conn_handle, int shard_flag);

static int get_bind_type (const char *tok);
static char *strip_caslog_prefix (char *line);
static char *find_line_comment (char *line);
static int tester_get_line (FILE * fp, T_STRING * t_str);
static int classify_line (const char *line);
static bool is_batch_begin (const char *line);
static bool is_batch_end (const char *line);
static int parse_directive (const char *line, T_BR_STMT * st);
static int parse_bind_line (const char *line, T_BR_STMT * st);

static void stmt_init (T_BR_STMT * st);
static void stmt_reset (T_BR_STMT * st);
static int stmt_add_batch_sql (T_BR_STMT * st, const char *sql);
static void bind_free_set (T_BR_BIND ** set, int nbind);

static int bind_one_set (int req, T_BR_BIND ** set, int nbind);
static int flush_current (int conn_handle, T_BR_STMT * st, int shard_flag);
static int flush_stmt (int conn_handle, T_BR_STMT * st, int shard_flag);
static int flush_call (int conn_handle, T_BR_STMT * st);
static int flush_batch (int conn_handle, T_BR_STMT * st);
static void print_out_params (T_BR_STMT * st, int req, int call_seq);
static void print_batch_result (T_CCI_QUERY_RESULT * qr, int k);

static void print_usage (void);
static void print_conn_result (char *broker_name, int conn_hd_id);
static void print_shard_result (void);
static void print_title (int shard_flag);
static void print_result (int row_count, int err_code, int shard_flag, int shard_id, char *time, char *query);
static int print_result_set (int req, T_CCI_ERROR * err_buf, T_CCI_COL_INFO * col_info, int col_count);
static void print_query_test_result (int ret);
static void print_line (const char *ch, int num);

static void free_br_tester_info (void);
static bool is_number_type (T_CCI_U_TYPE type);

static int
init_tester_info (char *broker_name)
{
  int i;
  int master_shm_id = 0;
  T_SHM_BROKER *shm_br = NULL;
  T_SHM_PROXY *shm_proxy = NULL;
  T_BROKER_INFO *broker_info_p = NULL;

  master_shm_id = get_master_shm_id ();
  if (master_shm_id <= 0)
    {
      return -1;
    }

  shm_br = (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER, SHM_MODE_MONITOR);
  if (shm_br == NULL)
    {
      fprintf (stderr, "master shared memory open error[0x%x]\n", master_shm_id);
      return -1;
    }

  for (i = 0; i < shm_br->num_broker; i++)
    {
      if (strcasecmp (broker_name, shm_br->br_info[i].name) == 0)
	{
	  broker_info_p = &shm_br->br_info[i];
	  break;
	}
    }

  if (broker_info_p == NULL)
    {

      fprintf (stderr, "Cannot find Broker [%s]\n", broker_name);
      uw_shm_detach (shm_br);
      return -1;
    }

  br_tester_info.broker_port = broker_info_p->port;
  br_tester_info.shard_flag = broker_info_p->shard_flag;

  if (broker_info_p->shard_flag == ON)
    {
      shm_proxy = (T_SHM_PROXY *) uw_shm_open (broker_info_p->proxy_shm_id, SHM_PROXY, SHM_MODE_MONITOR);
      if (shm_proxy == NULL)
	{
	  uw_shm_detach (shm_br);
	  fprintf (stderr, "proxy shared memory open error[0x%x]\n", broker_info_p->proxy_shm_id);
	  return -1;
	}
      br_tester_info.num_shard = shm_proxy->shm_shard_conn.num_shard_conn;

      uw_shm_detach (shm_proxy);

      if (br_tester_info.db_name == NULL)
	{
	  br_tester_info.db_name = strdup (broker_info_p->shard_db_name);
	}

      if (br_tester_info.db_user == NULL)
	{
	  br_tester_info.db_user = strdup (broker_info_p->shard_db_user);
	}

      if (br_tester_info.db_passwd == NULL)
	{
	  br_tester_info.db_passwd = strdup (broker_info_p->shard_db_password);
	}
    }

  init_default_conn_info (broker_info_p->appl_server);

  uw_shm_detach (shm_br);

  return 0;
}

static void
init_default_conn_info (int appl_server_type)
{
  const char *user_name;
  const char *user_password;

  if (br_tester_info.db_name == NULL)
    {
      br_tester_info.db_name = strdup (DEFAULT_EMPTY_STRING);
    }

  switch (appl_server_type)
    {
    case APPL_SERVER_CAS:
      user_name = DEFAULT_CUB_USER_NAME;
      user_password = DEFAULT_EMPTY_STRING;
      break;

    default:
      user_name = DEFAULT_EMPTY_STRING;
      user_password = DEFAULT_EMPTY_STRING;
    }

  if (br_tester_info.db_user == NULL)
    {
      br_tester_info.db_user = strdup (user_name);

      FREE_MEM (br_tester_info.db_passwd);
      br_tester_info.db_passwd = strdup (user_password);
    }

  if (br_tester_info.db_passwd == NULL)
    {
      br_tester_info.db_passwd = strdup (DEFAULT_EMPTY_STRING);
    }

  return;
}

static int
get_master_shm_id (void)
{
  int master_shm_id = 0;
  struct stat stat_buf;
  INI_TABLE *ini = NULL;
  const char *conf_file;
  char conf_file_path[BROKER_PATH_MAX];

  conf_file = envvar_get ("BROKER_CONF_FILE");

  if (conf_file != NULL)
    {
      strncpy_bufsize (conf_file_path, conf_file);
    }
  else
    {
      get_cubrid_file (FID_CUBRID_BROKER_CONF, conf_file_path, BROKER_PATH_MAX);
    }

  if (stat (conf_file_path, &stat_buf) == 0)
    {
      ini = ini_parser_load (conf_file_path);
      if (ini == NULL)
	{
	  fprintf (stderr, "cannot open conf file %s\n", conf_file_path);
	  return -1;
	}

      if (!ini_findsec (ini, SECTION_NAME))
	{
	  fprintf (stderr, "cannot find [%s] section in conf file %s\n", SECTION_NAME, conf_file_path);
	  ini_parser_free (ini);
	  return -1;
	}

      master_shm_id = ini_gethex (ini, SECTION_NAME, "MASTER_SHM_ID", 0, NULL);
      if (master_shm_id <= 0)
	{
	  fprintf (stderr, "cannot find MASTER_SHM_ID in [%s] section\n", SECTION_NAME);
	}
    }

  ini_parser_free (ini);

  return master_shm_id;
}

static void
get_time (struct timeval *start_time, char *time, int buf_len)
{
  struct timeval end_time;
  struct timeval elapsed_time;

  assert (time);
  assert (start_time);

  gettimeofday (&end_time, NULL);

  elapsed_time.tv_sec = end_time.tv_sec - start_time->tv_sec;
  elapsed_time.tv_usec = end_time.tv_usec - start_time->tv_usec;
  if (elapsed_time.tv_usec < 0)
    {
      elapsed_time.tv_sec--;
      elapsed_time.tv_usec += 1000000;
    }
  snprintf (time, buf_len, "%ld.%06ld sec", elapsed_time.tv_sec, elapsed_time.tv_usec);
  return;
}

/* Classifies the first positional argument. Returns 0 for a broker name (no
 * colon), 1 for a strict host:port (direct mode; host non-empty, port 1..65535),
 * and -1 for a malformed target. Broker names never contain a colon, so there is
 * no fallback: a colon means host:port must parse. Multiple colons (IPv6) are
 * rejected. On direct mode *host_out is a freshly allocated copy. */
static int
parse_conn_target (const char *arg, char **host_out, int *port_out)
{
  const char *colon = strchr (arg, ':');
  char *endp;
  long port;

  if (colon == NULL)
    {
      return 0;
    }

  if (strchr (colon + 1, ':') != NULL)
    {
      fprintf (stderr, "IPv6 host:port is not supported\n");
      return -1;
    }

  if (colon == arg)
    {
      fprintf (stderr, "empty host in host:port\n");
      return -1;
    }

  errno = 0;
  port = strtol (colon + 1, &endp, 10);
  if (*endp != '\0' || errno != 0 || port < 1 || port > 65535)
    {
      fprintf (stderr, "invalid port in host:port\n");
      return -1;
    }

  *host_out = (char *) MALLOC ((int) (colon - arg) + 1);
  if (*host_out == NULL)
    {
      fprintf (stderr, "malloc error\n");
      return -1;
    }
  memcpy (*host_out, arg, colon - arg);
  (*host_out)[colon - arg] = '\0';
  *port_out = (int) port;

  return 1;
}

/* Executes a single query with no bind parameters. Used for the -c command and
 * for the plain (directive-less, bind-less) statements of the -i file, so the
 * legacy result table output and shard iteration stay unchanged. A query that
 * carries bind markers cannot be run here: it returns BR_TESTER_BIND_REJECTED
 * and the caller reports the context-specific usage message. The caller also
 * prints the result header (print_title) before calling. */
static int
execute_test_with_query (int conn_handle, char *query, int shard_flag)
{
  int shard_id = 0;
  int err_num = 0;
  int ret, req, col_count;
  char time[TIME_BUF_SIZE];
  char query_with_hint[LINE_MAX];
  struct timeval start_time;
  T_CCI_ERROR err_buf;
  T_CCI_SQLX_CMD cmd_type;
  T_CCI_COL_INFO *col_info = NULL;

  do
    {
      memset (tester_err_msg, 0, sizeof (tester_err_msg));

      if (br_tester_info.shard_flag == ON && !br_tester_info.single_shard)
	{
	  snprintf (query_with_hint, sizeof (query_with_hint), "%s /*+ shard_id(%d) */" BR_TESTER_HINT, query,
		    shard_id);
	}
      else
	{
	  snprintf (query_with_hint, sizeof (query_with_hint), "%s" BR_TESTER_HINT, query);
	}

      gettimeofday (&start_time, NULL);

      req = cci_prepare (conn_handle, query_with_hint, 0, &err_buf);
      if (req < 0)
	{
	  snprintf_dots_truncate (tester_err_msg, sizeof (tester_err_msg) - 1, "ERROR CODE : %d\n%s\n\n",
				  err_buf.err_code, err_buf.err_msg);
	  ret = -1;
	  err_num++;
	  goto end_tran;
	}

      if (cci_get_bind_num (req) > 0)
	{
	  /* cannot run a bind query here; the caller reports the usage message */
	  cci_close_req_handle (req);
	  cci_end_tran (conn_handle, CCI_TRAN_ROLLBACK, &err_buf);
	  return BR_TESTER_BIND_REJECTED;
	}

      ret = cci_execute (req, 0, 0, &err_buf);
      if (ret < 0)
	{
	  snprintf_dots_truncate (tester_err_msg, sizeof (tester_err_msg) - 1, "ERROR CODE : %d\n%s\n\n",
				  err_buf.err_code, err_buf.err_msg);
	  err_num++;
	  goto end_tran;
	}

      if (br_tester_info.shard_flag == ON && br_tester_info.single_shard)
	{
	  int ret;

	  ret = cci_get_shard_id_with_req_handle (req, &shard_id, &err_buf);
	  if (ret < 0)
	    {
	      snprintf_dots_truncate (tester_err_msg, sizeof (tester_err_msg) - 1, "ERROR CODE : %d\n%s\n\n",
				      err_buf.err_code, err_buf.err_msg);
	      err_num++;
	      goto end_tran;
	    }
	}

      if (br_tester_info.verbose_mode)
	{
	  col_info = cci_get_result_info (req, &cmd_type, &col_count);
	  if (cmd_type == CUBRID_STMT_SELECT && col_info == NULL)
	    {
	      snprintf_dots_truncate (tester_err_msg, sizeof (tester_err_msg) - 1, "ERROR CODE : %d\n%s\n\n",
				      err_buf.err_code, err_buf.err_msg);
	      ret = -1;
	      err_num++;
	    }
	}

    end_tran:
      get_time (&start_time, time, sizeof (time));

      print_result (ret, err_buf.err_code, shard_flag, shard_id, time, query);

      if (ret >= 0 && br_tester_info.verbose_mode && cmd_type == CUBRID_STMT_SELECT)
	{
	  ret = print_result_set (req, &err_buf, col_info, col_count);
	  if (ret < 0)
	    {
	      err_num++;
	    }
	}

      cci_close_req_handle (req);

      cci_end_tran (conn_handle, CCI_TRAN_ROLLBACK, &err_buf);

      if (br_tester_info.shard_flag == OFF || br_tester_info.single_shard)
	{
	  break;
	}
    }
  while (++shard_id < br_tester_info.num_shard);

  return (-1 * err_num);
}

/* Maps a sql.log TYPE token to a CCI u_type; returns -1 for an unknown token.
 * BLOB/CLOB are not bindable here and resolve to CCI_U_TYPE_NULL. Kept in sync
 * with get_cci_type () in broker_log_replay.c. */
static int
get_bind_type (const char *tok)
{
  int type = -1;

  switch (tok[0])
    {
    case 'B':
      if (strcmp (tok, "BIGINT") == 0)
	type = CCI_U_TYPE_BIGINT;
      else if (strcmp (tok, "BIT") == 0)
	type = CCI_U_TYPE_BIT;
      else if (strcmp (tok, "BLOB") == 0)
	type = CCI_U_TYPE_NULL;
      break;

    case 'C':
      if (strcmp (tok, "CHAR") == 0)
	type = CCI_U_TYPE_CHAR;
      else if (strcmp (tok, "CLOB") == 0)
	type = CCI_U_TYPE_NULL;
      break;

    case 'D':
      if (strcmp (tok, "DOUBLE") == 0)
	type = CCI_U_TYPE_DOUBLE;
      else if (strcmp (tok, "DATE") == 0)
	type = CCI_U_TYPE_DATE;
      else if (strcmp (tok, "DATETIME") == 0)
	type = CCI_U_TYPE_DATETIME;
      else if (strcmp (tok, "DATETIMETZ") == 0)
	type = CCI_U_TYPE_DATETIMETZ;
      break;

    case 'E':
      if (strcmp (tok, "ENUM") == 0)
	type = CCI_U_TYPE_ENUM;
      break;

    case 'F':
      if (strcmp (tok, "FLOAT") == 0)
	type = CCI_U_TYPE_FLOAT;
      break;

    case 'I':
      if (strcmp (tok, "INT") == 0)
	type = CCI_U_TYPE_INT;
      break;

    case 'J':
      if (strcmp (tok, "JSON") == 0)
	type = CCI_U_TYPE_JSON;
      break;

    case 'M':
      if (strcmp (tok, "MONETARY") == 0)
	type = CCI_U_TYPE_MONETARY;
      break;

    case 'N':
      if (strcmp (tok, "NUMERIC") == 0)
	type = CCI_U_TYPE_NUMERIC;
      else if (strcmp (tok, "NULL") == 0)
	type = CCI_U_TYPE_NULL;
      break;

    case 'S':
      if (strcmp (tok, "SHORT") == 0)
	type = CCI_U_TYPE_SHORT;
      break;

    case 'T':
      if (strcmp (tok, "TIME") == 0)
	type = CCI_U_TYPE_TIME;
      else if (strcmp (tok, "TIMESTAMP") == 0)
	type = CCI_U_TYPE_TIMESTAMP;
      else if (strcmp (tok, "TIMESTAMPTZ") == 0)
	type = CCI_U_TYPE_TIMESTAMPTZ;
      break;

    case 'U':
      if (strcmp (tok, "UINT") == 0)
	type = CCI_U_TYPE_UINT;
      else if (strcmp (tok, "UBIGINT") == 0)
	type = CCI_U_TYPE_UBIGINT;
      else if (strcmp (tok, "USHORT") == 0)
	type = CCI_U_TYPE_USHORT;
      break;

    case 'V':
      if (strcmp (tok, "VARCHAR") == 0)
	type = CCI_U_TYPE_STRING;
      else if (strcmp (tok, "VARBIT") == 0)
	type = CCI_U_TYPE_VARBIT;
      break;

    default:
      break;
    }

  return type;
}

/* Skips a sql.log line prefix ("YY-MM-DD HH:MM:SS.mmm (seq) " or the month-first
 * form) so a copy-pasted log line and a hand-written line classify the same way.
 * A line without a recognizable prefix is returned unchanged - the tester's main
 * input is plain SQL, which must never be truncated. */
static char *
strip_caslog_prefix (char *line)
{
  char *p;
  size_t len = strlen (line);

  if (len >= 22 && line[2] == '-' && line[5] == '-' && line[8] == ' '
      && line[11] == ':' && line[14] == ':' && line[21] == ' ')
    {
      p = line + 22;
    }
  else if (len >= 19 && line[2] == '/' && line[5] == ' ' && line[8] == ':' && line[11] == ':' && line[18] == ' ')
    {
      p = line + 19;
    }
  else
    {
      return line;
    }

  if (*p == '(')
    {
      char *close = strchr (p, ')');

      if (close != NULL && close[1] == ' ')
	{
	  p = close + 2;
	}
    }

  return p;
}

/* Returns a pointer to the first '#' that begins a line comment: one that lies
 * outside any quoted string literal ('...') or quoted identifier ("...", `...`,
 * [...]). Returns NULL when the line has no such '#'. A doubled quote character
 * ('' "" `` ]]) is treated as an escaped quote, and inside '...'/"..." a
 * backslash escapes the next character, matching SQL literal rules. */
static char *
find_line_comment (char *line)
{
  char *p = line;
  char quote = '\0';		/* opening quote char of the region we are in, else '\0' */

  while (*p != '\0')
    {
      if (quote != '\0')
	{
	  char close = (quote == '[') ? ']' : quote;

	  if ((quote == '\'' || quote == '"') && *p == '\\' && p[1] != '\0')
	    {
	      p += 2;		/* backslash-escaped char, still inside */
	      continue;
	    }
	  if (*p == close)
	    {
	      if (p[1] == close)
		{
		  p += 2;	/* doubled quote: escaped, still inside */
		  continue;
		}
	      quote = '\0';	/* region closed */
	    }
	  p++;
	  continue;
	}

      if (*p == '\'' || *p == '"' || *p == '`' || *p == '[')
	{
	  quote = *p;		/* region opened */
	}
      else if (*p == '#')
	{
	  return p;		/* unquoted '#' -> comment start */
	}
      p++;
    }

  return NULL;
}

/* Reads one logical line (of any length) into t_str. Returns 0 on success, -1 at
 * end of file. Only the T_STRING container is reused from the log tools; the
 * sql.log prefix handling of ut_get_line () does not fit plain SQL lines. */
static int
tester_get_line (FILE * fp, T_STRING * t_str)
{
  char buf[LINE_MAX];

  t_string_clear (t_str);

  if (fgets (buf, sizeof (buf), fp) == NULL)
    {
      return -1;
    }

  while (1)
    {
      int len = (int) strlen (buf);

      if (t_string_add (t_str, buf, len) < 0)
	{
	  return -1;
	}

      if (len == 0 || buf[len - 1] == '\n')
	{
	  break;
	}

      if (fgets (buf, sizeof (buf), fp) == NULL)
	{
	  break;
	}
    }

  return 0;
}

static int
classify_line (const char *line)
{
  if (line[0] == '@')
    {
      return BR_LINE_DIRECTIVE;
    }

  if (strncasecmp (line, "bind", 4) == 0)
    {
      const char *p = line + 4;

      while (*p == ' ' || *p == '\t')
	{
	  p++;
	}

      if (*p >= '0' && *p <= '9')
	{
	  return BR_LINE_BIND;
	}
    }

  return BR_LINE_QUERY;
}

static bool
is_batch_begin (const char *line)
{
  const char *p;

  if (strncasecmp (line, "@batch", 6) != 0)
    {
      return false;
    }

  p = line + 6;
  while (*p == ' ' || *p == '\t')
    {
      p++;
    }

  return (strcasecmp (p, "begin") == 0);
}

static bool
is_batch_end (const char *line)
{
  const char *p;

  if (strncasecmp (line, "@batch", 6) != 0)
    {
      return false;
    }

  p = line + 6;
  while (*p == ' ' || *p == '\t')
    {
      p++;
    }

  return (strcasecmp (p, "end") == 0);
}

/* Parses a prefix directive (@execute/@array/@call, optional "(N)") into st.
 * @batch is handled separately by the caller. */
static int
parse_directive (const char *line, T_BR_STMT * st)
{
  const char *p = line + 1;
  const char *name_end;
  int n;
  int count = 1;
  T_BR_EXEC_MODE mode;

  name_end = p;
  while ((*name_end >= 'a' && *name_end <= 'z') || (*name_end >= 'A' && *name_end <= 'Z'))
    {
      name_end++;
    }
  n = (int) (name_end - p);

  if (n == 7 && strncasecmp (p, "execute", 7) == 0)
    {
      mode = BR_EXEC_MULTI;
    }
  else if (n == 5 && strncasecmp (p, "array", 5) == 0)
    {
      mode = BR_EXEC_ARRAY;
    }
  else if (n == 4 && strncasecmp (p, "call", 4) == 0)
    {
      mode = BR_EXEC_CALL;
    }
  else
    {
      fprintf (stderr, "unknown directive: %s\n", line);
      return -1;
    }

  p = name_end;
  while (*p == ' ' || *p == '\t')
    {
      p++;
    }

  if (*p == '(')
    {
      char *endp;

      if (str_to_int32 (&count, &endp, p + 1, 10) < 0)
	{
	  fprintf (stderr, "invalid directive count: %s\n", line);
	  return -1;
	}

      while (*endp == ' ' || *endp == '\t')
	{
	  endp++;
	}

      if (*endp != ')')
	{
	  fprintf (stderr, "invalid directive count: %s\n", line);
	  return -1;
	}

      if (count < 1 || count > BR_MAX_EXEC_COUNT)
	{
	  fprintf (stderr, "directive count out of range (1..%d): %s\n", BR_MAX_EXEC_COUNT, line);
	  return -1;
	}
    }
  else if (*p != '\0')
    {
      fprintf (stderr, "unexpected text after directive: %s\n", line);
      return -1;
    }

  st->mode = mode;
  st->exec_count = count;

  return 0;
}

/* Appends one bind parameter to the current set. A "bind 1" line starts a new
 * set (array row); other indices extend the last set and must increase by one. */
static int
parse_bind_line (const char *line, T_BR_STMT * st)
{
  const char *p = line + 4;
  char *endp;
  const char *val_start;
  char type_tok[64];
  char *t;
  int idx;
  int type;
  int size = 0;
  int n;
  int s;
  T_BR_PARAM_MODE mode = BR_PM_IN;
  T_BR_BIND *b;
  T_BR_BIND **new_set;

  while (*p == ' ' || *p == '\t')
    {
      p++;
    }

  if (str_to_int32 (&idx, &endp, p, 10) < 0 || idx <= 0)
    {
      fprintf (stderr, "invalid bind index: %s\n", line);
      return -1;
    }
  p = endp;
  while (*p == ' ' || *p == '\t')
    {
      p++;
    }

  if (*p == '(')
    {
      if (strncasecmp (p + 1, "INOUT", 5) == 0)
	{
	  mode = BR_PM_INOUT;
	}
      else if (strncasecmp (p + 1, "OUT", 3) == 0)
	{
	  mode = BR_PM_OUT;
	}
      else if (strncasecmp (p + 1, "IN", 2) == 0)
	{
	  mode = BR_PM_IN;
	}
      else
	{
	  fprintf (stderr, "unknown bind mode: %s\n", line);
	  return -1;
	}

      p = strchr (p, ')');
      if (p == NULL)
	{
	  fprintf (stderr, "malformed bind mode: %s\n", line);
	  return -1;
	}
      p++;
      while (*p == ' ' || *p == '\t')
	{
	  p++;
	}
    }

  if (*p != ':')
    {
      fprintf (stderr, "missing ':' in bind line: %s\n", line);
      return -1;
    }
  p++;
  while (*p == ' ' || *p == '\t')
    {
      p++;
    }

  n = 0;
  while (p[n] != '\0' && p[n] != ' ' && p[n] != '\t' && p[n] != '(')
    {
      n++;
    }
  if (n == 0 || n >= (int) sizeof (type_tok))
    {
      fprintf (stderr, "missing bind type: %s\n", line);
      return -1;
    }
  memcpy (type_tok, p, n);
  type_tok[n] = '\0';
  p += n;

  for (t = type_tok; *t != '\0'; t++)
    {
      if (*t >= 'a' && *t <= 'z')
	{
	  *t = (char) (*t - ('a' - 'A'));
	}
    }

  type = get_bind_type (type_tok);
  if (type < 0)
    {
      fprintf (stderr, "unknown bind type: %s\n", type_tok);
      return -1;
    }
  if (type == CCI_U_TYPE_NULL && strcmp (type_tok, "NULL") != 0)
    {
      fprintf (stderr, "warning: type %s is not bindable, binding NULL\n", type_tok);
    }

  while (*p == ' ' || *p == '\t')
    {
      p++;
    }

  /* optional size: character count for strings, byte count for BIT/VARBIT */
  if (*p == '(')
    {
      if (str_to_int32 (&size, &endp, p + 1, 10) < 0)
	{
	  fprintf (stderr, "malformed bind size: %s\n", line);
	  return -1;
	}
      endp = strchr (endp, ')');
      if (endp == NULL)
	{
	  fprintf (stderr, "malformed bind size: %s\n", line);
	  return -1;
	}
      p = endp + 1;
      while (*p == ' ' || *p == '\t')
	{
	  p++;
	}
    }

  val_start = p;

  if (mode != BR_PM_IN && st->mode != BR_EXEC_CALL)
    {
      fprintf (stderr, "out/inout parameter requires @call\n");
      return -1;
    }
  if (mode != BR_PM_OUT && type != CCI_U_TYPE_NULL && *val_start == '\0')
    {
      /* NULL (and BLOB/CLOB mapped to NULL) carries no value by design; every
       * other IN/INOUT type must have one. */
      fprintf (stderr, "in/inout parameter requires a value (use '' for an empty string)\n");
      return -1;
    }

  b = (T_BR_BIND *) MALLOC (sizeof (T_BR_BIND));
  if (b == NULL)
    {
      fprintf (stderr, "malloc error\n");
      return -1;
    }
  memset (b, 0, sizeof (T_BR_BIND));

  b->idx = idx;
  b->u_type = (T_CCI_U_TYPE) type;
  b->size = size;
  b->is_null = (type == CCI_U_TYPE_NULL);
  b->param_mode = mode;
  b->type_tok = strdup (type_tok);

  if (*val_start == '\'')
    {
      /* hand-written quoted form: drop the outer quotes and unescape '' -> ' */
      const char *q = val_start + 1;
      char *w;

      b->value = (char *) MALLOC (strlen (val_start) + 1);
      if (b->value != NULL)
	{
	  w = b->value;
	  while (*q != '\0')
	    {
	      if (q[0] == '\'' && q[1] == '\'')
		{
		  *w++ = '\'';
		  q += 2;
		}
	      else if (q[0] == '\'')
		{
		  break;
		}
	      else
		{
		  *w++ = *q++;
		}
	    }
	  *w = '\0';
	}
    }
  else
    {
      b->value = strdup (val_start);
    }

  if (b->type_tok == NULL || b->value == NULL)
    {
      fprintf (stderr, "malloc error\n");
      FREE_MEM (b->type_tok);
      FREE_MEM (b->value);
      FREE_MEM (b);
      return -1;
    }

  if (idx == 1)
    {
      /* start a new bind set (array row) */
      T_BR_BIND ***tmp_sets;
      int *tmp_nbind;

      tmp_sets = (T_BR_BIND ***) REALLOC (st->sets, sizeof (T_BR_BIND **) * (st->num_sets + 1));
      tmp_nbind = (int *) REALLOC (st->set_nbind, sizeof (int) * (st->num_sets + 1));
      if (tmp_sets == NULL || tmp_nbind == NULL)
	{
	  fprintf (stderr, "malloc error\n");
	  FREE_MEM (b->type_tok);
	  FREE_MEM (b->value);
	  FREE_MEM (b);
	  return -1;
	}
      st->sets = tmp_sets;
      st->set_nbind = tmp_nbind;
      st->sets[st->num_sets] = NULL;
      st->set_nbind[st->num_sets] = 0;
      st->num_sets++;
    }
  else
    {
      if (st->num_sets == 0)
	{
	  fprintf (stderr, "bind set must start at index 1: %s\n", line);
	  FREE_MEM (b->type_tok);
	  FREE_MEM (b->value);
	  FREE_MEM (b);
	  return -1;
	}
      if (idx != st->set_nbind[st->num_sets - 1] + 1)
	{
	  fprintf (stderr, "bind index must increase by one: %s\n", line);
	  FREE_MEM (b->type_tok);
	  FREE_MEM (b->value);
	  FREE_MEM (b);
	  return -1;
	}
    }

  s = st->num_sets - 1;
  new_set = (T_BR_BIND **) REALLOC (st->sets[s], sizeof (T_BR_BIND *) * (st->set_nbind[s] + 1));
  if (new_set == NULL)
    {
      fprintf (stderr, "malloc error\n");
      FREE_MEM (b->type_tok);
      FREE_MEM (b->value);
      FREE_MEM (b);
      return -1;
    }
  st->sets[s] = new_set;
  st->sets[s][st->set_nbind[s]] = b;
  st->set_nbind[s]++;

  return 0;
}

static void
stmt_init (T_BR_STMT * st)
{
  memset (st, 0, sizeof (T_BR_STMT));
  st->mode = BR_EXEC_SINGLE;
  st->exec_count = 1;
}

static void
bind_free_set (T_BR_BIND ** set, int nbind)
{
  int i;

  if (set == NULL)
    {
      return;
    }

  for (i = 0; i < nbind; i++)
    {
      if (set[i] != NULL)
	{
	  FREE_MEM (set[i]->type_tok);
	  FREE_MEM (set[i]->value);
	  FREE_MEM (set[i]);
	}
    }
  FREE_MEM (set);
}

static void
stmt_reset (T_BR_STMT * st)
{
  int i;

  FREE_MEM (st->query);

  for (i = 0; i < st->num_sets; i++)
    {
      bind_free_set (st->sets[i], st->set_nbind[i]);
    }
  FREE_MEM (st->sets);
  FREE_MEM (st->set_nbind);

  for (i = 0; i < st->batch_count; i++)
    {
      FREE_MEM (st->batch_sql[i]);
    }
  FREE_MEM (st->batch_sql);

  stmt_init (st);
}

static int
stmt_add_batch_sql (T_BR_STMT * st, const char *sql)
{
  if (st->batch_count >= st->batch_capacity)
    {
      int new_cap = (st->batch_capacity == 0) ? 8 : st->batch_capacity * 2;
      char **tmp = (char **) REALLOC (st->batch_sql, sizeof (char *) * new_cap);

      if (tmp == NULL)
	{
	  fprintf (stderr, "malloc error\n");
	  return -1;
	}
      st->batch_sql = tmp;
      st->batch_capacity = new_cap;
    }

  st->batch_sql[st->batch_count] = strdup (sql);
  if (st->batch_sql[st->batch_count] == NULL)
    {
      fprintf (stderr, "malloc error\n");
      return -1;
    }
  st->batch_count++;

  return 0;
}

static int
bind_one_set (int req, T_BR_BIND ** set, int nbind)
{
  int i;

  for (i = 0; i < nbind; i++)
    {
      T_BR_BIND *b = set[i];
      int res;

      if (b->u_type == CCI_U_TYPE_BIT || b->u_type == CCI_U_TYPE_VARBIT)
	{
	  /* the T_CCI_BIT lives in the bind and outlives cci_execute, as CCI_BIND_PTR requires */
	  b->bit.size = b->size;
	  b->bit.buf = b->value;
	  res = cci_bind_param (req, b->idx, CCI_A_TYPE_BIT, (void *) &b->bit, b->u_type, CCI_BIND_PTR);
	}
      else
	{
	  res = cci_bind_param (req, b->idx, CCI_A_TYPE_STR, b->value, b->u_type, 0);
	}

      if (res < 0)
	{
	  fprintf (stderr, "cci_bind_param failed at index %d\n", b->idx);
	  return -1;
	}
    }

  return 0;
}

/* Builds "<query>" followed by BR_TESTER_HINT in a freshly allocated buffer. */
static char *
build_hinted_query (const char *query)
{
  size_t len = strlen (query) + strlen (BR_TESTER_HINT) + 1;
  char *hinted = (char *) MALLOC (len);

  if (hinted == NULL)
    {
      fprintf (stderr, "malloc error\n");
      return NULL;
    }
  snprintf (hinted, len, "%s%s", query, BR_TESTER_HINT);

  return hinted;
}

/* Single / @execute(N) / @array(N). A plain single execute with no binds is
 * delegated to execute_test_with_query so the legacy result table and shard
 * iteration are preserved; the bind-aware paths run on the routed CAS only
 * (no per-shard iteration). */
static int
flush_stmt (int conn_handle, T_BR_STMT * st, int shard_flag)
{
  T_CCI_ERROR err_buf;
  char *hinted;
  char **col_vals = NULL;
  int *col_nulls = NULL;
  int req = -1;
  int expected;
  int ret = 0;
  int err_num = 0;
  int i;
  int s;

  if (st->mode == BR_EXEC_SINGLE && st->num_sets == 0)
    {
      int r = execute_test_with_query (conn_handle, st->query, shard_flag);

      if (r == BR_TESTER_BIND_REJECTED)
	{
	  fprintf (stderr, "query has bind parameter(s) but no bind line: %s\n", st->query);
	  return -1;		/* -i callers expect a negative value on failure */
	}
      return r;
    }

  if (st->mode == BR_EXEC_ARRAY)
    {
      if (st->num_sets < 1)
	{
	  fprintf (stderr, "@array requires at least one bind set\n");
	  return -1;
	}
    }
  else if (st->num_sets > 1)
    {
      fprintf (stderr, "multiple bind sets are only allowed with @array\n");
      return -1;
    }

  hinted = build_hinted_query (st->query);
  if (hinted == NULL)
    {
      return -1;
    }

  req = cci_prepare (conn_handle, hinted, 0, &err_buf);
  if (req < 0)
    {
      PRINT_CCI_ERROR ("ERROR CODE : %d\n%s\n\n", err_buf.err_code, err_buf.err_msg);
      FREE_MEM (hinted);
      return -1;
    }

  expected = cci_get_bind_num (req);
  for (s = 0; s < st->num_sets; s++)
    {
      if (st->set_nbind[s] != expected)
	{
	  fprintf (stderr, "bind count mismatch: prepared expects %d, got %d\n", expected, st->set_nbind[s]);
	  err_num++;
	  goto close;
	}
    }

  if (st->mode == BR_EXEC_ARRAY)
    {
      int j;
      int m = st->num_sets;

      cci_bind_param_array_size (req, m);

      /* one column of values/null-indicators is bound at a time; the arrays must
       * stay alive until cci_execute_array, so they are freed after the loop */
      col_vals = (char **) MALLOC (sizeof (char *) * m * expected);
      col_nulls = (int *) MALLOC (sizeof (int) * m * expected);
      if (col_vals == NULL || col_nulls == NULL)
	{
	  fprintf (stderr, "malloc error\n");
	  err_num++;
	  goto close;
	}

      for (j = 0; j < expected; j++)
	{
	  char **vals = col_vals + (j * m);
	  int *nulls = col_nulls + (j * m);

	  for (s = 0; s < m; s++)
	    {
	      vals[s] = st->sets[s][j]->value;
	      nulls[s] = st->sets[s][j]->is_null ? 1 : 0;
	    }
	  cci_bind_param_array (req, j + 1, CCI_A_TYPE_STR, vals, nulls, st->sets[0][j]->u_type);
	}

      for (i = 0; i < st->exec_count; i++)
	{
	  T_CCI_QUERY_RESULT *qr = NULL;
	  int n;
	  int r;

	  n = cci_execute_array (req, &qr, &err_buf);
	  if (n < 0)
	    {
	      PRINT_CCI_ERROR ("ERROR CODE : %d\n%s\n\n", err_buf.err_code, err_buf.err_msg);
	      err_num++;
	    }
	  else if (qr != NULL)
	    {
	      char rtime[TIME_BUF_SIZE];

	      rtime[0] = '\0';
	      for (r = 1; r <= m; r++)
		{
		  int err_no = CCI_QUERY_RESULT_ERR_NO (qr, r);

		  print_result ((err_no == 0) ? CCI_QUERY_RESULT_RESULT (qr, r) : -1, err_no, shard_flag, 0, rtime,
				st->query);
		}
	    }

	  if (qr != NULL)
	    {
	      cci_query_result_free (qr, m);
	    }
	}
    }
  else
    {
      for (i = 0; i < st->exec_count; i++)
	{
	  struct timeval start_time;
	  char time[TIME_BUF_SIZE];
	  T_CCI_SQLX_CMD cmd_type = CUBRID_STMT_NONE;
	  T_CCI_COL_INFO *col_info = NULL;
	  int col_count = 0;

	  memset (tester_err_msg, 0, sizeof (tester_err_msg));
	  gettimeofday (&start_time, NULL);

	  if (st->num_sets == 1 && bind_one_set (req, st->sets[0], st->set_nbind[0]) < 0)
	    {
	      err_num++;
	      break;
	    }

	  ret = cci_execute (req, 0, 0, &err_buf);
	  if (ret < 0)
	    {
	      snprintf_dots_truncate (tester_err_msg, sizeof (tester_err_msg) - 1, "ERROR CODE : %d\n%s\n\n",
				      err_buf.err_code, err_buf.err_msg);
	      err_num++;
	    }

	  if (ret >= 0 && br_tester_info.verbose_mode)
	    {
	      col_info = cci_get_result_info (req, &cmd_type, &col_count);
	      if (cmd_type == CUBRID_STMT_SELECT && col_info == NULL)
		{
		  snprintf_dots_truncate (tester_err_msg, sizeof (tester_err_msg) - 1, "ERROR CODE : %d\n%s\n\n",
					  err_buf.err_code, err_buf.err_msg);
		  ret = -1;
		  err_num++;
		}
	    }

	  get_time (&start_time, time, sizeof (time));
	  print_result (ret, err_buf.err_code, shard_flag, 0, time, st->query);

	  if (ret >= 0 && br_tester_info.verbose_mode && cmd_type == CUBRID_STMT_SELECT)
	    {
	      if (print_result_set (req, &err_buf, col_info, col_count) < 0)
		{
		  err_num++;
		}
	    }
	}
    }

close:
  FREE_MEM (col_vals);
  FREE_MEM (col_nulls);
  cci_close_req_handle (req);
  cci_end_tran (conn_handle, CCI_TRAN_ROLLBACK, &err_buf);
  FREE_MEM (hinted);

  return (err_num > 0) ? -1 : 0;
}

/* @call / @call(N). Prepares with CCI_PREPARE_CALL and repeats bind/execute/fetch
 * N times. CALL surfaces no SELECT-style result set (fetch returns a single OUT
 * tuple), so only OUT/INOUT/return values are collected via print_out_params. */
static int
flush_call (int conn_handle, T_BR_STMT * st)
{
  T_CCI_ERROR err_buf;
  char *hinted;
  int req = -1;
  int expected;
  int nbind;
  int err_num = 0;
  int c;
  int i;
  T_BR_BIND **set;

  if (st->num_sets > 1)
    {
      fprintf (stderr, "@call allows a single bind set (use @call(N) to repeat)\n");
      return -1;
    }

  hinted = build_hinted_query (st->query);
  if (hinted == NULL)
    {
      return -1;
    }

  req = cci_prepare (conn_handle, hinted, CCI_PREPARE_CALL, &err_buf);
  if (req < 0)
    {
      PRINT_CCI_ERROR ("ERROR CODE : %d\n%s\n\n", err_buf.err_code, err_buf.err_msg);
      FREE_MEM (hinted);
      return -1;
    }

  set = (st->num_sets == 1) ? st->sets[0] : NULL;
  nbind = (st->num_sets == 1) ? st->set_nbind[0] : 0;

  expected = cci_get_bind_num (req);
  if (nbind != expected)
    {
      fprintf (stderr, "bind count mismatch: prepared expects %d, got %d\n", expected, nbind);
      err_num++;
      goto close;
    }

  if (br_tester_info.verbose_mode)
    {
      PRINT_RESULT ("<Result of CALL Command>\n");
    }

  for (c = 1; c <= st->exec_count; c++)
    {
      for (i = 0; i < nbind; i++)
	{
	  T_BR_BIND *b = set[i];

	  if (b->param_mode == BR_PM_IN || b->param_mode == BR_PM_INOUT)
	    {
	      if (b->u_type == CCI_U_TYPE_BIT || b->u_type == CCI_U_TYPE_VARBIT)
		{
		  b->bit.size = b->size;
		  b->bit.buf = b->value;
		  cci_bind_param (req, b->idx, CCI_A_TYPE_BIT, (void *) &b->bit, b->u_type, CCI_BIND_PTR);
		}
	      else
		{
		  cci_bind_param (req, b->idx, CCI_A_TYPE_STR, b->value, b->u_type, 0);
		}
	    }

	  if (b->param_mode == BR_PM_OUT || b->param_mode == BR_PM_INOUT)
	    {
	      cci_register_out_param_ex (req, b->idx, b->u_type);
	    }
	}

      if (cci_execute (req, 0, 0, &err_buf) < 0)
	{
	  PRINT_CCI_ERROR ("ERROR CODE : %d\n%s\n\n", err_buf.err_code, err_buf.err_msg);
	  err_num++;
	  break;
	}

      /* CALL returns a single tuple carrying the OUT/return values; position the
       * cursor on it before print_out_params reads them via cci_get_data
       * (cci_get_data fails with CCI_ER_INVALID_CURSOR_POS without a fetch). */
      if (cci_cursor (req, 1, CCI_CURSOR_CURRENT, &err_buf) < 0 || cci_fetch (req, &err_buf) < 0)
	{
	  PRINT_CCI_ERROR ("ERROR CODE : %d\n%s\n\n", err_buf.err_code, err_buf.err_msg);
	  err_num++;
	  break;
	}

      print_out_params (st, req, (st->exec_count > 1) ? c : 0);
    }

close:
  cci_close_req_handle (req);
  cci_end_tran (conn_handle, CCI_TRAN_ROLLBACK, &err_buf);
  FREE_MEM (hinted);

  return (err_num > 0) ? -1 : 0;
}

/* @batch begin/end. Runs the collected plain SQL statements as one
 * cci_execute_batch; no prepare handle and no bind parameters. */
static int
flush_batch (int conn_handle, T_BR_STMT * st)
{
  T_CCI_ERROR err_buf;
  T_CCI_QUERY_RESULT *qr = NULL;
  char **hinted = NULL;
  int err_num = 0;
  int i;

  if (st->batch_count <= 0)
    {
      fprintf (stderr, "@batch block is empty\n");
      return -1;
    }

  hinted = (char **) MALLOC (sizeof (char *) * st->batch_count);
  if (hinted == NULL)
    {
      fprintf (stderr, "malloc error\n");
      return -1;
    }
  memset (hinted, 0, sizeof (char *) * st->batch_count);

  for (i = 0; i < st->batch_count; i++)
    {
      hinted[i] = build_hinted_query (st->batch_sql[i]);
      if (hinted[i] == NULL)
	{
	  err_num++;
	  goto done;
	}
    }

  if (cci_execute_batch (conn_handle, st->batch_count, hinted, &qr, &err_buf) < 0)
    {
      PRINT_CCI_ERROR ("ERROR CODE : %d\n%s\n\n", err_buf.err_code, err_buf.err_msg);
      err_num++;
    }
  else
    {
      print_batch_result (qr, st->batch_count);
    }

done:
  if (qr != NULL)
    {
      cci_query_result_free (qr, st->batch_count);
    }
  cci_end_tran (conn_handle, CCI_TRAN_ROLLBACK, &err_buf);

  for (i = 0; i < st->batch_count; i++)
    {
      FREE_MEM (hinted[i]);
    }
  FREE_MEM (hinted);

  return (err_num > 0) ? -1 : 0;
}

static void
print_out_params (T_BR_STMT * st, int req, int call_seq)
{
  T_BR_BIND **set;
  int nbind;
  int i;

  if (!br_tester_info.verbose_mode || st->num_sets < 1)
    {
      return;
    }

  set = st->sets[0];
  nbind = st->set_nbind[0];

  for (i = 0; i < nbind; i++)
    {
      T_BR_BIND *b = set[i];
      char *buf = NULL;
      int ind = 0;

      if (b->param_mode != BR_PM_OUT && b->param_mode != BR_PM_INOUT)
	{
	  continue;
	}

      /* the marker index is passed straight through: CCI skips the reserved
       * return-value column for CALL_SP. buf is CCI-owned and only valid until
       * the handle is closed, so it is read here; check ind < 0 before buf. */
      if (cci_get_data (req, b->idx, CCI_A_TYPE_STR, &buf, &ind) < 0)
	{
	  fprintf (stderr, "failed to get out parameter %d\n", b->idx);
	  continue;
	}

      if (call_seq > 0)
	{
	  PRINT_RESULT ("  [call %d] [out %d] %s %s\n", call_seq, b->idx, b->type_tok, (ind < 0) ? "NULL" : buf);
	}
      else
	{
	  PRINT_RESULT ("  [out %d] %s %s\n", b->idx, b->type_tok, (ind < 0) ? "NULL" : buf);
	}
    }
}

static void
print_batch_result (T_CCI_QUERY_RESULT * qr, int k)
{
  int i;

  if (!br_tester_info.verbose_mode)
    {
      return;
    }

  PRINT_RESULT ("<Result of BATCH Command>\n");

  for (i = 1; i <= k; i++)
    {
      int err_no = CCI_QUERY_RESULT_ERR_NO (qr, i);

      PRINT_RESULT ("  [stmt %d] result=%d err=%d\n", i, CCI_QUERY_RESULT_RESULT (qr, i), err_no);
      if (err_no != 0)
	{
	  PRINT_RESULT ("    %s\n", CCI_QUERY_RESULT_ERR_MSG (qr, i));
	}
    }
}

static int
flush_current (int conn_handle, T_BR_STMT * st, int shard_flag)
{
  switch (st->mode)
    {
    case BR_EXEC_CALL:
      return flush_call (conn_handle, st);

    case BR_EXEC_BATCH:
      return flush_batch (conn_handle, st);

    default:
      return flush_stmt (conn_handle, st, shard_flag);
    }
}

static int
execute_test (int conn_handle, int shard_flag)
{
  FILE *file;
  T_STRING *linebuf;
  T_BR_STMT st;
  bool in_batch = false;
  bool pending_directive = false;
  int err_num = 0;

  file = fopen (br_tester_info.input_file_name, "r");
  if (file == NULL)
    {
      fprintf (stderr, "cannot open input file %s\n", br_tester_info.input_file_name);
      return -1;
    }

  linebuf = t_string_make (LINE_MAX);
  if (linebuf == NULL)
    {
      fprintf (stderr, "malloc error\n");
      fclose (file);
      return -1;
    }

  stmt_init (&st);
  print_title (shard_flag);

  while (tester_get_line (file, linebuf) == 0)
    {
      char *line = strip_caslog_prefix (t_string_str (linebuf));
      char *hash;
      int kind;

      hash = find_line_comment (line);
      if (hash != NULL)
	{
	  *hash = '\0';
	}
      trim (line);
      if (line[0] == '\0')
	{
	  continue;
	}

      kind = classify_line (line);

      /* inside a @batch block only plain SQL (and @batch end) is accepted */
      if (in_batch)
	{
	  if (is_batch_end (line))
	    {
	      if (flush_batch (conn_handle, &st) < 0)
		{
		  err_num++;
		}
	      stmt_reset (&st);
	      in_batch = false;
	    }
	  else if (is_batch_begin (line))
	    {
	      fprintf (stderr, "nested @batch is not allowed\n");
	      err_num++;
	    }
	  else if (kind != BR_LINE_QUERY)
	    {
	      fprintf (stderr, "@batch block allows plain SQL only\n");
	      err_num++;
	    }
	  else if (stmt_add_batch_sql (&st, line) < 0)
	    {
	      err_num++;
	    }
	  continue;
	}

      if (kind == BR_LINE_DIRECTIVE)
	{
	  if (is_batch_end (line))
	    {
	      fprintf (stderr, "@batch end without @batch begin\n");
	      err_num++;
	      continue;
	    }

	  /* a completed statement is flushed before the new directive, exactly as
	   * for a new query line */
	  if (st.query != NULL)
	    {
	      if (flush_current (conn_handle, &st, shard_flag) < 0)
		{
		  err_num++;
		}
	      stmt_reset (&st);
	      pending_directive = false;
	    }

	  if (is_batch_begin (line))
	    {
	      if (pending_directive)
		{
		  fprintf (stderr, "directive must be followed by a query, not @batch\n");
		  err_num++;
		  stmt_reset (&st);
		  pending_directive = false;
		}
	      st.mode = BR_EXEC_BATCH;
	      in_batch = true;
	      continue;
	    }

	  if (pending_directive)
	    {
	      fprintf (stderr, "only one prefix directive is allowed per query\n");
	      err_num++;
	      stmt_reset (&st);
	      pending_directive = false;
	    }

	  if (parse_directive (line, &st) < 0)
	    {
	      err_num++;
	      stmt_reset (&st);
	    }
	  else
	    {
	      pending_directive = true;
	    }
	  continue;
	}

      if (kind == BR_LINE_BIND)
	{
	  if (st.query == NULL)
	    {
	      fprintf (stderr, "bind line has no preceding query: %s\n", line);
	      err_num++;
	    }
	  else if (parse_bind_line (line, &st) < 0)
	    {
	      err_num++;
	    }
	  continue;
	}

      /* BR_LINE_QUERY: flush the previous statement, then start a new one. A
       * pending directive (if any) applies to this query. */
      if (st.query != NULL)
	{
	  if (flush_current (conn_handle, &st, shard_flag) < 0)
	    {
	      err_num++;
	    }
	  stmt_reset (&st);
	}
      st.query = strdup (line);
      if (st.query == NULL)
	{
	  fprintf (stderr, "malloc error\n");
	  err_num++;
	}
      pending_directive = false;
    }

  if (in_batch)
    {
      fprintf (stderr, "@batch begin without matching @batch end\n");
      err_num++;
    }
  else if (st.query != NULL)
    {
      if (flush_current (conn_handle, &st, shard_flag) < 0)
	{
	  err_num++;
	}
    }
  else if (pending_directive)
    {
      fprintf (stderr, "directive must be followed by a query\n");
      err_num++;
    }

  stmt_reset (&st);
  t_string_free (linebuf);
  fclose (file);

  return (-1 * err_num);
}

static void
print_line (const char *ch, int num)
{
  int i;

  if (num <= 0)
    {
      return;
    }

  for (i = 0; i < num; i++)
    {
      PRINT_RESULT (ch);
    }
}

static void
print_usage (void)
{
  printf
    ("broker_tester <broker_name | host:port> [-D <database_name>] [-u <user_name>] [-p <user_password>] [-c <SQL_command>] [-i <input_file>] [-o <output_file>] [-v] [-s]\n");
  printf ("\t-D database-name (required for host:port)\n");
  printf ("\t-u alternate user name\n");
  printf ("\t-p password string, give \"\" for none\n");
  printf ("\t-c SQL-command (no bind parameters; use -i for bind/@call/@batch)\n");
  printf ("\t-i input-file-name\n");
  printf ("\t-o ouput-file-name\n");
  printf ("\t-v verbose mode\n");
  printf ("\t-s single shard database\n");
  printf ("\n");
  printf ("  -i file format (# comment, blank lines skipped):\n");
  printf ("\t<SQL>                        one statement per line\n");
  printf ("\tbind <n> [(IN|OUT|INOUT)] : <TYPE> [(<size>)] <value>\n");
  printf ("\t@execute(N) / @array(N) / @call[(N)]   directive for the next query\n");
  printf ("\t@batch begin ... @batch end            plain SQL batch block\n");
}

static void
print_title (int shard_flag)
{
  int title_len = 0;

  PRINT_TITLE (title_len, RESULT_FORMAT, "RESULT");

  if (shard_flag == ON)
    {
      PRINT_TITLE (title_len, STR_SHARD_ID_FORMAT, "SHARD_ID");
    }

  PRINT_TITLE (title_len, STR_ROWCOUNT_FORMAT, "ROW COUNT");

  PRINT_TITLE (title_len, TIME_FORMAT, "EXECUTION TIME");

  PRINT_TITLE (title_len, QUERY_FORMAT, "QUERY\n");

  print_line ("=", title_len);

  PRINT_RESULT ("\n");
}

static void
print_conn_result (char *broker_name, int conn_hd_id)
{
  if (conn_hd_id < 0)
    {
      PRINT_RESULT ("@ [FAIL] ");
    }
  else
    {
      PRINT_RESULT ("@ [OK] ");
    }

  PRINT_RESULT ("CONNECT %s DB [%s] USER [%s]\n\n", broker_name, br_tester_info.db_name, br_tester_info.db_user);
  return;
}

static void
print_query_test_result (int ret)
{
  if (ret < 0)
    {
      PRINT_RESULT ("@ [FAIL] ");
    }
  else
    {
      PRINT_RESULT ("@ [OK] ");
    }

  PRINT_RESULT ("QUERY TEST\n");

  return;
}

static void
print_shard_result (void)
{
  PRINT_RESULT ("@ SHARD ");

  if (br_tester_info.shard_flag == ON)
    {
      PRINT_RESULT ("ON\n\n");
    }
  else
    {
      PRINT_RESULT ("OFF\n\n");
    }

  return;
}

static void
print_result (int row_count, int err_code, int shard_flag, int shard_id, char *time, char *query)
{
  if (row_count >= 0)
    {
      PRINT_RESULT (RESULT_FORMAT, "OK");
    }
  else
    {
      char result_buf[15];

      snprintf (result_buf, sizeof (result_buf), "FAIL(%d) ", err_code);
      row_count = -1;
      PRINT_RESULT (RESULT_FORMAT, result_buf);
    }

  if (shard_flag == ON)
    {
      PRINT_RESULT (SHARD_ID_FORMAT, (row_count < 0) ? -1 : shard_id);
    }

  PRINT_RESULT (ROWCOUNT_FORMAT, row_count);

  PRINT_RESULT (TIME_FORMAT, time);

  PRINT_RESULT (QUERY_FORMAT, query);
  PRINT_RESULT ("\n");

  if (tester_err_msg[0] != '\0')
    {
      PRINT_CCI_ERROR ("%s", tester_err_msg);
    }

  return;
}

static int
print_result_set (int req, T_CCI_ERROR * err_buf, T_CCI_COL_INFO * col_info, int col_count)
{
  int i;
  int ind;
  int ret = 0;
  int title_len = 0;
  int malloc_size = 0;
  int *col_size_arr;
  char *data;
  char *col_name;
  char *data_with_quot = NULL;
  T_CCI_U_EXT_TYPE *col_type_arr;

  col_size_arr = (int *) malloc (sizeof (int) * col_count);
  if (col_size_arr == NULL)
    {
      fprintf (stderr, "malloc error\n");
      return -1;
    }

  col_type_arr = (T_CCI_U_EXT_TYPE *) malloc (sizeof (T_CCI_U_EXT_TYPE) * col_count);
  if (col_type_arr == NULL)
    {
      FREE_MEM (col_size_arr);

      fprintf (stderr, "malloc error\n");
      return -1;
    }

  PRINT_RESULT ("<Result of SELECT Command>\n");

  for (i = 1; i < col_count + 1; i++)
    {
      col_name = CCI_GET_RESULT_INFO_NAME (col_info, i);
      col_size_arr[i - 1] = MIN (MAX_DISPLAY_LENGTH, CCI_GET_RESULT_INFO_PRECISION (col_info, i));
      col_size_arr[i - 1] = MAX (col_size_arr[i - 1], (int) strlen (col_name));
      col_type_arr[i - 1] = CCI_GET_RESULT_INFO_TYPE (col_info, i);

      PRINT_TITLE (title_len, "  %-*s", col_size_arr[i - 1], col_name);
    }
  PRINT_RESULT ("\n");

  print_line ("-", title_len);

  PRINT_RESULT ("\n");

  while (1)
    {
      ret = cci_cursor (req, 1, CCI_CURSOR_CURRENT, err_buf);
      if (ret == CCI_ER_NO_MORE_DATA)
	{
	  ret = 0;
	  break;
	}

      if (ret < 0)
	{
	  PRINT_CCI_ERROR ("ERROR CODE : %d\n%s\n\n", err_buf->err_code, err_buf->err_msg);
	  goto end;
	}

      ret = cci_fetch (req, err_buf);
      if (ret < 0)
	{
	  PRINT_CCI_ERROR ("ERROR CODE : %d\n%s\n\n", err_buf->err_code, err_buf->err_msg);
	  goto end;
	}

      for (i = 1; i < col_count + 1; i++)
	{
	  ret = cci_get_data (req, i, CCI_A_TYPE_STR, &data, &ind);
	  if (ret < 0)
	    {
	      PRINT_CCI_ERROR ("ERROR CODE : %d\n%s\n\n", err_buf->err_code, err_buf->err_msg);
	      goto end;
	    }

	  if (is_number_type ((T_CCI_U_TYPE) col_type_arr[i - 1]))
	    {
	      PRINT_RESULT ("  %-*s", col_size_arr[i - 1], data);
	    }
	  else
	    {
	      int len = (int) strlen (data) + 3;
	      if (malloc_size < len)
		{
		  FREE_MEM (data_with_quot);
		  malloc_size = len;
		  data_with_quot = (char *) malloc (malloc_size);
		  if (data_with_quot == NULL)
		    {
		      fprintf (stderr, "malloc error\n");
		      ret = -1;
		      goto end;
		    }
		}
	      snprintf (data_with_quot, len, "'%s'", data);

	      PRINT_RESULT ("  %-*s", col_size_arr[i - 1], data_with_quot);
	    }
	}
      PRINT_RESULT ("\n");
    }
  PRINT_RESULT ("\n");

end:

  FREE_MEM (data_with_quot);
  FREE_MEM (col_size_arr);
  FREE_MEM (col_type_arr);

  return ret;
}

static bool
is_number_type (T_CCI_U_TYPE type)
{
  switch (type)
    {
    case CCI_U_TYPE_INT:
    case CCI_U_TYPE_SHORT:
    case CCI_U_TYPE_FLOAT:
    case CCI_U_TYPE_DOUBLE:
    case CCI_U_TYPE_BIGINT:
      return true;
    default:
      return false;
    }

  return false;
}

static void
free_br_tester_info (void)
{
  FREE_MEM (br_tester_info.db_name);
  FREE_MEM (br_tester_info.db_user);
  FREE_MEM (br_tester_info.db_passwd);
  FREE_MEM (br_tester_info.command);
  FREE_MEM (br_tester_info.input_file_name);
  FREE_MEM (br_tester_info.output_file_name);
}

int
main (int argc, char *argv[])
{
  int ret = 0;
  int opt;
  int conn_handle = -1;
  int conn_mode;
  char *direct_host = NULL;
  int direct_port = 0;
  char broker_name[BROKER_NAME_LEN];
  char conn_url[LINE_MAX];
  T_CCI_ERROR err_buf;

  if (argc < 2)
    {
      print_usage ();
      return -1;
    }

  memset (&br_tester_info, 0, sizeof (br_tester_info));

  strncpy (broker_name, argv[1], sizeof (broker_name) - 1);

  while ((opt = getopt (argc, argv, "D:u:p:c:i:o:sv")) != -1)
    {
      switch (opt)
	{
	case 'D':
	  br_tester_info.db_name = strdup (optarg);
	  break;
	case 'u':
	  br_tester_info.db_user = strdup (optarg);
	  break;
	case 'p':
	  br_tester_info.db_passwd = strdup (optarg);
	  break;
	case 'c':
	  br_tester_info.command = strdup (optarg);
	  break;
	case 'i':
	  br_tester_info.input_file_name = strdup (optarg);
	  break;
	case 'o':
	  br_tester_info.output_file_name = strdup (optarg);
	  break;
	case 'v':
	  br_tester_info.verbose_mode = true;
	  break;
	case 's':
	  br_tester_info.single_shard = true;
	  break;
	default:
	  print_usage ();
	  return -1;
	}
    }

  conn_mode = parse_conn_target (broker_name, &direct_host, &direct_port);
  if (conn_mode < 0)
    {
      free_br_tester_info ();
      return -1;
    }

  if (conn_mode == 1)
    {
      /* host:port direct mode: skip the master shm lookup. -D is required (no shm
       * default) and shard is unavailable, so -s is rejected. An empty user is
       * resolved to PUBLIC by the server. */
      if (br_tester_info.db_name == NULL)
	{
	  fprintf (stderr, "-D <database_name> is required for host:port connection\n");
	  FREE_MEM (direct_host);
	  free_br_tester_info ();
	  return -1;
	}
      if (br_tester_info.single_shard)
	{
	  fprintf (stderr, "-s cannot be used with host:port connection\n");
	  FREE_MEM (direct_host);
	  free_br_tester_info ();
	  return -1;
	}

      br_tester_info.broker_port = direct_port;
      br_tester_info.shard_flag = OFF;
      init_default_conn_info (APPL_SERVER_CAS);

      snprintf (conn_url, sizeof (conn_url), "cci:cubrid:%s:%u:%s:::", direct_host, direct_port,
		br_tester_info.db_name);
    }
  else
    {
      ret = init_tester_info (broker_name);
      if (ret < 0)
	{
	  free_br_tester_info ();
	  return -1;
	}

      snprintf (conn_url, sizeof (conn_url), "cci:cubrid:localhost:%u:%s:::", br_tester_info.broker_port,
		br_tester_info.db_name);
    }

  conn_handle = cci_connect_with_url_ex (conn_url, br_tester_info.db_user, br_tester_info.db_passwd, &err_buf);

  if (br_tester_info.output_file_name != NULL)
    {
      out_file_fp = fopen (br_tester_info.output_file_name, "w");
      if (out_file_fp == NULL)
	{
	  fprintf (stderr, "cannot open output file %s\n", br_tester_info.input_file_name);
	  goto end;
	}
    }

  print_conn_result (broker_name, conn_handle);

  if (conn_handle < 0)
    {
      PRINT_CCI_ERROR ("ERROR CODE : %d\n%s\n\n", err_buf.err_code, err_buf.err_msg);
      free_br_tester_info ();
      return -1;
    }

  ret = cci_set_autocommit (conn_handle, CCI_AUTOCOMMIT_FALSE);
  if (ret < 0)
    {
      fprintf (stderr, "cannot set autocommit mode\n");
      goto end;
    }


  print_shard_result ();

  if (br_tester_info.command != NULL)
    {
      print_title (br_tester_info.shard_flag);
      ret = execute_test_with_query (conn_handle, br_tester_info.command, br_tester_info.shard_flag);
      if (ret == BR_TESTER_BIND_REJECTED)
	{
	  fprintf (stderr, "bind query is not supported with -c; use -i\n");
	  ret = -1;
	  goto end;
	}
    }
  else if (br_tester_info.input_file_name != NULL)
    {
      ret = execute_test (conn_handle, br_tester_info.shard_flag);
    }
  else
    {
      goto end;
    }

  print_query_test_result (ret);

end:
  if (conn_handle >= 0)
    {
      cci_disconnect (conn_handle, &err_buf);
    }

  if (out_file_fp != NULL)
    {
      fclose (out_file_fp);
    }

  FREE_MEM (direct_host);
  free_br_tester_info ();

  return ret;
}
