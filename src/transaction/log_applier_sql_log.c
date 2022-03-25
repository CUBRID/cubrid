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
 *  log_applier_sql_log.c : SQL logging module for log applier
 */

#ident "$Id$"

#include <stdio.h>
#include <assert.h>
#include <time.h>
#include <libgen.h>
#include <errno.h>
#include <unistd.h>
#include "log_applier_sql_log.h"
#include "system_parameter.h"
#include "object_primitive.h"
#include "object_template.h"
#include "object_print.h"
#include "error_manager.h"
#include "parser.h"
#include "work_space.h"
#include "class_object.h"
#include "environment_variable.h"
#include "set_object.h"
#include "schema_manager.h"
#include "dbtype.h"

#include "db_value_printer.hpp"
#include "mem_block.hpp"
#include "string_buffer.hpp"

#define SL_LOG_FILE_MAX_SIZE   \
  (prm_get_integer_value (PRM_ID_HA_SQL_LOG_MAX_SIZE_IN_MB) * 1024 * 1024)
#define FILE_ID_FORMAT  "%d"
#define SQL_ID_FORMAT   "%010u"
#define CATALOG_FORMAT  FILE_ID_FORMAT " | " SQL_ID_FORMAT

typedef struct sl_info SL_INFO;
struct sl_info
{
  int curr_file_id;
  unsigned int last_inserted_sql_id;
};

SL_INFO sl_Info;

static FILE *log_fp;
static FILE *catalog_fp;
static char sql_log_base_path[PATH_MAX];
static char sql_catalog_path[PATH_MAX];

static int sl_write_sql (string_buffer & query, string_buffer * select);
static void sl_print_insert_att_names (string_buffer & strbuf, OBJ_TEMPASSIGN ** assignments, int num_assignments);
static void sl_print_insert_att_values (string_buffer & strbuf, OBJ_TEMPASSIGN ** assignments, int num_assignments);
static int sl_print_pk (string_buffer & strbuf, SM_CLASS * sm_class, DB_VALUE * key);
static void sl_print_midxkey (string_buffer & strbuf, SM_ATTRIBUTE ** attributes, const DB_MIDXKEY * midxkey);
static void sl_print_update_att_set (string_buffer & strbuf, OBJ_TEMPASSIGN ** assignments, int num_assignments);
static void sl_print_att_value (string_buffer & strbuf, const char *att_name, OBJ_TEMPASSIGN ** assignments,
				int num_assignments);
static DB_VALUE *sl_find_att_value (const char *att_name, OBJ_TEMPASSIGN ** assignments, int num_assignments);

static FILE *sl_open_next_file (FILE * old_fp);
static FILE *sl_log_open (void);
static int sl_read_catalog (void);
static int sl_write_catalog (void);
static int create_dir (const char *new_dir);

static char *
trim_single_quote (char *str, size_t len)
{
  if (len < 2 || str[0] != '\'' || str[len - 1] != '\'')
    {
      return str;
    }
  str[len - 1] = '\0';
  return str + 1;
}

static int
sl_print_select (string_buffer & strbuf, SM_CLASS * sm_class, DB_VALUE * key)
{
  strbuf ("SELECT * FROM [%s] WHERE ", sm_ch_name ((MOBJ) sm_class));

  if (sl_print_pk (strbuf, sm_class, key) != NO_ERROR)
    {
      return ER_FAILED;
    }

  strbuf (";");

  return NO_ERROR;
}

static int
sl_write_catalog (void)
{
  if (catalog_fp == NULL)
    {
      if ((catalog_fp = fopen (sql_catalog_path, "r+")) == NULL)
	{
	  catalog_fp = fopen (sql_catalog_path, "w");
	}
    }

  if (catalog_fp == NULL)
    {
      er_log_debug (ARG_FILE_LINE, "Cannot open SQL catalog file: %s", strerror (errno));
      return ER_FAILED;
    }

  fseek (catalog_fp, 0, SEEK_SET);
  fprintf (catalog_fp, CATALOG_FORMAT, sl_Info.curr_file_id, sl_Info.last_inserted_sql_id);

  fflush (catalog_fp);
  fsync (fileno (catalog_fp));

  return NO_ERROR;
}

static int
sl_read_catalog (void)
{
  FILE *read_catalog_fp;
  char info[LINE_MAX];

  read_catalog_fp = fopen (sql_catalog_path, "r");

  if (read_catalog_fp == NULL)
    {
      return sl_write_catalog ();
    }

  if (fgets (info, LINE_MAX, read_catalog_fp) == NULL)
    {
      if (read_catalog_fp != NULL)
	{
	  fclose (read_catalog_fp);
	}
      return ER_FAILED;
    }

  if (sscanf (info, CATALOG_FORMAT, &sl_Info.curr_file_id, &sl_Info.last_inserted_sql_id) != 2)
    {
      fclose (read_catalog_fp);
      return ER_FAILED;
    }

  fclose (read_catalog_fp);
  return NO_ERROR;
}

int
sl_init (const char *db_name, const char *repl_log_path)
{
  char tmp_log_path[PATH_MAX];
  char basename_buf[PATH_MAX];

  memset (&sl_Info, 0, sizeof (sl_Info));

  snprintf (tmp_log_path, PATH_MAX, "%s/sql_log/", repl_log_path);
  create_dir (tmp_log_path);

  strcpy (basename_buf, repl_log_path);
  snprintf (sql_log_base_path, PATH_MAX, "%s/sql_log/%s.sql.log", repl_log_path, basename (basename_buf));
  snprintf (sql_catalog_path, PATH_MAX, "%s/%s_applylogdb.sql.info", repl_log_path, db_name);

  sl_Info.curr_file_id = 0;
  sl_Info.last_inserted_sql_id = 0;

  if (log_fp != NULL)
    {
      fclose (log_fp);
      log_fp = NULL;
    }

  if (catalog_fp != NULL)
    {
      fclose (catalog_fp);
      catalog_fp = NULL;
    }

  if (sl_read_catalog () != NO_ERROR)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

static int
sl_print_pk (string_buffer & strbuf, SM_CLASS * sm_class, DB_VALUE * key)
{
  DB_MIDXKEY *midxkey;
  SM_ATTRIBUTE *pk_att;
  SM_CLASS_CONSTRAINT *pk_cons = classobj_find_class_primary_key (sm_class);

  if (pk_cons == NULL || pk_cons->attributes == NULL || pk_cons->attributes[0] == NULL)
    {
      return ER_FAILED;
    }

  if (DB_VALUE_TYPE (key) == DB_TYPE_MIDXKEY)
    {
      midxkey = db_get_midxkey (key);
      sl_print_midxkey (strbuf, pk_cons->attributes, midxkey);
    }
  else
    {
      pk_att = pk_cons->attributes[0];
      strbuf ("\"%s\"=", pk_att->header.name);
      db_value_printer printer (strbuf);
      printer.describe_value (key);
    }

  return NO_ERROR;
}

static void
sl_print_insert_att_names (string_buffer & strbuf, OBJ_TEMPASSIGN ** assignments, int num_assignments)
{
  if (num_assignments > 0)
    {
      strbuf ("\"%s\"", assignments[0]->att->header.name);
    }
  for (int i = 1; i < num_assignments; i++)
    {
      strbuf (", \"%s\"", assignments[i]->att->header.name);
    }
}

static void
sl_print_insert_att_values (string_buffer & strbuf, OBJ_TEMPASSIGN ** assignments, int num_assignments)
{
  db_value_printer printer (strbuf);

  if (num_assignments > 0)
    {
      printer.describe_value (assignments[0]->variable);
    }

  for (int i = 1; i < num_assignments; i++)
    {
      strbuf += ',';
      printer.describe_value (assignments[i]->variable);
    }
}

/*
 * sl_print_sql_midxkey - print midxkey in the following format.
 *    key1=value1 AND key2=value2 AND ...
 */
static void
sl_print_midxkey (string_buffer & strbuf, SM_ATTRIBUTE ** attributes, const DB_MIDXKEY * midxkey)
{
  int prev_i_index = 0;
  char *prev_i_ptr = NULL;
  DB_VALUE value;

  for (int i = 0; i < midxkey->ncolumns && attributes[i] != NULL; i++)
    {
      if (i > 0)
	{
	  strbuf (" AND ");
	}

      pr_midxkey_get_element_nocopy (midxkey, i, &value, &prev_i_index, &prev_i_ptr);
      strbuf ("\"%s\"=", attributes[i]->header.name);
      db_value_printer printer (strbuf);

      printer.describe_value (&value);
    }
}

static DB_VALUE *
sl_find_att_value (const char *att_name, OBJ_TEMPASSIGN ** assignments, int num_assignments)
{
  for (int i = 0; i < num_assignments; i++)
    {
      if (!strcmp (att_name, assignments[i]->att->header.name))
	{
	  return assignments[i]->variable;
	}
    }

  return NULL;
}

static void
sl_print_att_value (string_buffer & strbuf, const char *att_name, OBJ_TEMPASSIGN ** assignments, int num_assignments)
{
  DB_VALUE *val = sl_find_att_value (att_name, assignments, num_assignments);

  if (val != NULL)
    {
      db_value_printer printer (strbuf);
      printer.describe_value (val);
    }
}

static void
sl_print_update_att_set (string_buffer & strbuf, OBJ_TEMPASSIGN ** assignments, int num_assignments)
{
  db_value_printer printer (strbuf);

  for (int i = 0; i < num_assignments; i++)
    {
      strbuf ("\"%s\"=", assignments[i]->att->header.name);
      printer.describe_value (assignments[i]->variable);
      if (i != num_assignments - 1)
	{
	  strbuf (", ");
	}
    }
}

int
sl_write_insert_sql (DB_OTMPL * inst_tp, DB_VALUE * key)
{
  string_buffer insert_strbuf;

  insert_strbuf ("INSERT INTO [%s](", sm_ch_name ((MOBJ) (inst_tp->class_)));
  sl_print_insert_att_names (insert_strbuf, inst_tp->assignments, inst_tp->nassigns);
  insert_strbuf (") VALUES (");
  sl_print_insert_att_values (insert_strbuf, inst_tp->assignments, inst_tp->nassigns);
  insert_strbuf (");");

  string_buffer select_strbuf;

  if (sl_print_select (select_strbuf, inst_tp->class_, key) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (sl_write_sql (insert_strbuf, &select_strbuf) != NO_ERROR)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

int
sl_write_update_sql (DB_OTMPL * inst_tp, DB_VALUE * key)
{
  int result;

  if (strcmp (sm_ch_name ((MOBJ) (inst_tp->class_)), "db_serial") != 0)
    {
      /* ordinary tables */
      string_buffer update_strbuf;

      update_strbuf ("UPDATE [%s] SET ", sm_ch_name ((MOBJ) (inst_tp->class_)));
      sl_print_update_att_set (update_strbuf, inst_tp->assignments, inst_tp->nassigns);
      update_strbuf (" WHERE ");
      if (sl_print_pk (update_strbuf, inst_tp->class_, key) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      update_strbuf (";");

      string_buffer select_strbuf;

      if (sl_print_select (select_strbuf, inst_tp->class_, key) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      return sl_write_sql (update_strbuf, &select_strbuf);
    }
  else
    {
      /* db_serial */
      DB_VALUE *cur_value = sl_find_att_value ("current_val", inst_tp->assignments, inst_tp->nassigns);
      DB_VALUE *incr_value = sl_find_att_value ("increment_val", inst_tp->assignments, inst_tp->nassigns);

      if (cur_value == NULL || incr_value == NULL)
	{
	  return ER_FAILED;
	}

      DB_VALUE next_value;

      result = numeric_db_value_add (cur_value, incr_value, &next_value);
      if (result != NO_ERROR)
	{
	  return ER_FAILED;
	}

      string_buffer serial_name_strbuf;

      sl_print_att_value (serial_name_strbuf, "name", inst_tp->assignments, inst_tp->nassigns);
      char *serial_name = trim_single_quote ((char *) serial_name_strbuf.get_buffer (), serial_name_strbuf.len ());

      string_buffer alter_strbuf;
      char str_next_value[NUMERIC_MAX_STRING_SIZE];

      alter_strbuf ("ALTER SERIAL [%s] START WITH %s;", serial_name,
		    numeric_db_value_print (&next_value, str_next_value));

      return sl_write_sql (alter_strbuf, NULL);
    }
}

int
sl_write_delete_sql (char *class_name, MOBJ mclass, DB_VALUE * key)
{
  string_buffer delete_strbuf;

  delete_strbuf ("DELETE FROM [%s] WHERE ", class_name);
  if (sl_print_pk (delete_strbuf, (SM_CLASS *) mclass, key) != NO_ERROR)
    {
      return ER_FAILED;
    }
  delete_strbuf (";");

  string_buffer select_strbuf;

  if (sl_print_select (select_strbuf, (SM_CLASS *) mclass, key) != NO_ERROR)
    {
      return ER_FAILED;
    }

  return sl_write_sql (delete_strbuf, &select_strbuf);
}

int
sl_write_statement_sql (char *class_name, char *db_user, int item_type, const char *stmt_text, char *ha_sys_prm)
{
  int error = NO_ERROR;
  char default_ha_prm[LINE_MAX];
  SYSPRM_ERR rc;

  string_buffer statement_strbuf;
  statement_strbuf ("%s;", stmt_text);

  if (ha_sys_prm != NULL)
    {
      rc = sysprm_make_default_values (ha_sys_prm, default_ha_prm, sizeof (default_ha_prm));
      if (rc != PRM_ERR_NO_ERROR)
	{
	  return sysprm_set_error (rc, ha_sys_prm);
	}

      string_buffer setprm_strbuf;

      setprm_strbuf ("%s SET SYSTEM PARAMETERS '%s';", CA_MARK_TRAN_START, ha_sys_prm);	//set param
      if (sl_write_sql (setprm_strbuf, NULL) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      if (sl_write_sql (statement_strbuf, NULL) != NO_ERROR)
	{
	  sl_write_sql (setprm_strbuf, NULL);
	  return ER_FAILED;
	}

      setprm_strbuf.clear ();
      setprm_strbuf ("%s SET SYSTEM PARAMETERS '%s';", CA_MARK_TRAN_END, default_ha_prm);	//restore param
      if (sl_write_sql (setprm_strbuf, NULL) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  else
    {
      if (sl_write_sql (statement_strbuf, NULL) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  if (item_type == CUBRID_STMT_CREATE_CLASS)
    {
      if (db_user != NULL && strlen (db_user) > 0)
	{
	  statement_strbuf.clear ();
	  statement_strbuf ("GRANT ALL PRIVILEGES ON %s TO %s;", class_name, db_user);
	  if (sl_write_sql (statement_strbuf, NULL) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}
    }

  return NO_ERROR;
}

static int
sl_write_sql (string_buffer & query, string_buffer * select)
{
  time_t curr_time;
  char time_buf[20];

  assert (query.get_buffer () != NULL);

  if (log_fp == NULL)
    {
      if ((log_fp = sl_log_open ()) == NULL)
	{
	  return ER_FAILED;
	}
    }

  curr_time = time (NULL);
  strftime (time_buf, sizeof (time_buf), "%Y-%m-%d %H:%M:%S", localtime (&curr_time));

  /* -- datetime | sql_id | is_ddl | select length | query length */
  fprintf (log_fp, "-- %s | %u | %zu | %zu\n", time_buf, ++sl_Info.last_inserted_sql_id,
	   (select == NULL) ? 0 : select->len (), query.len ());

  /* print select for verifying data consistency */
  if (select != NULL)
    {
      /* -- select_length select * from tbl_name */
      fprintf (log_fp, "-- ");
      fwrite (select->get_buffer (), sizeof (char), select->len (), log_fp);
      fputc ('\n', log_fp);
    }

  /* print SQL query */
  fwrite (query.get_buffer (), sizeof (char), query.len (), log_fp);
  fputc ('\n', log_fp);

  fflush (log_fp);

  sl_write_catalog ();

  fseek (log_fp, 0, SEEK_END);
  if (ftell (log_fp) >= SL_LOG_FILE_MAX_SIZE)
    {
      log_fp = sl_open_next_file (log_fp);
    }

  return NO_ERROR;
}

static FILE *
sl_log_open (void)
{
  char cur_sql_log_path[PATH_MAX];
  FILE *fp;

  if (snprintf (cur_sql_log_path, PATH_MAX - 1, "%s.%d", sql_log_base_path, sl_Info.curr_file_id) < 0)
    {
      assert (false);
      return NULL;
    }

  fp = fopen (cur_sql_log_path, "r+");
  if (fp != NULL)
    {
      fseek (fp, 0, SEEK_END);
      if (ftell (fp) >= SL_LOG_FILE_MAX_SIZE)
	{
	  fp = sl_open_next_file (fp);
	}
    }
  else
    {
      fp = fopen (cur_sql_log_path, "w");
    }

  if (fp == NULL)
    {
      er_log_debug (ARG_FILE_LINE, "Failed to open SQL log file (%s): %s", cur_sql_log_path, strerror (errno));
    }

  return fp;
}

static FILE *
sl_open_next_file (FILE * old_fp)
{
  FILE *new_fp;
  char new_file_path[PATH_MAX];

  sl_Info.curr_file_id++;
  sl_Info.last_inserted_sql_id = 0;

  if (snprintf (new_file_path, PATH_MAX - 1, "%s.%d", sql_log_base_path, sl_Info.curr_file_id) < 0)
    {
      assert (false);
      return NULL;
    }

  fclose (old_fp);
  new_fp = fopen (new_file_path, "w");

  if (sl_write_catalog () != NO_ERROR)
    {
      fclose (new_fp);
      return NULL;
    }

  return new_fp;
}

static int
create_dir (const char *new_dir)
{
  char *p, path[PATH_MAX];

  if (new_dir == NULL)
    {
      return ER_FAILED;
    }

  strcpy (path, new_dir);

  p = path;
  if (path[0] == '/')
    {
      p = path + 1;
    }

  while (p != NULL)
    {
      p = strchr (p, '/');
      if (p != NULL)
	{
	  *p = '\0';
	}

      if (access (path, F_OK) < 0)
	{
	  if (mkdir (path, 0777) < 0)
	    {
	      return ER_FAILED;
	    }
	}
      if (p != NULL)
	{
	  *p = '/';
	  p++;
	}
    }
  return NO_ERROR;
}
