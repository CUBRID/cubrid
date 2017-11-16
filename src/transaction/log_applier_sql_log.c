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

int sl_write_sql (string_buffer& query, string_buffer& select);
void sl_print_insert_att_names (string_buffer& sb, OBJ_TEMPASSIGN ** assignments, int num_assignments);
void sl_print_insert_att_values (string_buffer& sb, OBJ_TEMPASSIGN ** assignments, int num_assignments);
int sl_print_pk (string_buffer& sb, SM_CLASS * sm_class, DB_VALUE * key);
void sl_print_midxkey (string_buffer& sb, SM_ATTRIBUTE ** attributes, const DB_MIDXKEY * midxkey);
void sl_print_update_att_set (string_buffer& sb, OBJ_TEMPASSIGN ** assignments, int num_assignments);
void sl_print_att_value (string_buffer, const char *att_name, OBJ_TEMPASSIGN ** assignments, int num_assignments);
DB_VALUE *sl_find_att_value (const char *att_name, OBJ_TEMPASSIGN ** assignments, int num_assignments);


static FILE *sl_open_next_file (FILE * old_fp);
static FILE *sl_log_open (void);
static int sl_read_catalog (void);
static int sl_write_catalog (void);
static void trim_single_quote (PARSER_VARCHAR * name);
static int create_dir (const char *new_dir);


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

int sl_print_pk (string_buffer& sb, SM_CLASS * sm_class, DB_VALUE * key)
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
      sl_print_midxkey (sb, pk_cons->attributes, midxkey);
    }
  else
    {
      pk_att = pk_cons->attributes[0];
      sb("\"%s\"=", pk_att->header.name);
      db_value_printer printer(sb);
      printer.describe_value (key);
    }
  return NO_ERROR;
}

void sl_print_insert_att_names (string_buffer& sb, OBJ_TEMPASSIGN ** assignments, int num_assignments)
{
  if(num_assignments > 0)
    {
      sb("\"%s\"", assignments[0]->att->header.name);
    }
  for (int i = 1; i < num_assignments; i++)
    {
      sb(", \"%s\"", assignments[i]->att->header.name);
    }
}

void sl_print_insert_att_values (string_buffer& sb, OBJ_TEMPASSIGN ** assignments, int num_assignments)
{
  PARSER_VARCHAR *buffer = NULL;
  PARSER_VARCHAR *value = NULL;
  int i;
  db_value_printer printer(sb);

  if(num_assignments > 0)
    {
      printer.describe_value (assignments[i]->variable);
    }
  for (i = 0; i < num_assignments; i++)
    {
      sb += ',';
      printer.describe_value (assignments[i]->variable);
    }
}

/*
 *  * sl_print_sql_midxkey -
 *   *      print midxkey in the following format.
 *    *      key1=value1 AND key2=value2 AND ...
 *     */
void sl_print_midxkey (string_buffer& sb, SM_ATTRIBUTE ** attributes, const DB_MIDXKEY * midxkey)
{
  int prev_i_index = 0;
  char *prev_i_ptr = NULL;
  DB_VALUE value;

  for (int i = 0; i < midxkey->ncolumns && attributes[i] != NULL; i++)
    {
      if (i > 0)
	{
	  sb(" AND ");
	}
      pr_midxkey_get_element_nocopy (midxkey, i, &value, &prev_i_index, &prev_i_ptr);
      sb("\"%s\"=", attributes[i]->header.name);
      db_value_printer printer(sb);
      printer.describe_value (&value);
    }
}

DB_VALUE *sl_find_att_value (const char *att_name, OBJ_TEMPASSIGN ** assignments, int num_assignments)
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

void sl_print_att_value (string_buffer& sb, const char *att_name, OBJ_TEMPASSIGN ** assignments, int num_assignments)
{
  DB_VALUE *val = sl_find_att_value (att_name, assignments, num_assignments);
  if (val != NULL)
    {
      db_value_printer printer(sb);
      printer.describe_value (val);
    }
}

void sl_print_update_att_set (string_buffer& sb, OBJ_TEMPASSIGN ** assignments, int num_assignments)
{
  db_value_printer printer(sb);
  for (int i = 0; i < num_assignments; i++)
    {
      sb("\"%s\"=", assignments[i]->att->header.name);
      printer.describe_value (assignments[i]->variable);
      if (i != num_assignments - 1)
	{
	  sb(", ");
	}
    }
}

int
sl_write_insert_sql (DB_OTMPL * inst_tp, DB_VALUE * key)
{
  mem::block_ext mb1; //bSolo: ToDo: what allocator to use?
  string_buffer sb1(mb1);
  sb1("INSERT INTO [%s](", sm_ch_name ((MOBJ) (inst_tp->class_)));
  sl_print_insert_att_names (sb1, inst_tp->assignments, inst_tp->nassigns);
  sb1(") VALUES (");
  sl_print_insert_att_values (sb1, inst_tp->assignments, inst_tp->nassigns);
  sb1(");");

  mem::block_ext mb2;//bSolo: ToDo: what allocator to use?
  string_buffer sb2(mb2);
  sb2("SELECT * FROM [%s] WHERE ", sm_ch_name ((MOBJ) (inst_tp->class_)));
  if(sl_print_pk (sb2, inst_tp->class_, key) != NO_ERROR)
    {
      return ER_FAILED;
    }
  sb2(";");

  if (sl_write_sql (sb1, sb2) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}

int
sl_write_update_sql (DB_OTMPL * inst_tp, DB_VALUE * key)
{
  mem::block_ext mb1; //bSolo: ToDo: what allocator to use?
  string_buffer sb1(mb1);
  mem::block_ext mb2; //bSolo: ToDo: what allocator to use?
  string_buffer sb2(mb2);

  char str_next_value[NUMERIC_MAX_STRING_SIZE];
  int result;

  if (strcmp (sm_ch_name ((MOBJ) (inst_tp->class_)), "db_serial") != 0)
    {
      /* ordinary tables */
      sb1("UPDATE [%s] SET ", sm_ch_name ((MOBJ) (inst_tp->class_)));
      sl_print_update_att_set (sb1, inst_tp->assignments, inst_tp->nassigns);
      sb1(" WHERE ");
      if(sl_print_pk (sb1, inst_tp->class_, key) != NO_ERROR)
        {
	  return ER_FAILED;
        }
      sb1(";");

      sb2("SELECT * FROM [%s] WHERE ", sm_ch_name ((MOBJ) (inst_tp->class_)));
      if(sl_print_pk (sb2, inst_tp->class_, key) != NO_ERROR)
        {
	  return ER_FAILED;
        }
      sb2(";");
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
      sb("ALTER SERIAL [");

      /*serial_name = */sl_print_att_value (sb, "name", inst_tp->assignments, inst_tp->nassigns);
      //trim_single_quote (serial_name);//bSolo: ToDo

      sb("] START WITH %s;", numeric_db_value_print (&next_value, str_next_value));
    }

  return sl_write_sql (sb1, sb2);
}

int sl_write_delete_sql (char *class_name, MOBJ mclass, DB_VALUE * key)
{
  mem::block_ext mb1; //bSolo: ToDo: what allocator to use?
  string_buffer sb1(mb1);
  sb1("DELETE FROM [%s] WHERE ", class_name);
  if (sl_print_pk (sb1, (SM_CLASS *) mclass, key) != NO_ERROR)
    {
      return ER_FAILED;
    }
  sb1(";");

  mem::block_ext mb2; //bSolo: ToDo: what allocator to use?
  string_buffer sb2(mb2);
  sb2("SELECT * FROM [%s] WHERE ", class_name);
  if (sl_print_pk (sb2, (SM_CLASS *) mclass, key) != NO_ERROR)
    {
      return ER_FAILED;
    }
  sb2(";");

  return sl_write_sql (sb1, sb2);
}

int
sl_write_statement_sql (char *class_name, char *db_user, int item_type, char *stmt_text, char *ha_sys_prm)
{
  int error = NO_ERROR;
  char default_ha_prm[LINE_MAX];
  SYSPRM_ERR rc;

  mem::block_ext mb1; //bSolo: ToDo: what allocator to use here?
  string_buffer sb(mb1)
  sb("%s;", stmt_text);

  if (ha_sys_prm != NULL)
    {
      mem::block_ext mb_param; //bSolo: ToDo: what allocator to use?
      string_buffer sb_param(mb_param);
      set_param("%s SET SYSTEM PARAMETERS '%s';", CA_MARK_TRAN_START, ha_sys_prm); //set param
      rc = sysprm_make_default_values (ha_sys_prm, default_ha_prm, sizeof (default_ha_prm));
      if (rc != PRM_ERR_NO_ERROR)
	{
	  return sysprm_set_error (rc, ha_sys_prm);
	}
      if (sl_write_sql (sb_param, NULL) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      sb_param.clear();
      sb_param("%s SET SYSTEM PARAMETERS '%s';", CA_MARK_TRAN_END, default_ha_prm); //restore param
      if (sl_write_sql (sb, NULL) != NO_ERROR)
	{
	  sl_write_sql (sb_param, NULL);
	  return ER_FAILED;
	}
      if (sl_write_sql (sb_param, NULL) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  else
    {
      if (sl_write_sql (sb, NULL) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  if (item_type == CUBRID_STMT_CREATE_CLASS)
    {
      if (db_user != NULL && strlen (db_user) > 0)
	{
          sb.clear();
	  sb("GRANT ALL PRIVILEGES ON %s TO %s;", class_name, db_user);
	  if (sl_write_sql (sb, NULL) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}
    }

  return NO_ERROR;
}

int sl_write_sql (string_buffer& query, string_buffer& select)
{
  time_t curr_time;
  char time_buf[20];

  assert (query.get_buffer() != NULL);

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
  fprintf (log_fp, "-- %s | %u | %d | %zu\n", time_buf, ++sl_Info.last_inserted_sql_id,
	   (select.get_buffer() == NULL) ? 0 : select.len(), query.len());

  /* print select for verifying data consistency */
  if (select.get_buffer() != NULL)
    {
      /* -- select_length select * from tbl_name */
      fprintf (log_fp, "-- ");
      fwrite (select.get_buffer(), sizeof (char), select.len(), log_fp);
      fputc ('\n', log_fp);
    }

  /* print SQL query */
  fwrite (query.get_buffer(), sizeof (char), query.len(), log_fp);
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
