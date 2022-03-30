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
 * classname_list (in) : classnames
 * oidlist (in)        : OID of classnames
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
