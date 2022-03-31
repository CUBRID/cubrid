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
 * flashback_cl.c -
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#if defined(WINDOWS)
#include <windows.h>
#include <conio.h>
#else /* WINDOWS */
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#endif /* LINUX */

#include "flashback_cl.h"
#include "object_representation.h"
#include "object_primitive.h"
#include "message_catalog.h"
#include "schema_manager.h"
#include "authenticate.h"
#include "utility.h"
#include "csql.h"

typedef enum
{
  FLASHBACK_INSERT = 0,
  FLASHBACK_UPDATE,
  FLASHBACK_DELETE
} FLASHBACK_DML_TYPE;

#if defined(WINDOWS)
static int
flashback_util_get_winsize ()
{
  CONSOLE_SCREEN_BUFFER_INFO csbi;

  GetConsoleScreenBufferInfo (GetStdHandle (STD_OUTPUT_HANDLE), &csbi);

  return csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
}

static char
flashback_util_get_char ()
{
  int c;

  c = getche ();

  return c;
}

#else
static char
flashback_util_get_char ()
{
  int c;
  static struct termios oldt, newt;

  tcgetattr (STDIN_FILENO, &oldt);
  newt = oldt;

  newt.c_lflag &= ~(ICANON);
  tcsetattr (STDIN_FILENO, TCSANOW, &newt);

  c = getchar ();
  tcsetattr (STDIN_FILENO, TCSANOW, &oldt);

  return c;
}

static int
flashback_util_get_winsize ()
{
  struct winsize w;

  ioctl (STDOUT_FILENO, TIOCGWINSZ, &w);

  return w.ws_row;
}
#endif

/*
 * flashback_find_class_index () - find the index of oidlist which contains classoid
 *
 * return              : class index
 * oidlist (in)        : OID of classnames
 * list_size (in)      : size of oidlist
 * classoid (in)       : classoid to find
 */

int
flashback_find_class_index (OID * oidlist, int list_size, OID classoid)
{
  int class_index = 0;

  for (class_index = 0; class_index < list_size; class_index++)
    {
      if (OID_EQ (&oidlist[class_index], &classoid))
	{
	  return class_index;
	}
    }

  return -1;
}

/*
 * flashback_unpack_and_print_summary () 
 *
 * return                   : error code
 * summary_buffer (in/out)  : input buffer which contains summary info, and return advanced buffer pointer
 * summary (out)            : brief summary information
 * num_summary (in)         : number of summary to unpack
 * classname_list (in)      : classnames
 * oidlist (in)             : OID of classnames
 */

int
flashback_unpack_and_print_summary (char **summary_buffer, FLASHBACK_SUMMARY_INFO_MAP * summary,
				    dynamic_array * classname_list, OID * oidlist)
{
  time_t summary_start_time = 0;
  time_t summary_end_time = 0;
  int num_summary = 0;

  TRANID trid;
  char *user = NULL;
  time_t start_time, end_time;
  int num_insert, num_update, num_delete;
  LOG_LSA start_lsa, end_lsa;
  int num_table;
  OID classoid;
  char *tmp_ptr = *summary_buffer;

  bool stop_print = false;
  const int max_window_size = flashback_util_get_winsize ();
  int line_cnt = 0;

  char stime_buf[20];
  char etime_buf[20];

  tmp_ptr = or_unpack_int64 (tmp_ptr, &summary_start_time);
  tmp_ptr = or_unpack_int64 (tmp_ptr, &summary_end_time);

  tmp_ptr = or_unpack_int (tmp_ptr, &num_summary);
  if (num_summary == 0)
    {
      printf ("There is nothing to flashback\n");

      return NO_ERROR;
    }


  strftime (stime_buf, 20, "%d-%m-%Y:%H:%M:%S", localtime (&summary_start_time));
  strftime (etime_buf, 20, "%d-%m-%Y:%H:%M:%S", localtime (&summary_end_time));

  printf ("Flashback Summary\n");
  printf ("Number of Transaction: %d\n", num_summary);
  printf ("Start date - End date:%20s -%20s\n", stime_buf, etime_buf);

  line_cnt = line_cnt + 3;

  printf
    ("Transaction id  User name                         Start time            End time              Num_insert  Num_update  Num_delete  Table\n");
  line_cnt++;

  for (int i = 0; i < num_summary; i++)
    {
      FLASHBACK_SUMMARY_INFO info = { 0, LSA_INITIALIZER, LSA_INITIALIZER };

      tmp_ptr = or_unpack_int (tmp_ptr, &trid);
      tmp_ptr = or_unpack_string_nocopy (tmp_ptr, &user);
      tmp_ptr = or_unpack_int64 (tmp_ptr, &start_time);
      tmp_ptr = or_unpack_int64 (tmp_ptr, &end_time);
      tmp_ptr = or_unpack_int (tmp_ptr, &num_insert);
      tmp_ptr = or_unpack_int (tmp_ptr, &num_update);
      tmp_ptr = or_unpack_int (tmp_ptr, &num_delete);
      tmp_ptr = or_unpack_log_lsa (tmp_ptr, &start_lsa);
      tmp_ptr = or_unpack_log_lsa (tmp_ptr, &end_lsa);
      tmp_ptr = or_unpack_int (tmp_ptr, &num_table);

      info.trid = trid;
      info.start_lsa = start_lsa;
      info.end_lsa = end_lsa;
      strcpy (info.user, user);

      summary->emplace (trid, info);

      if (!stop_print)
	{
	  if (start_time != 0)
	    {
	      strftime (stime_buf, 20, "%d-%m-%Y:%H:%M:%S", localtime (&start_time));
	    }

	  if (end_time != 0)
	    {
	      strftime (etime_buf, 20, "%d-%m-%Y:%H:%M:%S", localtime (&end_time));
	    }

	  printf ("%14d  ", trid);
	  printf ("%-32s  ", user);
	  printf ("%-20s  ", start_time == 0 ? "-" : stime_buf);
	  printf ("%-20s  ", end_time == 0 ? "-" : etime_buf);

	  printf ("%10d  ", num_insert);
	  printf ("%10d  ", num_update);
	  printf ("%10d  ", num_delete);
	}

      for (int j = 0; j < num_table; j++)
	{
	  int idx = 0;
	  char classname[SM_MAX_IDENTIFIER_LENGTH + 1] = "\0";

	  tmp_ptr = or_unpack_oid (tmp_ptr, &classoid);
	  if (!stop_print)
	    {
	      idx = flashback_find_class_index (oidlist, da_size (classname_list), classoid);
	      if (idx == -1)
		{
		  /* er_set */
		  return ER_FLASHBACK_INVALID_CLASS;
		}

	      da_get (classname_list, idx, classname);
	      if (j == 0)
		{
		  printf ("%-s\n", classname);
		  line_cnt++;
		}
	      else
		{
		  printf ("%130s%-s\n", "", classname);
		  line_cnt++;
		}
	    }
	}

      if (line_cnt >= max_window_size)
	{
	  char c;
	  printf ("press 'q' to quit or press anything to continue");

	  c = flashback_util_get_char ();
	  if (c == 'q')
	    {
	      stop_print = true;
	    }

	  line_cnt = 0;

	  /* Remove the message above, because above message is no more required */
	  printf ("\33[2K\r");
	}
    }

  *summary_buffer = tmp_ptr;

  return NO_ERROR;
}

static int
flashback_check_and_resize_sql_memory (char **sql, int req_size, int *max_sql_size)
{
  char *tmp = NULL;

  if (req_size <= *max_sql_size)
    {
      return NO_ERROR;
    }

  while (*max_sql_size < req_size)
    {
      *max_sql_size = *max_sql_size * 2;
    }

  tmp = (char *) realloc (*sql, *max_sql_size);

  if (tmp)
    {
      *sql = tmp;
    }
  else
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  return NO_ERROR;
}

#define IS_QOUTES_NEEDED(type)   (type == DB_TYPE_STRING || \
                                  type == DB_TYPE_TIME || \
                                  type == DB_TYPE_TIMESTAMP || \
                                  type == DB_TYPE_TIMESTAMPTZ || \
                                  type == DB_TYPE_TIMESTAMPLTZ || \
                                  type == DB_TYPE_DATE || \
                                  type == DB_TYPE_DATETIME || \
                                  type == DB_TYPE_DATETIMETZ || \
                                  type == DB_TYPE_DATETIMELTZ || \
                                  type == DB_TYPE_CHAR)

typedef enum
{
  PACK_INT = 0,
  PACK_INT64 = 1,
  PACK_FLOAT = 2,
  PACK_DOUBLE = 3,
  PACK_SHORT = 4,
  PACK_STRING = 7
} FLASHBACK_PACK_FUNC_TYPE;

static int
flashback_process_column_data (char **data, char **sql, int *max_sql_size, DB_TYPE type)
{
  char *ptr = *data;

  int error = NO_ERROR;

  int sql_length = strlen (*sql);

  FLASHBACK_PACK_FUNC_TYPE func_type;

  int i_data;
  double d_data;
  float f_data;
  short sh_data;
  char *s_data;
  INT64 b_data;

  /* maximum length of the string converted from a decimal
   * INT64 represents maximum of 9223372036854775807, and is 20 characters long */
  const int max_decimal_digits_len = 20;

  ptr = or_unpack_int (ptr, (int *) &func_type);

  switch (func_type)
    {
    case PACK_INT:
      ptr = or_unpack_int (ptr, &i_data);
      error = flashback_check_and_resize_sql_memory (sql, sql_length + max_decimal_digits_len, max_sql_size);
      if (error != NO_ERROR)
	{
	  return error;
	}

      sprintf (*sql + sql_length, "%d", i_data);
      break;
    case PACK_INT64:
      ptr = or_unpack_int64 (ptr, &b_data);

      error = flashback_check_and_resize_sql_memory (sql, sql_length + max_decimal_digits_len, max_sql_size);
      if (error != NO_ERROR)
	{
	  return error;
	}

      sprintf (*sql + sql_length, "%lld", b_data);
      break;
    case PACK_FLOAT:
      ptr = or_unpack_float (ptr, &f_data);
      error = flashback_check_and_resize_sql_memory (sql, sql_length + max_decimal_digits_len, max_sql_size);
      if (error != NO_ERROR)
	{
	  return error;
	}

      sprintf (*sql + sql_length, "%f", f_data);
      break;
    case PACK_DOUBLE:
      ptr = or_unpack_double (ptr, &d_data);
      error = flashback_check_and_resize_sql_memory (sql, sql_length + max_decimal_digits_len, max_sql_size);
      if (error != NO_ERROR)
	{
	  return error;
	}

      sprintf (*sql + sql_length, "%lf", d_data);
      break;
    case PACK_SHORT:
      ptr = or_unpack_short (ptr, &sh_data);
      error = flashback_check_and_resize_sql_memory (sql, sql_length + max_decimal_digits_len, max_sql_size);
      if (error != NO_ERROR)
	{
	  return error;
	}

      sprintf (*sql + sql_length, "%d", sh_data);
      break;
    case PACK_STRING:
      ptr = or_unpack_string_nocopy (ptr, &s_data);

      if (IS_QOUTES_NEEDED (type))
	{
	  int result_length = 0;
	  char *result_string = NULL;

	  result_string = string_to_string (s_data, '\'', '\0', strlen (s_data), &result_length, false, true);
	  if (result_string == NULL)
	    {
	      /* internally stirng_to_string() allocates memory for string */
	      util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }

	  error = flashback_check_and_resize_sql_memory (sql, sql_length + result_length + 1, max_sql_size);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }

	  sprintf (*sql + sql_length, "%s", result_string);

	  free_and_init (result_string);
	}
      else
	{
	  error = flashback_check_and_resize_sql_memory (sql, sql_length + strlen (s_data) + 1, max_sql_size);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }

	  sprintf (*sql + sql_length, "%s", s_data);
	}

      break;
    default:
      break;
    }

  *data = ptr;

  return NO_ERROR;
}

static void
flashback_print_detail (int trid, char *user, char *flashback, char *original, FILE * outfp)
{
  fprintf (outfp, "[TRANSACTION ID] %d \n", trid);
  fprintf (outfp, "[USER]           %-s \n", user);
  fprintf (outfp, "[ORIGINAL]       %-s \n", original);
  fprintf (outfp, "[FLASHBACK]      %-s \n", flashback);
}

static int
flashback_print_update (char **loginfo, int trid, char *user, const char *classname, bool is_detail, FILE * outfp)
{
  MOP classop;
  SM_CLASS *class_;
  SM_ATTRIBUTE *attr;

  int error = NO_ERROR;

  char *sql = NULL;
  int max_sql_size = 1024;

  char *cond_sql = NULL;
  int max_cond_sql_size = 1024;

  char *original_sql = NULL;
  int max_original_size = 1024;

  char *start_ptr;
  char *ptr;

  int num_change_col;
  int change_index;

  int num_cond_col;
  int cond_index;

  int i = 0;

  sql = (char *) malloc (max_sql_size);
  if (sql == NULL)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  cond_sql = (char *) malloc (max_cond_sql_size);
  if (sql == NULL)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  start_ptr = ptr = *loginfo;

  classop = sm_find_class (classname);
  error = au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      goto error;
    }

  sprintf (sql, "update [%s] set ", classname);
  strcpy (cond_sql, " where ");

  ptr = or_unpack_int (ptr, &num_change_col);
  for (i = 0; i < num_change_col; i++)
    {
      ptr = or_unpack_int (ptr, &change_index);
    }

  attr = class_->ordered_attributes;
  for (i = 0; i < num_change_col; i++)
    {
      /* check SQL length
       * cond_sql + column name + length of " = and/limit 1;" */
      error =
	flashback_check_and_resize_sql_memory (&cond_sql, strlen (cond_sql) + strlen (attr->header.name) + 15,
					       &max_cond_sql_size);
      if (error != NO_ERROR)
	{
	  goto error;
	}

      sprintf (cond_sql + strlen (cond_sql), "[%s] = ", attr->header.name);

      flashback_process_column_data (&ptr, &cond_sql, &max_cond_sql_size, attr->type->id);
      if (i != num_change_col - 1)
	{
	  strcat (cond_sql, " and ");
	}
      else
	{
	  strcat (cond_sql, " limit 1;");
	}

      attr = attr->order_link;
    }

  ptr = or_unpack_int (ptr, &num_cond_col);
  for (i = 0; i < num_change_col; i++)
    {
      ptr = or_unpack_int (ptr, &cond_index);
    }

  attr = class_->ordered_attributes;
  for (i = 0; i < num_cond_col; i++)
    {
      /* check SQL length
       * sql + column name + length of " = , " */
      error =
	flashback_check_and_resize_sql_memory (&sql, strlen (sql) + strlen (attr->header.name) + 8, &max_sql_size);
      if (error != NO_ERROR)
	{
	  goto error;
	}

      sprintf (sql + strlen (sql), "[%s] = ", attr->header.name);

      flashback_process_column_data (&ptr, &sql, &max_sql_size, attr->type->id);
      if (i != num_cond_col - 1)
	{
	  strcat (sql, ", ");
	}

      attr = attr->order_link;
    }

  error = flashback_check_and_resize_sql_memory (&sql, strlen (sql) + strlen (cond_sql) + 1, &max_sql_size);
  if (error != NO_ERROR)
    {
      goto error;
    }

  strcat (sql, cond_sql);

  if (is_detail)
    {
      ptr = *loginfo;

      original_sql = (char *) malloc (max_original_size);
      if (original_sql == NULL)
	{
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error;
	}

      sprintf (original_sql, "update [%s] set ", classname);

      ptr = or_unpack_int (ptr, &num_change_col);
      for (i = 0; i < num_change_col; i++)
	{
	  ptr = or_unpack_int (ptr, &change_index);
	}

      attr = class_->ordered_attributes;
      for (i = 0; i < num_change_col; i++)
	{
	  /* check SQL length
	   * cond_sql + column name + length of " = , " */
	  error =
	    flashback_check_and_resize_sql_memory (&original_sql,
						   strlen (original_sql) + strlen (attr->header.name) + 8,
						   &max_original_size);
	  if (error != NO_ERROR)
	    {
	      goto error;
	    }

	  sprintf (original_sql + strlen (original_sql), "[%s] = ", attr->header.name);

	  flashback_process_column_data (&ptr, &original_sql, &max_original_size, attr->type->id);
	  if (i != num_change_col - 1)
	    {
	      strcat (original_sql, ", ");
	    }

	  attr = attr->order_link;
	}

      /* check SQL length
       * original_sql + length of " where " */
      error = flashback_check_and_resize_sql_memory (&original_sql, strlen (original_sql) + 7, &max_original_size);
      if (error != NO_ERROR)
	{
	  goto error;
	}

      strcat (original_sql, " where ");

      ptr = or_unpack_int (ptr, &num_cond_col);
      for (i = 0; i < num_cond_col; i++)
	{
	  ptr = or_unpack_int (ptr, &cond_index);
	}

      attr = class_->ordered_attributes;
      for (i = 0; i < num_cond_col; i++)
	{
	  /* check SQL length
	   * cond_sql + column name + length of " = and/limit 1;" */
	  error =
	    flashback_check_and_resize_sql_memory (&original_sql,
						   strlen (original_sql) + strlen (attr->header.name) + 15,
						   &max_original_size);
	  if (error != NO_ERROR)
	    {
	      goto error;
	    }

	  sprintf (original_sql + strlen (original_sql), "[%s] = ", attr->header.name);

	  flashback_process_column_data (&ptr, &original_sql, &max_original_size, attr->type->id);
	  if (i != num_cond_col - 1)
	    {
	      strcat (original_sql, " and ");
	    }
	  else
	    {
	      strcat (original_sql, " limit 1;");
	    }

	  attr = attr->order_link;
	}
    }

  *loginfo = ptr;

  if (is_detail)
    {
      flashback_print_detail (trid, user, sql, original_sql, outfp);
    }
  else
    {
      fprintf (outfp, "%s\n", sql);
    }

  free_and_init (sql);
  free_and_init (cond_sql);

  if (original_sql != NULL)
    {
      free_and_init (original_sql);
    }

  return NO_ERROR;

error:
  if (sql != NULL)
    {
      free_and_init (sql);
    }

  if (cond_sql != NULL)
    {
      free_and_init (cond_sql);
    }

  if (original_sql != NULL)
    {
      free_and_init (original_sql);
    }

  return error;
}

static int
flashback_print_delete (char **loginfo, int trid, char *user, const char *classname, bool is_detail, FILE * outfp)
{
  MOP classop;
  SM_CLASS *class_;
  SM_ATTRIBUTE *attr;

  int error = NO_ERROR;

  char *sql = NULL;
  int max_sql_size = 1024;

  char *original_sql = NULL;
  int max_original_size = 1024;

  char *ptr;

  int num_change_col;

  int num_cond_col;
  int cond_index;

  int i = 0;

  sql = (char *) malloc (max_sql_size);
  if (sql == NULL)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  ptr = *loginfo;

  classop = sm_find_class (classname);
  error = au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      goto error;
    }

  sprintf (sql, "insert into [%s] values ( ", classname);

  ptr = or_unpack_int (ptr, &num_change_col);
  ptr = or_unpack_int (ptr, &num_cond_col);

  for (i = 0; i < num_cond_col; i++)
    {
      ptr = or_unpack_int (ptr, &cond_index);
    }

  attr = class_->ordered_attributes;
  for (i = 0; i < num_cond_col; i++)
    {
      /* check SQL length
       * sql + length of ", " */
      error = flashback_check_and_resize_sql_memory (&sql, strlen (sql) + 3, &max_sql_size);
      if (error != NO_ERROR)
	{
	  goto error;
	}

      flashback_process_column_data (&ptr, &sql, &max_sql_size, attr->type->id);
      if (i != num_cond_col - 1)
	{
	  strcat (sql, ", ");
	}
      else
	{
	  strcat (sql, " );");
	}

      attr = attr->order_link;
    }

  if (is_detail)
    {
      ptr = *loginfo;

      original_sql = (char *) malloc (max_original_size);
      if (original_sql == NULL)
	{
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error;
	}
      sprintf (original_sql, "delete from [%s] where ", classname);

      ptr = or_unpack_int (ptr, &num_change_col);
      ptr = or_unpack_int (ptr, &num_cond_col);

      for (i = 0; i < num_cond_col; i++)
	{
	  ptr = or_unpack_int (ptr, &cond_index);
	}

      attr = class_->ordered_attributes;
      for (i = 0; i < num_cond_col; i++)
	{
	  /* check SQL length
	   * cond_sql + column name + length of " = and/limit 1" */
	  error =
	    flashback_check_and_resize_sql_memory (&sql, strlen (original_sql) + strlen (attr->header.name) + 15,
						   &max_sql_size);
	  if (error != NO_ERROR)
	    {
	      goto error;
	    }

	  sprintf (original_sql + strlen (original_sql), "[%s] = ", attr->header.name);

	  flashback_process_column_data (&ptr, &original_sql, &max_original_size, attr->type->id);

	  if (i != num_cond_col - 1)
	    {
	      strcat (original_sql, " and ");
	    }
	  else
	    {
	      strcat (original_sql, " limit 1;");
	    }

	  attr = attr->order_link;
	}
    }

  *loginfo = ptr;

  if (is_detail)
    {
      flashback_print_detail (trid, user, sql, original_sql, outfp);
    }
  else
    {
      fprintf (outfp, "%s\n", sql);
    }

  free_and_init (sql);
  if (original_sql != NULL)
    {
      free_and_init (original_sql);
    }

  return NO_ERROR;

error:
  if (sql != NULL)
    {
      free_and_init (sql);
    }

  if (original_sql != NULL)
    {
      free_and_init (original_sql);
    }

  return error;
}

static int
flashback_print_insert (char **loginfo, int trid, char *user, const char *classname, bool is_detail, FILE * outfp)
{
  MOP classop;
  SM_CLASS *class_;
  SM_ATTRIBUTE *attr;

  int error = NO_ERROR;

  char *sql = NULL;
  int max_sql_size = 1024;

  char *original_sql = NULL;
  int max_original_size = 1024;

  char *ptr;

  int num_change_col;
  int change_index;

  int num_cond_col;

  int i = 0;

  sql = (char *) malloc (max_sql_size);
  if (sql == NULL)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  ptr = *loginfo;

  classop = sm_find_class (classname);
  error = au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      goto error;
    }

  sprintf (sql, "delete from [%s] where ", classname);

  ptr = or_unpack_int (ptr, &num_change_col);
  for (i = 0; i < num_change_col; i++)
    {
      ptr = or_unpack_int (ptr, &change_index);
    }

  attr = class_->ordered_attributes;
  for (i = 0; i < num_change_col; i++)
    {
      /* check SQL length
       * cond_sql + column name + length of " = and/limit 1" */
      error =
	flashback_check_and_resize_sql_memory (&sql, strlen (sql) + strlen (attr->header.name) + 15, &max_sql_size);
      if (error != NO_ERROR)
	{
	  goto error;
	}

      sprintf (sql + strlen (sql), "[%s] = ", attr->header.name);

      flashback_process_column_data (&ptr, &sql, &max_sql_size, attr->type->id);
      if (i != num_change_col - 1)
	{
	  strcat (sql, " and ");
	}
      else
	{
	  strcat (sql, " limit 1;");
	}

      attr = attr->order_link;
    }

  if (is_detail)
    {
      ptr = *loginfo;

      original_sql = (char *) malloc (max_original_size);
      if (original_sql == NULL)
	{
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error;
	}

      sprintf (original_sql, "insert into [%s] values ( ", classname);

      ptr = or_unpack_int (ptr, &num_change_col);
      for (i = 0; i < num_change_col; i++)
	{
	  ptr = or_unpack_int (ptr, &change_index);
	}

      attr = class_->ordered_attributes;
      for (i = 0; i < num_change_col; i++)
	{
	  /* check SQL length
	   * cond_sql + length of ", " */
	  error = flashback_check_and_resize_sql_memory (&original_sql, strlen (original_sql) + 3, &max_original_size);
	  if (error != NO_ERROR)
	    {
	      goto error;
	    }

	  flashback_process_column_data (&ptr, &original_sql, &max_original_size, attr->type->id);
	  if (i != num_change_col - 1)
	    {
	      strcat (original_sql, ", ");
	    }
	  else
	    {
	      strcat (original_sql, " );");
	    }

	  attr = attr->order_link;
	}
    }

  ptr = or_unpack_int (ptr, &num_cond_col);

  *loginfo = ptr;

  if (is_detail)
    {
      flashback_print_detail (trid, user, sql, original_sql, outfp);
    }
  else
    {
      fprintf (outfp, "%s\n", sql);
    }

  free_and_init (sql);
  if (original_sql != NULL)
    {
      free_and_init (original_sql);
    }

  return NO_ERROR;

error:
  if (sql != NULL)
    {
      free_and_init (sql);
    }

  if (original_sql != NULL)
    {
      free_and_init (original_sql);
    }

  return error;
}

int
flashback_print_loginfo (char *loginfo, int num_item, dynamic_array * classlist, OID * oidlist, bool is_detail,
			 FILE * outfp)
{
  int length;
  TRANID trid;
  char *user;
  int dataitem_type;

  int error = NO_ERROR;

  FLASHBACK_DML_TYPE dml_type;

  INT64 b_classoid;
  OID classoid;
  int class_index = 0;
  char classname[SM_MAX_IDENTIFIER_LENGTH] = "\0";

  char *ptr = loginfo;

  for (int i = 0; i < num_item; i++)
    {
      ptr = PTR_ALIGN (ptr, MAX_ALIGNMENT);
      ptr = or_unpack_int (ptr, &length);
      ptr = or_unpack_int (ptr, &trid);
      ptr = or_unpack_string_nocopy (ptr, &user);
      ptr = or_unpack_int (ptr, &dataitem_type);

      ptr = or_unpack_int (ptr, (int *) &dml_type);
      ptr = or_unpack_int64 (ptr, &b_classoid);
      memcpy (&classoid, &b_classoid, sizeof (OID));

      class_index = flashback_find_class_index (oidlist, da_size (classlist), classoid);

      da_get (classlist, class_index, classname);

      switch (dml_type)
	{
	case FLASHBACK_INSERT:
	  error = flashback_print_insert (&ptr, trid, user, classname, is_detail, outfp);
	  break;
	case FLASHBACK_UPDATE:
	  /* UPDATE */
	  error = flashback_print_update (&ptr, trid, user, classname, is_detail, outfp);
	  break;
	case FLASHBACK_DELETE:
	  /* DELETE */
	  error = flashback_print_delete (&ptr, trid, user, classname, is_detail, outfp);
	  break;
	default:
	  assert (false);
	  break;
	}

      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  return NO_ERROR;
}
