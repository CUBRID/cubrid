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

/* Marks the tool's own queries in sql.log. */
#define BR_TESTER_HINT                 " /* broker_tester */"

/* Positive sentinel: callers must map it to their own failure value. */
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
  T_CCI_U_TYPE u_type;
  char *type_tok;
  char *value;			/* a NULL bind is sent as "" */
  int size;
  bool is_null;
  T_BR_PARAM_MODE param_mode;
  T_CCI_BIT bit;		/* must outlive cci_execute */
} T_BR_BIND;

/* One statement pending flush. */
typedef struct
{
  char *query;
  T_BR_EXEC_MODE mode;
  int exec_count;
  T_BR_BIND ***sets;		/* sets[set][col]; one set is one array row */
  int *set_nbind;
  int num_sets;
  char **batch_sql;
  int batch_count;
  int batch_capacity;
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
static char *find_line_comment (char *line, bool bind_line);
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

static int bind_one_param (int req, T_BR_BIND * b);
static int bind_one_set (int req, T_BR_BIND ** set, int nbind);
static char *build_hinted_query (const char *query, int shard_id);
static int flush_current (int conn_handle, T_BR_STMT * st, int shard_flag);
static int flush_stmt (int conn_handle, T_BR_STMT * st, int shard_flag);
static int flush_stmt_one (int conn_handle, T_BR_STMT * st, int shard_flag, int shard_id, bool add_shard_hint);
static int flush_call (int conn_handle, T_BR_STMT * st, int shard_flag);
static int flush_call_one (int conn_handle, T_BR_STMT * st, int shard_flag, int shard_id, bool add_shard_hint);
static int flush_batch (int conn_handle, T_BR_STMT * st, int shard_flag);
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

/* Returns 0 for a broker name, 1 for host:port (direct mode), -1 if malformed.
 * On 1, *host_out is a freshly allocated copy the caller must free. */
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

/* Runs one query that has no bind parameters. Returns BR_TESTER_BIND_REJECTED if
 * the query carries markers. The caller prints print_title () beforehand. */
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

/* Returns -1 for an unknown token. BLOB/CLOB resolve to CCI_U_TYPE_NULL.
 * Keep in sync with get_cci_type () in broker_log_replay.c. */
static int
get_bind_type (const char *tok)
{
  int type = -1;

  switch (tok[0])
    {
    case 'B':
      if (strcmp (tok, "BIGINT") == 0)
	{
	  type = CCI_U_TYPE_BIGINT;
	}
      else if (strcmp (tok, "BIT") == 0)
	{
	  type = CCI_U_TYPE_BIT;
	}
      else if (strcmp (tok, "BLOB") == 0)
	{
	  type = CCI_U_TYPE_NULL;
	}
      break;

    case 'C':
      if (strcmp (tok, "CHAR") == 0)
	{
	  type = CCI_U_TYPE_CHAR;
	}
      else if (strcmp (tok, "CLOB") == 0)
	{
	  type = CCI_U_TYPE_NULL;
	}
      break;

    case 'D':
      if (strcmp (tok, "DOUBLE") == 0)
	{
	  type = CCI_U_TYPE_DOUBLE;
	}
      else if (strcmp (tok, "DATE") == 0)
	{
	  type = CCI_U_TYPE_DATE;
	}
      else if (strcmp (tok, "DATETIME") == 0)
	{
	  type = CCI_U_TYPE_DATETIME;
	}
      else if (strcmp (tok, "DATETIMETZ") == 0)
	{
	  type = CCI_U_TYPE_DATETIMETZ;
	}
      break;

    case 'E':
      if (strcmp (tok, "ENUM") == 0)
	{
	  type = CCI_U_TYPE_ENUM;
	}
      break;

    case 'F':
      if (strcmp (tok, "FLOAT") == 0)
	{
	  type = CCI_U_TYPE_FLOAT;
	}
      break;

    case 'I':
      if (strcmp (tok, "INT") == 0)
	{
	  type = CCI_U_TYPE_INT;
	}
      break;

    case 'J':
      if (strcmp (tok, "JSON") == 0)
	{
	  type = CCI_U_TYPE_JSON;
	}
      break;

    case 'M':
      if (strcmp (tok, "MONETARY") == 0)
	{
	  type = CCI_U_TYPE_MONETARY;
	}
      break;

    case 'N':
      if (strcmp (tok, "NUMERIC") == 0)
	{
	  type = CCI_U_TYPE_NUMERIC;
	}
      else if (strcmp (tok, "NULL") == 0)
	{
	  type = CCI_U_TYPE_NULL;
	}
      break;

    case 'O':
      if (strcmp (tok, "OBJECT") == 0)
	{
	  type = CCI_U_TYPE_OBJECT;
	}
      break;

    case 'S':
      if (strcmp (tok, "SHORT") == 0)
	{
	  type = CCI_U_TYPE_SHORT;
	}
      break;

    case 'T':
      if (strcmp (tok, "TIME") == 0)
	{
	  type = CCI_U_TYPE_TIME;
	}
      else if (strcmp (tok, "TIMESTAMP") == 0)
	{
	  type = CCI_U_TYPE_TIMESTAMP;
	}
      else if (strcmp (tok, "TIMESTAMPTZ") == 0)
	{
	  type = CCI_U_TYPE_TIMESTAMPTZ;
	}
      break;

    case 'U':
      if (strcmp (tok, "UINT") == 0)
	{
	  type = CCI_U_TYPE_UINT;
	}
      else if (strcmp (tok, "UBIGINT") == 0)
	{
	  type = CCI_U_TYPE_UBIGINT;
	}
      else if (strcmp (tok, "USHORT") == 0)
	{
	  type = CCI_U_TYPE_USHORT;
	}
      break;

    case 'V':
      if (strcmp (tok, "VARCHAR") == 0)
	{
	  type = CCI_U_TYPE_STRING;
	}
      else if (strcmp (tok, "VARBIT") == 0)
	{
	  type = CCI_U_TYPE_VARBIT;
	}
      break;

    default:
      break;
    }

  return type;
}

/* A line without a recognizable sql.log prefix is returned unchanged: plain SQL
 * must never be truncated. */
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

/* Returns the first '#' that starts a comment, or NULL. A '#' inside '...', "...",
 * `...` or [...] is not one. bind_line restricts it to a '#' after whitespace so a
 * value like "a#b" survives; quote the value to keep a '#' that follows a space. */
static char *
find_line_comment (char *line, bool bind_line)
{
  char *p = line;
  char quote = '\0';

  while (*p != '\0')
    {
      if (quote != '\0')
	{
	  char close = (quote == '[') ? ']' : quote;

	  if ((quote == '\'' || quote == '"') && *p == '\\' && p[1] != '\0')
	    {
	      p += 2;
	      continue;
	    }
	  if (*p == close)
	    {
	      if (p[1] == close)
		{
		  p += 2;
		  continue;
		}
	      quote = '\0';
	    }
	  p++;
	  continue;
	}

      if (*p == '\'' || *p == '"' || *p == '`' || *p == '[')
	{
	  quote = *p;
	}
      else if (*p == '#')
	{
	  if (!bind_line || p == line || p[-1] == ' ' || p[-1] == '\t')
	    {
	      return p;
	    }
	}
      p++;
    }

  return NULL;
}

/* Reads one logical line of any length. Returns 0, -1 at end of file, -2 on
 * allocation failure - a partial read must not be taken for a clean end of file. */
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
	  fprintf (stderr, "malloc error\n");
	  return -2;
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

/* @batch is handled by the caller, not here. */
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

/* Index 1 starts a new set; other indices must increase by one within a set. */
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
      const char *close = strchr (p, ')');
      const char *mode_start;
      const char *mode_end;
      int mode_len;

      if (close == NULL)
	{
	  fprintf (stderr, "malformed bind mode: %s\n", line);
	  return -1;
	}

      /* match the whole token so that (INVALID) is not taken for (IN) */
      mode_start = p + 1;
      while (mode_start < close && (*mode_start == ' ' || *mode_start == '\t'))
	{
	  mode_start++;
	}
      mode_end = close;
      while (mode_end > mode_start && (mode_end[-1] == ' ' || mode_end[-1] == '\t'))
	{
	  mode_end--;
	}
      mode_len = (int) (mode_end - mode_start);

      if (mode_len == 5 && strncasecmp (mode_start, "INOUT", 5) == 0)
	{
	  mode = BR_PM_INOUT;
	}
      else if (mode_len == 3 && strncasecmp (mode_start, "OUT", 3) == 0)
	{
	  mode = BR_PM_OUT;
	}
      else if (mode_len == 2 && strncasecmp (mode_start, "IN", 2) == 0)
	{
	  mode = BR_PM_IN;
	}
      else
	{
	  fprintf (stderr, "unknown bind mode: %s\n", line);
	  return -1;
	}

      p = close + 1;
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

  /* optional size: character count for strings, byte count for BIT/VARBIT.
   * A '(' that does not start with digits belongs to the value itself. */
  if (*p == '(' && str_to_int32 (&size, &endp, p + 1, 10) == 0)
    {
      while (*endp == ' ' || *endp == '\t')
	{
	  endp++;
	}
      if (*endp != ')')
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
  else
    {
      size = 0;
    }

  if ((type == CCI_U_TYPE_BIT || type == CCI_U_TYPE_VARBIT) && size <= 0)
    {
      fprintf (stderr, "warning: no size given for %s; using the value length\n", type_tok);
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

	  if (*q != '\'')
	    {
	      fprintf (stderr, "unterminated quoted value: %s\n", line);
	      goto bind_error;
	    }
	  if (q[1] != '\0')
	    {
	      fprintf (stderr, "unexpected text after quoted value: %s\n", line);
	      goto bind_error;
	    }
	}
    }
  else
    {
      b->value = strdup (val_start);
    }

  if (b->type_tok == NULL || b->value == NULL)
    {
      fprintf (stderr, "malloc error\n");
      goto bind_error;
    }

  if (idx == 1)
    {
      T_BR_BIND ***tmp_sets;
      int *tmp_nbind;

      tmp_sets = (T_BR_BIND ***) REALLOC (st->sets, sizeof (T_BR_BIND **) * (st->num_sets + 1));
      if (tmp_sets == NULL)
	{
	  fprintf (stderr, "malloc error\n");
	  goto bind_error;
	}
      st->sets = tmp_sets;	/* commit now so st->sets never holds a freed block */

      tmp_nbind = (int *) REALLOC (st->set_nbind, sizeof (int) * (st->num_sets + 1));
      if (tmp_nbind == NULL)
	{
	  fprintf (stderr, "malloc error\n");
	  goto bind_error;
	}
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
	  goto bind_error;
	}
      if (idx != st->set_nbind[st->num_sets - 1] + 1)
	{
	  fprintf (stderr, "bind index must increase by one: %s\n", line);
	  goto bind_error;
	}
    }

  s = st->num_sets - 1;
  new_set = (T_BR_BIND **) REALLOC (st->sets[s], sizeof (T_BR_BIND *) * (st->set_nbind[s] + 1));
  if (new_set == NULL)
    {
      fprintf (stderr, "malloc error\n");
      goto bind_error;
    }
  st->sets[s] = new_set;
  st->sets[s][st->set_nbind[s]] = b;
  st->set_nbind[s]++;

  return 0;

bind_error:
  FREE_MEM (b->type_tok);
  FREE_MEM (b->value);
  FREE_MEM (b);

  return -1;
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
bind_one_param (int req, T_BR_BIND * b)
{
  if (b->u_type == CCI_U_TYPE_BIT || b->u_type == CCI_U_TYPE_VARBIT)
    {
      /* CCI_BIND_PTR keeps the pointer, so the T_CCI_BIT and its buffer live in the
       * bind and outlive cci_execute. Without "(n)" the value length is used. */
      b->bit.size = (b->size > 0) ? b->size : (int) strlen (b->value);
      b->bit.buf = b->value;

      return cci_bind_param (req, b->idx, CCI_A_TYPE_BIT, (void *) &b->bit, b->u_type, CCI_BIND_PTR);
    }

  return cci_bind_param (req, b->idx, CCI_A_TYPE_STR, b->value, b->u_type, 0);
}

static int
bind_one_set (int req, T_BR_BIND ** set, int nbind)
{
  int i;

  for (i = 0; i < nbind; i++)
    {
      int res = bind_one_param (req, set[i]);

      if (res < 0)
	{
	  fprintf (stderr, "cci_bind_param failed at index %d (error %d)\n", set[i]->idx, res);
	  return -1;
	}
    }

  return 0;
}

/* shard_id < 0 omits the shard hint. The caller frees the returned buffer. */
static char *
build_hinted_query (const char *query, int shard_id)
{
  char shard_hint[64];
  size_t len;
  char *hinted;

  shard_hint[0] = '\0';
  if (shard_id >= 0)
    {
      snprintf (shard_hint, sizeof (shard_hint), " /*+ shard_id(%d) */", shard_id);
    }

  len = strlen (query) + strlen (shard_hint) + strlen (BR_TESTER_HINT) + 1;
  hinted = (char *) MALLOC (len);
  if (hinted == NULL)
    {
      fprintf (stderr, "malloc error\n");
      return NULL;
    }
  snprintf (hinted, len, "%s%s%s", query, shard_hint, BR_TESTER_HINT);

  return hinted;
}

/* One prepare per shard: the shard hint is part of the SQL text. */
static int
flush_stmt (int conn_handle, T_BR_STMT * st, int shard_flag)
{
  bool iterate_shards;
  int shard_id = 0;
  int err_num = 0;

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

  iterate_shards = (br_tester_info.shard_flag == ON && !br_tester_info.single_shard);

  do
    {
      int r = flush_stmt_one (conn_handle, st, shard_flag, shard_id, iterate_shards);

      if (r < 0)
	{
	  err_num++;
	}

      /* -2 means the statement itself is wrong, so it would fail the same way on
       * every shard; report it once */
      if (r == -2 || !iterate_shards)
	{
	  break;
	}
    }
  while (++shard_id < br_tester_info.num_shard);

  return (err_num > 0) ? -1 : 0;
}

static int
flush_stmt_one (int conn_handle, T_BR_STMT * st, int shard_flag, int shard_id, bool add_shard_hint)
{
  T_CCI_ERROR err_buf;
  char *hinted;
  char **col_vals = NULL;
  int *col_nulls = NULL;
  T_CCI_BIT *col_bits = NULL;
  struct timeval start_time;
  char time[TIME_BUF_SIZE];
  int req = -1;
  int expected;
  int ret = 0;
  int err_num = 0;
  int fatal = 0;
  int i;
  int s;

  if (st->mode == BR_EXEC_ARRAY)
    {
      if (st->num_sets < 1)
	{
	  fprintf (stderr, "@array requires at least one bind set\n");
	  return -2;
	}
    }
  else if (st->num_sets > 1)
    {
      fprintf (stderr, "multiple bind sets are only allowed with @array\n");
      return -2;
    }

  hinted = build_hinted_query (st->query, add_shard_hint ? shard_id : -1);
  if (hinted == NULL)
    {
      return -2;
    }

  memset (tester_err_msg, 0, sizeof (tester_err_msg));
  gettimeofday (&start_time, NULL);

  req = cci_prepare (conn_handle, hinted, 0, &err_buf);
  if (req < 0)
    {
      /* report the failing shard in the result table */
      snprintf_dots_truncate (tester_err_msg, sizeof (tester_err_msg) - 1, "ERROR CODE : %d\n%s\n\n",
			      err_buf.err_code, err_buf.err_msg);
      get_time (&start_time, time, sizeof (time));
      print_result (-1, err_buf.err_code, shard_flag, shard_id, time, st->query);

      cci_end_tran (conn_handle, CCI_TRAN_ROLLBACK, &err_buf);
      FREE_MEM (hinted);

      return -1;
    }

  expected = cci_get_bind_num (req);
  for (s = 0; s < st->num_sets; s++)
    {
      if (st->set_nbind[s] != expected)
	{
	  fprintf (stderr, "bind count mismatch: prepared expects %d, got %d\n", expected, st->set_nbind[s]);
	  fatal = 1;
	  goto close;
	}
    }

  if (st->mode == BR_EXEC_ARRAY)
    {
      int j;
      int m = st->num_sets;

      if (cci_bind_param_array_size (req, m) < 0)
	{
	  fprintf (stderr, "cci_bind_param_array_size failed for %d row(s)\n", m);
	  err_num++;
	  goto close;
	}

      /* one column of values/null-indicators is bound at a time; CCI keeps only
       * the pointers (BIND_PTR_STATIC), so the arrays must stay alive until
       * cci_execute_array and are freed after the loop */
      col_vals = (char **) MALLOC (sizeof (char *) * m * expected);
      col_nulls = (int *) MALLOC (sizeof (int) * m * expected);
      col_bits = (T_CCI_BIT *) MALLOC (sizeof (T_CCI_BIT) * m * expected);
      if (col_vals == NULL || col_nulls == NULL || col_bits == NULL)
	{
	  fprintf (stderr, "malloc error\n");
	  err_num++;
	  goto close;
	}

      for (j = 0; j < expected; j++)
	{
	  char **vals = col_vals + (j * m);
	  int *nulls = col_nulls + (j * m);
	  T_CCI_U_TYPE col_type = CCI_U_TYPE_NULL;
	  int res;

	  /* the array is bound one column at a time, so the column carries a single
	   * type; a NULL element has no type of its own and must not decide it */
	  for (s = 0; s < m; s++)
	    {
	      T_BR_BIND *b = st->sets[s][j];

	      if (b->u_type == CCI_U_TYPE_NULL)
		{
		  continue;
		}
	      if (col_type == CCI_U_TYPE_NULL)
		{
		  col_type = b->u_type;
		}
	      else if (col_type != b->u_type)
		{
		  fprintf (stderr, "bind type mismatch in @array column %d\n", j + 1);
		  fatal = 1;
		  goto close;
		}
	    }

	  for (s = 0; s < m; s++)
	    {
	      nulls[s] = st->sets[s][j]->is_null ? 1 : 0;
	    }

	  if (col_type == CCI_U_TYPE_BIT || col_type == CCI_U_TYPE_VARBIT)
	    {
	      T_CCI_BIT *bits = col_bits + (j * m);

	      for (s = 0; s < m; s++)
		{
		  T_BR_BIND *b = st->sets[s][j];

		  bits[s].size = (b->size > 0) ? b->size : (int) strlen (b->value);
		  bits[s].buf = b->value;
		}
	      res = cci_bind_param_array (req, j + 1, CCI_A_TYPE_BIT, bits, nulls, col_type);
	    }
	  else
	    {
	      for (s = 0; s < m; s++)
		{
		  vals[s] = st->sets[s][j]->value;
		}
	      res = cci_bind_param_array (req, j + 1, CCI_A_TYPE_STR, vals, nulls, col_type);
	    }

	  if (res < 0)
	    {
	      fprintf (stderr, "cci_bind_param_array failed at index %d (error %d)\n", j + 1, res);
	      err_num++;
	      goto close;
	    }
	}

      for (i = 0; i < st->exec_count; i++)
	{
	  T_CCI_QUERY_RESULT *qr = NULL;
	  int n;
	  int r;

	  gettimeofday (&start_time, NULL);

	  /* the return value is the number of results and qr stays NULL when the
	   * server reported none, so never walk qr by the array row count */
	  n = cci_execute_array (req, &qr, &err_buf);
	  if (n < 0)
	    {
	      PRINT_CCI_ERROR ("ERROR CODE : %d\n%s\n\n", err_buf.err_code, err_buf.err_msg);
	      err_num++;
	    }
	  else if (qr == NULL)
	    {
	      fprintf (stderr, "no query result returned for %d array row(s)\n", m);
	      err_num++;
	    }
	  else
	    {
	      get_time (&start_time, time, sizeof (time));

	      if (n != m)
		{
		  fprintf (stderr, "warning: %d result(s) for %d array row(s)\n", n, m);
		}

	      for (r = 1; r <= n; r++)
		{
		  int err_no = CCI_QUERY_RESULT_ERR_NO (qr, r);

		  print_result ((err_no == 0) ? CCI_QUERY_RESULT_RESULT (qr, r) : -1, err_no, shard_flag, shard_id,
				time, st->query);
		}
	    }

	  if (qr != NULL)
	    {
	      cci_query_result_free (qr, n);
	    }
	}
    }
  else
    {
      for (i = 0; i < st->exec_count; i++)
	{
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
	  else if (br_tester_info.shard_flag == ON && br_tester_info.single_shard)
	    {
	      /* without the hint the proxy picks the shard; report where it went */
	      cci_get_shard_id_with_req_handle (req, &shard_id, &err_buf);
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
	  print_result (ret, err_buf.err_code, shard_flag, shard_id, time, st->query);

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
  FREE_MEM (col_bits);
  cci_close_req_handle (req);
  cci_end_tran (conn_handle, CCI_TRAN_ROLLBACK, &err_buf);
  FREE_MEM (hinted);

  if (fatal)
    {
      return -2;
    }

  return (err_num > 0) ? -1 : 0;
}

/* CALL exposes no SELECT-style result set; fetch yields a single OUT tuple. */
static int
flush_call (int conn_handle, T_BR_STMT * st, int shard_flag)
{
  bool iterate_shards;
  int shard_id = 0;
  int err_num = 0;

  if (st->num_sets > 1)
    {
      fprintf (stderr, "@call allows a single bind set (use @call(N) to repeat)\n");
      return -1;
    }

  iterate_shards = (br_tester_info.shard_flag == ON && !br_tester_info.single_shard);

  do
    {
      int r = flush_call_one (conn_handle, st, shard_flag, shard_id, iterate_shards);

      if (r < 0)
	{
	  err_num++;
	}

      if (r == -2 || !iterate_shards)
	{
	  break;
	}
    }
  while (++shard_id < br_tester_info.num_shard);

  return (err_num > 0) ? -1 : 0;
}

static int
flush_call_one (int conn_handle, T_BR_STMT * st, int shard_flag, int shard_id, bool add_shard_hint)
{
  T_CCI_ERROR err_buf;
  char *hinted;
  struct timeval start_time;
  char time[TIME_BUF_SIZE];
  int req = -1;
  int expected;
  int nbind;
  int ret;
  int err_num = 0;
  int fatal = 0;
  int c;
  int i;
  T_BR_BIND **set;

  hinted = build_hinted_query (st->query, add_shard_hint ? shard_id : -1);
  if (hinted == NULL)
    {
      return -2;
    }

  memset (tester_err_msg, 0, sizeof (tester_err_msg));
  gettimeofday (&start_time, NULL);

  req = cci_prepare (conn_handle, hinted, CCI_PREPARE_CALL, &err_buf);
  if (req < 0)
    {
      snprintf_dots_truncate (tester_err_msg, sizeof (tester_err_msg) - 1, "ERROR CODE : %d\n%s\n\n",
			      err_buf.err_code, err_buf.err_msg);
      get_time (&start_time, time, sizeof (time));
      print_result (-1, err_buf.err_code, shard_flag, shard_id, time, st->query);

      cci_end_tran (conn_handle, CCI_TRAN_ROLLBACK, &err_buf);
      FREE_MEM (hinted);

      return -1;
    }

  set = (st->num_sets == 1) ? st->sets[0] : NULL;
  nbind = (st->num_sets == 1) ? st->set_nbind[0] : 0;

  expected = cci_get_bind_num (req);
  if (nbind != expected)
    {
      fprintf (stderr, "bind count mismatch: prepared expects %d, got %d\n", expected, nbind);
      fatal = 1;
      goto close;
    }

  for (c = 1; c <= st->exec_count; c++)
    {
      memset (tester_err_msg, 0, sizeof (tester_err_msg));
      gettimeofday (&start_time, NULL);

      for (i = 0; i < nbind; i++)
	{
	  T_BR_BIND *b = set[i];
	  int res;

	  if (b->param_mode == BR_PM_IN || b->param_mode == BR_PM_INOUT)
	    {
	      res = bind_one_param (req, b);
	      if (res < 0)
		{
		  fprintf (stderr, "cci_bind_param failed at index %d (error %d)\n", b->idx, res);
		  err_num++;
		  goto close;
		}
	    }

	  if (b->param_mode == BR_PM_OUT || b->param_mode == BR_PM_INOUT)
	    {
	      res = cci_register_out_param_ex (req, b->idx, b->u_type);
	      if (res < 0)
		{
		  fprintf (stderr, "cci_register_out_param_ex failed at index %d (error %d)\n", b->idx, res);
		  err_num++;
		  goto close;
		}
	    }
	}

      ret = cci_execute (req, 0, 0, &err_buf);
      if (ret < 0)
	{
	  snprintf_dots_truncate (tester_err_msg, sizeof (tester_err_msg) - 1, "ERROR CODE : %d\n%s\n\n",
				  err_buf.err_code, err_buf.err_msg);
	  err_num++;
	}
      else if (br_tester_info.shard_flag == ON && br_tester_info.single_shard)
	{
	  cci_get_shard_id_with_req_handle (req, &shard_id, &err_buf);
	}

      get_time (&start_time, time, sizeof (time));
      print_result (ret, err_buf.err_code, shard_flag, shard_id, time, st->query);

      if (ret < 0)
	{
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

  if (fatal)
    {
      return -2;
    }

  return (err_num > 0) ? -1 : 0;
}

/* No prepared handle here, so no shard hint can be attached. */
static int
flush_batch (int conn_handle, T_BR_STMT * st, int shard_flag)
{
  T_CCI_ERROR err_buf;
  T_CCI_QUERY_RESULT *qr = NULL;
  char **hinted = NULL;
  struct timeval start_time;
  char time[TIME_BUF_SIZE];
  int shard_id = 0;
  int err_num = 0;
  int n = 0;
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
      hinted[i] = build_hinted_query (st->batch_sql[i], -1);
      if (hinted[i] == NULL)
	{
	  err_num++;
	  goto done;
	}
    }

  memset (tester_err_msg, 0, sizeof (tester_err_msg));
  gettimeofday (&start_time, NULL);

  /* the return value is the number of results and qr stays NULL when the server
   * reported none, so never walk qr by the statement count */
  n = cci_execute_batch (conn_handle, st->batch_count, hinted, &qr, &err_buf);
  if (n < 0)
    {
      PRINT_CCI_ERROR ("ERROR CODE : %d\n%s\n\n", err_buf.err_code, err_buf.err_msg);
      err_num++;
    }
  else if (qr == NULL)
    {
      fprintf (stderr, "no query result returned for %d batched statement(s)\n", st->batch_count);
      err_num++;
    }
  else
    {
      get_time (&start_time, time, sizeof (time));

      if (n != st->batch_count)
	{
	  fprintf (stderr, "warning: %d result(s) for %d batched statement(s)\n", n, st->batch_count);
	}

      if (br_tester_info.shard_flag == ON)
	{
	  cci_get_shard_id_with_con_handle (conn_handle, &shard_id, &err_buf);
	}

      /* the batch is a single round trip, so every statement shares the time */
      for (i = 1; i <= n; i++)
	{
	  int err_no = CCI_QUERY_RESULT_ERR_NO (qr, i);

	  print_result ((err_no == 0) ? CCI_QUERY_RESULT_RESULT (qr, i) : -1, err_no, shard_flag, shard_id, time,
			st->batch_sql[i - 1]);
	}

      print_batch_result (qr, n);
    }

done:
  if (qr != NULL)
    {
      cci_query_result_free (qr, n);
    }
  cci_end_tran (conn_handle, CCI_TRAN_ROLLBACK, &err_buf);

  for (i = 0; i < st->batch_count; i++)
    {
      FREE_MEM (hinted[i]);
    }
  FREE_MEM (hinted);

  return (err_num > 0) ? -1 : 0;
}

/* Header and values are emitted per execution, and only when an OUT exists. */
static void
print_out_params (T_BR_STMT * st, int req, int call_seq)
{
  T_BR_BIND **set;
  int nbind;
  int num_out = 0;
  int i;

  if (!br_tester_info.verbose_mode || st->num_sets < 1)
    {
      return;
    }

  set = st->sets[0];
  nbind = st->set_nbind[0];

  for (i = 0; i < nbind; i++)
    {
      if (set[i]->param_mode == BR_PM_OUT || set[i]->param_mode == BR_PM_INOUT)
	{
	  num_out++;
	}
    }

  if (num_out == 0)
    {
      return;
    }

  PRINT_RESULT ("<Result of CALL Command>\n");

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

/* result and err_no are already in the result table; only the message is added. */
static void
print_batch_result (T_CCI_QUERY_RESULT * qr, int k)
{
  int num_err = 0;
  int i;

  if (!br_tester_info.verbose_mode)
    {
      return;
    }

  for (i = 1; i <= k; i++)
    {
      if (CCI_QUERY_RESULT_ERR_NO (qr, i) != 0)
	{
	  num_err++;
	}
    }

  if (num_err == 0)
    {
      return;
    }

  PRINT_RESULT ("<Result of BATCH Command>\n");

  for (i = 1; i <= k; i++)
    {
      if (CCI_QUERY_RESULT_ERR_NO (qr, i) != 0)
	{
	  PRINT_RESULT ("  [stmt %d] %s\n", i, CCI_QUERY_RESULT_ERR_MSG (qr, i));
	}
    }
}

static int
flush_current (int conn_handle, T_BR_STMT * st, int shard_flag)
{
  switch (st->mode)
    {
    case BR_EXEC_CALL:
      return flush_call (conn_handle, st, shard_flag);

    case BR_EXEC_BATCH:
      return flush_batch (conn_handle, st, shard_flag);

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
  bool skip_next_query = false;
  bool skip_binds = false;
  int err_num = 0;
  int rc;

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

  while ((rc = tester_get_line (file, linebuf)) == 0)
    {
      char *line = strip_caslog_prefix (t_string_str (linebuf));
      char *hash;
      int kind;

      /* classify first: a bind line keeps a '#' that is glued to the value */
      trim (line);
      if (line[0] == '\0')
	{
	  continue;
	}

      kind = classify_line (line);

      hash = find_line_comment (line, (kind == BR_LINE_BIND));
      if (hash != NULL)
	{
	  *hash = '\0';
	  trim (line);
	  if (line[0] == '\0')
	    {
	      continue;
	    }
	  kind = classify_line (line);
	}

      if (in_batch)
	{
	  if (is_batch_end (line))
	    {
	      if (flush_batch (conn_handle, &st, shard_flag) < 0)
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
	  skip_binds = false;

	  if (is_batch_end (line))
	    {
	      fprintf (stderr, "@batch end without @batch begin\n");
	      err_num++;
	      continue;
	    }

	  /* flush the completed statement before the new directive */
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
	      /* the query it belongs to is skipped, so a reported statement is
	       * never executed */
	      fprintf (stderr, "only one prefix directive is allowed per query\n");
	      err_num++;
	      stmt_reset (&st);
	      pending_directive = false;
	      skip_next_query = true;
	      continue;
	    }

	  if (parse_directive (line, &st) < 0)
	    {
	      err_num++;
	      stmt_reset (&st);
	      skip_next_query = true;
	    }
	  else
	    {
	      pending_directive = true;
	    }
	  continue;
	}

      if (kind == BR_LINE_BIND)
	{
	  if (skip_binds)
	    {
	      continue;
	    }
	  if (st.query == NULL)
	    {
	      fprintf (stderr, "bind line has no preceding query: %s\n", line);
	      err_num++;
	    }
	  else if (parse_bind_line (line, &st) < 0)
	    {
	      /* drop the statement: flushing it without the rejected bind would
	       * report a second, misleading error */
	      err_num++;
	      stmt_reset (&st);
	      skip_binds = true;
	    }
	  continue;
	}

      /* a pending directive, if any, applies to this query */
      if (st.query != NULL)
	{
	  if (flush_current (conn_handle, &st, shard_flag) < 0)
	    {
	      err_num++;
	    }
	  stmt_reset (&st);
	}

      if (skip_next_query)
	{
	  /* the bind lines of the skipped query must go with it, otherwise they
	   * are reported as having no preceding query */
	  skip_next_query = false;
	  skip_binds = true;
	  pending_directive = false;
	  continue;
	}
      skip_binds = false;

      st.query = strdup (line);
      if (st.query == NULL)
	{
	  fprintf (stderr, "malloc error\n");
	  err_num++;
	}
      pending_directive = false;
    }

  if (rc == -2)
    {
      fprintf (stderr, "failed to read input file %s\n", br_tester_info.input_file_name);
      err_num++;
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

  /* keep stdout line buffered even when redirected, so the result rows stay
   * interleaved with the error messages stderr writes unbuffered */
  setvbuf (stdout, NULL, _IOLBF, 0);

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
      /* direct mode skips the master shm lookup, so there is no default db name and
       * no shard info. An empty user is resolved to PUBLIC by the server. */
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

  /* the error count must not become the exit status: it is truncated to 8 bits,
   * so a multiple of 256 would be reported as success */
  return (ret < 0) ? 1 : 0;
}
