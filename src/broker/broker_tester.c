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

#include "cas_cci.h"

#if defined(WINDOWS)
#include "broker_wsa_init.h"
#endif /* WINDOWS */

#include "ini_parser.h"

#define TESTER_ERR_MSG_SIZE            1024
#define TIME_BUF_SIZE                  50
#define	MAX_DISPLAY_LENGTH             20

#define DEFAULT_EMPTY_STRING           "\0"
#define DEFAULT_CUB_USER_NAME          "PUBLIC"
#define DEFAULT_ORACLE_USER_NAME       "scott"
#define DEFAULT_ORACLE_PASSWORD        "tiger"
#define DEFAULT_MYSQL_USER_NAME        "root"

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

#define PRINT_RESULT(...)                            \
        do {                                         \
          if (out_file_fp != NULL)                   \
          {                                          \
            fprintf (out_file_fp ,__VA_ARGS__);      \
          }                                          \
          fprintf (stdout, __VA_ARGS__);             \
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

static int init_tester_info (char *broker_name);
static void init_default_conn_info (int appl_server_type);

static int get_master_shm_id (void);
static void get_time (struct timeval *start_time, char *time, int buf_len);

static int execute_test_with_query (int conn_handle, char *query,
				    int shard_flag);
static int execute_test (int conn_handle, int shard_flag);

static void print_usage (void);
static void print_conn_result (char *broker_name, int conn_hd_id);
static void print_shard_result (void);
static void print_title (int shard_flag);
static void print_result (int row_count, int err_code, int shard_flag,
			  int shard_id, char *time, char *query);
static int print_result_set (int req, T_CCI_ERROR * err_buf,
			     T_CCI_COL_INFO * col_info, int col_count);
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

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER,
				  SHM_MODE_MONITOR);
  if (shm_br == NULL)
    {
      fprintf (stderr, "master shared memory open error[0x%x]\n",
	       master_shm_id);
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
      shm_proxy = (T_SHM_PROXY *) uw_shm_open (broker_info_p->proxy_shm_id,
					       SHM_PROXY, SHM_MODE_MONITOR);
      if (shm_proxy == NULL)
	{
	  uw_shm_detach (shm_br);
	  fprintf (stderr, "proxy shared memory open error[0x%x]\n",
		   broker_info_p->proxy_shm_id);
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
	  br_tester_info.db_passwd =
	    strdup (broker_info_p->shard_db_password);
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

    case APPL_SERVER_CAS_ORACLE:
      user_name = DEFAULT_ORACLE_USER_NAME;
      user_password = DEFAULT_ORACLE_PASSWORD;
      break;

    case APPL_SERVER_CAS_MYSQL:
      user_name = DEFAULT_MYSQL_USER_NAME;
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
  INI_TABLE *ini;
  const char *conf_file;
  char conf_file_path[BROKER_PATH_MAX];

  conf_file = envvar_get ("BROKER_CONF_FILE");

  if (conf_file != NULL)
    {
      strncpy (conf_file_path, conf_file, strlen (conf_file));
    }
  else
    {
      get_cubrid_file (FID_CUBRID_BROKER_CONF, conf_file_path,
		       BROKER_PATH_MAX);
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
	  fprintf (stderr, "cannot find [%s] section in conf file %s\n",
		   SECTION_NAME, conf_file_path);
	  ini_parser_free (ini);
	  return -1;
	}

      master_shm_id =
	ini_gethex (ini, SECTION_NAME, "MASTER_SHM_ID", 0, NULL);
      if (master_shm_id <= 0)
	{
	  fprintf (stderr, "cannot find MASTER_SHM_ID in [%s] section\n",
		   SECTION_NAME);
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
  snprintf (time, buf_len, "%ld.%06ld sec", elapsed_time.tv_sec,
	    elapsed_time.tv_usec);
  return;
}

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
	  snprintf (query_with_hint, sizeof (query_with_hint),
		    "%s /*+ shard_id(%d) */ /* broker_tester */", query,
		    shard_id);
	}
      else
	{
	  snprintf (query_with_hint, sizeof (query_with_hint),
		    "%s /* broker_tester */", query);
	}

      gettimeofday (&start_time, NULL);

      req = cci_prepare (conn_handle, query_with_hint, 0, &err_buf);
      if (req < 0)
	{
	  snprintf (tester_err_msg, sizeof (tester_err_msg),
		    "ERROR CODE : %d\n%s\n\n", err_buf.err_code,
		    err_buf.err_msg);
	  ret = -1;
	  err_num++;
	  goto end_tran;
	}

      ret = cci_execute (req, 0, 0, &err_buf);
      if (ret < 0)
	{
	  snprintf (tester_err_msg, sizeof (tester_err_msg),
		    "ERROR CODE : %d\n%s\n\n", err_buf.err_code,
		    err_buf.err_msg);
	  err_num++;
	  goto end_tran;
	}

      if (br_tester_info.shard_flag == ON && br_tester_info.single_shard)
	{
	  int ret;

	  ret = cci_get_shard_id_with_req_handle (req, &shard_id, &err_buf);
	  if (ret < 0)
	    {
	      snprintf (tester_err_msg, sizeof (tester_err_msg),
			"ERROR CODE : %d\n%s\n\n", err_buf.err_code,
			err_buf.err_msg);
	      err_num++;
	      goto end_tran;
	    }
	}

      if (br_tester_info.verbose_mode)
	{
	  col_info = cci_get_result_info (req, &cmd_type, &col_count);
	  if (cmd_type == CUBRID_STMT_SELECT && col_info == NULL)
	    {
	      snprintf (tester_err_msg, sizeof (tester_err_msg),
			"ERROR CODE : %d\n%s\n\n", err_buf.err_code,
			err_buf.err_msg);
	      ret = -1;
	      err_num++;
	    }
	}

    end_tran:
      get_time (&start_time, time, sizeof (time));

      print_result (ret, err_buf.err_code, shard_flag, shard_id, time, query);

      if (ret >= 0
	  && br_tester_info.verbose_mode && cmd_type == CUBRID_STMT_SELECT)
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

static int
execute_test (int conn_handle, int shard_flag)
{
  char query[LINE_MAX];
  char *p;
  int ret = 0;
  int err_num = 0;
  FILE *file = NULL;

  file = fopen (br_tester_info.input_file_name, "r");
  if (file == NULL)
    {
      fprintf (stderr, "cannot open input file %s\n",
	       br_tester_info.input_file_name);
      return -1;
    }

  print_title (shard_flag);

  while (fgets (query, LINE_MAX - 1, file) != NULL)
    {
      trim (query);

      p = strchr (query, '#');

      if (p != NULL)
	{
	  *p = '\0';
	}

      if (query[0] == '\0')
	{
	  continue;
	}

      ret = execute_test_with_query (conn_handle, query, shard_flag);
      if (ret < 0)
	{
	  err_num++;
	}
    }

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
    ("broker_tester <broker_name> [-D <database_name>] [-u <user_name>] [-p <user_password>] [-c <SQL_command>] [-i <input_file>] [-o <output_file>] [-v] [-s]\n");
  printf ("\t-D database-name\n");
  printf ("\t-u alternate user name\n");
  printf ("\t-p password string, give \"\" for none\n");
  printf ("\t-c SQL-command\n");
  printf ("\t-i input-file-name\n");
  printf ("\t-o ouput-file-name\n");
  printf ("\t-v verbose mode\n");
  printf ("\t-s single shard database\n");
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

  PRINT_RESULT ("CONNECT %s DB [%s] USER [%s]\n\n", broker_name,
		br_tester_info.db_name, br_tester_info.db_user);
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
print_result (int row_count, int err_code, int shard_flag, int shard_id,
	      char *time, char *query)
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
print_result_set (int req, T_CCI_ERROR * err_buf, T_CCI_COL_INFO * col_info,
		  int col_count)
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
  T_CCI_U_TYPE *col_type_arr;

  col_size_arr = (int *) malloc (sizeof (int) * col_count);
  if (col_size_arr == NULL)
    {
      fprintf (stderr, "malloc error\n");
      return -1;
    }

  col_type_arr = (T_CCI_U_TYPE *) malloc (sizeof (T_CCI_U_TYPE) * col_count);
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
      col_size_arr[i - 1] =
	MIN (MAX_DISPLAY_LENGTH, CCI_GET_RESULT_INFO_PRECISION (col_info, i));
      col_size_arr[i - 1] = MAX (col_size_arr[i - 1], strlen (col_name));
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
	  PRINT_CCI_ERROR ("ERROR CODE : %d\n%s\n\n", err_buf->err_code,
			   err_buf->err_msg);
	  goto end;
	}

      ret = cci_fetch (req, err_buf);
      if (ret < 0)
	{
	  PRINT_CCI_ERROR ("ERROR CODE : %d\n%s\n\n", err_buf->err_code,
			   err_buf->err_msg);
	  goto end;
	}

      for (i = 1; i < col_count + 1; i++)
	{
	  ret = cci_get_data (req, i, CCI_A_TYPE_STR, &data, &ind);
	  if (ret < 0)
	    {
	      PRINT_CCI_ERROR ("ERROR CODE : %d\n%s\n\n", err_buf->err_code,
			       err_buf->err_msg);
	      goto end;
	    }

	  if (is_number_type (col_type_arr[i - 1]))
	    {
	      PRINT_RESULT ("  %-*s", col_size_arr[i - 1], data);
	    }
	  else
	    {
	      int len = strlen (data) + 3;
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

  ret = init_tester_info (broker_name);
  if (ret < 0)
    {
      return -1;
    }

  snprintf (conn_url, sizeof (conn_url), "cci:cubrid:localhost:%u:%s:::",
	    br_tester_info.broker_port, br_tester_info.db_name);
  conn_handle =
    cci_connect_with_url_ex (conn_url, br_tester_info.db_user,
			     br_tester_info.db_passwd, &err_buf);

  print_conn_result (broker_name, conn_handle);

  if (conn_handle < 0)
    {
      PRINT_CCI_ERROR ("ERROR CODE : %d\n%s\n\n", err_buf.err_code,
		       err_buf.err_msg);
      free_br_tester_info ();
      return -1;
    }

  ret = cci_set_autocommit (conn_handle, CCI_AUTOCOMMIT_FALSE);
  if (ret < 0)
    {
      fprintf (stderr, "cannot set autocommit mode\n");
      goto end;
    }

  if (br_tester_info.output_file_name != NULL)
    {
      out_file_fp = fopen (br_tester_info.output_file_name, "w");
      if (out_file_fp == NULL)
	{
	  fprintf (stderr, "cannot open output file %s\n",
		   br_tester_info.input_file_name);
	  goto end;
	}
    }

  print_shard_result ();

  if (br_tester_info.command != NULL)
    {
      print_title (br_tester_info.shard_flag);

      ret =
	execute_test_with_query (conn_handle, br_tester_info.command,
				 br_tester_info.shard_flag);
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

  free_br_tester_info ();

  return ret;
}
