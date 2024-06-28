/*
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
 * unload_object.c: Utility that emits database object definitions in database
 *               object loader format.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <fcntl.h>
#include <assert.h>
#if defined(WINDOWS)
#include <io.h>
#else
#include <unistd.h>
#endif
#include <ctype.h>
#include <sys/stat.h>
#include <math.h>

#include "utility.h"
#include "misc_string.h"
#include "memory_alloc.h"
#include "dbtype.h"
#include "object_representation.h"
#include "work_space.h"
#include "class_object.h"
#include "object_primitive.h"
#include "set_object.h"
#include "db.h"
#include "schema_manager.h"
#include "server_interface.h"
#include "load_object.h"
#include "unload_object_file.h"	// ctshim
#include "db_value_printer.hpp"
#include "network_interface_cl.h"
#include "printer.hpp"

#include "message_catalog.h"
#include "string_opfunc.h"
#if defined(WINDOWS)
#include "porting.h"
#endif

static void print_set (print_output & output_ctx, DB_SET * set);
static int fprint_special_set (TEXT_OUTPUT * tout, DB_SET * set);
static int bfmt_print (int bfmt, const DB_VALUE * the_db_bit, char *string, int max_size);
static const char *strnchr (const char *str, char ch, int nbytes);
static int print_quoted_str (TEXT_OUTPUT * tout, const char *str, int len, int max_token_len);
static void itoa_strreverse (char *begin, char *end);
#if defined(UNUSED_FUNCTION) && !defined(SUPPORT_THREAD_UNLOAD)
static int itoa_print (TEXT_OUTPUT * tout, DB_BIGINT value, int base);
#else
static int itoa_print_base10 (TEXT_OUTPUT * tout, DB_BIGINT value);
#define itoa_print(o, v, b)  itoa_print_base10((o), (v))
#endif
static int fprint_special_strings (TEXT_OUTPUT * tout, DB_VALUE * value);


class text_block_queue
{
private:
  pthread_mutex_t m_cs_lock;
  TEXT_OUTPUT **m_q_blk;
  int m_q_size;
  int m_front;
  int m_rear;

public:
    text_block_queue ()
  {
    m_cs_lock = PTHREAD_MUTEX_INITIALIZER;
    m_q_blk = NULL;
    m_q_size = 0;
    m_front = m_rear = 0;
  }
   ~text_block_queue ()
  {
    if (m_q_blk)
      free (m_q_blk);
  }

  void initqueue (int size)
  {
    assert (m_q_blk == NULL);
    m_q_blk = (TEXT_OUTPUT **) calloc (size, sizeof (TEXT_OUTPUT *));
    m_q_size = size;
    m_front = m_rear = 0;
  }

  bool enqueue (TEXT_OUTPUT * tout)
  {
    pthread_mutex_lock (&m_cs_lock);
    assert (m_q_blk != NULL);
    if ((m_rear + 1) % m_q_size == m_front)
      {				// full
	pthread_mutex_unlock (&m_cs_lock);
	return false;
      }

    ++m_rear %= m_q_size;
    assert (m_q_blk[m_rear] == NULL);
    m_q_blk[m_rear] = tout;
    pthread_mutex_unlock (&m_cs_lock);
    return true;
  }

  TEXT_OUTPUT *dequeue ()
  {
    TEXT_OUTPUT *pt = NULL;

    pthread_mutex_lock (&m_cs_lock);
    assert (m_q_blk != NULL);
    if (m_front == m_rear)
      {
	;			// empty
      }
    else
      {
	++m_front %= m_q_size;
	pt = m_q_blk[m_front];
	m_q_blk[m_front] = NULL;
      }
    pthread_mutex_unlock (&m_cs_lock);
    return pt;
  }
};

pthread_mutex_t g_cs_lock_text_output_free = PTHREAD_MUTEX_INITIALIZER;
TEXT_OUTPUT *g_text_output_freenode = NULL;
class text_block_queue g_text_blk_queue;
int g_fd_handle = -1;

void
init_blk_queue (int size)
{
  g_text_blk_queue.initqueue (size);
}

bool
put_blk_queue (TEXT_OUTPUT * tout)
{
  return g_text_blk_queue.enqueue (tout);

}

TEXT_OUTPUT *
get_blk_queue ()
{
  return g_text_blk_queue.dequeue ();
}


#define JUMP_TAIL_PTR(p)  while((p)->next) (p) = (p)->next
#if defined(SUPPORT_THREAD_UNLOAD)

int
text_print_end (TEXT_OUTPUT * tout)
{
#if 0
  TEXT_OUTPUT *tp;
  TEXT_OUTPUT *head = tout->head;
  for (tp = head; tp && tp->count > 0; tp = head->next)
    {
      /* flush to disk */
#if defined(SUPPORT_THREAD_UNLOAD)
      if (tp->count != write (head->fd, tp->buffer, tp->count))
#else
      if (tout->count != (int) fwrite (tout->buffer, 1, tout->count, tout->fp))
#endif
	{
	  return ER_IO_WRITE;
	}

      /* re-init */
      tp->ptr = tp->buffer;
      tp->count = 0;

      if (head != tp)
	{
	  head->next = tp->next;
	  release_text_output_mem (tp);
	}
    }
  head->next = NULL;

#else
  if (tout && tout->head)
    {
      TEXT_OUTPUT *tp;
      TEXT_OUTPUT *head = tout->head;

      if (head->count <= 0)
	{
	  for (tp = head; tp; tp = head->next)
	    {
	      /* re-init */
	      tp->ptr = tp->buffer;
	      tp->count = 0;
	      head->next = tp->next;
	      release_text_output_mem (tp);
	    }

	  return NO_ERROR;
	}

      do
	{
	  if (put_blk_queue (tout->head))
	    break;
	  usleep (100);
	}
      while (true);
    }
#endif

  return NO_ERROR;
}

bool
init_text_output_mem (int size)
{
  int i;
  TEXT_OUTPUT *tp;

  pthread_mutex_lock (&g_cs_lock_text_output_free);
  assert (g_text_output_freenode == NULL);
  for (i = 0; i < size; i++)
    {
      tp = (TEXT_OUTPUT *) calloc (sizeof (TEXT_OUTPUT), 1);
      if (tp == NULL)
	return false;

      if (g_text_output_freenode)
	tp->next = g_text_output_freenode;
      else
	tp->next = NULL;

      g_text_output_freenode = tp;
    }
  pthread_mutex_unlock (&g_cs_lock_text_output_free);

  return true;
}

void
quit_text_output_mem ()
{
  TEXT_OUTPUT *tp;
  pthread_mutex_lock (&g_cs_lock_text_output_free);
  assert (g_text_output_freenode != NULL);
  while (g_text_output_freenode)
    {
      tp = g_text_output_freenode;
      g_text_output_freenode = tp->next;

      if (tp->buffer)
	{
	  free (tp->buffer);
	}
      free (tp);
    }
  pthread_mutex_unlock (&g_cs_lock_text_output_free);
}

TEXT_OUTPUT *
get_text_output_mem (TEXT_OUTPUT * head_ptr)
{
  TEXT_OUTPUT *tp = NULL;

  pthread_mutex_lock (&g_cs_lock_text_output_free);
  if (g_text_output_freenode)
    {
      tp = g_text_output_freenode;
      g_text_output_freenode = tp->next;
    }
  pthread_mutex_unlock (&g_cs_lock_text_output_free);

  if (!tp)
    {
      tp = (TEXT_OUTPUT *) calloc (sizeof (TEXT_OUTPUT), 1);
      assert (tp);
    }

  tp->head = head_ptr ? head_ptr : tp;
  tp->next = NULL;
  return tp;
}

void
release_text_output_mem (TEXT_OUTPUT * to)
{
  pthread_mutex_lock (&g_cs_lock_text_output_free);
  to->next = g_text_output_freenode;
  to->head = NULL;
  g_text_output_freenode = to;
  pthread_mutex_unlock (&g_cs_lock_text_output_free);
}


#endif

/*
 * print_set - Print the contents of a real DB_SET (not a set descriptor).
 *    return: void
 *    output_ctx(in): output context
 *    set(in): set reference
 */
static void
print_set (print_output & output_ctx, DB_SET * set)
{
  DB_VALUE element_value;
  int len, i;
  len = set_size (set);
  output_ctx ("{");
  for (i = 0; i < len; i++)
    {
      if (set_get_element (set, i, &element_value) == NO_ERROR)
	{
	  desc_value_print (output_ctx, &element_value);
	  if (i < len - 1)
	    {
	      output_ctx (", ");
	    }
	}
    }
  output_ctx ("}");
}

/*
 * fprint_special_set - Print the contents of a real DB_SET (not a set
 * descriptor).
 *    return: NO_ERROR, if successful, error code otherwise
 *    tout(in/out): TEXT_OUTPUT structure
 *    set(in): set reference
 */
static int
fprint_special_set (TEXT_OUTPUT * tout, DB_SET * set)
{
  int error = NO_ERROR;
  DB_VALUE element_value;
  int len, i;
  len = set_size (set);
  CHECK_PRINT_ERROR (text_print (tout, "{", 1, NULL));
  for (i = 0; i < len; i++)
    {
      if (set_get_element (set, i, &element_value) == NO_ERROR)
	{
	  CHECK_PRINT_ERROR (desc_value_special_fprint (tout, &element_value));
	  if (i < len - 1)
	    {
	      CHECK_PRINT_ERROR (text_print (tout, ",\n ", 2, NULL));
	    }
	}
    }
  CHECK_PRINT_ERROR (text_print (tout, "}", 1, NULL));
exit_on_end:
  return error;
exit_on_error:
  CHECK_EXIT_ERROR (error);
  goto exit_on_end;
}

/*
 * bfmt_print - Change the given string to a representation of the given bit
 * string value in the given format.
 *    return: -1 if max_size too small, 0 if successful
 *    bfmt(in): format of bit string (binary or hex format)
 *    the_db_bit(in): input DB_VALUE
 *    string(out): output buffer
 *    max_size(in): size of string
 * Note:
 *   max_size specifies the maximum number of chars that can be stored in
 *   the string (including final '\0' char); if this is not long enough to
 *   contain the new string, then an error is returned.
 */
#define  MAX_DISPLAY_COLUMN    70
#define DBL_MAX_DIGITS    ((int)ceil(DBL_MAX_EXP * log10(FLT_RADIX)))

#define BITS_IN_BYTE            8
#define HEX_IN_BYTE             2
#define BITS_IN_HEX             4
#define BYTE_COUNT(bit_cnt)     (((bit_cnt)+BITS_IN_BYTE-1)/BITS_IN_BYTE)
#define BYTE_COUNT_HEX(bit_cnt) (((bit_cnt)+BITS_IN_HEX-1)/BITS_IN_HEX)

static int
bfmt_print (int bfmt, const DB_VALUE * the_db_bit, char *string, int max_size)
{
  /*
   * Description:
   */
  int length = 0;
  int string_index = 0;
  int byte_index;
  int bit_index;
  const char *bstring;
  int error = NO_ERROR;
  static char digits[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
  };
  /* Get the buffer and the length from the_db_bit */
  bstring = db_get_bit (the_db_bit, &length);
  switch (bfmt)
    {
    case 0:			/* BIT_STRING_BINARY */
      if (length + 1 > max_size)
	{
	  error = -1;
	}
      else
	{
	  for (byte_index = 0; byte_index < BYTE_COUNT (length); byte_index++)
	    {
	      for (bit_index = 7; bit_index >= 0 && string_index < length; bit_index--)
		{
		  *string = digits[((bstring[byte_index] >> bit_index) & 0x1)];
		  string++;
		  string_index++;
		}
	    }
	  *string = '\0';
	}
      break;
    case 1:			/* BIT_STRING_HEX */
      if (BYTE_COUNT_HEX (length) + 1 > max_size)
	{
	  error = -1;
	}
      else
	{
	  for (byte_index = 0; byte_index < BYTE_COUNT (length); byte_index++)
	    {
	      *string = digits[((bstring[byte_index] >> BITS_IN_HEX) & 0x0f)];
	      string++;
	      string_index++;
	      if (string_index < BYTE_COUNT_HEX (length))
		{
		  *string = digits[((bstring[byte_index] & 0x0f))];
		  string++;
		  string_index++;
		}
	    }
	  *string = '\0';
	}
      break;
    default:
      break;
    }

  return error;
}

/*
 * strnchr - strchr with string length constraints
 *    return: a pointer to the given 'ch', or a null pointer if not found
 *    str(in): string
 *    ch(in): character to find
 *    nbytes(in): length of string
 */
const static char *
strnchr (const char *str, char ch, int nbytes)
{
  for (; nbytes; str++, nbytes--)
    {
      if (*str == ch)
	{
	  return str;
	}
    }
  return NULL;
}


static int
flush_quoted_str (TEXT_OUTPUT * tout, const char *st, int tlen)
{
  int ret = NO_ERROR;
  int cpsize;
  int capacity = tout->iosize - tout->count;

  if (capacity <= tlen)
    {
      cpsize = (capacity < tlen) ? capacity : tlen;
      memcpy (tout->ptr, st, cpsize);
      tout->ptr += cpsize;
      tout->count += cpsize;
      tlen -= cpsize;
      assert (tout->count == tout->iosize);
      ret = text_print_flush (tout);
      JUMP_TAIL_PTR (tout);
      if ((ret == NO_ERROR) && (tlen > 0))
	{
	  memcpy (tout->ptr, st + cpsize, tlen);
	  tout->ptr += tlen;
	  tout->count += tlen;
	}
    }
  else
    {
      memcpy (tout->ptr, st, tlen);
      tout->ptr += tlen;
      tout->count += tlen;
    }

  return ret;
}


/*
 * print_quoted_str - print quoted string sequences separated by new line to
 * TEXT_OUTPUT given
 *    return: NO_ERROR if successful, error code otherwise
 *    tout(out): destination buffer
 *    str(in) : string input
 *    len(in): length of string
 *    max_token_len(in): width of string to format
 * Note:
 *  FIXME :: return error in fwrite...
 */
static int
print_quoted_str (TEXT_OUTPUT * tout, const char *str, int len, int max_token_len)
{
#if defined(SUPPORT_THREAD_UNLOAD)
  int error = NO_ERROR;
  const char *end;
  int write_len = 0;
  const char *st;

  JUMP_TAIL_PTR (tout);

  if (tout->iosize <= tout->count)
    {
      assert (tout->iosize == tout->count);
      CHECK_PRINT_ERROR (text_print_flush (tout));
      JUMP_TAIL_PTR (tout);
    }

  /* opening quote */
  tout->ptr[0] = '\'';
  tout->ptr++;
  tout->count++;

  end = str + len;
  for (st = str; str < end && *str; str++)
    {
      if (*str == '\'')
	{
	  if ((error = flush_quoted_str (tout, st, (int) (str - st))) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  if ((error = flush_quoted_str (tout, "''", 2)) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  write_len += 2;
	  st = str + 1;		// reset start point
	}
      else if (++write_len >= max_token_len)
	{
	  if ((error = flush_quoted_str (tout, st, ((int) (str - st) + 1))) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  error = ((str + 1) < end && str[1]) ? flush_quoted_str (tout, "'+\n '", 5) : flush_quoted_str (tout, "'", 1);
	  if (error != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  write_len = 0;	// reset the number of characters written per line.
	  st = str + 1;		// reset start point
	}
    }

  if (st < str)
    {
      if ((error = flush_quoted_str (tout, st, ((int) (str - st)))) != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  if (tout->iosize <= tout->count)
    {
      CHECK_PRINT_ERROR (text_print_flush (tout));
      JUMP_TAIL_PTR (tout);
    }

  /* closing quote */
  tout->ptr[0] = '\'';
  tout->ptr++;
  tout->count++;

#else // #if defined(SUPPORT_THREAD_UNLOAD)
  int error = NO_ERROR;
  const char *p, *end;
  int partial_len, write_len, left_nbytes;
  const char *internal_quote_p;
  /* opening quote */
  CHECK_PRINT_ERROR (text_print (tout, "'", 1, NULL));
  left_nbytes = 0;
  internal_quote_p = strnchr (str, '\'', len);	/* first found single-quote */
  for (p = str, end = str + len, partial_len = len; p < end; p += write_len, partial_len -= write_len)
    {
      write_len = MIN (partial_len, left_nbytes > 0 ? left_nbytes : max_token_len);
      if (internal_quote_p == NULL || (p + write_len <= internal_quote_p))
	{
	  /* not found single-quote in write_len */
	  CHECK_PRINT_ERROR (text_print (tout, p, write_len, NULL));
	  if (p + write_len < end)	/* still has something to work */
	    {
	      CHECK_PRINT_ERROR (text_print (tout, "\'+\n \'", 5, NULL));
	    }
	  left_nbytes = 0;
	}
      else
	{
	  left_nbytes = write_len;
	  write_len = CAST_STRLEN (internal_quote_p - p + 1);
	  CHECK_PRINT_ERROR (text_print (tout, p, write_len, NULL));
	  left_nbytes -= (write_len + 1);
	  /*
	   * write internal "'" as "''", check for still has something to
	   * work
	   */
	  CHECK_PRINT_ERROR (text_print
			     (tout, (left_nbytes <= 0) ? "'\'+\n \'" : "'", (left_nbytes <= 0) ? 6 : 1, NULL));
	  /* found the next single-quote */
	  internal_quote_p = strnchr (p + write_len, '\'', partial_len - write_len);
	}
    }

  /* closing quote */
  CHECK_PRINT_ERROR (text_print (tout, "'", 1, NULL));
#endif // #if defined(SUPPORT_THREAD_UNLOAD)

exit_on_end:
  return error;
exit_on_error:
  CHECK_EXIT_ERROR (error);
  goto exit_on_end;
}

#define INTERNAL_BUFFER_SIZE (400)	/* bigger than DBL_MAX_DIGITS */

#if defined(UNUSED_FUNCTION) && !defined(SUPPORT_THREAD_UNLOAD)
/*
 * itoa_strreverse - reverse a string
 *    return: void
 *    begin(in/out): begin position of a string
 *    end(in/out): end position of a string
 */
static void
itoa_strreverse (char *begin, char *end)
{
  char aux;
  while (end > begin)
    {
      aux = *end;
      *end-- = *begin;
      *begin++ = aux;
    }
}

/*
 * itoa_print - 'itoa' print to TEXT_OUTPUT
 *    return: NO_ERROR, if successful, error number, if not successful.
 *    tout(out): output
 *    value(in): value container
 *    base(in): radix
 * Note:
 *     Ansi C "itoa" based on Kernighan & Ritchie's "Ansi C"
 *     with slight modification to optimize for specific architecture:
 */
static int
itoa_print (TEXT_OUTPUT * tout, DB_BIGINT value, int base)
{
  int error = NO_ERROR;
  char *wstr;
  bool is_negative;
  DB_BIGINT quotient;
  DB_BIGINT remainder;
  int nbytes;
  static const char itoa_digit[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  wstr = tout->ptr;
  /* Validate base */
  if (base < 2 || base > 35)
    {
      goto exit_on_error;	/* give up */
    }

  /* Take care of sign - in case of INT_MIN, it remains as it is */
  is_negative = (value < 0) ? true : false;
  if (is_negative)
    {
      value = -value;		/* change to the positive number */
    }

  /* Conversion. Number is reversed. */
  do
    {
      quotient = value / base;
      remainder = value % base;
      *wstr++ = itoa_digit[(remainder >= 0) ? remainder : -remainder];
    }
  while ((value = quotient) != 0);
  if (is_negative)
    {
      *wstr++ = '-';
    }
  *wstr = '\0';			/* Null terminate */
  /* Reverse string */
  itoa_strreverse (tout->ptr, wstr - 1);
  nbytes = CAST_STRLEN (wstr - tout->ptr);
  tout->ptr += nbytes;
  tout->count += nbytes;
exit_on_end:
  return error;
exit_on_error:
  CHECK_EXIT_ERROR (error);
  goto exit_on_end;
}
#else
static int
itoa_print_base10 (TEXT_OUTPUT * tout, DB_BIGINT value)
{
  int nbytes;

  JUMP_TAIL_PTR (tout);

  nbytes = sprintf (tout->ptr, "%" PRId64, value);
  if (nbytes < 0)
    {
      return ER_GENERIC_ERROR;
    }

  tout->ptr += nbytes;
  tout->count += nbytes;
  return NO_ERROR;
}
#endif

/*
 * fprint_special_strings - print special DB_VALUE to TEXT_OUTPUT
 *    return: NO_ERROR if successful, error code otherwise
 *    tout(out): output
 *    value(in): DB_VALUE
 */
static int
fprint_special_strings (TEXT_OUTPUT * tout, DB_VALUE * value)
{
  int error = NO_ERROR;
  char buf[INTERNAL_BUFFER_SIZE];
  char *ptr;
  const char *str_ptr = NULL;
  char *json_body = NULL;
  DB_TYPE type;
  int len;
  DB_DATETIMETZ *dt_tz;
  DB_TIMESTAMPTZ *ts_tz;
  type = DB_VALUE_TYPE (value);

  JUMP_TAIL_PTR (tout);

  switch (type)
    {
    case DB_TYPE_NULL:
      CHECK_PRINT_ERROR (text_print (tout, "NULL", 4, NULL));
      break;
    case DB_TYPE_BIGINT:
      if (tout->iosize - tout->count < INTERNAL_BUFFER_SIZE)
	{
	  /* flush remaining buffer */
	  CHECK_PRINT_ERROR (text_print_flush (tout));
	  JUMP_TAIL_PTR (tout);
	}
      CHECK_PRINT_ERROR (itoa_print (tout, db_get_bigint (value), 10 /* base */ ));
      break;
    case DB_TYPE_INTEGER:
      if (tout->iosize - tout->count < INTERNAL_BUFFER_SIZE)
	{
	  /* flush remaining buffer */
	  CHECK_PRINT_ERROR (text_print_flush (tout));
	  JUMP_TAIL_PTR (tout);
	}
      CHECK_PRINT_ERROR (itoa_print (tout, db_get_int (value), 10 /* base */ ));
      break;
    case DB_TYPE_SMALLINT:
      if (tout->iosize - tout->count < INTERNAL_BUFFER_SIZE)
	{
	  /* flush remaining buffer */
	  CHECK_PRINT_ERROR (text_print_flush (tout));
	  JUMP_TAIL_PTR (tout);
	}
      CHECK_PRINT_ERROR (itoa_print (tout, db_get_short (value), 10 /* base */ ));
      break;
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
      {
	char *pos;
	pos = tout->ptr;
	CHECK_PRINT_ERROR (text_print
			   (tout, NULL, 0, "%.*g", (type == DB_TYPE_FLOAT) ? 10 : 17,
			    (type == DB_TYPE_FLOAT) ? db_get_float (value) : db_get_double (value)));
	/* if tout flushed, then this float/double should be the first content */
	if ((pos < tout->ptr && !strchr (pos, '.')) || (pos > tout->ptr && !strchr (tout->buffer, '.')))
	  {
	    CHECK_PRINT_ERROR (text_print (tout, ".", 1, NULL));
	  }
      }
      break;
    case DB_TYPE_ENUMERATION:
      if (tout->iosize - tout->count < INTERNAL_BUFFER_SIZE)
	{
	  /* flush remaining buffer */
	  CHECK_PRINT_ERROR (text_print_flush (tout));
	  JUMP_TAIL_PTR (tout);
	}
      CHECK_PRINT_ERROR (itoa_print (tout, db_get_enum_short (value), 10 /* base */ ));
      break;
    case DB_TYPE_DATE:
      db_date_to_string (buf, MAX_DISPLAY_COLUMN, db_get_date (value));
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "date '%s'", buf));
      break;
    case DB_TYPE_TIME:
      db_time_to_string (buf, MAX_DISPLAY_COLUMN, db_get_time (value));
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "time '%s'", buf));
      break;
    case DB_TYPE_TIMESTAMP:
      db_timestamp_to_string (buf, MAX_DISPLAY_COLUMN, db_get_timestamp (value));
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "timestamp '%s'", buf));
      break;
    case DB_TYPE_TIMESTAMPLTZ:
      db_timestampltz_to_string (buf, MAX_DISPLAY_COLUMN, db_get_timestamp (value));
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "timestampltz '%s'", buf));
      break;
    case DB_TYPE_TIMESTAMPTZ:
      ts_tz = db_get_timestamptz (value);
      db_timestamptz_to_string (buf, MAX_DISPLAY_COLUMN, &ts_tz->timestamp, &ts_tz->tz_id);
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "timestamptz '%s'", buf));
      break;
    case DB_TYPE_DATETIME:
      db_datetime_to_string (buf, MAX_DISPLAY_COLUMN, db_get_datetime (value));
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "datetime '%s'", buf));
      break;
    case DB_TYPE_DATETIMELTZ:
      db_datetimeltz_to_string (buf, MAX_DISPLAY_COLUMN, db_get_datetime (value));
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "datetimeltz '%s'", buf));
      break;
    case DB_TYPE_DATETIMETZ:
      dt_tz = db_get_datetimetz (value);
      db_datetimetz_to_string (buf, MAX_DISPLAY_COLUMN, &dt_tz->datetime, &dt_tz->tz_id);
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "datetimetz '%s'", buf));
      break;
    case DB_TYPE_MONETARY:
      /* Always print symbol before value, even if for turkish lira the user format is after value :
       * intl_get_currency_symbol_position */
      CHECK_PRINT_ERROR (text_print
			 (tout, NULL, 0, "%s%.*f", intl_get_money_esc_ISO_symbol (db_get_monetary (value)->type), 2,
			  db_get_monetary (value)->amount));
      break;
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      CHECK_PRINT_ERROR (text_print (tout, "N", 1, NULL));
      /* fall through */
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
      str_ptr = db_get_string (value);
      len = db_get_string_size (value);
      if (len < 0)
	{
	  len = (int) strlen (str_ptr);
	}

      CHECK_PRINT_ERROR (print_quoted_str (tout, str_ptr, len, MAX_DISPLAY_COLUMN));
      break;
    case DB_TYPE_NUMERIC:
      ptr = numeric_db_value_print (value, buf);
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, !strchr (ptr, '.') ? "%s." : "%s", ptr));
      break;
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      {
	int max_size = ((db_get_string_length (value) + 3) / 4) + 1;
	if (max_size > INTERNAL_BUFFER_SIZE)
	  {
	    ptr = (char *) malloc (max_size);
	    if (ptr == NULL)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) max_size);
		break;		/* FIXME */
	      }
	  }
	else
	  {
	    ptr = buf;
	  }

	if (bfmt_print (1 /* BIT_STRING_HEX */ , value, ptr, max_size) ==
	    NO_ERROR)
	  {
	    CHECK_PRINT_ERROR (text_print (tout, "X", 1, NULL));
	    CHECK_PRINT_ERROR (print_quoted_str (tout, ptr, max_size - 1, MAX_DISPLAY_COLUMN));
	  }

	if (ptr != buf)
	  {
	    free_and_init (ptr);
	  }
	break;
      }

      /* other stubs */
    case DB_TYPE_ERROR:
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "%d", db_get_error (value)));
      break;
    case DB_TYPE_POINTER:
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "%p", db_get_pointer (value)));
      break;
    case DB_TYPE_JSON:
      json_body = db_get_json_raw_body (value);
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "'%s'", json_body));
      db_private_free (NULL, json_body);
      break;
    default:
      /* the others are handled by callers or internal-use only types */
      break;
    }

exit_on_end:
  return error;
exit_on_error:
  CHECK_EXIT_ERROR (error);
  goto exit_on_end;
}

/*
 * desc_value_special_fprint - Print a description of the given value.
 *    return: NO_ERROR, if successful, error number, if not successful.
 *    tout(out):  TEXT_OUTPUT
 *    value(in): value container
 * Note:
 *    This is based on db_value_print() but has extensions for the
 *    handling of set descriptors, and ELO's used by the desc_ module.
 *    String printing is also hacked for "unprintable" characters.
 */
int
desc_value_special_fprint (TEXT_OUTPUT * tout, DB_VALUE * value)
{
  int error = NO_ERROR;
  JUMP_TAIL_PTR (tout);
  switch (DB_VALUE_TYPE (value))
    {
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      CHECK_PRINT_ERROR (fprint_special_set (tout, db_get_set (value)));
      break;
    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_CANT_PRINT_ELO));
      break;
    default:
      CHECK_PRINT_ERROR (fprint_special_strings (tout, value));
      break;
    }

exit_on_end:
  return error;
exit_on_error:
  CHECK_EXIT_ERROR (error);
  goto exit_on_end;
}

/*
 * desc_value_print - Print a description of the given value.
 *    return: void
 *    output_ctx(in): output context
 *    value(in): value container
 * Note:
 *    This is based on db_value_print() but has extensions for the
 *    handling of set descriptors, and ELO's used by the desc_ module.
 *    String printing is also hacked for "unprintable" characters.
 */
void
desc_value_print (print_output & output_ctx, DB_VALUE * value)
{
  switch (DB_VALUE_TYPE (value))
    {
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      print_set (output_ctx, db_get_set (value));
      break;
    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_CANT_PRINT_ELO));
      break;
    default:
      db_print_value (output_ctx, value);
      break;
    }
}


/*
 * text_print_flush - flush TEXT_OUTPUT contents to file
 *    return: NO_ERROR if successful, ER_IO_WRITE if file I/O error occurred
 *    tout(in/out): TEXT_OUTPUT structure
 */
int
text_print_flush (TEXT_OUTPUT * tout)
{
#if 1
  TEXT_OUTPUT *tp;
  JUMP_TAIL_PTR (tout);
  tp = get_text_output_mem (tout->head);
  if (tp->buffer == NULL)
    {				// alloc_text_output (TEXT_OUTPUT * obj_out, int blk_size)

      tp->iosize = tout->iosize;
      tp->buffer = (char *) malloc (tout->iosize + 1);
      tp->ptr = tp->buffer;	/* init */
      tp->count = 0;		/* init */
      tp->next = NULL;
    }

  tout->next = tp;


  //tout = tout->next;
#else /////////////////////////////////////////////////////////////////////////
  /* flush to disk */
#if defined(SUPPORT_THREAD_UNLOAD)
  if (tout->count != write (tout->fd, tout->buffer, tout->count))
#else
  if (tout->count != (int) fwrite (tout->buffer, 1, tout->count, tout->fp))
#endif
    {
      return ER_IO_WRITE;
    }

  /* re-init */
  tout->ptr = tout->buffer;
  tout->count = 0;
#endif
  return NO_ERROR;
}

/*
 * text_print - print formatted text to TEXT_OUTPUT
 *    return: NO_ERROR if successful, error code otherwise
 *    tout(out): TEXT_OUTPUT
 *    buf(in): source buffer
 *    buflen(in): length of buffer
 *    fmt(in): format string
 *    ...(in): arguments
 */
int
text_print (TEXT_OUTPUT * tout, const char *buf, int buflen, char const *fmt, ...)
{
  int error = NO_ERROR;
  int nbytes, size;
  va_list ap;
  assert (buflen >= 0);

start:
  JUMP_TAIL_PTR (tout);

  size = tout->iosize - tout->count;	/* free space size */
  if (buflen)
    {
      nbytes = buflen;		/* unformatted print */
    }
  else
    {
      va_start (ap, fmt);
      nbytes = vsnprintf (tout->ptr, size, fmt, ap);
      va_end (ap);
    }

  if (nbytes > 0)
    {
      if (nbytes < size)
	{			/* OK */
	  if (buflen > 0)
	    {			/* unformatted print */
	      assert (buf != NULL);
	      memcpy (tout->ptr, buf, buflen);
	      *(tout->ptr + buflen) = '\0';	/* Null terminate */
	    }
	  tout->ptr += nbytes;
	  tout->count += nbytes;
	}
      else
	{			/* need more buffer */
	  CHECK_PRINT_ERROR (text_print_flush (tout));
	  goto start;		/* retry */
	}
    }

exit_on_end:
  return error;
exit_on_error:
  CHECK_EXIT_ERROR (error);
  goto exit_on_end;
}
