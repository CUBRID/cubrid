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
 * checksumdb.c - Main for checksum database
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "authenticate.h"
#include "error_code.h"
#include "system_parameter.h"
#include "message_catalog.h"
#include "db.h"
#include "utility.h"
#include "parser.h"
#include "object_print.h"
#include "schema_manager.h"
#include "transaction_cl.h"
#include "util_func.h"
#include "client_support.h"
#include "connection_support.h"
#include "environment_variable.h"
#include "network_interface_cl.h"
#include "locator_cl.h"
#include "db_value_printer.hpp"
#include "mem_block.hpp"
#include "string_buffer.hpp"
#include "dbtype.h"

#define CHKSUM_DEFAULT_LIST_SIZE	10
#define CHKSUM_MIN_CHUNK_SIZE		100
#define CHKSUM_DEFAULT_TABLE_NAME	"db_ha_checksum"
#define CHKSUM_SCHEMA_TABLE_SUFFIX	"_schema"

#define CHKSUM_TABLE_CLASS_NAME_COL	"class_name"
#define CHKSUM_TABLE_CHUNK_ID_COL	"chunk_id"
#define CHKSUM_TABLE_LOWER_BOUND_COL	"chunk_lower_bound"
#define CHKSUM_TABLE_CHUNK_CHECKSUM_COL	"chunk_checksum"
#define CHKSUM_TABLE_COUNT_COL		"chunk_count"
#define CHKSUM_TABLE_MASTER_CHEKSUM_COL	"master_checksum"
#define CHKSUM_TABLE_BEGINS_AT_COL	"begins_at"
#define CHKSUM_TABLE_ELAPSED_TIME_COL	"elapsed_time"

#define CHKSUM_TABLE_MASTER_SCHEMA_COL  "master_schema_def"
#define CHKSUM_TABLE_SCHEMA_COL 	"schema_def"
#define CHKSUM_TABLE_SCHEMA_TIME_COL	"collected_time"
#define CHKSUM_TABLE_SCHEMA_REPID_COL	"representation_id"

#define CHKSUM_STOP_ON_ERROR(err, arg) (((err) != NO_ERROR) && \
      ((ER_IS_SERVER_DOWN_ERROR(err) == true) || \
      ((arg)->cont_on_err == false)))

#define CHKSUM_PRINT_AND_LOG(fp, ...) \
  do {\
    fprintf(stdout, __VA_ARGS__);\
    fprintf(fp, __VA_ARGS__);\
  }while (0)

typedef struct chksum_result CHKSUM_RESULT;
struct chksum_result
{
  char class_name[SM_MAX_IDENTIFIER_LENGTH];
  char *last_lower_bound;
  int last_chunk_id;
  int last_chunk_cnt;
  CHKSUM_RESULT *next;
};

typedef struct chksum_arg CHKSUM_ARG;
struct chksum_arg
{
  int chunk_size;
  int sleep_msecs;
  int timeout_msecs;
  bool resume;
  bool cont_on_err;
  bool schema_only;
  dynamic_array *include_list;
  dynamic_array *exclude_list;
};

CHKSUM_RESULT *chksum_Prev_results = NULL;
char chksum_result_Table_name[SM_MAX_IDENTIFIER_LENGTH];
char chksum_schema_Table_name[SM_MAX_IDENTIFIER_LENGTH];

static int chksum_drop_and_create_checksum_table (void);
static int chksum_init_checksum_tables (bool resume);
static int chksum_get_prev_checksum_results (void);
static CHKSUM_RESULT *chksum_get_checksum_result (const char *table_name);
static void chksum_free_results (CHKSUM_RESULT * results);
static bool chksum_need_skip_table (const char *table_name, CHKSUM_ARG * chksum_arg);
static int chksum_set_initial_chunk_id_and_lower_bound (PARSER_CONTEXT * parser, const char *table_name,
							DB_CONSTRAINT * pk_cons, int *chunk_id,
							PARSER_VARCHAR ** lower_bound);
static PARSER_VARCHAR *chksum_print_pk_list (PARSER_CONTEXT * parser, DB_CONSTRAINT * pk, int *pk_col_cnt,
					     bool include_decs);
static PARSER_VARCHAR *chksum_print_select_last_chunk (PARSER_CONTEXT * parser, const char *table_name,
						       PARSER_VARCHAR * pk_list, PARSER_VARCHAR * pk_orderby,
						       PARSER_VARCHAR * prev_lower_bound, int limit);
static PARSER_VARCHAR *chksum_print_checksum_query (PARSER_CONTEXT * parser, const char *table_name,
						    DB_ATTRIBUTE * attributes, PARSER_VARCHAR * lower_bound,
						    int chunk_id, int chunk_size);
static PARSER_VARCHAR *chksum_print_lower_bound_string (PARSER_CONTEXT * parser, DB_VALUE values[], DB_CONSTRAINT * pk,
							int pk_col_cnt);
static PARSER_VARCHAR *chksum_get_quote_escaped_lower_bound (PARSER_CONTEXT * parser,
							     PARSER_VARCHAR * orig_lower_bound);
static PARSER_VARCHAR *chksum_print_attribute_list (PARSER_CONTEXT * parser, DB_ATTRIBUTE * attributes);
static PARSER_VARCHAR *chksum_get_next_lower_bound (PARSER_CONTEXT * parser, const char *table_name,
						    DB_CONSTRAINT * pk_cons, PARSER_VARCHAR * prev_lower_bound,
						    int chunk_size, int *exec_error);
static PARSER_VARCHAR *chksum_get_initial_lower_bound (PARSER_CONTEXT * parser, const char *table_name,
						       DB_CONSTRAINT * pk_cons, int *exec_error);
static PARSER_VARCHAR *chksum_print_select_master_checksum (PARSER_CONTEXT * parser, const char *table_name,
							    int chunk_id);
static PARSER_VARCHAR *chksum_print_update_master_checksum (PARSER_CONTEXT * parser, const char *table_name,
							    int chunk_id, int master_checksum);
static int chksum_update_master_checksum (PARSER_CONTEXT * parser, const char *table_name, int chunk_id);
static int chksum_set_repl_info_and_demote_table_lock (const char *table_name, const char *checksum_query,
						       const OID * class_oidp);
static int chksum_update_current_schema_definition (const char *table_name, int repid);
static int chksum_insert_schema_definition (const char *table_name, int repid);
static int chksum_calculate_checksum (PARSER_CONTEXT * parser, const OID * class_oidp, const char *table_name,
				      DB_ATTRIBUTE * attributes, PARSER_VARCHAR * lower_bound, int chunk_id,
				      int chunk_size);
static int chksum_start (CHKSUM_ARG * chksum_arg);
static int chksum_report (const char *command_name, const char *database);
static int chksum_report_summary (FILE * fp);
static int chksum_report_diff (FILE * fp);
static int chksum_report_schema_diff (FILE * fp);
static void chksum_report_header (FILE * fp, const char *database);
static FILE *chksum_report_open_file (const char *command_name);


static FILE *
chksum_report_open_file (const char *command_name)
{
  FILE *fp;
  char file_name[PATH_MAX];
  char file_path[PATH_MAX];

  snprintf (file_name, PATH_MAX, "%s_report.log", command_name);
  envvar_logdir_file (file_path, PATH_MAX, file_name);

  fp = fopen (file_path, "a");

  return fp;
}

static void
chksum_report_header (FILE * fp, const char *database)
{
  time_t report_time;
  struct tm *report_tm_p;
  HA_SERVER_STATE state = HA_SERVER_STATE_NA;

  report_time = time (NULL);
  report_tm_p = localtime (&report_time);

  state = css_ha_server_state ();

  CHKSUM_PRINT_AND_LOG (fp, "=================================" "===============================\n");
  CHKSUM_PRINT_AND_LOG (fp, " target DB: %s (state: %s)\n", database, css_ha_server_state_string (state));
  CHKSUM_PRINT_AND_LOG (fp, " report time: %04d-%02d-%02d %02d:%02d:%02d\n", report_tm_p->tm_year + 1900,
			report_tm_p->tm_mon + 1, report_tm_p->tm_mday, report_tm_p->tm_hour, report_tm_p->tm_min,
			report_tm_p->tm_sec);
  CHKSUM_PRINT_AND_LOG (fp, " checksum table name: %s, %s\n", chksum_result_Table_name, chksum_schema_Table_name);
  CHKSUM_PRINT_AND_LOG (fp, "=================================" "===============================\n\n");

  return;
}

static int
chksum_report_schema_diff (FILE * fp)
{
#define QUERY_BUF_SIZE		1024
  DB_QUERY_RESULT *query_result = NULL;
  DB_QUERY_ERROR query_error;
  DB_VALUE out_value;
  int res, error = NO_ERROR;
  char query_buf[QUERY_BUF_SIZE];

  CHKSUM_PRINT_AND_LOG (fp, "------------------------\n");
  CHKSUM_PRINT_AND_LOG (fp, " different table schema\n");
  CHKSUM_PRINT_AND_LOG (fp, "------------------------\n");

  snprintf (query_buf, sizeof (query_buf),
	    "SELECT " CHKSUM_TABLE_CLASS_NAME_COL ", " CHKSUM_TABLE_SCHEMA_TIME_COL ", " CHKSUM_TABLE_SCHEMA_COL ", "
	    CHKSUM_TABLE_MASTER_SCHEMA_COL " FROM %s WHERE " CHKSUM_TABLE_SCHEMA_COL " IS NULL OR "
	    CHKSUM_TABLE_SCHEMA_COL " <> " CHKSUM_TABLE_MASTER_SCHEMA_COL, chksum_schema_Table_name);

  res = db_execute (query_buf, &query_result, &query_error);
  if (res > 0)
    {
      int pos, out_val_idx;
      char time_buf[256];

      pos = db_query_first_tuple (query_result);
      while (pos == DB_CURSOR_SUCCESS)
	{
	  out_val_idx = 0;

	  /* class name */
	  error = db_query_get_tuple_value (query_result, out_val_idx++, &out_value);
	  if (error != NO_ERROR)
	    {
	      db_query_end (query_result);
	      return error;
	    }

	  CHKSUM_PRINT_AND_LOG (fp, "<table name>\n%s\n", db_get_string (&out_value));
	  db_value_clear (&out_value);

	  /* collected time */
	  error = db_query_get_tuple_value (query_result, out_val_idx++, &out_value);
	  if (error != NO_ERROR)
	    {
	      db_query_end (query_result);
	      return error;
	    }

	  if (DB_IS_NULL (&out_value))
	    {
	      snprintf (time_buf, sizeof (time_buf), "UNKNOWN");
	    }
	  else
	    {
	      db_datetime_to_string (time_buf, sizeof (time_buf), db_get_datetime (&out_value));
	    }
	  db_value_clear (&out_value);

	  /* current schema */
	  CHKSUM_PRINT_AND_LOG (fp, "<current schema - collected at %s>\n", time_buf);
	  error = db_query_get_tuple_value (query_result, out_val_idx++, &out_value);
	  if (error != NO_ERROR)
	    {
	      db_query_end (query_result);
	      return error;
	    }

	  if (DB_IS_NULL (&out_value))
	    {
	      CHKSUM_PRINT_AND_LOG (fp, "NULL\n");
	    }
	  else
	    {
	      CHKSUM_PRINT_AND_LOG (fp, "%s\n", db_get_string (&out_value));
	    }
	  db_value_clear (&out_value);

	  /* master schema */
	  CHKSUM_PRINT_AND_LOG (fp, "<schema from master>\n");
	  error = db_query_get_tuple_value (query_result, out_val_idx++, &out_value);
	  if (error != NO_ERROR)
	    {
	      db_query_end (query_result);
	      return error;
	    }

	  if (DB_IS_NULL (&out_value))
	    {
	      CHKSUM_PRINT_AND_LOG (fp, "NULL\n");
	    }
	  else
	    {
	      CHKSUM_PRINT_AND_LOG (fp, "%s\n", db_get_string (&out_value));
	    }
	  db_value_clear (&out_value);

	  pos = db_query_next_tuple (query_result);
	  CHKSUM_PRINT_AND_LOG (fp, "\n");
	}
      db_query_end (query_result);

      CHKSUM_PRINT_AND_LOG (fp,
			    "* Due to schema inconsistency, the checksum "
			    "difference of the above table(s) may not be reported.\n");
    }
  else if (res == 0)
    {
      CHKSUM_PRINT_AND_LOG (fp, "NONE\n\n");
      db_query_end (query_result);
    }
  else
    {
      error = res;
      CHKSUM_PRINT_AND_LOG (fp, "ERROR\n\n");
    }

  return error;
#undef QUERY_BUF_SIZE
}

static int
chksum_report_diff (FILE * fp)
{
#define QUERY_BUF_SIZE		1024
  DB_QUERY_RESULT *query_result = NULL;
  DB_QUERY_ERROR query_error;
  DB_VALUE out_value;
  int res, error = NO_ERROR;
  char query_buf[QUERY_BUF_SIZE];

  CHKSUM_PRINT_AND_LOG (fp, "-------------------------------" "---------------------------------\n");
  CHKSUM_PRINT_AND_LOG (fp, "table name\tdiff chunk id\tchunk lower bound\n");
  CHKSUM_PRINT_AND_LOG (fp, "-------------------------------" "---------------------------------\n");

  snprintf (query_buf, sizeof (query_buf),
	    "SELECT " CHKSUM_TABLE_CLASS_NAME_COL ", " CHKSUM_TABLE_CHUNK_ID_COL ", " CHKSUM_TABLE_LOWER_BOUND_COL
	    " FROM %s WHERE " CHKSUM_TABLE_MASTER_CHEKSUM_COL " <> " CHKSUM_TABLE_CHUNK_CHECKSUM_COL " OR "
	    CHKSUM_TABLE_CHUNK_CHECKSUM_COL " IS NULL", chksum_result_Table_name);

  res = db_execute (query_buf, &query_result, &query_error);
  if (res > 0)
    {
      int pos, out_val_idx;

      pos = db_query_first_tuple (query_result);
      while (pos == DB_CURSOR_SUCCESS)
	{
	  out_val_idx = 0;

	  /* class name */
	  error = db_query_get_tuple_value (query_result, out_val_idx++, &out_value);
	  if (error != NO_ERROR)
	    {
	      db_query_end (query_result);
	      return error;
	    }
	  CHKSUM_PRINT_AND_LOG (fp, "%-15s ", db_get_string (&out_value));
	  db_value_clear (&out_value);

	  /* chunk id */
	  error = db_query_get_tuple_value (query_result, out_val_idx++, &out_value);
	  if (error != NO_ERROR)
	    {
	      db_query_end (query_result);
	      return error;
	    }
	  CHKSUM_PRINT_AND_LOG (fp, "%-15d ", db_get_int (&out_value));
	  db_value_clear (&out_value);

	  /* lower bound */
	  error = db_query_get_tuple_value (query_result, out_val_idx++, &out_value);
	  if (error != NO_ERROR)
	    {
	      db_query_end (query_result);
	      return error;
	    }
	  CHKSUM_PRINT_AND_LOG (fp, "%s\n", db_get_string (&out_value));
	  db_value_clear (&out_value);

	  pos = db_query_next_tuple (query_result);
	}
      CHKSUM_PRINT_AND_LOG (fp, "\n");
      db_query_end (query_result);
    }
  else if (res == 0)
    {
      CHKSUM_PRINT_AND_LOG (fp, "NONE\n\n");
      db_query_end (query_result);
    }
  else
    {
      error = res;
      CHKSUM_PRINT_AND_LOG (fp, "ERROR\n\n");
    }

  return error;
#undef QUERY_BUF_SIZE
}

static int
chksum_report_summary (FILE * fp)
{
#define QUERY_BUF_SIZE		1024
  DB_QUERY_RESULT *query_result = NULL;
  DB_QUERY_ERROR query_error;
  DB_VALUE out_value;
  int num_chunks;
  int res, error = NO_ERROR;
  char query_buf[QUERY_BUF_SIZE];

  CHKSUM_PRINT_AND_LOG (fp,
			"-------------------------------------------------" "-------------------------------------\n");
  CHKSUM_PRINT_AND_LOG (fp, "table name\ttotal # of chunks\t# of diff chunks\t" "total/avg/min/max time\n");
  CHKSUM_PRINT_AND_LOG (fp,
			"-------------------------------------------------" "-------------------------------------\n");

  // *INDENT-OFF*
  snprintf (query_buf, sizeof (query_buf),
        "SELECT "
          CHKSUM_TABLE_CLASS_NAME_COL ", "
          "CAST (COUNT (*) AS INTEGER), "
          "CAST (COUNT ("
              "CASE WHEN " CHKSUM_TABLE_MASTER_CHEKSUM_COL " <> " CHKSUM_TABLE_CHUNK_CHECKSUM_COL " "
                         "OR " CHKSUM_TABLE_CHUNK_CHECKSUM_COL " IS NULL "
                         "THEN 1 "
              "END"
            ") AS INTEGER), "
          "SUM (" CHKSUM_TABLE_ELAPSED_TIME_COL "), "
          "MIN (" CHKSUM_TABLE_ELAPSED_TIME_COL "), "
          "MAX (" CHKSUM_TABLE_ELAPSED_TIME_COL ") "
        "FROM "
          "%s "
        "GROUP BY "
          CHKSUM_TABLE_CLASS_NAME_COL,
        chksum_result_Table_name);
  // *INDENT-ON*

  res = db_execute (query_buf, &query_result, &query_error);
  if (res > 0)
    {
      int pos, error, out_val_idx;

      pos = db_query_first_tuple (query_result);
      while (pos == DB_CURSOR_SUCCESS)
	{
	  out_val_idx = 0;

	  /* class_name */
	  error = db_query_get_tuple_value (query_result, out_val_idx++, &out_value);
	  if (error != NO_ERROR)
	    {
	      db_query_end (query_result);
	      return error;
	    }
	  CHKSUM_PRINT_AND_LOG (fp, "%-15s ", db_get_string (&out_value));
	  db_value_clear (&out_value);

	  /* total num of chunks */
	  error = db_query_get_tuple_value (query_result, out_val_idx++, &out_value);
	  if (error != NO_ERROR)
	    {
	      db_query_end (query_result);
	      return error;
	    }
	  num_chunks = db_get_int (&out_value);
	  CHKSUM_PRINT_AND_LOG (fp, "%-23d ", num_chunks);
	  db_value_clear (&out_value);

	  /* total num of diff chunk */
	  error = db_query_get_tuple_value (query_result, out_val_idx++, &out_value);
	  if (error != NO_ERROR)
	    {
	      db_query_end (query_result);
	      return error;
	    }
	  CHKSUM_PRINT_AND_LOG (fp, "%-23d ", db_get_int (&out_value));
	  db_value_clear (&out_value);

	  /* total elapsed time */
	  error = db_query_get_tuple_value (query_result, out_val_idx++, &out_value);
	  if (error != NO_ERROR)
	    {
	      db_query_end (query_result);
	      return error;
	    }

	  CHKSUM_PRINT_AND_LOG (fp, "%d / %d ", db_get_int (&out_value), db_get_int (&out_value) / num_chunks);
	  db_value_clear (&out_value);

	  /* min elapsed time */
	  error = db_query_get_tuple_value (query_result, out_val_idx++, &out_value);
	  if (error != NO_ERROR)
	    {
	      db_query_end (query_result);
	      return error;
	    }

	  CHKSUM_PRINT_AND_LOG (fp, "/ %d ", db_get_int (&out_value));
	  db_value_clear (&out_value);

	  /* max elapsed time */
	  error = db_query_get_tuple_value (query_result, out_val_idx++, &out_value);
	  if (error != NO_ERROR)
	    {
	      db_query_end (query_result);
	      return error;
	    }

	  CHKSUM_PRINT_AND_LOG (fp, "/ %d (ms)\n", db_get_int (&out_value));
	  db_value_clear (&out_value);

	  pos = db_query_next_tuple (query_result);
	}

      CHKSUM_PRINT_AND_LOG (fp, "\n");
      db_query_end (query_result);
    }
  else if (res == 0)
    {
      CHKSUM_PRINT_AND_LOG (fp, "NONE\n\n");
      db_query_end (query_result);
    }
  else
    {
      error = res;
      CHKSUM_PRINT_AND_LOG (fp, "ERROR\n\n");
    }

  return error;
#undef QUERY_BUF_SIZE
}

static int
chksum_report (const char *command_name, const char *database)
{
  FILE *fp;
  char err_msg[LINE_MAX];
  char *missing_table = NULL;
  int error = NO_ERROR;

  fp = chksum_report_open_file (command_name);
  if (fp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CHKSUM_GENERIC_ERR, 2, "Failed to open a report file", ER_FAILED);
      return ER_FAILED;
    }

  assert (chksum_result_Table_name != NULL && chksum_result_Table_name[0] != '\0');
  if (db_find_class (chksum_result_Table_name) == NULL)
    {
      missing_table = chksum_result_Table_name;
    }
  else if (db_find_class (chksum_schema_Table_name) == NULL)
    {
      missing_table = chksum_schema_Table_name;
    }

  if (missing_table != NULL)
    {
      snprintf (err_msg, sizeof (err_msg), "Cannot find table %s", missing_table);

      error = er_errid ();
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CHKSUM_GENERIC_ERR, 2, err_msg, error);
      return error;
    }

  chksum_report_header (fp, database);

  error = chksum_report_schema_diff (fp);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  error = chksum_report_diff (fp);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  error = chksum_report_summary (fp);

exit:
  fflush (fp);
  fclose (fp);

  return error;
}

/*
 * chksum_init_checksum_tables () -
 * 	- validate and initialize checksum tables
 *   return:
 */
static int
chksum_init_checksum_tables (bool resume)
{
  DB_OBJECT *classobj;
  char err_msg[LINE_MAX];
  int error = NO_ERROR;
  char *invalid_table = NULL;
  char *missing_table = NULL;

  /* check checksum result table */
  classobj = db_find_class (chksum_result_Table_name);
  if (classobj != NULL)
    {
      if (db_get_attribute (classobj, CHKSUM_TABLE_CLASS_NAME_COL) == NULL
	  || db_get_attribute (classobj, CHKSUM_TABLE_CHUNK_ID_COL) == NULL
	  || db_get_attribute (classobj, CHKSUM_TABLE_LOWER_BOUND_COL) == NULL
	  || db_get_attribute (classobj, CHKSUM_TABLE_CHUNK_CHECKSUM_COL) == NULL
	  || db_get_attribute (classobj, CHKSUM_TABLE_COUNT_COL) == NULL
	  || db_get_attribute (classobj, CHKSUM_TABLE_MASTER_CHEKSUM_COL) == NULL
	  || db_get_attribute (classobj, CHKSUM_TABLE_BEGINS_AT_COL) == NULL
	  || db_get_attribute (classobj, CHKSUM_TABLE_ELAPSED_TIME_COL) == NULL)
	{
	  invalid_table = chksum_result_Table_name;
	  error = er_errid ();
	  goto end;
	}
    }
  else if (resume == true)
    {
      /* resumed but no checksum table */
      missing_table = chksum_result_Table_name;
      error = er_errid ();
      goto end;
    }

  /* check checksum schema table */
  classobj = db_find_class (chksum_schema_Table_name);
  if (classobj != NULL)
    {
      if (db_get_attribute (classobj, CHKSUM_TABLE_CLASS_NAME_COL) == NULL
	  || db_get_attribute (classobj, CHKSUM_TABLE_MASTER_SCHEMA_COL) == NULL
	  || db_get_attribute (classobj, CHKSUM_TABLE_SCHEMA_COL) == NULL
	  || db_get_attribute (classobj, CHKSUM_TABLE_SCHEMA_TIME_COL) == NULL
	  || db_get_attribute (classobj, CHKSUM_TABLE_SCHEMA_REPID_COL) == NULL)
	{
	  invalid_table = chksum_schema_Table_name;
	  error = er_errid ();
	  goto end;
	}
    }
  else if (resume == true)
    {
      missing_table = chksum_schema_Table_name;
      error = er_errid ();
      goto end;
    }

  if (resume == false)
    {
      error = chksum_drop_and_create_checksum_table ();
      if (error != NO_ERROR)
	{
	  snprintf (err_msg, sizeof (err_msg), "Failed to drop and create checksum tables");
	}
    }

end:
  if (error != NO_ERROR)
    {
      if (invalid_table != NULL)
	{
	  snprintf (err_msg, sizeof (err_msg), "Invalid checksum table [%s] exists", invalid_table);
	}
      else if (missing_table != NULL && resume == true)
	{
	  snprintf (err_msg, sizeof (err_msg), "Failed to resume calculation. Table [%s] not found", missing_table);
	}

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CHKSUM_GENERIC_ERR, 2, err_msg, error);
    }

  return error;
}

/*
 * chksum_create_table () -
 * 	- create checksum tables
 *   return:
 */
static int
chksum_drop_and_create_checksum_table (void)
{
#define QUERY_BUF_SIZE		2048
  DB_QUERY_RESULT *query_result = NULL;
  DB_QUERY_ERROR query_error;
  int res, error = NO_ERROR;
  char query_buf[QUERY_BUF_SIZE];

  snprintf (query_buf, sizeof (query_buf), "DROP TABLE IF EXISTS %s;"	/* 0 */
	    "CREATE TABLE %s"	/* 1 */
	    "(%s VARCHAR (255) NOT NULL,"	/* 2 */
	    " %s INT NOT NULL,"	/* 3 */
	    " %s VARCHAR,"	/* 4 */
	    " %s INT NOT NULL,"	/* 5 */
	    " %s INT,"		/* 6 */
	    " %s INT,"		/* 7 */
	    " %s DATETIME DEFAULT sys_datetime,"	/* 8 */
	    " %s INT,"		/* 9 */
	    " CONSTRAINT UNIQUE INDEX (%s, %s));"	/* 10, 11 */
	    "DROP TABLE IF EXISTS %s;"	/* 12 */
	    "CREATE TABLE %s"	/* 13 */
	    "(%s VARCHAR (255) NOT NULL,"	/* 14 */
	    " %s INT NOT NULL,"	/* 15 */
	    " %s VARCHAR,"	/* 16 */
	    " %s VARCHAR,"	/* 17 */
	    " %s DATETIME,"	/* 18 */
	    " PRIMARY KEY (%s, %s));",	/* 19, 20 */
	    chksum_result_Table_name,	/* 0 */
	    chksum_result_Table_name,	/* 1 */
	    CHKSUM_TABLE_CLASS_NAME_COL,	/* 2 */
	    CHKSUM_TABLE_CHUNK_ID_COL,	/* 3 */
	    CHKSUM_TABLE_LOWER_BOUND_COL,	/* 4 */
	    CHKSUM_TABLE_COUNT_COL,	/* 5 */
	    CHKSUM_TABLE_CHUNK_CHECKSUM_COL,	/* 6 */
	    CHKSUM_TABLE_MASTER_CHEKSUM_COL,	/* 7 */
	    CHKSUM_TABLE_BEGINS_AT_COL,	/* 8 */
	    CHKSUM_TABLE_ELAPSED_TIME_COL,	/* 9 */
	    CHKSUM_TABLE_CLASS_NAME_COL,	/* 10 */
	    CHKSUM_TABLE_CHUNK_ID_COL,	/* 11 */
	    chksum_schema_Table_name,	/* 12 */
	    chksum_schema_Table_name,	/* 13 */
	    CHKSUM_TABLE_CLASS_NAME_COL,	/* 14 */
	    CHKSUM_TABLE_SCHEMA_REPID_COL,	/* 15 */
	    CHKSUM_TABLE_SCHEMA_COL,	/* 16 */
	    CHKSUM_TABLE_MASTER_SCHEMA_COL,	/* 17 */
	    CHKSUM_TABLE_SCHEMA_TIME_COL,	/* 18 */
	    CHKSUM_TABLE_CLASS_NAME_COL,	/* 19 */
	    CHKSUM_TABLE_SCHEMA_REPID_COL);	/* 20 */

  res = db_execute (query_buf, &query_result, &query_error);
  if (res >= 0)
    {
      db_query_end (query_result);
      error = db_commit_transaction ();
    }
  else
    {
      error = res;
    }

  return error;

#undef QUERY_BUF_SIZE
}


/*
 * chksum_free_results () -
 * 	- free memory used for previous checksum results
 *   return:
 */
static void
chksum_free_results (CHKSUM_RESULT * results)
{
  CHKSUM_RESULT *res, *next_res;

  res = results;
  while (res != NULL)
    {
      next_res = res->next;

      if (res->last_lower_bound != NULL)
	{
	  free_and_init (res->last_lower_bound);
	}

      free_and_init (res);

      res = next_res;
    }

  return;
}

/*
 * chksum_get_checksum_result ()
 * 	- get previous checksum result for a table
 *   return: checksum result
 *   table_name(in): source table name
 */
static CHKSUM_RESULT *
chksum_get_checksum_result (const char *table_name)
{
  CHKSUM_RESULT *res;

  if (chksum_Prev_results == NULL || table_name == NULL)
    {
      return NULL;
    }

  res = chksum_Prev_results;
  while (res != NULL)
    {
      if (strcmp (res->class_name, table_name) == 0)
	{
	  return res;
	}

      res = res->next;
    }

  return NULL;
}

/*
 * chksum_get_prev_checksum_results ()
 * 	- get previous checksum results
 *   return: error
 */
static int
chksum_get_prev_checksum_results (void)
{
#define QUERY_BUF_SIZE		2048

  CHKSUM_RESULT *checksum_result = NULL;
  DB_QUERY_RESULT *query_result = NULL;
  DB_QUERY_ERROR query_error;
  int res, error = NO_ERROR;
  char query_buf[QUERY_BUF_SIZE];
  DB_VALUE value;

  if (chksum_Prev_results != NULL)
    {
      chksum_free_results (chksum_Prev_results);
    }

  snprintf (query_buf, sizeof (query_buf),
	    "SELECT " "C1." CHKSUM_TABLE_CLASS_NAME_COL ", " "C1." CHKSUM_TABLE_CHUNK_ID_COL ", " "C1."
	    CHKSUM_TABLE_LOWER_BOUND_COL ", " "C1." CHKSUM_TABLE_COUNT_COL " FROM " " %s AS C1 INNER JOIN (SELECT "
	    CHKSUM_TABLE_CLASS_NAME_COL ", " "MAX (" CHKSUM_TABLE_CHUNK_ID_COL ") " "AS MAX_ID FROM %s GROUP BY "
	    CHKSUM_TABLE_CLASS_NAME_COL ") C2 " "ON C1." CHKSUM_TABLE_CLASS_NAME_COL " = C2."
	    CHKSUM_TABLE_CLASS_NAME_COL " AND C1." CHKSUM_TABLE_CHUNK_ID_COL " = C2.MAX_ID", chksum_result_Table_name,
	    chksum_result_Table_name);

  res = db_execute (query_buf, &query_result, &query_error);
  if (res >= 0)
    {
      int pos;
      int out_val_idx;
      const char *db_string_p = NULL;

      pos = db_query_first_tuple (query_result);
      while (pos == DB_CURSOR_SUCCESS)
	{
	  out_val_idx = 0;

	  checksum_result = (CHKSUM_RESULT *) malloc (sizeof (CHKSUM_RESULT));
	  if (checksum_result == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (CHKSUM_RESULT));
	      res = ER_OUT_OF_VIRTUAL_MEMORY;
	      break;
	    }

	  memset (checksum_result, 0, sizeof (CHKSUM_RESULT));

	  /* class_name */
	  error = db_query_get_tuple_value (query_result, out_val_idx++, &value);
	  if (error != NO_ERROR)
	    {
	      db_query_end (query_result);
	      return error;
	    }

	  db_string_p = db_get_string (&value);
	  if (db_string_p != NULL)
	    {
	      snprintf (checksum_result->class_name, SM_MAX_IDENTIFIER_LENGTH, "%s", db_string_p);
	    }
	  db_value_clear (&value);

	  /* chunk_id */
	  error = db_query_get_tuple_value (query_result, out_val_idx++, &value);
	  if (error != NO_ERROR)
	    {
	      db_query_end (query_result);
	      return error;
	    }

	  checksum_result->last_chunk_id = db_get_int (&value);
	  db_value_clear (&value);

	  /* chunk_lower_bound */
	  error = db_query_get_tuple_value (query_result, out_val_idx++, &value);
	  if (error != NO_ERROR)
	    {
	      db_query_end (query_result);
	      return error;
	    }

	  db_string_p = db_get_string (&value);
	  if (db_string_p != NULL)
	    {
	      checksum_result->last_lower_bound = strdup (db_string_p);
	    }
	  db_value_clear (&value);

	  /* chunk_count */
	  error = db_query_get_tuple_value (query_result, out_val_idx++, &value);
	  if (error != NO_ERROR)
	    {
	      db_query_end (query_result);
	      return error;
	    }

	  checksum_result->last_chunk_cnt = db_get_int (&value);
	  db_value_clear (&value);

	  checksum_result->next = chksum_Prev_results;
	  chksum_Prev_results = checksum_result;

	  pos = db_query_next_tuple (query_result);
	}
      db_query_end (query_result);
    }
  else
    {
      error = res;
    }

  return error;
#undef QUERY_BUF_SIZE
}

/*
 * chksum_set_intial_chunk_id_and_lower_bound ()
 * 	- set initial values to be used for calculating checksum
 *   return: error
 *   parser(in):
 *   table_name(in): source table name
 *   pk_cons(in): primary key constraint info
 *   chunk_id(out):
 *   lower_bound(out): initial starting point
 */
static int
chksum_set_initial_chunk_id_and_lower_bound (PARSER_CONTEXT * parser, const char *table_name, DB_CONSTRAINT * pk_cons,
					     int *chunk_id, PARSER_VARCHAR ** lower_bound)
{
  CHKSUM_RESULT *prev_result = NULL;
  int error = NO_ERROR;

  assert (lower_bound != NULL);

  *chunk_id = 0;
  *lower_bound = NULL;

  prev_result = chksum_get_checksum_result (table_name);
  if (prev_result != NULL)
    {
      *chunk_id = prev_result->last_chunk_id;

      assert (prev_result->last_lower_bound != NULL && prev_result->last_lower_bound[0] != '\0');

      *lower_bound = pt_append_nulstring (parser, NULL, prev_result->last_lower_bound);
    }
  else
    {
      /* no previous work exists */
      *lower_bound = chksum_get_initial_lower_bound (parser, table_name, pk_cons, &error);
    }

  return error;
}

/*
 * chksum_print_pk_list ()
 * 	- print primary key column list
 *   return: primary key column list
 *   parser(in):
 *   pk(in): primary key info
 *   pk_col_cnt(out):
 *   include_decs(in): include DESC if exists
 */
static PARSER_VARCHAR *
chksum_print_pk_list (PARSER_CONTEXT * parser, DB_CONSTRAINT * pk, int *pk_col_cnt, bool include_decs)
{
  PARSER_VARCHAR *buffer = NULL;
  DB_ATTRIBUTE **pk_attrs = NULL;
  const int *asc_desc = NULL;
  int i;

  if (parser == NULL)
    {
      return NULL;
    }

  pk_attrs = db_constraint_attributes (pk);
  if (pk_attrs == NULL)
    {
      return NULL;
    }

  asc_desc = db_constraint_asc_desc (pk);
  if (asc_desc == NULL)
    {
      return NULL;
    }

  for (i = 0; pk_attrs[i] != NULL; i++)
    {
      if (i > 0)
	{
	  buffer = pt_append_nulstring (parser, buffer, ", ");
	}

      buffer = pt_append_nulstring (parser, buffer, db_attribute_name (pk_attrs[i]));

      if (include_decs == true && asc_desc[i] == 1)
	{
	  buffer = pt_append_nulstring (parser, buffer, " DESC");
	}
    }

  if (pk_col_cnt != NULL)
    {
      *pk_col_cnt = i;
    }

  return buffer;
}

/*
 * chksum_print_select_last_chunk ()
 * 	- print a query to get lower bound condition
 *   return: a query to get lower bound condition
 *   parser(in):
 *   table_name(in): source table name
 *   pk_list(in):
 *   pk_orderby(in): pk column list used for ORDER BY
 *   prev_lower_bound(in):
 *   limit(in): number used for LIMIT
 */
static PARSER_VARCHAR *
chksum_print_select_last_chunk (PARSER_CONTEXT * parser, const char *table_name, PARSER_VARCHAR * pk_list,
				PARSER_VARCHAR * pk_orderby, PARSER_VARCHAR * prev_lower_bound, int limit)
{
  PARSER_VARCHAR *buffer = NULL;
  char limit_str[15];

  if (parser == NULL)
    {
      return NULL;
    }

  sprintf (limit_str, "%d", limit);

  buffer = pt_append_nulstring (parser, buffer, "SELECT ");
  buffer = pt_append_varchar (parser, buffer, pk_list);

  buffer = pt_append_nulstring (parser, buffer, " FROM ");
  buffer = pt_append_nulstring (parser, buffer, table_name);

  if (prev_lower_bound != NULL)
    {
      buffer = pt_append_nulstring (parser, buffer, " WHERE ");
      buffer = pt_append_varchar (parser, buffer, prev_lower_bound);
    }

  buffer = pt_append_nulstring (parser, buffer, " ORDER BY ");
  buffer = pt_append_varchar (parser, buffer, pk_orderby);

  buffer = pt_append_nulstring (parser, buffer, " LIMIT ");
  buffer = pt_append_nulstring (parser, buffer, limit_str);

  return buffer;
}

/*
 * chksum_print_checksum_query ()
 * 	- print a query for calculating checksum
 *   return: a query for calculating checksum
 *   parser(in):
 *   table_name(in): source table name
 *   attributes(in): attributes info
 *   lower_bound(in): starting point of chunk
 *   chunk_id(in):
 *   chunk_size(in):
 */
static PARSER_VARCHAR *
chksum_print_checksum_query (PARSER_CONTEXT * parser, const char *table_name, DB_ATTRIBUTE * attributes,
			     PARSER_VARCHAR * lower_bound, int chunk_id, int chunk_size)
{
  PARSER_VARCHAR *buffer = NULL;
  PARSER_VARCHAR *att_list = NULL;
  PARSER_VARCHAR *escaped_lower_bound = NULL;
  char chunk_id_str[15];
  char chunk_size_str[15];

  assert (parser != NULL);

  sprintf (chunk_id_str, "%d", chunk_id);
  sprintf (chunk_size_str, "%d", chunk_size);


  att_list = chksum_print_attribute_list (parser, attributes);
  escaped_lower_bound = chksum_get_quote_escaped_lower_bound (parser, lower_bound);

  /* query for calculating checksum */
  buffer = pt_append_nulstring (parser, buffer, "REPLACE INTO ");
  buffer = pt_append_nulstring (parser, buffer, chksum_result_Table_name);
  buffer =
    pt_append_nulstring (parser, buffer,
			 "(" CHKSUM_TABLE_CLASS_NAME_COL ", " CHKSUM_TABLE_CHUNK_ID_COL ", "
			 CHKSUM_TABLE_LOWER_BOUND_COL ", " CHKSUM_TABLE_COUNT_COL ", " CHKSUM_TABLE_CHUNK_CHECKSUM_COL
			 ", " CHKSUM_TABLE_BEGINS_AT_COL ") " "SELECT '");
  buffer = pt_append_nulstring (parser, buffer, table_name);
  buffer = pt_append_nulstring (parser, buffer, "', ");
  buffer = pt_append_nulstring (parser, buffer, chunk_id_str);
  buffer = pt_append_nulstring (parser, buffer, ", '");
  buffer = pt_append_varchar (parser, buffer, escaped_lower_bound);
  buffer =
    pt_append_nulstring (parser, buffer,
			 "', " " count (*), " " BIT_XOR (crc32_result), " " SYS_DATETIME " "FROM"
			 " (SELECT CRC32(CONCAT_WS('', ");
  buffer = pt_append_varchar (parser, buffer, att_list);
  buffer = pt_append_nulstring (parser, buffer, ")) AS crc32_result" "  FROM ");
  buffer = pt_append_nulstring (parser, buffer, table_name);
  buffer = pt_append_nulstring (parser, buffer, " WHERE ");
  buffer = pt_append_varchar (parser, buffer, lower_bound);
  buffer = pt_append_nulstring (parser, buffer, " LIMIT ");
  buffer = pt_append_nulstring (parser, buffer, chunk_size_str);
  buffer = pt_append_nulstring (parser, buffer, ");");

  /* query for updating elapsed time */
  buffer = pt_append_nulstring (parser, buffer, " UPDATE ");
  buffer = pt_append_nulstring (parser, buffer, chksum_result_Table_name);
  buffer = pt_append_nulstring (parser, buffer, " SET ");
  buffer =
    pt_append_nulstring (parser, buffer,
			 CHKSUM_TABLE_ELAPSED_TIME_COL " = SYS_DATETIME - " CHKSUM_TABLE_BEGINS_AT_COL " WHERE "
			 CHKSUM_TABLE_CLASS_NAME_COL " = '");
  buffer = pt_append_nulstring (parser, buffer, table_name);
  buffer = pt_append_nulstring (parser, buffer, "' AND " CHKSUM_TABLE_CHUNK_ID_COL " = ");
  buffer = pt_append_nulstring (parser, buffer, chunk_id_str);
  buffer = pt_append_nulstring (parser, buffer, ";");


  return buffer;
}

/*
 * chksum_print_lower_bound_string ()
 * 	- print lower bound string which will be used
 * 	  in WHERE clause of chunking query
 *   return: lower bound string
 *   parser(in):
 *   values(in): value of each attribute
 *   pk(in): primary key info
 *   pk_col_cnt(in): the number of columns in primary key
 */
static PARSER_VARCHAR *
chksum_print_lower_bound_string (PARSER_CONTEXT * parser, DB_VALUE values[], DB_CONSTRAINT * pk, int pk_col_cnt)
{
  PARSER_VARCHAR *buffer = NULL;
  PARSER_VARCHAR *value = NULL;
  DB_ATTRIBUTE **pk_attrs = NULL;
  const int *asc_desc = NULL;
  int i;
  int col_cnt = 0;

  if (parser == NULL)
    {
      return NULL;
    }

  pk_attrs = db_constraint_attributes (pk);
  if (pk_attrs == NULL)
    {
      return NULL;
    }

  asc_desc = db_constraint_asc_desc (pk);
  if (asc_desc == NULL)
    {
      return NULL;
    }

  col_cnt = pk_col_cnt;

  string_buffer sb;
  db_value_printer printer (sb);
  while (col_cnt > 0)
    {
      if (col_cnt < pk_col_cnt)
	{
	  buffer = pt_append_nulstring (parser, buffer, " OR ");
	}

      buffer = pt_append_nulstring (parser, buffer, "(");
      for (i = 0; pk_attrs[i] != NULL && i < col_cnt; i++)
	{
	  if (i > 0)
	    {
	      buffer = pt_append_nulstring (parser, buffer, " AND ");
	    }

	  buffer = pt_append_nulstring (parser, buffer, db_attribute_name (pk_attrs[i]));

	  if (asc_desc[i] == 1)
	    {
	      buffer = pt_append_nulstring (parser, buffer, "<");
	    }
	  else
	    {
	      buffer = pt_append_nulstring (parser, buffer, ">");
	    }

	  if (col_cnt == pk_col_cnt || i < (col_cnt - 1))
	    {
	      buffer = pt_append_nulstring (parser, buffer, "=");
	    }

	  sb.clear ();
	  printer.describe_value (&values[i]);
	  buffer = pt_append_nulstring (parser, buffer, sb.get_buffer ());
	}

      buffer = pt_append_nulstring (parser, buffer, ")");
      col_cnt--;
    }

  return buffer;
}

/*
 * chksum_print_attribute_list ()
 * 	- print comma-separated attributes list
 *   return: attributes list
 *   parser(in):
 *   attributes(in):
 */
static PARSER_VARCHAR *
chksum_print_attribute_list (PARSER_CONTEXT * parser, DB_ATTRIBUTE * attributes)
{
  PARSER_VARCHAR *buffer = NULL;
  DB_ATTRIBUTE *att;

  if (parser == NULL)
    {
      return NULL;
    }

  att = attributes;
  while (att != NULL)
    {
      buffer = pt_append_nulstring (parser, buffer, "`");
      buffer = pt_append_nulstring (parser, buffer, db_attribute_name (att));
      buffer = pt_append_nulstring (parser, buffer, "`");

      att = db_attribute_next (att);
      if (att != NULL)
	{
	  buffer = pt_append_nulstring (parser, buffer, ", ");
	}
    }

  return buffer;
}

/*
 * chksum_get_quote_escaped_lower_bound ()
 * 	- escape single quotes in lower bound string
 *   return: escaped lower bound string to be used in WHERE clause
 *   parser(in):
 *   orig_lower_bound(in):
 */
static PARSER_VARCHAR *
chksum_get_quote_escaped_lower_bound (PARSER_CONTEXT * parser, PARSER_VARCHAR * orig_lower_bound)
{
  PARSER_VARCHAR *buffer = NULL;
  char *start, *end, *pos;
  int length = 0;

  assert (parser != NULL);

  start = (char *) orig_lower_bound->bytes;
  end = (char *) orig_lower_bound->bytes + orig_lower_bound->length;

  while (start < end)
    {
      pos = start;
      while (pos != NULL && pos < end && (*pos) != '\'')
	{
	  pos++;
	}

      /* a quote found */
      if (pos < end)
	{
	  length = pos - start + 1;
	  buffer = pt_append_bytes (parser, buffer, start, length);
	  buffer = pt_append_nulstring (parser, buffer, "'");
	}
      else
	{
	  buffer = pt_append_bytes (parser, buffer, start, end - start);
	}

      start = pos + 1;
    }

  return buffer;
}

/*
 * chksum_get_next_lower_bound ()
 * 	- get a starting point for a next chunk
 *   return: lower bound string to be used in WHERE clause
 *   parser(in):
 *   table_name(in): source table name
 *   pk_cons(in): primary key constraint info
 *   prev_lower_bound(in): previous lower bound
 *   chunk_size(in):
 *   exec_error(out): error
 */
static PARSER_VARCHAR *
chksum_get_next_lower_bound (PARSER_CONTEXT * parser, const char *table_name, DB_CONSTRAINT * pk_cons,
			     PARSER_VARCHAR * prev_lower_bound, int chunk_size, int *exec_error)
{
  DB_QUERY_RESULT *query_result = NULL;
  DB_QUERY_ERROR query_error;
  PARSER_VARCHAR *select_last_chunk = NULL;
  PARSER_VARCHAR *pk_list = NULL;
  PARSER_VARCHAR *pk_orderby = NULL;
  PARSER_VARCHAR *lower_bound_str = NULL;
  DB_VALUE *out_values = NULL;
  char err_msg[LINE_MAX];
  char chunk_size_str[15];
  int pk_col_cnt = 0;
  int res, i;
  const char *query;

  *exec_error = NO_ERROR;

  sprintf (chunk_size_str, "%d", chunk_size);

  pk_list = chksum_print_pk_list (parser, pk_cons, &pk_col_cnt, false);
  pk_orderby = chksum_print_pk_list (parser, pk_cons, NULL, true);

  select_last_chunk =
    chksum_print_select_last_chunk (parser, table_name, pk_list, pk_orderby, prev_lower_bound, chunk_size);

  query = (const char *) pt_get_varchar_bytes (select_last_chunk);
  res = db_execute (query, &query_result, &query_error);

  if (prev_lower_bound != NULL && res < chunk_size)
    {
      /* no more chunk to process */
      db_query_end (query_result);

      return NULL;
    }
  else if (res > 0)
    {
      int pos, error, col_cnt;

      pos = db_query_last_tuple (query_result);
      switch (pos)
	{
	case DB_CURSOR_SUCCESS:
	  col_cnt = db_query_column_count (query_result);
	  assert (col_cnt == pk_col_cnt);

	  out_values = (DB_VALUE *) malloc (sizeof (DB_VALUE) * pk_col_cnt);
	  if (out_values == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_VALUE) * pk_col_cnt);
	      res = ER_OUT_OF_VIRTUAL_MEMORY;
	      break;
	    }

	  error = db_query_get_tuple_valuelist (query_result, pk_col_cnt, out_values);
	  if (error != NO_ERROR)
	    {
	      res = error;
	      break;
	    }

	  lower_bound_str = chksum_print_lower_bound_string (parser, out_values, pk_cons, pk_col_cnt);
	  break;
	case DB_CURSOR_END:
	case DB_CURSOR_ERROR:
	default:
	  res = ER_FAILED;
	  break;
	}
      db_query_end (query_result);
    }

  if (res < 0)
    {
      snprintf (err_msg, LINE_MAX, "Failed to get lower bound condition " "for table %s", table_name);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CHKSUM_GENERIC_ERR, 2, err_msg, res);

      *exec_error = res;
    }

  if (out_values != NULL)
    {
      for (i = 0; i < pk_col_cnt; i++)
	{
	  db_value_clear (&out_values[i]);
	}

      free_and_init (out_values);
    }

  return lower_bound_str;
}

/*
 * chksum_get_initial_lower_bound ()
 * 	- get initial starting point for chunking table
 *   return: lower bound string to be used in WHERE clause
 *   parser(in):
 *   table_name(in): source table name
 *   pk_cons(in): primary key constraint info
 *   exec_error(out): error
 */
static PARSER_VARCHAR *
chksum_get_initial_lower_bound (PARSER_CONTEXT * parser, const char *table_name, DB_CONSTRAINT * pk_cons,
				int *exec_error)
{
  return chksum_get_next_lower_bound (parser, table_name, pk_cons, NULL, 1, exec_error);
}

/*
 * chksum_print_select_master_checksum ()
 * 	- print a query to get the calculated checksum
 *   return: select query
 *   parser(in):
 *   table_name(in): source table name
 *   chunk_id(in):
 */
static PARSER_VARCHAR *
chksum_print_select_master_checksum (PARSER_CONTEXT * parser, const char *table_name, int chunk_id)
{
  PARSER_VARCHAR *buffer = NULL;
  char chunk_id_str[15];

  snprintf (chunk_id_str, sizeof (chunk_id_str), "%d", chunk_id);

  buffer = pt_append_nulstring (parser, buffer, "SELECT chunk_checksum FROM ");
  buffer = pt_append_nulstring (parser, buffer, chksum_result_Table_name);
  buffer = pt_append_nulstring (parser, buffer, " WHERE class_name = '");
  buffer = pt_append_nulstring (parser, buffer, table_name);
  buffer = pt_append_nulstring (parser, buffer, "' AND chunk_id = ");
  buffer = pt_append_nulstring (parser, buffer, chunk_id_str);

  return buffer;
}

/*
 * chksum_print_update_master_checksum () - print update checksum query
 *   return: update checksum query
 *   parser(in):
 *   table_name(in): source table name
 *   master_checksum(in): caculated checksum
 */
static PARSER_VARCHAR *
chksum_print_update_master_checksum (PARSER_CONTEXT * parser, const char *table_name, int chunk_id, int master_checksum)
{
  PARSER_VARCHAR *buffer = NULL;
  char chunk_id_str[15];
  char master_checksum_str[15];

  snprintf (chunk_id_str, sizeof (chunk_id_str), "%d", chunk_id);
  snprintf (master_checksum_str, sizeof (master_checksum_str), "%d", master_checksum);

  buffer = pt_append_nulstring (parser, buffer, "UPDATE /*+ USE_SBR */ ");
  buffer = pt_append_nulstring (parser, buffer, chksum_result_Table_name);
  buffer = pt_append_nulstring (parser, buffer, " SET " CHKSUM_TABLE_MASTER_CHEKSUM_COL " = ");
  buffer = pt_append_nulstring (parser, buffer, master_checksum_str);
  buffer = pt_append_nulstring (parser, buffer, " WHERE " CHKSUM_TABLE_CLASS_NAME_COL " = '");
  buffer = pt_append_nulstring (parser, buffer, table_name);
  buffer = pt_append_nulstring (parser, buffer, "' AND " CHKSUM_TABLE_CHUNK_ID_COL " = ");
  buffer = pt_append_nulstring (parser, buffer, chunk_id_str);

  return buffer;
}

/*
 * chksum_update_master_checksum () - update master checksum
 * 				      to check integrity
 *   return: error code
 *   parser(in):
 *   table_name(in): source table name
 *   chunk_id(in):
 */
static int
chksum_update_master_checksum (PARSER_CONTEXT * parser, const char *table_name, int chunk_id)
{
  PARSER_VARCHAR *update_checksum_query = NULL;
  PARSER_VARCHAR *select_checksum_query = NULL;

  DB_QUERY_RESULT *query_result = NULL;
  DB_QUERY_ERROR query_error;
  DB_VALUE value;

  int master_checksum = 0;
  const char *query;
  int res, error;
  char err_msg[LINE_MAX];

  select_checksum_query = chksum_print_select_master_checksum (parser, table_name, chunk_id);
  if (select_checksum_query == NULL)
    {
      return ER_FAILED;
    }

  query = (const char *) pt_get_varchar_bytes (select_checksum_query);
  res = db_execute (query, &query_result, &query_error);
  if (res >= 0)
    {
      int pos;

      pos = db_query_first_tuple (query_result);

      switch (pos)
	{
	case DB_CURSOR_SUCCESS:
	  error = db_query_get_tuple_value (query_result, 0, &value);
	  if (error != NO_ERROR)
	    {
	      res = error;
	      break;
	    }

	  master_checksum = db_get_int (&value);
	  db_value_clear (&value);
	  break;
	case DB_CURSOR_END:
	case DB_CURSOR_ERROR:
	default:
	  res = ER_FAILED;
	  break;
	}
      db_query_end (query_result);

      update_checksum_query = chksum_print_update_master_checksum (parser, table_name, chunk_id, master_checksum);
      if (update_checksum_query == NULL)
	{
	  return ER_FAILED;
	}

      query = (const char *) pt_get_varchar_bytes (update_checksum_query);
      res = db_execute (query, &query_result, &query_error);
      if (res >= 0)
	{
	  db_query_end (query_result);
	}
    }

  if (res < 0)
    {
      snprintf (err_msg, LINE_MAX, "Failed to update master checksum. " "(table name: %s, chunk id: %d)", table_name,
		chunk_id);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CHKSUM_GENERIC_ERR, 2, err_msg, res);
    }

  return res;
}

/*
 * chksum_set_repl_info_and_demote_table_lock -
 * return: error code
 *
 * table_name(in):
 * checksum_query(in):
 *
 * NOTE: insert replication log and release demote table lock to IS lock
 */
static int
chksum_set_repl_info_and_demote_table_lock (const char *table_name, const char *checksum_query, const OID * class_oidp)
{
  REPL_INFO repl_info;
  REPL_INFO_SBR repl_stmt;

  repl_stmt.statement_type = CUBRID_STMT_INSERT;
  repl_stmt.name = (char *) table_name;
  repl_stmt.stmt_text = (char *) checksum_query;
  repl_stmt.db_user = db_get_user_name ();
  repl_stmt.sys_prm_context = NULL;

  repl_info.repl_info_type = REPL_INFO_TYPE_SBR;
  repl_info.info = (char *) &repl_stmt;

  return chksum_insert_repl_log_and_demote_table_lock (&repl_info, class_oidp);
}

/*
 * chksum_update_my_schema_definition - update schema definition of
 * 					a given table using SBR
 * return: error code
 *
 * table_name(in):
 */
static int
chksum_update_current_schema_definition (const char *table_name, int repid)
{
#define QUERY_BUF_SIZE		2048
  DB_QUERY_RESULT *query_result = NULL;
  DB_QUERY_ERROR query_error;
  int res, error = NO_ERROR;
  char query_buf[QUERY_BUF_SIZE];

  snprintf (query_buf, sizeof (query_buf), "UPDATE /*+ USE_SBR */ %s "	/* 1 */
	    "SET " CHKSUM_TABLE_SCHEMA_COL " = SCHEMA_DEF ('%s'), "	/* 2 */
	    CHKSUM_TABLE_SCHEMA_TIME_COL " = SYS_DATETIME "	/* collected time */
	    "WHERE " CHKSUM_TABLE_CLASS_NAME_COL " = '%s' "	/* 3 */
	    "AND " CHKSUM_TABLE_SCHEMA_REPID_COL " = %d;",	/* 4 */
	    chksum_schema_Table_name,	/* 1 */
	    table_name, table_name, repid);	/* 2, 3, 4 */

  res = db_execute (query_buf, &query_result, &query_error);
  if (res >= 0)
    {
      db_query_end (query_result);
    }
  else
    {
      error = res;
    }

  return error;

#undef QUERY_BUF_SIZE
}

/*
 * chksum_update_my_schema_definition - insert schema definition of
 * 					a given table and replicate it
 * 					through row-based replication
 * return: error code
 *
 * table_name(in):
 * repid(in): only for internal use
 */
static int
chksum_insert_schema_definition (const char *table_name, int repid)
{
#define QUERY_BUF_SIZE		2048
  DB_QUERY_RESULT *query_result = NULL;
  DB_QUERY_ERROR query_error;
  int res, error = NO_ERROR;
  char query_buf[QUERY_BUF_SIZE];

  snprintf (query_buf, sizeof (query_buf), "REPLACE INTO %s " "SELECT '%s', %d, NULL, SCHEMA_DEF ('%s'), NULL;",
	    chksum_schema_Table_name, table_name, repid, table_name);

  res = db_execute (query_buf, &query_result, &query_error);
  if (res >= 0)
    {
      db_query_end (query_result);

      res = chksum_update_current_schema_definition (table_name, repid);
      if (res < 0)
	{
	  error = res;
	}
    }
  else
    {
      error = res;
    }

  return error;

#undef QUERY_BUF_SIZE
}

/*
 * chksum_calculate_checksum () - calculate checksum for a chunk
 *   return: error code
 *   parser(in):
 *   class_oidp(in): source table class oid
 *   table_name(in): source table name
 *   attributes(in): table attributes
 *   lower_bound(in): starting point of chunk
 *   chunk_id(in):
 *   chunk_size(in):
 */
static int
chksum_calculate_checksum (PARSER_CONTEXT * parser, const OID * class_oidp, const char *table_name,
			   DB_ATTRIBUTE * attributes, PARSER_VARCHAR * lower_bound, int chunk_id, int chunk_size)
{
  PARSER_VARCHAR *checksum_query = NULL;
  DB_QUERY_RESULT *query_result = NULL;
  DB_QUERY_ERROR query_error;
  char err_msg[LINE_MAX];
  const char *query;
  int res;
  int error = NO_ERROR;

  checksum_query = chksum_print_checksum_query (parser, table_name, attributes, lower_bound, chunk_id, chunk_size);
  if (checksum_query == NULL)
    {
      return ER_FAILED;
    }

  query = (const char *) pt_get_varchar_bytes (checksum_query);

  /*
   * write replication log first and release all locks
   * to avoid long lock wait of other concurrent clients on active server
   */
  error = chksum_set_repl_info_and_demote_table_lock (table_name, query, class_oidp);
  if (error != NO_ERROR)
    {
      snprintf (err_msg, LINE_MAX,
		"Failed to write a checksum replication log." " (table name: %s, chunk id: %d, lower bound: %s)",
		table_name, chunk_id, (char *) pt_get_varchar_bytes (lower_bound));
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CHKSUM_GENERIC_ERR, 2, err_msg, error);
      return error;
    }

  res = db_execute (query, &query_result, &query_error);
  if (res >= 0)
    {
      db_query_end (query_result);

      res = chksum_update_master_checksum (parser, table_name, chunk_id);
      if (res < 0)
	{
	  error = res;
	}
    }
  else
    {
      snprintf (err_msg, LINE_MAX, "Failed to calculate checksum. " "(table name: %s, chunk id: %d, lower bound: %s)",
		table_name, chunk_id, (char *) pt_get_varchar_bytes (lower_bound));

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CHKSUM_GENERIC_ERR, 2, err_msg, res);
      error = res;
    }

  return error;
}

/*
 * chksum_need_skip_table() -
 *   return: skip or not
 *   table_name(in):
 *   chksum_arg(in):
 */
static bool
chksum_need_skip_table (const char *table_name, CHKSUM_ARG * chksum_arg)
{
  int i;
  bool match_need_skip = false;
  dynamic_array *list = NULL;
  char table_in_list[SM_MAX_IDENTIFIER_LENGTH];

  if (table_name == NULL || (pt_user_specified_name_compare (table_name, chksum_result_Table_name) == 0)
      || (pt_user_specified_name_compare (table_name, chksum_schema_Table_name) == 0))
    {
      return true;
    }

  if (chksum_arg->include_list == NULL && chksum_arg->exclude_list == NULL)
    {
      return false;
    }

  /* cannot have both lists */
  assert (chksum_arg->include_list == NULL || chksum_arg->exclude_list == NULL);

  list = chksum_arg->exclude_list;
  if (list != NULL)
    {
      match_need_skip = true;
    }
  else
    {
      list = chksum_arg->include_list;
    }

  for (i = 0; i < da_size (list); i++)
    {
      da_get (list, i, table_in_list);
      if (strcmp (table_name, table_in_list) == 0)
	{
	  return match_need_skip;
	}
    }

  return !match_need_skip;
}

/*
 * chksum_start() - calculate checksum values
 * 	to check replication integrity
 *   return: error code
 */
static int
chksum_start (CHKSUM_ARG * chksum_arg)
{
  PARSER_CONTEXT *parser = NULL;
  DB_OBJLIST *tbl_list = NULL, *tbl = NULL;
  DB_OBJECT *classobj = NULL;
  DB_CONSTRAINT *constraints = NULL, *pk_cons = NULL;
  DB_ATTRIBUTE *attributes = NULL;
  PARSER_VARCHAR *lower_bound = NULL, *next_lower_bound = NULL;
  OID *class_oidp = NULL;

  char err_msg[LINE_MAX];
  const char *table_name = NULL;
  int error = NO_ERROR;
  int chunk_id = 0;
  int repid = -1;
  int prev_repid = -1;
  bool force_refetch_class_info;

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_CHKSUM_GENERIC_ERR, 2, "checksum calculation started", 0);

  if (chksum_init_checksum_tables (chksum_arg->resume) != NO_ERROR)
    {
      goto exit;
    }

  if (chksum_arg->resume == true)
    {
      error = chksum_get_prev_checksum_results ();
      if (error != NO_ERROR)
	{
	  snprintf (err_msg, LINE_MAX, "Failed to load previous checksum result");
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CHKSUM_GENERIC_ERR, 2, err_msg, error);
	  goto exit;
	}
    }

  tbl_list = db_fetch_all_classes (DB_FETCH_READ);

  /* commit here to invalidate snapshot captured by db_fetch_all_classes */
  error = db_commit_transaction ();
  if (error != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CHKSUM_GENERIC_ERR, 2, "Failed to get the list of tables", error);
      goto exit;
    }

  for (tbl = tbl_list; tbl != NULL; tbl = tbl->next)
    {
      classobj = tbl->op;
      if (db_is_system_class (classobj) || db_is_vclass (classobj))
	{
	  continue;
	}

      table_name = db_get_class_name (classobj);
      if (table_name == NULL)
	{
	  continue;
	}

      if (chksum_need_skip_table (table_name, chksum_arg) == true)
	{
	  continue;
	}

      prev_repid = -1;
      chunk_id = 0;
      lower_bound = NULL;
      error = NO_ERROR;
      force_refetch_class_info = false;

      parser = parser_create_parser ();

      while (true)
	{
	  repid = sm_get_class_repid (classobj);
	  if (repid == -1)
	    {
	      /* the table has been deleted in the middle of calculation */
	      break;
	    }
	  else if (repid != prev_repid || force_refetch_class_info == true)
	    {
	      /* schema has been changed or previous tran aborted */
	      table_name = db_get_class_name (classobj);
	      attributes = db_get_attributes (classobj);
	      if (table_name == NULL || attributes == NULL)
		{
		  error = er_errid ();
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CHKSUM_GENERIC_ERR, 2,
			  "Failed to load table information", error);
		  break;
		}

	      constraints = db_get_constraints (classobj);
	      if (constraints == NULL)
		{
		  /* no primary key */
		  break;
		}

	      pk_cons = db_constraint_find_primary_key (constraints);
	      if (pk_cons == NULL)
		{
		  break;
		}

	      if (prev_repid != repid)
		{
		  error = chksum_insert_schema_definition (table_name, repid);
		  if (error == NO_ERROR)
		    {
		      error = db_commit_transaction ();
		    }

		  if (error != NO_ERROR)
		    {
		      snprintf (err_msg, LINE_MAX, "Failed to update schema definition" " of [%s]", table_name);
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CHKSUM_GENERIC_ERR, 2, err_msg, error);

		      (void) db_abort_transaction ();
		      break;
		    }

		  if (chksum_arg->schema_only == true)
		    {
		      break;
		    }
		  else
		    {
		      /* continue here since commit unlocks the current class and there might be a chance that the
		       * class is modified. */
		      prev_repid = repid;
		      continue;
		    }
		}

	      prev_repid = repid;
	      force_refetch_class_info = false;
	    }

	  if (locator_fetch_class (classobj, DB_FETCH_QUERY_READ) == NULL)
	    {
	      snprintf (err_msg, LINE_MAX, "Failed to acquire a table READ lock for [%s]", table_name);

	      error = er_errid ();
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CHKSUM_GENERIC_ERR, 2, err_msg, error);
	      break;
	    }

	  class_oidp = ws_oid (classobj);

	  tran_set_query_timeout (chksum_arg->timeout_msecs);

	  if (chunk_id == 0 && lower_bound == NULL)
	    {
	      error =
		chksum_set_initial_chunk_id_and_lower_bound (parser, table_name, pk_cons, &chunk_id, &lower_bound);
	      if (error != NO_ERROR)
		{
		  (void) db_abort_transaction ();

		  if (error != ER_INTERRUPTED)
		    {
		      break;
		    }

		  error = NO_ERROR;
		  force_refetch_class_info = true;

		  SLEEP_MILISEC (0, chksum_arg->sleep_msecs);
		  continue;
		}

	      if (lower_bound == NULL)
		{
		  /* no record. abort to unlock class */
		  (void) db_abort_transaction ();
		  break;
		}
	    }

	  assert (lower_bound != NULL);

	  error =
	    chksum_calculate_checksum (parser, class_oidp, table_name, attributes, lower_bound, chunk_id,
				       chksum_arg->chunk_size);
	  if (error != NO_ERROR)
	    {
	      (void) db_abort_transaction ();

	      if (error != ER_INTERRUPTED)
		{
		  break;
		}

	      error = NO_ERROR;
	      force_refetch_class_info = true;

	      SLEEP_MILISEC (0, chksum_arg->sleep_msecs);
	      continue;
	    }

	  next_lower_bound =
	    chksum_get_next_lower_bound (parser, table_name, pk_cons, lower_bound, chksum_arg->chunk_size, &error);
	  if (error != NO_ERROR)
	    {
	      (void) db_abort_transaction ();

	      if (error != ER_INTERRUPTED)
		{
		  break;
		}

	      error = NO_ERROR;
	      force_refetch_class_info = true;

	      SLEEP_MILISEC (0, chksum_arg->sleep_msecs);
	      continue;
	    }

	  lower_bound = next_lower_bound;

	  error = db_commit_transaction ();
	  if (error != NO_ERROR)
	    {
	      break;
	    }

	  if (lower_bound == NULL)
	    {
	      /* move onto the next table */
	      chunk_id = 0;
	      break;
	    }
	  else
	    {
	      chunk_id++;
	    }

	  SLEEP_MILISEC (0, chksum_arg->sleep_msecs);
	}

      parser_free_parser (parser);

      if (CHKSUM_STOP_ON_ERROR (error, chksum_arg) == true)
	{
	  break;
	}

      if (error != NO_ERROR)
	{
	  snprintf (err_msg, sizeof (err_msg), "Table [%s] skipped due to error", table_name);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CHKSUM_GENERIC_ERR, 2, err_msg, error);
	}
    }

exit:
  if (tbl_list != NULL)
    {
      db_objlist_free (tbl_list);
    }

  if (chksum_Prev_results != NULL)
    {
      chksum_free_results (chksum_Prev_results);
    }

  if (error == NO_ERROR)
    {
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_CHKSUM_GENERIC_ERR, 2, "checksum calculation completed", 0);
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CHKSUM_GENERIC_ERR, 2, "checksum calculation terminated", error);
    }

  return error;
}

/*
 * checksumdb() - checksumdb main routine
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
checksumdb (UTIL_FUNCTION_ARG * arg)
{
#if defined (WINDOWS)
  PRINT_AND_LOG_ERR_MSG (msgcat_message
			 (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHECKSUMDB, CHECKSUMDB_MSG_HA_NOT_SUPPORT),
			 basename (arg->argv0));

  return EXIT_FAILURE;
#else /* WINDOWS */
#if defined (CS_MODE)
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *database_name = NULL;
  CHKSUM_ARG chksum_arg;
  char *incl_class_file = NULL;
  char *excl_class_file = NULL;
  char *checksum_table = NULL;
  bool report_only = false;
  HA_SERVER_STATE ha_state = HA_SERVER_STATE_NA;
  int error = NO_ERROR;

  memset (&chksum_arg, 0, sizeof (CHKSUM_ARG));

  if (utility_get_option_string_table_size (arg_map) != 1)
    {
      goto print_checksumdb_usage;
    }

  database_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  if (database_name == NULL)
    {
      goto print_checksumdb_usage;
    }

  if (check_database_name (database_name))
    {
      goto error_exit;
    }

  checksum_table = utility_get_option_string_value (arg_map, CHECKSUM_TABLE_NAME_S, 0);
  if (sm_check_name (checksum_table) > 0)
    {
      snprintf (chksum_result_Table_name, SM_MAX_IDENTIFIER_LENGTH, "%s", checksum_table);
    }
  else
    {
      snprintf (chksum_result_Table_name, SM_MAX_IDENTIFIER_LENGTH, "%s", CHKSUM_DEFAULT_TABLE_NAME);
    }

  if (snprintf (chksum_schema_Table_name, SM_MAX_IDENTIFIER_LENGTH - 1, "%s%s", chksum_result_Table_name,
		CHKSUM_SCHEMA_TABLE_SUFFIX) < 0)
    {
      assert (false);
      goto error_exit;
    }

  report_only = utility_get_option_bool_value (arg_map, CHECKSUM_REPORT_ONLY_S);
  if (report_only == true)
    {
      goto begin;
    }

  incl_class_file = utility_get_option_string_value (arg_map, CHECKSUM_INCLUDE_CLASS_FILE_S, 0);
  excl_class_file = utility_get_option_string_value (arg_map, CHECKSUM_EXCLUDE_CLASS_FILE_S, 0);

  if (incl_class_file != NULL && excl_class_file != NULL)
    {
      /* cannot have both */
      goto print_checksumdb_usage;
    }

  if (incl_class_file != NULL)
    {
      chksum_arg.include_list = da_create (CHKSUM_DEFAULT_LIST_SIZE, SM_MAX_IDENTIFIER_LENGTH);
      if (util_get_table_list_from_file (incl_class_file, chksum_arg.include_list) != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message
				 (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHECKSUMDB, CHECKSUMDB_MSG_INVALID_INPUT_FILE),
				 incl_class_file);
	  goto error_exit;
	}
    }

  if (excl_class_file != NULL)
    {
      chksum_arg.exclude_list = da_create (CHKSUM_DEFAULT_LIST_SIZE, SM_MAX_IDENTIFIER_LENGTH);
      if (util_get_table_list_from_file (excl_class_file, chksum_arg.exclude_list) != NO_ERROR)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message
				 (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHECKSUMDB, CHECKSUMDB_MSG_INVALID_INPUT_FILE),
				 excl_class_file);
	  goto error_exit;
	}
    }

  chksum_arg.chunk_size = utility_get_option_int_value (arg_map, CHECKSUM_CHUNK_SIZE_S);
  if (chksum_arg.chunk_size < CHKSUM_MIN_CHUNK_SIZE)
    {
      goto print_checksumdb_usage;
    }

  chksum_arg.resume = utility_get_option_bool_value (arg_map, CHECKSUM_RESUME_S);

  chksum_arg.schema_only = utility_get_option_bool_value (arg_map, CHECKSUM_SCHEMA_ONLY_S);

  chksum_arg.sleep_msecs = utility_get_option_int_value (arg_map, CHECKSUM_SLEEP_S);
  if (chksum_arg.sleep_msecs < 0)
    {
      chksum_arg.sleep_msecs = 0;
    }

  chksum_arg.timeout_msecs = utility_get_option_int_value (arg_map, CHECKSUM_TIMEOUT_S);
  if (chksum_arg.timeout_msecs < 0)
    {
      chksum_arg.timeout_msecs = 0;
    }

  chksum_arg.cont_on_err = utility_get_option_bool_value (arg_map, CHECKSUM_CONT_ON_ERROR_S);

begin:
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", database_name, arg->command_name);
  er_init (er_msg_file, ER_NEVER_EXIT);

  AU_DISABLE_PASSWORDS ();

  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  if (db_login ("DBA", NULL) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  /* initialize system parameters */
  if (sysprm_load_and_init (database_name, NULL, SYSPRM_LOAD_ALL) != NO_ERROR)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_SERVICE_PROPERTY_FAIL);
      error = ER_FAILED;
      goto error_exit;
    }

  error = db_restart (arg->command_name, TRUE, database_name);
  if (error != NO_ERROR)
    {
      fprintf (stderr, "%s\n", db_error_string (3));
      goto error_exit;
    }

  db_set_lock_timeout (-1);
  db_set_isolation (TRAN_REPEATABLE_READ);

  /* initialize system parameters */
  if (sysprm_load_and_init (database_name, NULL, SYSPRM_LOAD_ALL) != NO_ERROR)
    {
      (void) db_shutdown ();

      error = ER_FAILED;
      goto error_exit;
    }

  if (report_only == true)
    {
      error = chksum_report (arg->command_name, database_name);
    }
  else
    {
      ha_state = css_ha_server_state ();
      if (ha_state != HA_SERVER_STATE_ACTIVE)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message
				 (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHECKSUMDB, CHECKSUMDB_MSG_MUST_RUN_ON_ACTIVE),
				 database_name, css_ha_server_state_string (ha_state));

	  (void) db_shutdown ();

	  error = ER_FAILED;
	  goto error_exit;
	}

      error = chksum_start (&chksum_arg);
    }

  if (error != NO_ERROR)
    {
      (void) db_shutdown ();

      goto error_exit;
    }

  if (chksum_arg.include_list != NULL)
    {
      da_destroy (chksum_arg.include_list);
    }

  if (chksum_arg.exclude_list != NULL)
    {
      da_destroy (chksum_arg.exclude_list);
    }

  (void) db_shutdown ();

  return EXIT_SUCCESS;

print_checksumdb_usage:
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHECKSUMDB, CHECKSUMDB_MSG_USAGE),
	   basename (arg->argv0));
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  if (chksum_arg.include_list != NULL)
    {
      da_destroy (chksum_arg.include_list);
    }

  if (chksum_arg.exclude_list != NULL)
    {
      da_destroy (chksum_arg.exclude_list);
    }

  return EXIT_FAILURE;
#else /* CS_MODE */
  PRINT_AND_LOG_ERR_MSG (msgcat_message
			 (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHECKSUMDB, CHECKSUMDB_MSG_NOT_IN_STANDALONE),
			 basename (arg->argv0));

error_exit:
  return EXIT_FAILURE;
#endif /* !CS_MODE */
#endif /* !WINDOWS */
}
