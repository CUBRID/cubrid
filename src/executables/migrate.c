/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 */

/*
 * migrate.c :
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>

#include "dbi.h"
#include "porting.h"
#include "utility.h"
#include "databases_file.h"
#include "boot_sr.h"
#include "log_impl.h"
#include "btree.h"
#include "transform.h"

#define V9_0_LEVEL (9.0f)
#define V9_1_LEVEL (9.1f)

#define FOPEN_AND_CHECK(fp, filename, mode) \
do { \
  (fp) = fopen ((filename), (mode)); \
  if ((fp) == NULL) \
    { \
      printf ("file open error: %s, %d\n", (filename), errno); \
      return ER_FAILED; \
    } \
} while (0)

#define FSEEK_AND_CHECK(fp, size, origin, filename) \
do { \
  if (fseek ((fp), (size), (origin)) < 0) \
    { \
      printf ("file seek error: %s, %d\n", (filename), errno); \
      fclose ((fp)); \
      return ER_FAILED; \
    } \
} while (0)

#define FREAD_AND_CHECK(ptr, size, n, fp, filename) \
do { \
  fread ((ptr), (size), (n), (fp)); \
  if (ferror ((fp)) != 0) \
    { \
      printf ("file fread error: %s, %d\n", (filename), errno); \
      fclose ((fp)); \
      return ER_FAILED; \
    } \
} while (0)

#define FWRITE_AND_CHECK(ptr, size, n, fp, filename) \
do { \
  fwrite ((ptr), (size), (n), (fp)); \
  if (ferror ((fp)) != 0) \
    { \
      printf ("file fwrite error: %s, %d\n", (filename), errno); \
      fclose ((fp)); \
      return ER_FAILED; \
    } \
} while (0)

#define FFLUSH_AND_CHECK(fp, filename) \
do { \
  if (fflush ((fp)) != 0) \
    { \
      printf ("file fflush error: %s, %d\n", (filename), errno); \
      fclose ((fp)); \
      return ER_FAILED; \
    } \
} while (0)

#define FCLOSE_AND_CHECK(fp, filename) \
do { \
  if (fclose ((fp)) != 0) \
    { \
      printf ("file fclose error: %s, %d\n", (filename), errno); \
      return ER_FAILED; \
    } \
} while (0)

extern int btree_fix_overflow_oid_page_all_btrees (void);
extern int catcls_get_db_collation (THREAD_ENTRY * thread_p,
				    LANG_COLL_COMPAT ** db_collations,
				    int *coll_cnt);

static int get_active_log_vol_path (const char *db_path, char *logvol_path);
static int check_and_fix_compat_level (const char *db_name,
				       const char *vol_path);
static int get_db_path (const char *db_name, char *db_full_path);
static int change_db_collation_query_spec (void);
static int check_collations (const char *db_name, int *need_manual_migr);

static int
get_active_log_vol_path (const char *db_path, char *logvol_path)
{
  char vol_info_path[PATH_MAX], vol_path[PATH_MAX];
  FILE *vol_info_fp = NULL;
  int volid = NULL_VOLID;
  char scan_format[32];

  fileio_make_volume_info_name (vol_info_path, db_path);

  FOPEN_AND_CHECK (vol_info_fp, vol_info_path, "r");

  sprintf (scan_format, "%%d %%%ds", (int) (sizeof (vol_path) - 1));
  while (true)
    {
      if (fscanf (vol_info_fp, scan_format, &volid, vol_path) != 2)
	{
	  break;
	}

      if (volid != LOG_DBLOG_ACTIVE_VOLID)
	{
	  continue;
	}

      strcpy (logvol_path, vol_path);
      break;
    }

  FCLOSE_AND_CHECK (vol_info_fp, vol_info_path);
  return NO_ERROR;
}

static int
check_and_fix_compat_level (const char *db_name, const char *db_path)
{
  FILE *fp = NULL;
  char vol_path[PATH_MAX];
  LOG_HEADER *hdr;
  LOG_PAGE *hdr_page;
  char log_io_page[IO_MAX_PAGE_SIZE];

  if (get_active_log_vol_path (db_path, vol_path) != NO_ERROR)
    {
      printf ("Can't found log active volume path.\n");
      return ER_FAILED;
    }

  hdr_page = (LOG_PAGE *) log_io_page;
  hdr = (struct log_header *) hdr_page->area;

  FOPEN_AND_CHECK (fp, vol_path, "rb+");
  FREAD_AND_CHECK (log_io_page, sizeof (char), LOG_PAGESIZE, fp, vol_path);

  if (hdr->db_compatibility == V9_1_LEVEL)
    {
      printf ("This database (%s) is already updated.\n", db_name);
      return ER_FAILED;
    }

  if (hdr->db_compatibility != V9_0_LEVEL)
    {
      printf ("Cannot migrate this database: "
	      "%s is not CUBRID 9.0 BETA database.\n", db_name);
      return ER_FAILED;
    }

  hdr->db_compatibility = rel_disk_compatible ();

  rewind (fp);
  FWRITE_AND_CHECK (log_io_page, sizeof (char), LOG_PAGESIZE, fp, vol_path);
  FFLUSH_AND_CHECK (fp, vol_path);
  FCLOSE_AND_CHECK (fp, vol_path);

  return NO_ERROR;
}

static int
undo_fix_compat_level (const char *db_path)
{
  FILE *fp = NULL;
  char vol_path[PATH_MAX];
  LOG_HEADER *hdr;
  LOG_PAGE *hdr_page;
  char log_io_page[IO_MAX_PAGE_SIZE];

  if (get_active_log_vol_path (db_path, vol_path) != NO_ERROR)
    {
      printf ("Can't found log active volume path.\n");
      return ER_FAILED;
    }

  hdr_page = (LOG_PAGE *) log_io_page;
  hdr = (struct log_header *) hdr_page->area;

  FOPEN_AND_CHECK (fp, vol_path, "rb+");
  FREAD_AND_CHECK (log_io_page, sizeof (char), LOG_PAGESIZE, fp, vol_path);

  hdr->db_compatibility = V9_0_LEVEL;

  rewind (fp);
  FWRITE_AND_CHECK (log_io_page, sizeof (char), LOG_PAGESIZE, fp, vol_path);
  FFLUSH_AND_CHECK (fp, vol_path);
  FCLOSE_AND_CHECK (fp, vol_path);

  return NO_ERROR;
}

static int
get_db_path (const char *db_name, char *db_full_path)
{
  DB_INFO *dir = NULL;
  DB_INFO *db = NULL;

  if (cfg_read_directory (&dir, false) != NO_ERROR)
    {
      printf ("Can't found databases.txt.\n");
      return ER_FAILED;
    }

  db = cfg_find_db_list (dir, db_name);

  if (db == NULL)
    {
      if (dir)
	{
	  cfg_free_directory (dir);
	}

      printf ("Unknown database: %s\n", db_name);
      return ER_FAILED;
    }

  COMPOSE_FULL_NAME (db_full_path, PATH_MAX, db->pathname, db_name);

  return NO_ERROR;
}

static int
change_db_collation_query_spec (void)
{
  DB_OBJECT *mop;
  char stmt[2048];

  printf ("Start to fix db_collation view query specification\n\n");

  mop = db_find_class (CTV_DB_COLLATION_NAME);
  if (mop == NULL)
    {
      return er_errid ();
    }

  sprintf (stmt,
	   "SELECT [c].[coll_id], [c].[coll_name],"
	   " CASE [c].[charset_id]"
	   "  WHEN %d THEN 'iso88591'"
	   "  WHEN %d THEN 'utf8'"
	   "  WHEN %d THEN 'euckr'"
	   "  WHEN %d THEN 'ascii'"
	   "  WHEN %d THEN 'raw-bits'"
	   "  WHEN %d THEN 'raw-bytes'"
	   "  WHEN %d THEN 'NONE'"
	   "  ELSE 'OTHER'"
	   " END,"
	   " CASE [c].[built_in]"
	   "  WHEN 0 THEN 'No'"
	   "  WHEN 1 THEN 'Yes'"
	   "  ELSE 'ERROR'"
	   " END,"
	   " CASE [c].[expansions]"
	   "  WHEN 0 THEN 'No'"
	   "  WHEN 1 THEN 'Yes'"
	   "  ELSE 'ERROR'"
	   " END,"
	   " [c].[contractions],"
	   " CASE [c].[uca_strength]"
	   "  WHEN 0 THEN 'Not applicable'"
	   "  WHEN 1 THEN 'Primary'"
	   "  WHEN 2 THEN 'Secondary'"
	   "  WHEN 3 THEN 'Tertiary'"
	   "  WHEN 4 THEN 'Quaternary'"
	   "  WHEN 5 THEN 'Identity'"
	   "  ELSE 'Unknown'"
	   " END"
	   " FROM [%s] [c]"
	   " ORDER BY [c].[coll_id]",
	   INTL_CODESET_ISO88591, INTL_CODESET_UTF8,
	   INTL_CODESET_KSC5601_EUC, INTL_CODESET_ASCII,
	   INTL_CODESET_RAW_BITS, INTL_CODESET_RAW_BYTES,
	   INTL_CODESET_NONE, CT_COLLATION_NAME);

  return db_change_query_spec (mop, stmt, 1);
}

#if defined(WINDOWS)
static BOOL WINAPI
#else
static void
#endif
intr_handler (int sig_no)
{
  /* do nothing */
#if defined(WINDOWS)
  if (sig_no == CTRL_C_EVENT)
    {
      /* ignore */
      return TRUE;
    }

  return FALSE;
#endif /* WINDOWS */
}

/*
 * check_collations() - 
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
static int
check_collations (const char *db_name, int *need_manual_migr)
{
#define FILE_STMT_NAME "cubrid_migr_9.0beta_to_9.1_collations_"
#define QUERY_SIZE 1024
#define MAX_BUILT_IN_COLL_ID_V9_0 8

  LANG_COLL_COMPAT *db_collations = NULL;
  DB_QUERY_RESULT *query_result = NULL;
  const LANG_COLL_COMPAT *db_coll;
  FILE *f_stmt = NULL;
  char f_stmt_name[PATH_MAX];
  int i, db_coll_cnt;
  int status = EXIT_SUCCESS;
  int db_status;
  char *vclass_names = NULL;
  int vclass_names_used = 0;
  int vclass_names_alloced = 0;
  char *part_tables = NULL;
  int part_tables_used = 0;
  int part_tables_alloced = 0;
  int user_coll_cnt = 0;

  assert (db_name != NULL);
  assert (need_manual_migr != NULL);

  *need_manual_migr = 0;

  printf ("Checking database collation\n\n");

  /* read all collations from DB : id, name, checksum */
  db_status = catcls_get_db_collation (NULL, &db_collations, &db_coll_cnt);
  if (db_status != NO_ERROR)
    {
      if (db_collations != NULL)
	{
	  db_private_free (NULL, db_collations);
	  db_collations = NULL;
	}
      status = EXIT_FAILURE;
      goto exit;
    }

  assert (db_collations != NULL);

  strcpy (f_stmt_name, FILE_STMT_NAME);
  strcat (f_stmt_name, db_name);
  strcat (f_stmt_name, ".sql");

  f_stmt = fopen (f_stmt_name, "wt");
  if (f_stmt == NULL)
    {
      printf ("Unable to create report file %s.\n", f_stmt_name);
      goto exit;
    }

  for (i = 0; i < db_coll_cnt; i++)
    {
      DB_QUERY_ERROR query_error;
      char query[QUERY_SIZE];
      bool is_obs_coll = false;
      bool check_atts = false;
      bool check_views = false;
      bool check_triggers = false;

      db_coll = &(db_collations[i]);

      assert (db_coll->coll_id >= 0);

      if (db_coll->coll_id <= MAX_BUILT_IN_COLL_ID_V9_0)
	{
	  continue;
	}

      user_coll_cnt++;

      check_views = true;
      check_atts = true;
      check_triggers = true;
      is_obs_coll = true;

      if (check_atts)
	{
	  /* first drop foreign keys on attributes using collation */
	  /* CLASS_NAME, INDEX_NAME */

	  sprintf (query, "SELECT A.class_of.class_name, I.index_name "
		   "from _db_attribute A, _db_index I, "
		   "_db_index_key IK, _db_domain D "
		   "where D.object_of = A AND D.collation_id = %d AND "
		   "NOT (A.class_of.class_name IN (SELECT "
		   "CONCAT (P.class_of.class_name, '__p__', P.pname) "
		   "FROM _db_partition P WHERE "
		   "CONCAT (P.class_of.class_name, '__p__', P.pname) = "
		   "A.class_of.class_name AND P.pname IS NOT NULL))"
		   "AND A.attr_name = IK.key_attr_name AND IK in I.key_attrs "
		   "AND I.is_foreign_key = 1 AND I.class_of = A.class_of",
		   db_coll->coll_id);

	  db_status = db_execute (query, &query_result, &query_error);

	  if (db_status < 0)
	    {
	      status = EXIT_FAILURE;
	      goto exit;
	    }
	  else if (db_status > 0)
	    {
	      DB_VALUE class_name;
	      DB_VALUE index_name;

	      printf ("----------------------------------------\n");
	      printf ("Foreign keys on attributes having collation '%s' must "
		      "be dropped before changing collation of attributes\n"
		      "Table | Foreign Key\n", db_coll->coll_name);

	      while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
		{
		  if (db_query_get_tuple_value (query_result, 0, &class_name)
		      != NO_ERROR
		      || db_query_get_tuple_value (query_result, 1,
						   &index_name) != NO_ERROR)
		    {
		      status = EXIT_FAILURE;
		      goto exit;
		    }

		  assert (DB_VALUE_TYPE (&class_name) == DB_TYPE_STRING);
		  assert (DB_VALUE_TYPE (&index_name) == DB_TYPE_STRING);

		  fprintf (stdout, "%s | %s\n", DB_GET_STRING (&class_name),
			   DB_GET_STRING (&index_name));
		  sprintf (query, "ALTER TABLE [%s] DROP FOREIGN KEY [%s];",
			   DB_GET_STRING (&class_name),
			   DB_GET_STRING (&index_name));
		  fprintf (f_stmt, "%s\n", query);
		  *need_manual_migr = 1;
		}
	    }

	  if (query_result != NULL)
	    {
	      db_query_end (query_result);
	      query_result = NULL;
	    }

	  /* CLASS_NAME, CLASS_TYPE, ATTR_NAME, ATTR_FULL_TYPE, PARTITIONED */
	  /* Ex : ATTR_FULL_TYPE = CHAR(20) */

	  sprintf (query, "SELECT A.class_of.class_name, "
		   "A.class_of.class_type, "
		   "A.attr_name, "
		   "CONCAT (CASE D.data_type WHEN 4 THEN 'VARCHAR' "
		   "WHEN 25 THEN 'CHAR' WHEN 27 THEN 'NCHAR VARYING' "
		   "WHEN 26 THEN 'NCHAR' END, "
		   "IF (D.prec < 0 AND "
		   "(D.data_type = 4 OR D.data_type = 27) ,"
		   "'', CONCAT ('(', D.prec,')'))), "
		   "CASE WHEN A.class_of.sub_classes IS NULL THEN 0 "
		   "ELSE NVL((SELECT 1 FROM _db_partition p "
		   "WHERE p.class_of = A.class_of AND p.pname IS NULL AND "
		   "LOCATE(A.attr_name, TRIM(SUBSTRING(p.pexpr FROM 8 FOR "
		   "(POSITION(' FROM ' IN p.pexpr)-8)))) > 0 ), 0) "
		   "END "
		   "FROM _db_domain D,_db_attribute A "
		   "WHERE D.object_of = A AND D.collation_id = %d "
		   "AND NOT (A.class_of.class_name IN "
		   "(SELECT CONCAT (P.class_of.class_name, '__p__', P.pname) "
		   "FROM _db_partition P WHERE "
		   "CONCAT (P.class_of.class_name, '__p__', P.pname) "
		   " = A.class_of.class_name AND P.pname IS NOT NULL)) "
		   "ORDER BY A.class_of.class_name", db_coll->coll_id);

	  db_status = db_execute (query, &query_result, &query_error);

	  if (db_status < 0)
	    {
	      status = EXIT_FAILURE;
	      goto exit;
	    }
	  else if (db_status > 0)
	    {
	      DB_VALUE class_name;
	      DB_VALUE attr;
	      DB_VALUE attr_def;
	      DB_VALUE ct;
	      DB_VALUE has_part;

	      printf ("----------------------------------------\n");
	      printf ("Attributes having collation '%s'; "
		      "they should be redefined before migration."
		      "\nTable | Attribute Domain\n", db_coll->coll_name);

	      while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
		{
		  bool add_to_part_tables = false;

		  if (db_query_get_tuple_value (query_result, 0, &class_name)
		      != NO_ERROR
		      || db_query_get_tuple_value (query_result, 1, &ct)
		      != NO_ERROR
		      || db_query_get_tuple_value (query_result, 2, &attr)
		      != NO_ERROR
		      || db_query_get_tuple_value (query_result, 3, &attr_def)
		      != NO_ERROR
		      || db_query_get_tuple_value (query_result, 4, &has_part)
		      != NO_ERROR)
		    {
		      status = EXIT_FAILURE;
		      goto exit;
		    }

		  assert (DB_VALUE_TYPE (&class_name) == DB_TYPE_STRING);
		  assert (DB_VALUE_TYPE (&attr) == DB_TYPE_STRING);
		  assert (DB_VALUE_TYPE (&attr_def) == DB_TYPE_STRING);
		  assert (DB_VALUE_TYPE (&ct) == DB_TYPE_INTEGER);
		  assert (DB_VALUE_TYPE (&has_part) == DB_TYPE_INTEGER);

		  printf ("%s | %s %s\n", DB_GET_STRING (&class_name),
			  DB_GET_STRING (&attr), DB_GET_STRING (&attr_def));

		  /* output query to fix schema */
		  if (DB_GET_INTEGER (&ct) == 0)
		    {
		      if (DB_GET_INTEGER (&has_part) == 1)
			{
			  /* class is partitioned, remove partition;
			   * we cannot change the collation of an attribute
			   * having partitions */
			  fprintf (f_stmt,
				   "ALTER TABLE [%s] REMOVE PARTITIONING;\n",
				   DB_GET_STRING (&class_name));
			  add_to_part_tables = true;
			}
		      snprintf (query, sizeof (query) - 1, "ALTER TABLE [%s] "
				"MODIFY [%s] %s COLLATE utf8_bin;",
				DB_GET_STRING (&class_name),
				DB_GET_STRING (&attr),
				DB_GET_STRING (&attr_def));
		    }
		  else
		    {
		      snprintf (query, sizeof (query) - 1, "DROP VIEW [%s];",
				DB_GET_STRING (&class_name));

		      if (vclass_names == NULL
			  || vclass_names_alloced <= vclass_names_used)
			{
			  if (vclass_names_alloced == 0)
			    {
			      vclass_names_alloced =
				1 + DB_MAX_IDENTIFIER_LENGTH;
			    }
			  vclass_names =
			    db_private_realloc (NULL, vclass_names,
						2 * vclass_names_alloced);

			  if (vclass_names == NULL)
			    {
			      status = EXIT_FAILURE;
			      goto exit;
			    }
			  vclass_names_alloced *= 2;
			}

		      memcpy (vclass_names + vclass_names_used,
			      DB_GET_STRING (&class_name),
			      DB_GET_STRING_SIZE (&class_name));
		      vclass_names_used += DB_GET_STRING_SIZE (&class_name);
		      memcpy (vclass_names + vclass_names_used, "\0", 1);
		      vclass_names_used += 1;
		    }
		  fprintf (f_stmt, "%s\n", query);
		  *need_manual_migr = 1;

		  if (add_to_part_tables)
		    {
		      if (part_tables == NULL
			  || part_tables_alloced <= part_tables_used)
			{
			  if (part_tables_alloced == 0)
			    {
			      part_tables_alloced =
				1 + DB_MAX_IDENTIFIER_LENGTH;
			    }
			  part_tables =
			    db_private_realloc (NULL, part_tables,
						2 * part_tables_alloced);

			  if (part_tables == NULL)
			    {
			      status = EXIT_FAILURE;
			      goto exit;
			    }
			  part_tables_alloced *= 2;
			}

		      memcpy (part_tables + part_tables_used,
			      DB_GET_STRING (&class_name),
			      DB_GET_STRING_SIZE (&class_name));
		      part_tables_used += DB_GET_STRING_SIZE (&class_name);
		      memcpy (part_tables + part_tables_used, "\0", 1);
		      part_tables_used += 1;
		    }
		}

	      if (part_tables != NULL)
		{
		  char *curr_tbl = part_tables;
		  int tbl_size = strlen (curr_tbl);

		  printf ("----------------------------------------\n");
		  printf ("Partitioned tables; the partioning should be "
			  "removed before migration and recreated after if "
			  "necessary.\nTable:\n");

		  while (tbl_size > 0)
		    {
		      printf ("%s\n", curr_tbl);
		      curr_tbl += tbl_size + 1;
		      if (curr_tbl >= part_tables + part_tables_used)
			{
			  break;
			}
		      tbl_size = strlen (curr_tbl);
		    }
		}
	    }

	  if (query_result != NULL)
	    {
	      db_query_end (query_result);
	      query_result = NULL;
	    }
	}

      if (check_views)
	{
	  sprintf (query, "SELECT class_of.class_name, spec "
		   "FROM _db_query_spec "
		   "WHERE LOCATE ('collate %s', spec) > 0",
		   db_coll->coll_name);

	  db_status = db_execute (query, &query_result, &query_error);

	  if (db_status < 0)
	    {
	      status = EXIT_FAILURE;
	      goto exit;
	    }
	  else if (db_status > 0)
	    {
	      DB_VALUE view;
	      DB_VALUE query_spec;

	      printf ("----------------------------------------\n");
	      printf ("Query views containing collation '%s'; they "
		      "should be redefined or dropped before migration :\n"
		      "View | Query\n", db_coll->coll_name);

	      while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
		{
		  bool already_dropped = false;

		  if (db_query_get_tuple_value (query_result, 0, &view)
		      != NO_ERROR
		      || db_query_get_tuple_value (query_result, 1,
						   &query_spec) != NO_ERROR)
		    {
		      status = EXIT_FAILURE;
		      goto exit;
		    }

		  assert (DB_VALUE_TYPE (&view) == DB_TYPE_STRING);
		  assert (DB_VALUE_TYPE (&query_spec) == DB_TYPE_STRING);

		  printf ("%s | %s\n", DB_GET_STRING (&view),
			  DB_GET_STRING (&query_spec));

		  /* output query to fix schema */
		  if (vclass_names != NULL)
		    {
		      char *search = vclass_names;
		      int view_name_size = DB_GET_STRING_SIZE (&view);

		      /* search if the view was already put in .SQL file */
		      while (search + view_name_size
			     < vclass_names + vclass_names_used)
			{
			  if (memcmp (search, DB_GET_STRING (&view),
				      view_name_size) == 0
			      && *(search + view_name_size) == '\0')
			    {
			      already_dropped = true;
			      break;
			    }

			  while (*search++ != '\0')
			    {
			      ;
			    }
			}
		    }

		  if (!already_dropped)
		    {
		      snprintf (query, sizeof (query) - 1, "DROP VIEW [%s];",
				DB_GET_STRING (&view));
		      fprintf (f_stmt, "%s\n", query);
		    }
		  *need_manual_migr = 1;
		}
	    }

	  if (query_result != NULL)
	    {
	      db_query_end (query_result);
	      query_result = NULL;
	    }
	}

      if (check_triggers)
	{
	  sprintf (query, "SELECT name, condition FROM db_trigger "
		   "WHERE LOCATE ('collate %s', condition) > 0",
		   db_coll->coll_name);

	  db_status = db_execute (query, &query_result, &query_error);

	  if (db_status < 0)
	    {
	      status = EXIT_FAILURE;
	      goto exit;
	    }
	  else if (db_status > 0)
	    {
	      DB_VALUE trig_name;
	      DB_VALUE trig_cond;

	      printf ("----------------------------------------\n");
	      printf ("Triggers having condition containing "
		      "collation '%s'; they should be redefined or dropped "
		      "before migration :\n Trigger | Condition\n",
		      db_coll->coll_name);

	      while (db_query_next_tuple (query_result) == DB_CURSOR_SUCCESS)
		{
		  if (db_query_get_tuple_value (query_result, 0, &trig_name)
		      != NO_ERROR
		      || db_query_get_tuple_value (query_result, 1,
						   &trig_cond) != NO_ERROR)
		    {
		      status = EXIT_FAILURE;
		      goto exit;
		    }

		  assert (DB_VALUE_TYPE (&trig_name) == DB_TYPE_STRING);
		  assert (DB_VALUE_TYPE (&trig_cond) == DB_TYPE_STRING);

		  printf ("%s | %s\n", DB_GET_STRING (&trig_name),
			  DB_GET_STRING (&trig_cond));

		  /* output query to fix schema */
		  snprintf (query, sizeof (query) - 1, "DROP TRIGGER [%s];",
			    DB_GET_STRING (&trig_name));
		  fprintf (f_stmt, "%s\n", query);
		  *need_manual_migr = 1;
		}
	    }

	  if (query_result != NULL)
	    {
	      db_query_end (query_result);
	      query_result = NULL;
	    }
	}

      /* CUBRID 9.0 does not support function index with COLLATE modifier */
    }

  printf ("----------------------------------------\n");
  printf ("----------------------------------------\n");
  printf ("There are %d user defined collations in database\n",
	  user_coll_cnt);
  if (*need_manual_migr)
    {
      printf ("Manual intervention on database is required before "
	      "migration to 9.1.\n Please check file '%s' and execute it "
	      "before migration\n", f_stmt_name);
    }
  else
    {
      if (f_stmt != NULL)
	{
	  fclose (f_stmt);
	  f_stmt = NULL;
	}
      remove (f_stmt_name);
    }

  printf ("----------------------------------------\n");
  printf ("----------------------------------------\n");

exit:
  if (vclass_names != NULL)
    {
      db_private_free (NULL, vclass_names);
      vclass_names = NULL;
    }

  if (part_tables != NULL)
    {
      db_private_free (NULL, part_tables);
      part_tables = NULL;
    }

  if (f_stmt != NULL)
    {
      fclose (f_stmt);
      f_stmt = NULL;
    }

  if (query_result != NULL)
    {
      db_query_end (query_result);
      query_result = NULL;
    }
  if (db_collations != NULL)
    {
      db_private_free (NULL, db_collations);
      db_collations = NULL;
    }

  return status;

#undef FILE_STMT_NAME
#undef QUERY_SIZE
#undef MAX_BUILT_IN_COLL_ID_V9_0
}


int
main (int argc, char *argv[])
{
  const char *db_name;
  char db_full_path[PATH_MAX];
  int coll_need_manual_migr = 0;

  if (argc != 2)
    {
      printf ("Usage: %s <database name>\n", argv[0]);
      return EXIT_FAILURE;
    }

#if defined(WINDOWS)
  SetConsoleCtrlHandler ((PHANDLER_ROUTINE) intr_handler, TRUE);
#else
  os_set_signal_handler (SIGINT, intr_handler);
#endif

  db_name = argv[1];

  printf ("CUBRID Migration: 9.0 BETA to 9.1\n\n");

  if (rel_disk_compatible () != V9_1_LEVEL)
    {
      /* invalid cubrid library */
      printf ("CUBRID library version is invalid.\n"
	      "Please upgrade to CUBRID 9.1 and retry migrate.\n");
      return EXIT_FAILURE;
    }

  if (check_database_name (db_name))
    {
      return EXIT_FAILURE;
    }

  if (get_db_path (db_name, db_full_path) != NO_ERROR)
    {
      return EXIT_FAILURE;
    }

  if (check_and_fix_compat_level (db_name, db_full_path) != NO_ERROR)
    {
      return EXIT_FAILURE;
    }

  AU_DISABLE_PASSWORDS ();

  (void) boot_set_skip_check_ct_classes (true);
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);

  if (db_restart (argv[0], TRUE, db_name) != NO_ERROR)
    {
      printf ("\n%s\n", db_error_string (3));

      goto error_undo_compat;
    }

  au_disable ();

  if (check_collations (db_name, &coll_need_manual_migr) != EXIT_SUCCESS
      || coll_need_manual_migr != 0)
    {
      db_abort_transaction ();
      db_shutdown ();

      goto error_undo_compat;
    }

  assert (coll_need_manual_migr == 0);

  printf ("Updating database with new system collations\n\n");
  if (synccoll_force () != EXIT_SUCCESS)
    {
      db_abort_transaction ();
      db_shutdown ();

      goto error_undo_compat;
    }

  if (change_db_collation_query_spec () != NO_ERROR
      || btree_fix_overflow_oid_page_all_btrees () != NO_ERROR)
    {
      printf ("\n%s\n", db_error_string (3));

      db_abort_transaction ();
      db_shutdown ();

      goto error_undo_compat;
    }

  db_commit_transaction ();
  db_shutdown ();

  printf ("\nMigration to CUBRID 9.1 has been completed successfully.\n");

  return NO_ERROR;

error_undo_compat:
  if (undo_fix_compat_level (db_full_path) != NO_ERROR)
    {
      printf ("\nRecovering db_compatibility level fails.\n");
    }

  printf ("\nMigration failed.\n");
  return EXIT_FAILURE;
}
