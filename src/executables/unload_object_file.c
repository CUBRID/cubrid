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
#include <string.h>
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
#include "unload_object_file.h"
#include "db_value_printer.hpp"
#include "network_interface_cl.h"
#include "printer.hpp"

#include "message_catalog.h"
#include "string_opfunc.h"
#include "porting.h"
#include "language_support.h"
#include "compressor.hpp"
#if defined (SA_MODE)
#include "internal_lob_file.hpp"
#include "thread_manager.hpp"
#endif /* SA_MODE */

volatile bool error_occurred = false;
int g_io_buffer_size = 4096;
int g_fd_handle = INVALID_FILE_NO;
bool g_multi_thread_mode = false;
UNLD_THR_PARAM *g_thr_param = NULL;
int g_sampling_records = -1;


static void print_set (print_output & output_ctx, DB_SET * set);
static int fprint_special_set (TEXT_OUTPUT * tout, DB_SET * set);
static int bfmt_print (int bfmt, const DB_VALUE * the_db_bit, char *string, int max_size);
static int print_quoted_str (TEXT_OUTPUT * tout, const char *str, int len, int max_token_len);
static int fprint_clob_value (TEXT_OUTPUT * tout, DB_VALUE * value);
static int fprint_blob_value (TEXT_OUTPUT * tout, DB_VALUE * value);
static int fprint_special_strings (TEXT_OUTPUT * tout, DB_VALUE * value);

static int write_object_file (TEXT_BUFFER_BLK * head);

#define INTERNAL_LOB_UNLOAD_SUFFIX "_internal_lob"
/* Sidecar format 2: the payload is stored as LZ4-compressed blocks instead of hex, and the
 * file carries the unload-time charset / collation so loaddb can detect a mismatched target.
 * Format 1 (hex body, no metadata line) is still accepted by loaddb. */
#define INTERNAL_LOB_UNLOAD_MAGIC "CUBRID_INTERNAL_LOB_UNLOAD 2\n"
/* Uncompressed bytes per compressed block; also the read granularity on the load side. */
#define INTERNAL_LOB_UNLOAD_BLOCK_SIZE (1024 * 1024)
#define INTERNAL_LOB_UNLOAD_COMPRESS_NONE 'N'
#define INTERNAL_LOB_UNLOAD_COMPRESS_LZ4 'L'
#define INTERNAL_LOB_UNLOAD_LOCATOR_PREFIX "@internal_lob:"
#define INTERNAL_LOB_UNLOAD_CHUNK_SIZE (1024 * 1024)

static FILE *internal_lob_unload_fp = NULL;
static pthread_mutex_t internal_lob_unload_lock = PTHREAD_MUTEX_INITIALIZER;

static bool internal_lob_unload_is_locator_key (const char *data, int size);
static bool internal_lob_unload_parse_locator_metadata (const char *data, int size, DB_BIGINT * length,
							DB_BIGINT * bit_length);
static int internal_lob_unload_sidecar_write_stream (char lob_type, const char *key, int key_len,
						     DB_BIGINT data_len, DB_BIGINT bit_length);
static int fprint_internal_lob_ref_if_locator (TEXT_OUTPUT * tout, DB_VALUE * value, bool * printed);

#if !defined(WINDOWS)
extern S_WAITING_INFO wi_write_file;
#endif
class text_buffer_mgr
{
private:
  pthread_mutex_t m_cs_lock;
  TEXT_BUFFER_BLK *m_free_list;
  int m_max_cnt_free_list;
  int m_cnt_free_list;


  void quit_text_buffer_mgr ()
  {
    TEXT_BUFFER_BLK *tp;

      pthread_mutex_lock (&m_cs_lock);
    while (m_free_list)
      {
	tp = m_free_list;
	m_free_list = tp->next;

	if (tp->buffer)
	  {
	    free (tp->buffer);
	  }
	free (tp);
	m_cnt_free_list--;
      }
    pthread_mutex_unlock (&m_cs_lock);
  }

public:
  text_buffer_mgr ()
  {
    //m_cs_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_init (&m_cs_lock, NULL);
    m_free_list = NULL;
    m_max_cnt_free_list = m_cnt_free_list = 0;
  }
  ~text_buffer_mgr ()
  {
    quit_text_buffer_mgr ();
    (void) pthread_mutex_destroy (&m_cs_lock);
  }

  bool init_text_buffer_mgr (int count)
  {
    pthread_mutex_lock (&m_cs_lock);
    assert (m_free_list == NULL);
    m_max_cnt_free_list = count;
    pthread_mutex_unlock (&m_cs_lock);
    return true;
  }

  TEXT_BUFFER_BLK *get_text_buffer (int alloc_sz)
  {
    TEXT_BUFFER_BLK *tp = NULL;

    if (alloc_sz <= g_io_buffer_size)
      {
	alloc_sz = g_io_buffer_size;
	pthread_mutex_lock (&m_cs_lock);
	if (m_free_list)
	  {
	    tp = m_free_list;
	    m_free_list = tp->next;
	    m_cnt_free_list--;
	  }
	pthread_mutex_unlock (&m_cs_lock);
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

  void release_text_buffer (TEXT_BUFFER_BLK * tp)
  {
    /* re-init */
    tp->ptr = tp->buffer;
    tp->count = 0;

    if (tp->iosize == g_io_buffer_size)
      {
	pthread_mutex_lock (&m_cs_lock);
	if (m_cnt_free_list < m_max_cnt_free_list)
	  {
	    tp->next = m_free_list;
	    m_free_list = tp;
	    m_cnt_free_list++;
	    pthread_mutex_unlock (&m_cs_lock);

	    return;
	  }
	pthread_mutex_unlock (&m_cs_lock);
      }

    if (tp->buffer)
      {
	free (tp->buffer);
      }
    free (tp);
  }
};

class write_block_queue
{
private:
  pthread_mutex_t m_cs_lock;
  TEXT_BUFFER_BLK **m_q_blk;
  int m_q_size;
  int m_front;
  int m_rear;

public:
    write_block_queue ()
  {
    //m_cs_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_init (&m_cs_lock, NULL);
    m_q_blk = NULL;
    m_q_size = 0;
    m_front = m_rear = 0;
  }
   ~write_block_queue ()
  {
    if (m_q_blk)
      {
	TEXT_BUFFER_BLK *head, *pt;
	assert (m_front == m_rear);
	while (m_front != m_rear)
	  {
	    ++m_front %= m_q_size;
	    head = m_q_blk[m_front];
	    m_q_blk[m_front] = NULL;
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

	free (m_q_blk);
      }

    (void) pthread_mutex_destroy (&m_cs_lock);
  }

  bool init_queue (int size)
  {
    assert (m_q_blk == NULL);
    m_front = m_rear = 0;
    m_q_blk = (TEXT_BUFFER_BLK **) calloc (size, sizeof (TEXT_BUFFER_BLK *));
    if (m_q_blk)
      {
	m_q_size = size;
	return true;
      }
    else
      {
	m_q_size = 0;
	return false;
      }
  }

  bool enqueue (TEXT_BUFFER_BLK * tout)
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

  TEXT_BUFFER_BLK *dequeue ()
  {
    TEXT_BUFFER_BLK *pt = NULL;

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

// define 
static class write_block_queue c_write_blk_queue;
static class text_buffer_mgr c_text_buf_mgr;

bool
init_queue_n_list_for_object_file (int q_size, int blk_size)
{
  if (c_write_blk_queue.init_queue (q_size))
    {
      return c_text_buf_mgr.init_text_buffer_mgr (blk_size);
    }

  return false;
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


  pt = c_text_buf_mgr.get_text_buffer (alloc_sz);
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

  head = c_write_blk_queue.dequeue ();
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

      head = c_write_blk_queue.dequeue ();
    }

  return 1;
}

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

inline bool
need_append_dot (const char *val)
{
  while (*val)
    {
      if (*val == '.' || *val == 'e' || *val == 'E')
	{
	  return false;
	}
      val++;
    }
  return true;
}

static bool
internal_lob_unload_is_locator_key (const char *data, int size)
{
  return internal_lob_unload_parse_locator_metadata (data, size, NULL, NULL);
}

int
internal_lob_unload_sidecar_open (const char *output_dirname, const char *output_prefix)
{
  char path[PATH_MAX];
  int written;

  if (output_dirname == NULL)
    {
      output_dirname = ".";
    }
  if (output_prefix == NULL)
    {
      output_prefix = "";
    }

  internal_lob_unload_sidecar_close ();

  written = snprintf (path, sizeof (path), "%s/%s%s", output_dirname, output_prefix, INTERNAL_LOB_UNLOAD_SUFFIX);
  if (written < 0 || written >= (int) sizeof (path))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
      return ER_FAILED;
    }

  internal_lob_unload_fp = fopen (path, "wb");
  if (internal_lob_unload_fp == NULL)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
      return ER_FAILED;
    }

  if (fputs (INTERNAL_LOB_UNLOAD_MAGIC, internal_lob_unload_fp) == EOF)
    {
      internal_lob_unload_sidecar_close ();
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
      return ER_FAILED;
    }

  /* CLOB / BLOB carry no per-value charset or collation; the bytes are interpreted with the
   * database defaults at read time. Record those defaults so loaddb can tell whether the target
   * database would read the same bytes as different characters. */
  if (fprintf (internal_lob_unload_fp, "%s\t%s\n", lang_charset_name (LANG_SYS_CODESET),
	       lang_get_collation_name (LANG_SYS_COLLATION)) < 0)
    {
      internal_lob_unload_sidecar_close ();
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
      return ER_FAILED;
    }

  return NO_ERROR;
}

void
internal_lob_unload_sidecar_close (void)
{
  if (internal_lob_unload_fp != NULL)
    {
      fclose (internal_lob_unload_fp);
      internal_lob_unload_fp = NULL;
    }
}

static int
internal_lob_unload_sidecar_write_block (FILE * fp, const char *data, int len, char *comp_buffer, int comp_capacity,
					 DB_BIGINT * stored_len)
{
  char header[OR_INT_SIZE];
  int compressed_size;

  assert (len > 0 && len <= INTERNAL_LOB_UNLOAD_BLOCK_SIZE);

  compressed_size = cubcompress::compress < cubcompress::LZ4 > (data, len, comp_buffer, comp_capacity,
								cubcompress::lz4_options ());
  if (compressed_size <= 0 || compressed_size > comp_capacity)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
      return ER_FAILED;
    }

  /* [uncompressed size][compressed size][compressed bytes] - the uncompressed size lets the
   * reader size its output buffer without assuming a full block (the tail block is shorter). */
  OR_PUT_INT (header, len);
  if (fwrite (header, 1, OR_INT_SIZE, fp) != OR_INT_SIZE)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
      return ER_FAILED;
    }
  OR_PUT_INT (header, compressed_size);
  if (fwrite (header, 1, OR_INT_SIZE, fp) != OR_INT_SIZE)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
      return ER_FAILED;
    }
  if (fwrite (comp_buffer, 1, (size_t) compressed_size, fp) != (size_t) compressed_size)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
      return ER_FAILED;
    }

  *stored_len += (DB_BIGINT) (OR_INT_SIZE * 2 + compressed_size);
  return NO_ERROR;
}

static bool
internal_lob_unload_parse_locator_metadata (const char *data, int size, DB_BIGINT * length, DB_BIGINT * bit_length)
{
  char locator_buf[128];
  int volid = 0;
  int pageid = 0;
  int slotid = 0;
  long long parsed_length = 0;
  unsigned long long parsed_token = 0;
  int consumed = 0;
  int token_consumed = 0;
  int prefix_len = (int) strlen (INTERNAL_LOB_UNLOAD_LOCATOR_PREFIX);
  char *oid_part = NULL;

  if (length != NULL)
    {
      *length = 0;
    }
  if (bit_length != NULL)
    {
      *bit_length = -1;
    }

  if (data == NULL || size <= prefix_len || size >= (int) sizeof (locator_buf))
    {
      return false;
    }
  if (memcmp (data, INTERNAL_LOB_UNLOAD_LOCATOR_PREFIX, (size_t) prefix_len) != 0)
    {
      return false;
    }

  memcpy (locator_buf, data, (size_t) size);
  locator_buf[size] = '\0';

  oid_part = locator_buf + prefix_len;
  if (oid_part[0] == 'A' && oid_part[1] == ':')
    {
      oid_part += 2;
    }

  if (sscanf (oid_part, "%d|%d|%d:%lld%n", &volid, &pageid, &slotid, &parsed_length, &consumed) != 4
      || parsed_length < 0)
    {
      return false;
    }
  (void) volid;
  (void) pageid;
  (void) slotid;

  if (oid_part[consumed] != ':'
      || sscanf (oid_part + consumed + 1, "%llx%n", &parsed_token, &token_consumed) != 1
      || parsed_token == 0 || token_consumed <= 0 || oid_part[consumed + 1 + token_consumed] != '\0')
    {
      return false;
    }

  if (length != NULL)
    {
      *length = (DB_BIGINT) parsed_length;
    }
  if (bit_length != NULL)
    {
      *bit_length = -1;
    }
  return true;
}

static int
internal_lob_unload_sidecar_write_stream (char lob_type, const char *key, int key_len, DB_BIGINT data_len,
					  DB_BIGINT bit_length)
{
  char *buffer = NULL;
  char *comp_buffer = NULL;
  DB_BIGINT offset = 0;
  DB_BIGINT stored_len = 0;
  off_t stored_len_pos = -1;
  int buffer_size = INTERNAL_LOB_UNLOAD_BLOCK_SIZE;
  int comp_capacity = 0;
  int error = NO_ERROR;
#if defined (SA_MODE)
  INTERNAL_LOB_LOCATOR locator;
  INTERNAL_LOB_READER reader;
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
#elif defined (CS_MODE)
  INT64 stream_token = 0;
  bool stream_opened = false;
#endif /* CS_MODE */

  if (internal_lob_unload_fp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
      return ER_FAILED;
    }
  if (key == NULL || key_len <= 0 || data_len < 0 || bit_length < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
      return ER_FAILED;
    }

  if (data_len > 0)
    {
      if (data_len < (DB_BIGINT) buffer_size)
	{
	  buffer_size = (int) data_len;
	}

      buffer = (char *) malloc ((size_t) buffer_size);
      if (buffer == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) buffer_size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      comp_capacity = cubcompress::bound < cubcompress::LZ4 > (buffer_size);
      if (comp_capacity <= 0)
	{
	  free_and_init (buffer);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
	  return ER_FAILED;
	}
      comp_buffer = (char *) malloc ((size_t) comp_capacity);
      if (comp_buffer == NULL)
	{
	  free_and_init (buffer);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) comp_capacity);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

#if defined (SA_MODE)
      if (!internal_lob_parse_locator_string (key, key_len, &locator))
	{
	  error = ER_FAILED;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  goto exit_before_lock;
	}
      error = internal_lob_read_open (thread_p, locator, reader);
      if (error != NO_ERROR)
	{
	  goto exit_before_lock;
	}
#elif defined (CS_MODE)
      /* One forward-only server cursor for the whole value (the same path csql streams with).  Reading by
       * offset instead made the server re-walk the chain from its start on every block, i.e. O(size^2). */
      error = internal_lob_stream_open_from_server (key, key_len, &stream_token);
      if (error != NO_ERROR)
	{
	  goto exit_before_lock;
	}
      stream_opened = true;
#else
      error = ER_FAILED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto exit_before_lock;
#endif /* !SA_MODE && !CS_MODE */
    }

  pthread_mutex_lock (&internal_lob_unload_lock);

  if (fprintf (internal_lob_unload_fp, "%c\t%d\t", lob_type, key_len) < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
      error = ER_FAILED;
      goto exit;
    }
  if (fwrite (key, 1, (size_t) key_len, internal_lob_unload_fp) != (size_t) key_len)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
      error = ER_FAILED;
      goto exit;
    }
  /* The stored size is only known after compressing, so reserve a fixed-width field and
   * rewrite it once the body is complete. */
  stored_len_pos = ftello (internal_lob_unload_fp);
  if (stored_len_pos < 0
      || fprintf (internal_lob_unload_fp, "\t%lld\t%lld\t%c\t%020lld\t", (long long) data_len,
		  (long long) bit_length, INTERNAL_LOB_UNLOAD_COMPRESS_LZ4, 0LL) < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
      error = ER_FAILED;
      goto exit;
    }

  while (offset < data_len)
    {
      DB_BIGINT remaining = data_len - offset;
      int request_size = (remaining > (DB_BIGINT) buffer_size) ? buffer_size : (int) remaining;
      int nread = 0;

#if defined (SA_MODE)
      error = internal_lob_read_pull (thread_p, reader, oos_buffer (buffer, (size_t) request_size), nread);
#elif defined (CS_MODE)
      error = internal_lob_stream_read_from_server (stream_token, buffer, request_size, &nread);
#else
      error = ER_FAILED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
#endif
      if (error != NO_ERROR)
	{
	  goto exit;
	}
      if (nread <= 0 || nread > request_size || offset + nread > data_len)
	{
	  error = ER_FAILED;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  goto exit;
	}

      error = internal_lob_unload_sidecar_write_block (internal_lob_unload_fp, buffer, nread, comp_buffer,
						       comp_capacity, &stored_len);
      if (error != NO_ERROR)
	{
	  goto exit;
	}
      offset += (DB_BIGINT) nread;
    }

  if (fputc ('\n', internal_lob_unload_fp) == EOF)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
      error = ER_FAILED;
      goto exit;
    }

  /* Patch the reserved stored-size field now that the body length is known. */
  {
    off_t end_pos = ftello (internal_lob_unload_fp);

    if (end_pos < 0 || fseeko (internal_lob_unload_fp, stored_len_pos, SEEK_SET) != 0
	|| fprintf (internal_lob_unload_fp, "\t%lld\t%lld\t%c\t%020lld\t", (long long) data_len,
		    (long long) bit_length, INTERNAL_LOB_UNLOAD_COMPRESS_LZ4, (long long) stored_len) < 0
	|| fseeko (internal_lob_unload_fp, end_pos, SEEK_SET) != 0)
      {
	er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
	error = ER_FAILED;
      }
  }

exit:
  pthread_mutex_unlock (&internal_lob_unload_lock);
exit_before_lock:
#if defined (CS_MODE)
  if (stream_opened)
    {
      (void) internal_lob_stream_close_from_server (stream_token);
    }
#endif /* CS_MODE */
  if (buffer != NULL)
    {
      free_and_init (buffer);
    }
  if (comp_buffer != NULL)
    {
      free_and_init (comp_buffer);
    }
  return error;
}

static int
fprint_internal_lob_ref_if_locator (TEXT_OUTPUT * tout, DB_VALUE * value, bool * printed)
{
  int error = NO_ERROR;
  DB_TYPE lob_type;
  char lob_type_char;
  const char *key = NULL;
  int key_len = 0;
  int key_bit_length = 0;
  DB_BIGINT data_len = 0;
  DB_BIGINT bit_length = 0;

  *printed = false;

  lob_type = DB_VALUE_DOMAIN_TYPE (value);
  if (lob_type == DB_TYPE_CLOB)
    {
      key = db_get_string (value);
      key_len = db_get_string_size (value);
      lob_type_char = 'C';
    }
  else if (lob_type == DB_TYPE_BLOB)
    {
      key = (const char *) db_get_bit (value, &key_bit_length);
      key_len = (key_bit_length + 7) / 8;
      lob_type_char = 'B';
    }
  else
    {
      return NO_ERROR;
    }

  if (!internal_lob_unload_is_locator_key (key, key_len))
    {
      return NO_ERROR;
    }

  if (key == NULL || key_len <= 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
      return ER_FAILED;
    }

  if (!internal_lob_unload_parse_locator_metadata (key, key_len, &data_len, NULL))
    {
      return NO_ERROR;
    }

  if (lob_type == DB_TYPE_CLOB)
    {
      bit_length = 0;
    }
  else
    {
      if (data_len > DB_BIGINT_MAX - 7)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
	  return ER_FAILED;
	}
      bit_length = data_len;
      data_len = (data_len + 7) / 8;
    }

  error = internal_lob_unload_sidecar_write_stream (lob_type_char, key, key_len, data_len, bit_length);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "^L'%c|", lob_type_char));
  CHECK_PRINT_ERROR (text_print (tout, key, key_len, NULL));
  CHECK_PRINT_ERROR (text_print (tout, "'", 1, NULL));
  *printed = true;

exit_on_error:
exit:
  return error;
}

/*
 * fprint_clob_value - print a CLOB DB_VALUE to TEXT_OUTPUT
 *    return: NO_ERROR if successful, error code otherwise
 *    tout(out): output
 *    value(in): DB_VALUE of type DB_TYPE_CLOB
 * Note:
 *    Internal CLOB is stored inline like a character string, so it is printed
 *    as a quoted string. Kept separate from the CHAR/VARCHAR case so that
 *    CLOB-specific formatting can diverge without touching that path.
 */
static int
fprint_clob_value (TEXT_OUTPUT * tout, DB_VALUE * value)
{
  int error = NO_ERROR;
  const char *str_ptr;
  int len;
  bool printed = false;

  CHECK_PRINT_ERROR (fprint_internal_lob_ref_if_locator (tout, value, &printed));
  if (printed)
    {
      return NO_ERROR;
    }

  str_ptr = db_get_string (value);
  len = db_get_string_size (value);
  if (len < 0)
    {
      len = (int) strlen (str_ptr);
    }

  CHECK_PRINT_ERROR (print_quoted_str (tout, str_ptr, len, MAX_DISPLAY_COLUMN));

exit_on_error:
  return error;
}

/*
 * fprint_blob_value - print an internal BLOB DB_VALUE to TEXT_OUTPUT
 *    return: NO_ERROR if successful, error code otherwise
 *    tout(out): output
 *    value(in): DB_VALUE of type DB_TYPE_BLOB
 * Note:
 *    Internal BLOB is stored inline like a bit string, so it is printed as a
 *    hex string. Kept separate from the BIT/VARBIT case so that BLOB-specific
 *    formatting can diverge without touching that path.
 */
static int
fprint_blob_value (TEXT_OUTPUT * tout, DB_VALUE * value)
{
  int error = NO_ERROR;
  char buf[INTERNAL_BUFFER_SIZE];
  char *ptr = NULL;
  int max_size = ((db_get_string_length (value) + 3) / 4) + 1;
  bool printed = false;

  CHECK_PRINT_ERROR (fprint_internal_lob_ref_if_locator (tout, value, &printed));
  if (printed)
    {
      return NO_ERROR;
    }

  if (max_size > INTERNAL_BUFFER_SIZE)
    {
      ptr = (char *) malloc (max_size);
      if (ptr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) max_size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }
  else
    {
      ptr = buf;
    }

  if (bfmt_print (1 /* BIT_STRING_HEX */ , value, ptr, max_size) != NO_ERROR)
    {
      error = ER_GENERIC_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto exit_on_error;
    }

  CHECK_PRINT_ERROR (text_print (tout, "X", 1, NULL));
  CHECK_PRINT_ERROR (print_quoted_str (tout, ptr, max_size - 1, MAX_DISPLAY_COLUMN));

exit_on_error:
  if (ptr != buf)
    {
      free_and_init (ptr);
    }
  return error;
}

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
	char *pos = NULL;
	TEXT_BUFFER_BLK *tail_ptr_bk = NULL;

	if (tout->tail_ptr)
	  {
	    tail_ptr_bk = tout->tail_ptr;
	    pos = tout->tail_ptr->ptr;
	  }
	CHECK_PRINT_ERROR (text_print
			   (tout, NULL, 0, "%.*g", (type == DB_TYPE_FLOAT) ? 10 : 17,
			    (type == DB_TYPE_FLOAT) ? db_get_float (value) : db_get_double (value)));
	/* if tout flushed, then this float/double should be the first content */
	if (pos == NULL || tail_ptr_bk != tout->tail_ptr)
	  {
	    assert (tout->tail_ptr != NULL);
	    pos = tout->tail_ptr->buffer;
	  }

	if (need_append_dot (pos))
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
    case DB_TYPE_TIMESTAMPLTZ:
      db_timestampltz_to_string (buf, INTERNAL_BUFFER_SIZE, db_get_timestamp (value));
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "timestampltz '%s'", buf));
      break;
    case DB_TYPE_TIMESTAMPTZ:
      ts_tz = db_get_timestamptz (value);
      db_timestamptz_to_string (buf, INTERNAL_BUFFER_SIZE, &ts_tz->timestamp, &ts_tz->tz_id);
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "timestamptz '%s'", buf));
      break;
    case DB_TYPE_DATETIME:
      db_datetime_to_string (buf, INTERNAL_BUFFER_SIZE, db_get_datetime (value));
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "datetime '%s'", buf));
      break;
    case DB_TYPE_DATETIMELTZ:
      db_datetimeltz_to_string (buf, INTERNAL_BUFFER_SIZE, db_get_datetime (value));
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "datetimeltz '%s'", buf));
      break;
    case DB_TYPE_DATETIMETZ:
      dt_tz = db_get_datetimetz (value);
      db_datetimetz_to_string (buf, INTERNAL_BUFFER_SIZE, &dt_tz->datetime, &dt_tz->tz_id);
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "datetimetz '%s'", buf));
      break;
    case DB_TYPE_MONETARY:
      /* Always print symbol before value, even if for turkish lira the user format is after value :
       * intl_get_currency_symbol_position */
      CHECK_PRINT_ERROR (text_print
			 (tout, NULL, 0, "%s%.*f", intl_get_money_esc_ISO_symbol (db_get_monetary (value)->type), 2,
			  db_get_monetary (value)->amount));
      break;

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
    case DB_TYPE_CLOB:
      CHECK_PRINT_ERROR (fprint_clob_value (tout, value));
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
    case DB_TYPE_BLOB:
      CHECK_PRINT_ERROR (fprint_blob_value (tout, value));
      break;

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

  switch (DB_VALUE_TYPE (value))
    {
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      CHECK_PRINT_ERROR (fprint_special_set (tout, db_get_set (value)));
      break;
    case DB_TYPE_BFILE:
    case DB_TYPE_CFILE:
      fprintf (stderr, "%s", msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_CANT_PRINT_ELO));
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
    case DB_TYPE_BFILE:
    case DB_TYPE_CFILE:
      fprintf (stderr, "%s", msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_CANT_PRINT_ELO));
      break;
    default:
      db_print_value (output_ctx, value);
      break;
    }
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
      int tlen = 0;
      assert (buf != NULL);

      while (buflen >= size)
	{
	  memcpy (tout->tail_ptr->ptr, buf + tlen, size);
	  *(tout->tail_ptr->ptr + size) = '\0';	/* Null terminate */
	  tout->tail_ptr->ptr += size;
	  tout->tail_ptr->count += size;
	  buflen -= size;
	  tlen += size;
	  CHECK_PRINT_ERROR (get_text_output_mem (tout, buflen));
	  size = tout->tail_ptr->iosize;
	}

      if (buflen > 0)
	{
	  memcpy (tout->tail_ptr->ptr, buf + tlen, buflen);
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
	  c_text_buf_mgr.release_text_buffer (tp);
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
	  if (c_write_blk_queue.enqueue (head))
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

      c_text_buf_mgr.release_text_buffer (tp);
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
