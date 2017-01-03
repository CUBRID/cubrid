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
#include "object_template.h"
#include "object_print.h"
#include "error_manager.h"
#include "parser.h"
#include "work_space.h"
#include "class_object.h"
#include "environment_variable.h"
#include "set_object.h"
#include "cci_applier.h"
#include "schema_manager.h"

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

static FILE *sl_open_next_file (FILE * old_fp);
static FILE *sl_log_open (void);
static int sl_write_sql (PARSER_VARCHAR * query, PARSER_VARCHAR * select);
static int sl_read_catalog (void);
static int sl_write_catalog (void);
static void trim_single_quote (PARSER_VARCHAR * name);
static int create_dir (const char *new_dir);

static PARSER_VARCHAR *sl_print_midxkey (const PARSER_CONTEXT * parser, SM_ATTRIBUTE ** attributes,
					 const DB_MIDXKEY * midxkey);
static PARSER_VARCHAR *sl_print_pk (PARSER_CONTEXT * parser, SM_CLASS * sm_class, DB_VALUE * key);
static PARSER_VARCHAR *sl_print_insert_att_values (PARSER_CONTEXT * parser, OBJ_TEMPASSIGN ** assignments,
						   int num_assignments);
static PARSER_VARCHAR *sl_print_insert_att_names (PARSER_CONTEXT * parser, OBJ_TEMPASSIGN ** assignments,
						  int num_assignments);
static PARSER_VARCHAR *sl_print_update_att_set (PARSER_CONTEXT * parser, OBJ_TEMPASSIGN ** assignments,
						int num_assignments);
static DB_VALUE *sl_find_att_value (PARSER_CONTEXT * parser, const char *att_name, OBJ_TEMPASSIGN ** assignments,
				    int num_assignments);
static PARSER_VARCHAR *sl_print_att_value (PARSER_CONTEXT * parser, const char *att_name, OBJ_TEMPASSIGN ** assignments,
					   int num_assignments);
static PARSER_VARCHAR *sl_print_select (const PARSER_CONTEXT * parser, const char *class_name, PARSER_VARCHAR * key);

static int
sl_write_catalog ()
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
sl_read_catalog ()
{
  FILE *catalog_fp;
  char info[LINE_MAX];

  catalog_fp = fopen (sql_catalog_path, "r");

  if (catalog_fp == NULL)
    {
      return sl_write_catalog ();
    }

  if (fgets (info, LINE_MAX, catalog_fp) == NULL)
    {
      return ER_FAILED;
    }

  if (sscanf (info, CATALOG_FORMAT, &sl_Info.curr_file_id, &sl_Info.last_inserted_sql_id) != 2)
    {
      return ER_FAILED;
    }

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

static PARSER_VARCHAR *
sl_print_pk (PARSER_CONTEXT * parser, SM_CLASS * sm_class, DB_VALUE * key)
{
  PARSER_VARCHAR *buffer = NULL;
  PARSER_VARCHAR *value = NULL;
  DB_MIDXKEY *midxkey;
  SM_ATTRIBUTE *pk_att;
  SM_CLASS_CONSTRAINT *pk_cons;

  pk_cons = classobj_find_class_primary_key (sm_class);
  if (pk_cons == NULL || pk_cons->attributes == NULL || pk_cons->attributes[0] == NULL)
    {
      return NULL;
    }

  if (DB_VALUE_TYPE (key) == DB_TYPE_MIDXKEY)
    {
      midxkey = db_get_midxkey (key);
      buffer = sl_print_midxkey (parser, pk_cons->attributes, midxkey);
    }
  else
    {
      pk_att = pk_cons->attributes[0];

      value = describe_value (parser, NULL, key);
      buffer = pt_append_nulstring (parser, buffer, "\"");
      buffer = pt_append_nulstring (parser, buffer, pk_att->header.name);
      buffer = pt_append_nulstring (parser, buffer, "\"");
      buffer = pt_append_nulstring (parser, buffer, "=");
      buffer = pt_append_varchar (parser, buffer, value);
    }
  return buffer;
}

static PARSER_VARCHAR *
sl_print_insert_att_names (PARSER_CONTEXT * parser, OBJ_TEMPASSIGN ** assignments, int num_assignments)
{
  PARSER_VARCHAR *buffer = NULL;
  int i;

  for (i = 0; i < num_assignments; i++)
    {
      buffer = pt_append_nulstring (parser, buffer, "\"");
      buffer = pt_append_nulstring (parser, buffer, assignments[i]->att->header.name);
      buffer = pt_append_nulstring (parser, buffer, "\"");
      if (i != num_assignments - 1)
	{
	  buffer = pt_append_nulstring (parser, buffer, ", ");
	}
    }
  return buffer;
}


static PARSER_VARCHAR *
sl_print_insert_att_values (PARSER_CONTEXT * parser, OBJ_TEMPASSIGN ** assignments, int num_assignments)
{
  PARSER_VARCHAR *buffer = NULL;
  PARSER_VARCHAR *value = NULL;
  int i;

  for (i = 0; i < num_assignments; i++)
    {
      value = describe_value (parser, NULL, assignments[i]->variable);
      buffer = pt_append_varchar (parser, buffer, value);

      if (i != num_assignments - 1)
	{
	  buffer = pt_append_nulstring (parser, buffer, ",");
	}
    }
  return buffer;
}

static PARSER_VARCHAR *
sl_print_select (const PARSER_CONTEXT * parser, const char *class_name, PARSER_VARCHAR * key)
{
  PARSER_VARCHAR *buffer = NULL;
  buffer = pt_append_nulstring (parser, buffer, "SELECT * FROM [");
  buffer = pt_append_nulstring (parser, buffer, class_name);
  buffer = pt_append_nulstring (parser, buffer, "] WHERE ");
  buffer = pt_append_varchar (parser, buffer, key);
  buffer = pt_append_nulstring (parser, buffer, ";");

  return buffer;
}

/*
 *  * sl_print_sql_midxkey -
 *   *      print midxkey in the following format.
 *    *      key1=value1 AND key2=value2 AND ...
 *     */
static PARSER_VARCHAR *
sl_print_midxkey (const PARSER_CONTEXT * parser, SM_ATTRIBUTE ** attributes, const DB_MIDXKEY * midxkey)
{
  int i = 0;
  int prev_i_index;
  char *prev_i_ptr;
  DB_VALUE value;
  PARSER_VARCHAR *buffer = NULL;

  prev_i_index = 0;
  prev_i_ptr = NULL;

  for (i = 0; i < midxkey->ncolumns && attributes[i] != NULL; i++)
    {
      if (i > 0)
	{
	  buffer = pt_append_nulstring (parser, buffer, " AND ");
	}

      pr_midxkey_get_element_nocopy (midxkey, i, &value, &prev_i_index, &prev_i_ptr);

      buffer = pt_append_nulstring (parser, buffer, "\"");
      buffer = pt_append_nulstring (parser, buffer, attributes[i]->header.name);
      buffer = pt_append_nulstring (parser, buffer, "\"");
      buffer = pt_append_nulstring (parser, buffer, "=");
      buffer = describe_value (parser, buffer, &value);
    }

  return buffer;
}

static DB_VALUE *
sl_find_att_value (PARSER_CONTEXT * parser, const char *att_name, OBJ_TEMPASSIGN ** assignments, int num_assignments)
{
  int i;

  for (i = 0; i < num_assignments; i++)
    {
      if (!strcmp (att_name, assignments[i]->att->header.name))
	{
	  return assignments[i]->variable;
	}
    }
  return NULL;
}

static PARSER_VARCHAR *
sl_print_att_value (PARSER_CONTEXT * parser, const char *att_name, OBJ_TEMPASSIGN ** assignments, int num_assignments)
{
  DB_VALUE *val;

  val = sl_find_att_value (parser, att_name, assignments, num_assignments);
  if (val != NULL)
    {
      return describe_value (parser, NULL, val);
    }
  return NULL;
}

static PARSER_VARCHAR *
sl_print_update_att_set (PARSER_CONTEXT * parser, OBJ_TEMPASSIGN ** assignments, int num_assignments)
{
  PARSER_VARCHAR *buffer = NULL;
  PARSER_VARCHAR *value = NULL;
  int i;

  for (i = 0; i < num_assignments; i++)
    {
      value = describe_value (parser, NULL, assignments[i]->variable);
      buffer = pt_append_nulstring (parser, buffer, "\"");
      buffer = pt_append_nulstring (parser, buffer, assignments[i]->att->header.name);
      buffer = pt_append_nulstring (parser, buffer, "\"");
      buffer = pt_append_nulstring (parser, buffer, "=");
      buffer = pt_append_varchar (parser, buffer, value);

      if (i != num_assignments - 1)
	{
	  buffer = pt_append_nulstring (parser, buffer, ", ");
	}
    }
  return buffer;
}

int
sl_write_insert_sql (DB_OTMPL * inst_tp, DB_VALUE * key)
{
  PARSER_CONTEXT *parser;
  PARSER_VARCHAR *buffer = NULL;
  PARSER_VARCHAR *att_names, *att_values;
  PARSER_VARCHAR *pkey;
  PARSER_VARCHAR *select;

  parser = parser_create_parser ();

  att_names = sl_print_insert_att_names (parser, inst_tp->assignments, inst_tp->nassigns);
  att_values = sl_print_insert_att_values (parser, inst_tp->assignments, inst_tp->nassigns);

  buffer = pt_append_nulstring (parser, buffer, "INSERT INTO [");
  buffer = pt_append_nulstring (parser, buffer, sm_ch_name ((MOBJ) (inst_tp->class_)));
  buffer = pt_append_nulstring (parser, buffer, "](");
  buffer = pt_append_varchar (parser, buffer, att_names);
  buffer = pt_append_nulstring (parser, buffer, ") VALUES (");
  buffer = pt_append_varchar (parser, buffer, att_values);
  buffer = pt_append_nulstring (parser, buffer, ");");

  pkey = sl_print_pk (parser, inst_tp->class_, key);
  if (pkey == NULL)
    {
      parser_free_parser (parser);
      return ER_FAILED;
    }

  select = sl_print_select (parser, sm_ch_name ((MOBJ) (inst_tp->class_)), pkey);

  if (sl_write_sql (buffer, select) != NO_ERROR)
    {
      parser_free_parser (parser);
      return ER_FAILED;
    }

  parser_free_parser (parser);
  return NO_ERROR;
}

int
sl_write_update_sql (DB_OTMPL * inst_tp, DB_VALUE * key)
{
  PARSER_CONTEXT *parser;
  PARSER_VARCHAR *buffer = NULL, *select = NULL;
  PARSER_VARCHAR *att_set, *pkey;
  PARSER_VARCHAR *serial_name, *serial_value;
  DB_VALUE *cur_value, *incr_value;
  DB_VALUE next_value;
  char str_next_value[NUMERIC_MAX_STRING_SIZE];
  bool is_db_serial = false;
  int result;

  parser = parser_create_parser ();

  if (strcmp (sm_ch_name ((MOBJ) (inst_tp->class_)), "db_serial") != 0)
    {
      /* ordinary tables */
      att_set = sl_print_update_att_set (parser, inst_tp->assignments, inst_tp->nassigns);
      pkey = sl_print_pk (parser, inst_tp->class_, key);
      if (pkey == NULL)
	{
	  result = ER_FAILED;
	  goto end;
	}

      buffer = pt_append_nulstring (parser, buffer, "UPDATE [");
      buffer = pt_append_nulstring (parser, buffer, sm_ch_name ((MOBJ) (inst_tp->class_)));
      buffer = pt_append_nulstring (parser, buffer, "] SET ");
      buffer = pt_append_varchar (parser, buffer, att_set);
      buffer = pt_append_nulstring (parser, buffer, " WHERE ");
      buffer = pt_append_varchar (parser, buffer, pkey);
      buffer = pt_append_nulstring (parser, buffer, ";");

      select = sl_print_select (parser, sm_ch_name ((MOBJ) (inst_tp->class_)), pkey);

      result = sl_write_sql (buffer, select);
    }
  else
    {
      /* db_serial */
      serial_name = sl_print_att_value (parser, "name", inst_tp->assignments, inst_tp->nassigns);
      trim_single_quote (serial_name);

      cur_value = sl_find_att_value (parser, "current_val", inst_tp->assignments, inst_tp->nassigns);
      incr_value = sl_find_att_value (parser, "increment_val", inst_tp->assignments, inst_tp->nassigns);
      if (cur_value == NULL || incr_value == NULL)
	{
	  result = ER_FAILED;
	  goto end;
	}

      result = numeric_db_value_add (cur_value, incr_value, &next_value);
      if (result != NO_ERROR)
	{
	  goto end;
	}
      numeric_db_value_print (&next_value, str_next_value);

      buffer = pt_append_nulstring (parser, buffer, "ALTER SERIAL [");
      buffer = pt_append_varchar (parser, buffer, serial_name);
      buffer = pt_append_nulstring (parser, buffer, "] START WITH ");
      buffer = pt_append_nulstring (parser, buffer, str_next_value);
      buffer = pt_append_nulstring (parser, buffer, ";");

      result = sl_write_sql (buffer, NULL);
    }

end:
  parser_free_parser (parser);

  if (result != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}

int
sl_write_delete_sql (char *class_name, MOBJ mclass, DB_VALUE * key)
{
  PARSER_CONTEXT *parser;
  PARSER_VARCHAR *buffer = NULL, *pkey;
  PARSER_VARCHAR *select;

  parser = parser_create_parser ();

  pkey = sl_print_pk (parser, (SM_CLASS *) mclass, key);
  if (pkey == NULL)
    {
      parser_free_parser (parser);
      return ER_FAILED;
    }

  buffer = pt_append_nulstring (parser, buffer, "DELETE FROM [");
  buffer = pt_append_nulstring (parser, buffer, class_name);
  buffer = pt_append_nulstring (parser, buffer, "] WHERE ");
  buffer = pt_append_varchar (parser, buffer, pkey);
  buffer = pt_append_nulstring (parser, buffer, ";");

  select = sl_print_select (parser, class_name, pkey);

  if (sl_write_sql (buffer, select) != NO_ERROR)
    {
      parser_free_parser (parser);
      return ER_FAILED;
    }

  parser_free_parser (parser);
  return NO_ERROR;
}

int
sl_write_statement_sql (char *class_name, char *db_user, int item_type, char *stmt_text, char *ha_sys_prm)
{
  int rc, error = NO_ERROR;
  PARSER_CONTEXT *parser;
  PARSER_VARCHAR *buffer = NULL, *grant = NULL;
  PARSER_VARCHAR *set_param = NULL, *restore_param = NULL;
  char default_ha_prm[LINE_MAX];

  parser = parser_create_parser ();

  buffer = pt_append_nulstring (parser, buffer, stmt_text);
  buffer = pt_append_nulstring (parser, buffer, ";");

  if (ha_sys_prm != NULL)
    {
      set_param = pt_append_nulstring (parser, set_param, CA_MARK_TRAN_START);
      set_param = pt_append_nulstring (parser, set_param, " SET SYSTEM PARAMETERS '");
      set_param = pt_append_nulstring (parser, set_param, ha_sys_prm);
      set_param = pt_append_nulstring (parser, set_param, "';");

      rc = sysprm_make_default_values (ha_sys_prm, default_ha_prm, sizeof (default_ha_prm));
      if (rc != PRM_ERR_NO_ERROR)
	{
	  error = sysprm_set_error (rc, ha_sys_prm);
	  goto end;
	}

      restore_param = pt_append_nulstring (parser, restore_param, CA_MARK_TRAN_END);
      restore_param = pt_append_nulstring (parser, restore_param, " SET SYSTEM PARAMETERS '");
      restore_param = pt_append_nulstring (parser, restore_param, default_ha_prm);
      restore_param = pt_append_nulstring (parser, restore_param, "';");

      if (sl_write_sql (set_param, NULL) != NO_ERROR)
	{
	  error = ER_FAILED;
	  goto end;
	}

      if (sl_write_sql (buffer, NULL) != NO_ERROR)
	{
	  sl_write_sql (restore_param, NULL);
	  error = ER_FAILED;
	  goto end;
	}

      if (sl_write_sql (restore_param, NULL) != NO_ERROR)
	{
	  error = ER_FAILED;
	  goto end;
	}
    }
  else
    {
      if (sl_write_sql (buffer, NULL) != NO_ERROR)
	{
	  error = ER_FAILED;
	  goto end;
	}
    }

  if (item_type == CUBRID_STMT_CREATE_CLASS)
    {
      if (db_user != NULL && strlen (db_user) > 0)
	{
	  grant = pt_append_nulstring (parser, grant, "GRANT ALL PRIVILEGES ON ");
	  grant = pt_append_nulstring (parser, grant, class_name);
	  grant = pt_append_nulstring (parser, grant, " TO ");
	  grant = pt_append_nulstring (parser, grant, db_user);
	  grant = pt_append_nulstring (parser, grant, ";");

	  if (sl_write_sql (grant, NULL) != NO_ERROR)
	    {
	      error = ER_FAILED;
	      goto end;
	    }
	}
    }
end:
  parser_free_parser (parser);
  return error;
}

static int
sl_write_sql (PARSER_VARCHAR * query, PARSER_VARCHAR * select)
{
  time_t curr_time;
  char time_buf[20];

  assert (query != NULL);

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
  fprintf (log_fp, "-- %s | %u | %d | %d\n", time_buf, ++sl_Info.last_inserted_sql_id,
	   (select == NULL) ? 0 : select->length, query->length);

  /* print select for verifying data consistency */
  if (select != NULL)
    {
      /* -- select_length select * from tbl_name */
      fprintf (log_fp, "-- ");
      fwrite (select->bytes, sizeof (char), select->length, log_fp);
      fputc ('\n', log_fp);
    }

  /* print SQL query */
  fwrite (query->bytes, sizeof (char), query->length, log_fp);
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
sl_log_open ()
{
  char cur_sql_log_path[PATH_MAX];
  FILE *fp;

  snprintf (cur_sql_log_path, PATH_MAX, "%s.%d", sql_log_base_path, sl_Info.curr_file_id);

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

  snprintf (new_file_path, PATH_MAX, "%s.%d", sql_log_base_path, sl_Info.curr_file_id);

  fclose (old_fp);
  new_fp = fopen (new_file_path, "w");

  if (sl_write_catalog () != NO_ERROR)
    {
      fclose (new_fp);
      return NULL;
    }

  return new_fp;
}


static void
trim_single_quote (PARSER_VARCHAR * name)
{
  if (name->length == 0 || name->bytes[0] != '\'' || name->bytes[name->length - 1] != '\'')
    {
      return;
    }

  memmove (name->bytes, name->bytes + 1, name->length - 1);
  name->length = name->length - 2;

  return;
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
