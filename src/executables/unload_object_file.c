/*
 *
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
 * unload_object_file.c: 
 */

#ident "$Id$"

#if !defined(WINDOWS)
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#endif

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
#include <errno.h>

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
#include "large_object.h"
#include "server_interface.h"
#include "load_object.h"
#include "object_print.h"
#include "unload_object_file.h"
#include "network_interface_cl.h"

#include "message_catalog.h"
#include "string_opfunc.h"
#include "porting.h"

volatile bool error_occurred = false;
int g_io_buffer_size = 4096;
int g_fd_handle = INVALID_FILE_NO;
bool g_multi_thread_mode = false;
UNLD_THR_PARAM *g_thr_param = NULL;
int g_sampling_records = -1;

static int fprint_special_set (TEXT_OUTPUT * tout, DB_SET * set);
static int bfmt_print (int bfmt, const DB_VALUE * the_db_bit, char *string, int max_size);
static int print_quoted_str (TEXT_OUTPUT * tout, const char *str, int len, int max_token_len);
static int fprint_special_strings (TEXT_OUTPUT * tout, DB_VALUE * value);

static int write_object_file (TEXT_BUFFER_BLK * head);

#if !defined(WINDOWS)
extern S_WAITING_INFO wi_write_file;
#endif

struct text_buffer_mgr
{
  pthread_mutex_t m_cs_lock;
  TEXT_BUFFER_BLK *m_free_list;
  int m_max_cnt_free_list;
  int m_cnt_free_list;
};

struct write_block_queue
{
  pthread_mutex_t m_cs_lock;
  TEXT_BUFFER_BLK **m_q_blk;
  int m_q_size;
  int m_front;
  int m_rear;
};

// define 
static struct text_buffer_mgr c_text_buf_mgr = { PTHREAD_MUTEX_INITIALIZER, NULL, 0, 0 };
static struct write_block_queue c_write_blk_queue = { PTHREAD_MUTEX_INITIALIZER, NULL, 0, 0, 0 };

  void quit_text_buffer_mgr (struct text_buffer_mgr* ptbm)
  {
    TEXT_BUFFER_BLK *tp;

    pthread_mutex_lock (&ptbm->m_cs_lock);
    while (ptbm->m_free_list)
      {
	tp = ptbm->m_free_list;
	ptbm->m_free_list = tp->next;

	if (tp->buffer)
	  {
	    free (tp->buffer);
	  }
	free (tp);
	ptbm->m_cnt_free_list--;
      }
    pthread_mutex_unlock (&ptbm->m_cs_lock);
  }

  void text_buffer_mgr_constructor (struct text_buffer_mgr* ptbm)
  {    
    pthread_mutex_init (&ptbm->m_cs_lock, NULL);
    ptbm->m_free_list = NULL;
    ptbm->m_max_cnt_free_list = ptbm->m_cnt_free_list = 0;
  }
  void text_buffer_mgr_destrouctor (struct text_buffer_mgr* ptbm)
  {
    quit_text_buffer_mgr (ptbm);
    (void) pthread_mutex_destroy (&ptbm->m_cs_lock);
  }

  bool init_text_buffer_mgr (struct text_buffer_mgr* ptbm, int count)
  {
    pthread_mutex_lock (&ptbm->m_cs_lock);
    assert (ptbm->m_free_list == NULL);
    ptbm->m_max_cnt_free_list = count;
    pthread_mutex_unlock (&ptbm->m_cs_lock);
    return true;
  }

  TEXT_BUFFER_BLK *get_text_buffer (struct text_buffer_mgr* ptbm, int alloc_sz)
  {
    TEXT_BUFFER_BLK *tp = NULL;

    if (alloc_sz <= g_io_buffer_size)
      {
	alloc_sz = g_io_buffer_size;
	pthread_mutex_lock (&ptbm->m_cs_lock);
	if (ptbm->m_free_list)
	  {
	    tp = ptbm->m_free_list;
	    ptbm->m_free_list = tp->next;
	    ptbm->m_cnt_free_list--;
	  }
	pthread_mutex_unlock (&ptbm->m_cs_lock);
      }

    if (!tp)
      {
	tp = (TEXT_BUFFER_BLK *) calloc (sizeof (TEXT_BUFFER_BLK), 1);
	if (tp == NULL)
	  {
	    assert (false);
	    return NULL;
	  }
      }

    tp->next = NULL;
    if (tp->buffer == NULL)
      {
	tp->buffer = (char *) malloc (alloc_sz + 1);
	if (tp->buffer == NULL)
	  {
	    assert (false);
	    free (tp);
	    return NULL;
	  }

	tp->ptr = tp->buffer;
	tp->iosize = alloc_sz;
	tp->count = 0;
      }

    return tp;
  }

  void release_text_buffer (struct text_buffer_mgr* ptbm, TEXT_BUFFER_BLK * tp)
  {
    /* re-init */
    tp->ptr = tp->buffer;
    tp->count = 0;

    if (tp->iosize == g_io_buffer_size)
      {
	pthread_mutex_lock (&ptbm->m_cs_lock);
	if (ptbm->m_cnt_free_list < ptbm->m_max_cnt_free_list)
	  {
	    tp->next = ptbm->m_free_list;
	    ptbm->m_free_list = tp;
	    ptbm->m_cnt_free_list++;
	    pthread_mutex_unlock (&ptbm->m_cs_lock);

	    return;
	  }
	pthread_mutex_unlock (&ptbm->m_cs_lock);
      }

    if (tp->buffer)
      {
	free (tp->buffer);
      }
    free (tp);
  }

void  write_block_queue_constructor (struct write_block_queue* pwbq)
  {
    pthread_mutex_init (&pwbq->m_cs_lock, NULL);
    pwbq->m_q_blk = NULL;
    pwbq->m_q_size = 0;
    pwbq->m_front = pwbq->m_rear = 0;
  }
void write_block_queue_destrouctor (struct write_block_queue* pwbq)
  {
    if (pwbq->m_q_blk)
      {
	TEXT_BUFFER_BLK *head, *pt;
	assert (pwbq->m_front == pwbq->m_rear);
	while (pwbq->m_front != pwbq->m_rear)
	  {
            pwbq->m_front++;    
	    pwbq->m_front %= pwbq->m_q_size;
	    head = pwbq->m_q_blk[pwbq->m_front];
	    pwbq->m_q_blk[pwbq->m_front] = NULL;
	    while (head)
	      {
		pt = head;
		head = head->next;
		if (pt->buffer)
		  {
		    free (pt->buffer);
		  }
		free (pt);
	      }
	  }

	free (pwbq->m_q_blk);
        pwbq->m_q_blk = NULL;
      }

    (void) pthread_mutex_destroy (&pwbq->m_cs_lock);
  }

  bool init_queue (struct write_block_queue* pwbq, int size)
  {
    assert (pwbq->m_q_blk == NULL);
    pwbq->m_front = pwbq->m_rear = 0;
    pwbq->m_q_blk = (TEXT_BUFFER_BLK **) calloc (size, sizeof (TEXT_BUFFER_BLK *));
    if (pwbq->m_q_blk)
      {
	pwbq->m_q_size = size;
	return true;
      }
    else
      {
	pwbq->m_q_size = 0;
	return false;
      }
  }

  bool enqueue (struct write_block_queue* pwbq, TEXT_BUFFER_BLK * tout)
  {
    pthread_mutex_lock (&pwbq->m_cs_lock);
    assert (pwbq->m_q_blk != NULL);
    if ((pwbq->m_rear + 1) % pwbq->m_q_size == pwbq->m_front)
      {				// full
	pthread_mutex_unlock (&pwbq->m_cs_lock);
	return false;
      }

    pwbq->m_rear++;
    pwbq->m_rear %= pwbq->m_q_size;
    assert (pwbq->m_q_blk[pwbq->m_rear] == NULL);
    pwbq->m_q_blk[pwbq->m_rear] = tout;
    pthread_mutex_unlock (&pwbq->m_cs_lock);
    return true;
  }

  TEXT_BUFFER_BLK *dequeue (struct write_block_queue* pwbq)
  {
    TEXT_BUFFER_BLK *pt = NULL;

    pthread_mutex_lock (&pwbq->m_cs_lock);
    assert (pwbq->m_q_blk != NULL);
    if (pwbq->m_front == pwbq->m_rear)
      {
	;			// empty
      }
    else
      {
        pwbq->m_front++;
	pwbq->m_front %= pwbq->m_q_size;
	pt = pwbq->m_q_blk[pwbq->m_front];
	pwbq->m_q_blk[pwbq->m_front] = NULL;
      }
    pthread_mutex_unlock (&pwbq->m_cs_lock);
    return pt;
  }

bool
init_queue_n_list_for_object_file (int q_size, int blk_size)
{
  //text_buffer_mgr_constructor (&c_text_buf_mgr);
  //write_block_queue_constructor(&c_write_blk_queue);

  if (init_queue (&c_write_blk_queue, q_size))
    {
      return init_text_buffer_mgr (&c_text_buf_mgr, blk_size); 
    }

  return false;
}

void
quit_queue_n_list_for_object_file()
{
  write_block_queue_destrouctor  (&c_write_blk_queue);      
  text_buffer_mgr_destrouctor  (&c_text_buf_mgr);  
}

static int
get_text_output_mem (TEXT_OUTPUT * tout, int alloc_sz)
{
  TEXT_BUFFER_BLK *pt = NULL;

  if (alloc_sz > g_io_buffer_size)
    {
      alloc_sz += g_io_buffer_size;
    }
  else
    {
      alloc_sz = g_io_buffer_size;
    }


  pt = get_text_buffer (&c_text_buf_mgr, alloc_sz);
  if (pt == NULL)
    {
      return ER_FAILED;
    }

  if (tout->head_ptr == NULL)
    {
      tout->record_cnt = 0;
      tout->head_ptr = pt;
    }
  else
    {
      tout->tail_ptr->next = pt;
    }
  tout->tail_ptr = pt;

  return NO_ERROR;
}

int
flushing_write_blk_queue ()
{
  TEXT_BUFFER_BLK *head;

  head = dequeue (&c_write_blk_queue);
  if (head == NULL)
    {
      return 0;
    }

  while (head)
    {
      if (write_object_file (head) != NO_ERROR)
	{
	  return -1;
	}

      head = dequeue (&c_write_blk_queue);
    }

  return 1;
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


static int
flush_quoted_str (TEXT_OUTPUT * tout, const char *st, int tlen)
{
  int ret = NO_ERROR;
  int capacity = tout->tail_ptr->iosize - tout->tail_ptr->count;

  if (capacity <= tlen)
    {
      memcpy (tout->tail_ptr->ptr, st, capacity);
      tout->tail_ptr->ptr += capacity;
      tout->tail_ptr->count += capacity;
      tlen -= capacity;
      assert (tout->tail_ptr->count == tout->tail_ptr->iosize);

      ret = get_text_output_mem (tout, tlen);
      if ((ret == NO_ERROR) && (tlen > 0))
	{
	  memcpy (tout->tail_ptr->ptr, st + capacity, tlen);
	  tout->tail_ptr->ptr += tlen;
	  tout->tail_ptr->count += tlen;
	}
    }
  else
    {
      memcpy (tout->tail_ptr->ptr, st, tlen);
      tout->tail_ptr->ptr += tlen;
      tout->tail_ptr->count += tlen;
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
  int error = NO_ERROR;
  const char *end;
  int write_len = 0;
  const char *st;

  if (tout->tail_ptr == NULL)
    {
      assert (tout->head_ptr == NULL);
      CHECK_PRINT_ERROR (get_text_output_mem (tout, len));
    }
  else if (tout->tail_ptr->iosize <= tout->tail_ptr->count)
    {
      assert (tout->tail_ptr->iosize == tout->tail_ptr->count);
      CHECK_PRINT_ERROR (get_text_output_mem (tout, len));
    }

  /* opening quote */
  tout->tail_ptr->ptr[0] = '\'';
  tout->tail_ptr->ptr++;
  tout->tail_ptr->count++;

  end = str + len;
  for (st = str; str < end; str++)
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

	  write_len = 0;	// reset the number of characters written per line.
	  st = str + 1;		// reset start point 

	  if ((str + 1) < end && str[1])
	    {
	      error = flush_quoted_str (tout, "'+\n '", 5);
	      if (error != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }
	}
    }

  if (st < str)
    {
      if ((error = flush_quoted_str (tout, st, ((int) (str - st)))) != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  if (tout->tail_ptr->iosize <= tout->tail_ptr->count)
    {
      CHECK_PRINT_ERROR (get_text_output_mem (tout, 1));
    }

  /* closing quote */
  tout->tail_ptr->ptr[0] = '\'';
  tout->tail_ptr->ptr++;
  tout->tail_ptr->count++;

exit_on_end:
  return error;
exit_on_error:
  CHECK_EXIT_ERROR (error);
  return error;
}

#define INTERNAL_BUFFER_SIZE (400)	/* bigger than DBL_MAX_DIGITS */

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

  type = DB_VALUE_TYPE (value);

  switch (type)
    {
    case DB_TYPE_NULL:
      CHECK_PRINT_ERROR (text_print (tout, "NULL", 4, NULL));
      break;
    case DB_TYPE_BIGINT:
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "%" PRId64, db_get_bigint (value)));
      break;
    case DB_TYPE_INTEGER:
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "%d", db_get_int (value)));
      break;
    case DB_TYPE_SMALLINT:
       CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "%d", (int) db_get_short (value)));
      break;
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
      {
	char *pos;

	if (tout->tail_ptr == NULL)
	  {
	    assert (tout->head_ptr == NULL);
	    CHECK_PRINT_ERROR (get_text_output_mem (tout, -1));
	  }

	pos = tout->tail_ptr->ptr;
	CHECK_PRINT_ERROR (text_print
			   (tout, NULL, 0, "%.*g", (type == DB_TYPE_FLOAT) ? 10 : 17,
			    (type == DB_TYPE_FLOAT) ? db_get_float (value) : db_get_double (value)));
	/* if tout flushed, then this float/double should be the first content */
	if ((pos < tout->tail_ptr->ptr && !strchr (pos, '.'))
	    || (pos > tout->tail_ptr->ptr && !strchr (tout->tail_ptr->buffer, '.')))
	  {
	    CHECK_PRINT_ERROR (text_print (tout, ".", 1, NULL));
	  }
      }
      break;
    case DB_TYPE_ENUMERATION:      
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "%d", (int) db_get_enum_short (value)));
      break;
    case DB_TYPE_DATE:
      db_date_to_string (buf, INTERNAL_BUFFER_SIZE, db_get_date (value));
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "date '%s'", buf));
      break;
    case DB_TYPE_TIME:
      db_time_to_string (buf, INTERNAL_BUFFER_SIZE, db_get_time (value));
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "time '%s'", buf));
      break;
    case DB_TYPE_TIMESTAMP:
      db_timestamp_to_string (buf, INTERNAL_BUFFER_SIZE, db_get_timestamp (value));
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "timestamp '%s'", buf));
      break;
   
    case DB_TYPE_DATETIME:
      db_datetime_to_string (buf, INTERNAL_BUFFER_SIZE, db_get_datetime (value));
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "datetime '%s'", buf));
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

  if (tout->tail_ptr == NULL)
    {
      assert (tout->head_ptr == NULL);
      CHECK_PRINT_ERROR (get_text_output_mem (tout, ((buflen > 0) ? buflen : -1)));
    }

  size = tout->tail_ptr->iosize - tout->tail_ptr->count;	/* free space size */
  if (buflen > 0)
    {
      assert (buf != NULL);

      while (buflen >= size)
	{
	  memcpy (tout->tail_ptr->ptr, buf, size);
	  *(tout->tail_ptr->ptr + size) = '\0';	/* Null terminate */
	  tout->tail_ptr->ptr += size;
	  tout->tail_ptr->count += size;
	  buflen -= size;
	  CHECK_PRINT_ERROR (get_text_output_mem (tout, buflen));
	  size = tout->tail_ptr->iosize;
	}

      if (buflen > 0)
	{
	  memcpy (tout->tail_ptr->ptr, buf, buflen);
	  *(tout->tail_ptr->ptr + buflen) = '\0';	/* Null terminate */

	  tout->tail_ptr->ptr += buflen;
	  tout->tail_ptr->count += buflen;
	}
    }
  else
    {
      va_start (ap, fmt);
      nbytes = vsnprintf (tout->tail_ptr->ptr, size, fmt, ap);
      va_end (ap);

      if (nbytes < 0)
	return ER_FAILED;

      if (nbytes >= size)
	{
	  CHECK_PRINT_ERROR (get_text_output_mem (tout, nbytes));
	  size = tout->tail_ptr->iosize;
	  va_start (ap, fmt);
	  nbytes = vsnprintf (tout->tail_ptr->ptr, size, fmt, ap);
	  va_end (ap);
	  if (nbytes < 0)
	    return ER_FAILED;
	}

      assert (nbytes < size);
      tout->tail_ptr->ptr += nbytes;
      tout->tail_ptr->count += nbytes;
    }

  return error;
exit_on_error:
  CHECK_EXIT_ERROR (error);
  return error;
}

int
text_print_request_flush (TEXT_OUTPUT * tout, bool force)
{
  TEXT_BUFFER_BLK *tp, *head;

  if (tout == NULL || tout->head_ptr == NULL)
    {
      return NO_ERROR;
    }

  head = tout->head_ptr;
  if (head->count <= 0)
    {
      // empty block
      while (head)
	{
	  tp = head;
	  head = head->next;
	  release_text_buffer (&c_text_buf_mgr, tp);
	}

      tout->head_ptr = NULL;
      tout->tail_ptr = NULL;
      tout->record_cnt = 0;
      return NO_ERROR;
    }

  if (!g_multi_thread_mode && g_fd_handle != INVALID_FILE_NO)
    {
      tout->head_ptr = NULL;
      tout->tail_ptr = NULL;
      tout->record_cnt = 0;
      return write_object_file (head);
    }

  int flag = 0;

  tout->record_cnt++;
  if (force || head->next || ((head->iosize - head->count) < (((double) head->count / tout->record_cnt) * 1.5)))
    {
      do
	{
	  if (enqueue (&c_write_blk_queue, head))
	    {
	      break;
	    }

	  if (flag == 0)
	    {
	      TIMER_BEGIN ((g_sampling_records >= 0), &(g_thr_param[tout->ref_thread_param_idx].wi_add_Q));
	      flag++;
	    }

	  YIELD_THREAD ();
	  if (error_occurred)
	    {
	      return ER_FAILED;
	    }
	}
      while (true);
      if (flag)
	{
	  TIMER_END ((g_sampling_records >= 0), &(g_thr_param[tout->ref_thread_param_idx].wi_add_Q));
	}

      tout->head_ptr = NULL;
      tout->tail_ptr = NULL;
      tout->record_cnt = 0;
    }

  return NO_ERROR;
}

static int
write_object_file (TEXT_BUFFER_BLK * head)
{
  TEXT_BUFFER_BLK *tp;
  int error = NO_ERROR;
  int _errno;

  TIMER_BEGIN ((g_sampling_records >= 0), &wi_write_file);
  for (tp = head; tp && tp->count > 0; tp = head)
    {
      head = tp->next;
      /* write to disk */
      if (error == NO_ERROR)
	{
	  if (tp->count != write (g_fd_handle, tp->buffer, tp->count))
	    {
#if defined(WINDOWS)
	      _errno = _doserrno;
#else
	      _errno = errno;
#endif
	      // TODDO: EAGAIN, EINTR ?
	      assert (_errno == EINTR || _errno == EBADF);
	      error = ER_IO_WRITE;
	    }
	}

      release_text_buffer (&c_text_buf_mgr, tp);
    }
  TIMER_END ((g_sampling_records >= 0), &wi_write_file);

  return error;
}

#if !defined(WINDOWS)
void
timespec_diff (struct timespec *start, struct timespec *end, struct timespec *diff)
{
  diff->tv_sec = end->tv_sec - start->tv_sec;
  diff->tv_nsec = end->tv_nsec - start->tv_nsec;
  if (diff->tv_nsec < 0)
    {
      diff->tv_sec--;
      diff->tv_nsec += NANO_PREC_VAL;
    }
}

void
timespec_accumulate (struct timespec *ts_sum, struct timespec *ts_start)
{
  struct timespec te, tdiff;

  clock_gettime (CLOCK_MONOTONIC /* CLOCK_REALTIME */ , &te);
  timespec_diff (ts_start, &te, &tdiff);

  ts_sum->tv_sec += tdiff.tv_sec;
  ts_sum->tv_nsec += tdiff.tv_nsec;
  while (ts_sum->tv_nsec >= NANO_PREC_VAL)
    {
      ts_sum->tv_sec++;
      ts_sum->tv_nsec -= NANO_PREC_VAL;
    }
}

void
timespec_addup (struct timespec *ts_sum, struct timespec *ts_add)
{
  ts_sum->tv_sec += ts_add->tv_sec;
  ts_sum->tv_nsec += ts_add->tv_nsec;
  while (ts_sum->tv_nsec >= NANO_PREC_VAL)
    {
      ts_sum->tv_sec++;
      ts_sum->tv_nsec -= NANO_PREC_VAL;
    }
}

void
print_message_with_time (FILE * fp, const char *msg)
{
  struct timespec specific_time;
  struct tm *now;

  if (fp == NULL)
    {
      return;
    }

  clock_gettime (CLOCK_REALTIME, &specific_time);
  now = localtime (&specific_time.tv_sec);

  fprintf (fp, "time: %04d/%02d/%02d %02d:%02d:%02d.%09ld, %s\n", 1900 + now->tm_year,
	   now->tm_mon + 1, now->tm_mday, now->tm_hour, now->tm_min, now->tm_sec, specific_time.tv_nsec,
	   (msg ? msg : ""));
}
#endif


