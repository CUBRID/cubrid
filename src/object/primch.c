/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 *  primch.c - his file contains code for handling the values of primitive
 *  types in memory and for conversion between the disk representation
 *
 * Note:
 *    Note that this file is an overflow of code from prim.c which has
 *    all the basic comments and explanation.  The reason for the split
 *    into two files was simply that too much code was generated for one
 *    code segment on the PC.  crp
 *
 *    At the time of the split, the following types were moved into here:
 *    CHAR, VARCHAR (originally STRING), NCHAR, VARNCHAR, BIT, VARBIT
 * TODO: merge this file to object_primitive.c and remove
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "ustring.h"
#include "memory_manager_1.h"
#include "db.h"
#if !defined (SERVER_MODE)
#include "work_space.h"
#endif
#include "object_representation.h"
#include "object_primitive.h"
#include "set_object_1.h"
#if !defined (SERVER_MODE)
#include "elo_class.h"
#include "locator_cl.h"
#endif
#include "object_print_1.h"
#include "memory_manager_4.h"
#include "intl.h"
#include "language_support.h"
#include "qp_str.h"
#include "object_accessor.h"
#include "server.h"

/* this must be the last header file included!!! */
#include "dbval.h"

/*
 * NOTE - The stuff from here to the matching note below should probably be
 * in a private .h file used by prim.c and by primch.c to keep them in sync.
 */
#define STR_SIZE(prec, codeset)                                           \
    (LANG_VARIABLE_CHARSET(codeset)		?	(prec)*2	: \
     ((codeset) == INTL_CODESET_RAW_BITS)	?	((prec+7)/8)	: \
                                                        (prec))

#define BITS_IN_BYTE			8
#define BITS_TO_BYTES(bit_cnt)		(((bit_cnt) + 7) / 8)

/* left for future extension */
#define DO_CONVERSION_TO_SRVR_STR(codeset)  false
#define DO_CONVERSION_TO_SQLTEXT(codeset)   false

#define MR_CMP_RETURN_CODE(c, reverse, dom) \
    (((reverse) || ((dom) && (dom)->is_desc)) ? \
     (((c) < 0) ? DB_GT : ((c) > 0) ? DB_LT : DB_EQ) : \
     (((c) < 0) ? DB_LT : ((c) > 0) ? DB_GT : DB_EQ))

#define DB_DOMAIN_INIT_CHAR(value, precision)                 \
  do {                                                        \
    (value)->domain.general_info.type = DB_TYPE_CHAR;         \
    (value)->domain.general_info.is_null = 1;                 \
    (value)->domain.char_info.length =                        \
    (precision) == DB_DEFAULT_PRECISION ?                     \
    TP_FLOATING_PRECISION_VALUE : (precision);                \
    (value)->need_clear = false;                              \
    (value)->data.ch.info.codeset = 0;                        \
  } while (0)

/*
 * KLUDGE, we generally use -1 to indicate a "floating" domain precision
 * but there are some cases, notably in the generation of list file
 * column domains where DB_MAX_STRING_LENGTH-1 is used.  This results
 * in incorrect packing/unpacking of the list file results as the domains
 * don't match.
 * Need to have a meeting to discuss this so we can handle this consistently
 * everywhere.
 */
#define IS_FLOATING_PRECISION(prec) \
  ((prec) == TP_FLOATING_PRECISION_VALUE)

static void mr_initmem_string (void *mem);
static int mr_setmem_string (void *memptr, TP_DOMAIN * domain,
			     DB_VALUE * value);
static int mr_getmem_string (void *memptr, TP_DOMAIN * domain,
			     DB_VALUE * value, bool copy);
static int mr_lengthmem_string (void *memptr, TP_DOMAIN * domain, int disk);
static void mr_writemem_string (OR_BUF * buf, void *memptr,
				TP_DOMAIN * domain);
static void mr_readmem_string (OR_BUF * buf, void *memptr, TP_DOMAIN * domain,
			       int size);
static void mr_freemem_string (void *memptr);
static void mr_initval_string (DB_VALUE * value, int precision, int scale);
static int mr_setval_string (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_lengthval_string (DB_VALUE * value, int disk);
static int mr_writeval_string (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_string (OR_BUF * buf, DB_VALUE * value,
			      TP_DOMAIN * domain, int size, bool copy,
			      char *copy_buf, int copy_buf_len);
static int mr_cmpdisk_string (void *mem1, void *mem2, TP_DOMAIN * domain,
			      int do_reverse, int do_coercion,
			      int total_order, int *start_colp);
static int mr_cmpval_string (DB_VALUE * value1, DB_VALUE * value2,
			     TP_DOMAIN * domain, int do_reverse,
			     int do_coercion, int total_order,
			     int *start_colp);
static void mr_initmem_char (void *memptr);
static int mr_setmem_char (void *memptr, TP_DOMAIN * domain,
			   DB_VALUE * value);
static int mr_getmem_char (void *mem, TP_DOMAIN * domain,
			   DB_VALUE * value, bool copy);
static int mr_lengthmem_char (void *memptr, TP_DOMAIN * domain, int disk);
static void mr_writemem_char (OR_BUF * buf, void *mem, TP_DOMAIN * domain);
static void mr_readmem_char (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
			     int size);
static void mr_freemem_char (void *memptr);
static void mr_initval_char (DB_VALUE * value, int precision, int scale);
static int mr_setval_char (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_lengthval_char (DB_VALUE * value, int disk);
static int mr_writeval_char (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_char (OR_BUF * buf, DB_VALUE * value,
			    TP_DOMAIN * domain, int disk_size, bool copy,
			    char *copy_buf, int copy_buf_len);
static int mr_cmpdisk_char (void *mem1, void *mem2, TP_DOMAIN * domain,
			    int do_reverse, int do_coercion, int total_order,
			    int *start_colp);
static int mr_cmpval_char (DB_VALUE * value1, DB_VALUE * value2,
			   TP_DOMAIN * domain, int do_reverse,
			   int do_coercion, int total_order, int *start_colp);
static void mr_initmem_nchar (void *memptr);
static int mr_setmem_nchar (void *memptr, TP_DOMAIN * domain,
			    DB_VALUE * value);
static int mr_getmem_nchar (void *mem, TP_DOMAIN * domain,
			    DB_VALUE * value, bool copy);
static int mr_lengthmem_nchar (void *memptr, TP_DOMAIN * domain, int disk);
static void mr_writemem_nchar (OR_BUF * buf, void *mem, TP_DOMAIN * domain);
static void mr_readmem_nchar (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
			      int size);
static void mr_freemem_nchar (void *memptr);
static void mr_initval_nchar (DB_VALUE * value, int precision, int scale);
static int mr_setval_nchar (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_lengthval_nchar (DB_VALUE * value, int disk);
static int mr_writeval_nchar (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_nchar (OR_BUF * buf, DB_VALUE * value,
			     TP_DOMAIN * domain, int disk_size, bool copy,
			     char *copy_buf, int copy_buf_len);
static int mr_cmpdisk_nchar (void *mem1, void *mem2, TP_DOMAIN * domain,
			     int do_reverse, int do_coercion, int total_order,
			     int *start_colp);
static int mr_cmpval_nchar (DB_VALUE * value1, DB_VALUE * value2,
			    TP_DOMAIN * domain, int do_reverse,
			    int do_coercion, int total_order,
			    int *start_colp);
static void mr_initmem_varnchar (void *mem);
static int mr_setmem_varnchar (void *memptr, TP_DOMAIN * domain,
			       DB_VALUE * value);
static int mr_getmem_varnchar (void *memptr, TP_DOMAIN * domain,
			       DB_VALUE * value, bool copy);
static int mr_lengthmem_varnchar (void *memptr, TP_DOMAIN * domain, int disk);
static void mr_writemem_varnchar (OR_BUF * buf, void *memptr,
				  TP_DOMAIN * domain);
static void mr_readmem_varnchar (OR_BUF * buf, void *memptr,
				 TP_DOMAIN * domain, int size);
static void mr_freemem_varnchar (void *memptr);
static void mr_initval_varnchar (DB_VALUE * value, int precision, int scale);
static int mr_setval_varnchar (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_lengthval_varnchar (DB_VALUE * value, int disk);
static int mr_writeval_varnchar (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_varnchar (OR_BUF * buf, DB_VALUE * value,
				TP_DOMAIN * domain, int size, bool copy,
				char *copy_buf, int copy_buf_len);
static int mr_cmpdisk_varnchar (void *mem1, void *mem2, TP_DOMAIN * domain,
				int do_reverse, int do_coercion,
				int total_order, int *start_colp);
static int mr_cmpval_varnchar (DB_VALUE * value1, DB_VALUE * value2,
			       TP_DOMAIN * domain, int do_reverse,
			       int do_coercion, int total_order,
			       int *start_colp);
static void mr_initmem_bit (void *memptr);
static int mr_setmem_bit (void *memptr, TP_DOMAIN * domain, DB_VALUE * value);
static int mr_getmem_bit (void *mem, TP_DOMAIN * domain, DB_VALUE * value,
			  bool copy);
static int mr_lengthmem_bit (void *memptr, TP_DOMAIN * domain, int disk);
static void mr_writemem_bit (OR_BUF * buf, void *mem, TP_DOMAIN * domain);
static void mr_readmem_bit (OR_BUF * buf, void *mem, TP_DOMAIN * domain,
			    int size);
static void mr_freemem_bit (void *memptr);
static void mr_initval_bit (DB_VALUE * value, int precision, int scale);
static int mr_setval_bit (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_lengthval_bit (DB_VALUE * value, int disk);
static int mr_writeval_bit (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_bit (OR_BUF * buf, DB_VALUE * value, TP_DOMAIN * domain,
			   int disk_size, bool copy, char *copy_buf,
			   int copy_buf_len);
static int mr_cmpdisk_bit (void *mem1, void *mem2, TP_DOMAIN * domain,
			   int do_reverse, int do_coercion, int total_order,
			   int *start_colp);
static int mr_cmpval_bit (DB_VALUE * value1, DB_VALUE * value2,
			  TP_DOMAIN * domain, int do_reverse, int do_coercion,
			  int total_order, int *start_colp);
static void mr_initmem_varbit (void *mem);
static int mr_setmem_varbit (void *memptr, TP_DOMAIN * domain,
			     DB_VALUE * value);
static int mr_getmem_varbit (void *memptr, TP_DOMAIN * domain,
			     DB_VALUE * value, bool copy);
static int mr_lengthmem_varbit (void *memptr, TP_DOMAIN * domain, int disk);
static void mr_writemem_varbit (OR_BUF * buf, void *memptr,
				TP_DOMAIN * domain);
static void mr_readmem_varbit (OR_BUF * buf, void *memptr, TP_DOMAIN * domain,
			       int size);
static void mr_freemem_varbit (void *memptr);
static void mr_initval_varbit (DB_VALUE * value, int precision, int scale);
static int mr_setval_varbit (DB_VALUE * dest, DB_VALUE * src, bool copy);
static int mr_lengthval_varbit (DB_VALUE * value, int disk);
static int mr_writeval_varbit (OR_BUF * buf, DB_VALUE * value);
static int mr_readval_varbit (OR_BUF * buf, DB_VALUE * value,
			      TP_DOMAIN * domain, int size, bool copy,
			      char *copy_buf, int copy_buf_len);
static int mr_cmpdisk_varbit (void *mem1, void *mem2, TP_DOMAIN * domain,
			      int do_reverse, int do_coercion,
			      int total_order, int *start_colp);
static int mr_cmpval_varbit (DB_VALUE * value1, DB_VALUE * value2,
			     TP_DOMAIN * domain, int do_reverse,
			     int do_coercion, int total_order,
			     int *start_colp);

/*
 * NOTE - The stuff from here to the matching note above should probably be
 * in a private .h file used by prim.c and by primch.c to keep them in sync.
 */

/*
 * TYPE VARCHAR (originally STRING)
 */
/*
 * VARCHAR variable length strings.
 * Originally these were stored as NULL terminated C strings but this must
 * change due to requirements for strings with embedded NULLs.
 * Since the reset of the system and all of the applications are not yet
 * prepared for non-NULL terminated strings, we still maintain these NULL
 * terminated though that may be removed in the future.
 *
 * The "disk" representation generated by "writeval" is used for both packing
 * "standalone" varchar values and also for values inside the disk
 * representation of an object.  In the object, we don't need to store
 * the extra length field since we know how long the thing is by looking at
 * the offset table.  This will waste four bytes per varchar stored
 * in the database.  One way aroud this would be to pass a "object_rep" flag
 * into all the writemem & writeval function so we would know not to
 * store the tag.  Unfortunately we can't get this from the domain since
 * that only tells us the maximum size.
 *
 * Some of this packing/unpacking logic could go into or_ functions
 * if that is convenient.
 *
 */

static void
mr_initmem_string (void *mem)
{
  *(char **) mem = NULL;
}


/*
 * The main difference between "memory" strings and "value" strings is that
 * the length tag is stored as an in-line prefix in the memory block allocated
 * to hold the string characters.
 */
static int
mr_setmem_string (void *memptr, TP_DOMAIN * domain, DB_VALUE * value)
{
  int error = NO_ERROR;
  char *src, *cur, *new_, **mem;
  int src_precision, src_length, new_length;

  /* get the current memory contents */
  mem = (char **) memptr;
  cur = *mem;

  if (value == NULL || (src = DB_GET_STRING (value)) == NULL)
    {
      /* remove the current value */
      if (cur != NULL)
	{
	  db_private_free_and_init (NULL, cur);
	  mr_initmem_string (memptr);
	}
    }
  else
    {
      /*
       * Get information from the value.  Ignore precision for the time being
       * since we really only care about the byte size of the value for varchar.
       * Whether or not the value "fits" should have been checked by now.
       */
      src_precision = DB_GET_STRING_PRECISION (value);
      src_length = DB_GET_STRING_SIZE (value);	/* size in bytes */

      if (src_length < 0)
	src_length = strlen (src);

      /* Currently we NULL terminate the workspace string.
       * Could try to do the single byte size hack like we have in the
       * disk representation.
       */
      new_length = src_length + sizeof (int) + 1;
      new_ = (char *) db_private_alloc (NULL, new_length);
      if (new_ == NULL)
	error = er_errid ();
      else
	{
	  if (cur != NULL)
	    db_private_free_and_init (NULL, cur);

	  /* pack in the length prefix */
	  *(int *) new_ = src_length;
	  cur = new_ + sizeof (int);
	  /* store the string */
	  memcpy (cur, src, src_length);
	  /* NULL terminate the stored string for safety */
	  cur[src_length] = '\0';
	  *mem = new_;
	}
    }

  return error;
}

static int
mr_getmem_string (void *memptr, TP_DOMAIN * domain, DB_VALUE * value,
		  bool copy)
{
  int error = NO_ERROR;
  int mem_length;
  char **mem, *cur, *new_;

  /* get to the current value */
  mem = (char **) memptr;
  cur = *mem;

  if (cur == NULL)
    {
      db_value_domain_init (value, DB_TYPE_VARCHAR, domain->precision, 0);
      value->need_clear = false;
    }
  else
    {
      /* extract the length prefix and the pointer to the actual string data */
      mem_length = *(int *) cur;
      cur += sizeof (int);

      if (!copy)
	{
	  db_make_varchar (value, domain->precision, cur, mem_length);
	  value->need_clear = false;
	}
      else
	{
	  /* return it with a NULL terminator */
	  new_ = (char *) db_private_alloc (NULL, mem_length + 1);
	  if (new_ == NULL)
	    error = er_errid ();
	  else
	    {
	      memcpy (new_, cur, mem_length);
	      new_[mem_length] = '\0';
	      db_make_varchar (value, domain->precision, new_, mem_length);
	      value->need_clear = true;
	    }
	}
    }
  return error;
}


/*
 * For the disk representation, we may be adding pad bytes to round up to a
 * word boundary.
 *
 * We are currently adding a NULL terminator to the disk representation
 * for some code on the server that still manufactures pointers directly into
 * the disk buffer and assumes it is a NULL terminated string.  This terminator
 * can be removed after the server has been updated.  The logic for maintaining
 * the terminator is actuall in the or_put_varchar, family of functions.
 */
static int
mr_lengthmem_string (void *memptr, TP_DOMAIN * domain, int disk)
{
  char **mem, *cur;
  int len;

  len = 0;
  if (!disk)
    {
      len = tp_String.size;
    }
  else if (memptr != NULL)
    {
      mem = (char **) memptr;
      cur = *mem;
      if (cur != NULL)
	{
	  len = *(int *) cur;
	  len = or_packed_varchar_length (len);
	}
    }

  return len;
}


static void
mr_writemem_string (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
{
  char **mem, *cur;
  int len;

  mem = (char **) memptr;
  cur = *mem;
  if (cur != NULL)
    {
      len = *(int *) cur;
      cur += sizeof (int);
      or_put_varchar (buf, cur, len);
    }
}


/*
 * The amount of memory requested is currently calculated based on the
 * stored size prefix.  If we ever go to a system where we avoid storing the
 * size, then we could use the size argument passed in to this function but
 * that may also include any padding byte that added to bring us up to a word
 * boundary.
 * Might want some way to determine which bytes at the end of a string are
 * padding.
 */
static void
mr_readmem_string (OR_BUF * buf, void *memptr, TP_DOMAIN * domain, int size)
{
  char **mem, *cur, *new_;
  int len;
  int mem_length, pad;
  char *start;
  int rc = NO_ERROR;

  /*
   * we must have an explicit size here as it can't be determined from the
   * domain
   */
  if (size < 0)
    return;

  if (memptr == NULL)
    {
      if (size)
	or_advance (buf, size);
    }
  else
    {
      mem = (char **) memptr;
      cur = *mem;
      /* should we be checking for existing strings ? */
#if 0
      if (cur != NULL)
	db_private_free_and_init (NULL, cur);
#endif

      new_ = NULL;
      if (size)
	{
	  start = buf->ptr;

	  /* KLUDGE, we have some knowledge of how the thing is stored here in
	   * order have some control over the conversion between the packed
	   * length prefix and the full word memory length prefix.
	   * Might want to put this in another specialized or_ function.
	   */

	  /* Get just the length prefix. */
	  len = or_get_varchar_length (buf, &rc);

	  /*
	   * Allocate storage for this string, including our own full word size
	   * prefix and a NULL terminator.
	   */
	  mem_length = len + sizeof (int) + 1;

	  new_ = db_private_alloc (NULL, mem_length);
	  if (new_ == NULL)
	    or_abort (buf);
	  else
	    {
	      /* store the length in our memory prefix */
	      *(int *) new_ = len;
	      cur = new_ + sizeof (int);

	      /*
	       * read the string, INLCUDING the NULL terminator (which is
	       * expected)
	       */
	      or_get_data (buf, cur, len + 1);
	      /* align like or_get_varchar */
	      or_get_align32 (buf);
	    }

	  /* If we were given a size, check to see if for some reason this is
	   * larger than the already word aligned string that we have now
	   * extracted.  This shouldn't be the case but since we've got a
	   * length, we may as well obey it.
	   */
	  pad = size - (int) (buf->ptr - start);
	  if (pad > 0)
	    or_advance (buf, pad);
	}
      *mem = new_;
    }
}

static void
mr_freemem_string (void *memptr)
{
  char *cur;

  if (memptr != NULL)
    {
      cur = *(char **) memptr;
      if (cur != NULL)
	db_private_free_and_init (NULL, cur);
    }
}

static void
mr_initval_string (DB_VALUE * value, int precision, int scale)
{
  db_make_varchar (value, precision, NULL, 0);
  value->need_clear = false;
}

static int
mr_setval_string (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  int error = NO_ERROR;
  int src_precision, src_length;
  char *src_str, *new_;

  if (src == NULL || (DB_IS_NULL (src) && db_value_precision (src) == 0))
    {
      error =
	db_value_domain_init (dest, DB_TYPE_VARCHAR, DB_DEFAULT_PRECISION, 0);
    }
  else if (DB_IS_NULL (src) || (src_str = db_get_string (src)) == NULL)
    {
      error =
	db_value_domain_init (dest, DB_TYPE_VARCHAR, db_value_precision (src),
			      0);
    }
  else
    {
      /* Get information from the value. */
      src_precision = db_value_precision (src);
      src_length = db_get_string_size (src);
      if (src_length < 0)
	src_length = strlen (src_str);

      /* should we be paying attention to this? it is extremely dangerous */
      if (!copy)
	{
	  error = db_make_varchar (dest, src_precision, src_str, src_length);
	}
      else
	{
	  new_ = db_private_alloc (NULL, src_length + 1);
	  if (new_ == NULL)
	    {
	      db_value_domain_init (dest, DB_TYPE_VARCHAR, src_precision, 0);
	      error = er_errid ();
	    }
	  else
	    {
	      memcpy (new_, src_str, src_length);
	      new_[src_length] = '\0';
	      db_make_varchar (dest, src_precision, new_, src_length);
	      dest->need_clear = true;
	    }
	}
    }

  return error;
}


/*
 * Ignoring precision as byte size is really the only important thing for
 * varchar.
 */
static int
mr_lengthval_string (DB_VALUE * value, int disk)
{
  int len;
  const char *str;

  if (!value || value->domain.general_info.is_null)
    return 0;
  str = value->data.ch.medium.buf;
  len = value->data.ch.medium.size;
  if (!str)
    return 0;
  if (len < 0)
    len = strlen (str);
  return (disk) ? or_packed_varchar_length (len) : len;
}


/*
 * Ignoring precision as byte size is really the only important thing for
 * varchar.
 */
static int
mr_writeval_string (OR_BUF * buf, DB_VALUE * value)
{
  int src_length;
  char *str;
  int rc = NO_ERROR;

  if (value != NULL && (str = db_get_string (value)) != NULL)
    {
      src_length = db_get_string_size (value);	/* size in bytes */
      if (src_length < 0)
	src_length = strlen (str);

      rc = or_put_varchar (buf, str, src_length);
    }
  return rc;
}


static int
mr_readval_string (OR_BUF * buf, DB_VALUE * value,
		   TP_DOMAIN * domain, int size, bool copy,
		   char *copy_buf, int copy_buf_len)
{
  int pad, precision;
  char *new_, *start = NULL;
  int str_length;
  int rc = NO_ERROR;

  if (value == NULL)
    {
      if (size == -1)
	rc = or_skip_varchar (buf);
      else
	{
	  if (size)
	    rc = or_advance (buf, size);
	}
    }
  else
    {
      /*
       * Allow the domain to be NULL in which case this must be a maximum
       * precision varchar.  Should be used only by the tfcl.c class object
       * accessors.
       */
      if (domain != NULL)
	precision = domain->precision;
      else
	precision = DB_MAX_VARCHAR_PRECISION;

      if (!copy)
	{
	  str_length = or_get_varchar_length (buf, &rc);
	  if (rc == NO_ERROR)
	    {
	      db_make_varchar (value, precision, buf->ptr, str_length);
	      value->need_clear = false;
	      rc = or_skip_varchar_remainder (buf, str_length);
	    }
	}
      else
	{
	  if (size == 0)
	    {
	      /* its NULL */
	      db_value_domain_init (value, DB_TYPE_VARCHAR, precision, 0);
	    }
	  else
	    {			/* size != 0 */
	      if (size == -1)
		{
		  /* Standard packed varchar with a size prefix */
		  ;		/* do nothing */
		}
	      else
		{		/* size != -1 */
		  /* Standard packed varchar within an area of fixed size,
		   * usually this means we're looking at the disk
		   * representation of an attribute.
		   * Just like the -1 case except we advance past the additional
		   * padding.
		   */
		  start = buf->ptr;
		}		/* size != -1 */

	      str_length = or_get_varchar_length (buf, &rc);
	      if (rc != NO_ERROR)
		{
		  return ER_FAILED;
		}

	      if (copy_buf && copy_buf_len >= str_length + 1)
		{
		  /* read buf image into the copy_buf */
		  new_ = copy_buf;
		}
	      else
		{
		  /*
		   * Allocate storage for the string including the kludge
		   * NULL terminator
		   */
		  new_ = db_private_alloc (NULL, str_length + 1);
		}

	      if (new_ == NULL)
		{
		  /* need to be able to return errors ! */
		  db_value_domain_init (value, domain->type->id,
					TP_FLOATING_PRECISION_VALUE, 0);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 0);
		  or_abort (buf);
		  return ER_FAILED;
		}
	      else
		{
		  /* read the kludge NULL terminator */
		  if ((rc =
		       or_get_data (buf, new_, str_length + 1)) == NO_ERROR)
		    {
		      /* round up to a word boundary */
		      rc = or_get_align32 (buf);
		    }
		  if (rc != NO_ERROR)
		    {
		      if (new_ != copy_buf)
			db_private_free_and_init (NULL, new_);
		      return ER_FAILED;
		    }

		  db_make_varchar (value, precision, new_, str_length);
		  value->need_clear = (new_ != copy_buf) ? true : false;

		  if (size == -1)
		    {
		      /* Standard packed varchar with a size prefix */
		      ;		/* do nothing */
		    }
		  else
		    {		/* size != -1 */
		      /* Standard packed varchar within an area of fixed size,
		       * usually this means we're looking at the disk
		       * representation of an attribute. Just like the -1 case
		       * except we advance past the additional padding.
		       */
		      pad = size - (int) (buf->ptr - start);
		      if (pad > 0)
			rc = or_advance (buf, pad);
		    }		/* size != -1 */
		}		/* else */
	    }			/* size != 0 */
	}
    }
  return rc;
}

static int
mr_cmpdisk_string (void *mem1, void *mem2,
		   TP_DOMAIN * domain, int do_reverse,
		   int do_coercion, int total_order, int *start_colp)
{
  int c = DB_UNK;
  int str_length1, str_length2;
  OR_BUF buf1, buf2;
  int rc = NO_ERROR;

  or_init (&buf1, (char *) mem1, 0);
  str_length1 = or_get_varchar_length (&buf1, &rc);
  if (rc == NO_ERROR)
    {

      or_init (&buf2, (char *) mem2, 0);
      str_length2 = or_get_varchar_length (&buf2, &rc);
      if (rc == NO_ERROR)
	{

	  c = qstr_compare ((unsigned char *) buf1.ptr, str_length1,
			    (unsigned char *) buf2.ptr, str_length2);
	  c = MR_CMP_RETURN_CODE (c, do_reverse, domain);
	  return c;
	}
    }

  return DB_UNK;
}

static int
mr_cmpval_string (DB_VALUE * value1, DB_VALUE * value2,
		  TP_DOMAIN * domain, int do_reverse,
		  int do_coercion, int total_order, int *start_colp)
{
  int c;

  c = qstr_compare ((unsigned char *) DB_GET_STRING (value1),
		    (int) DB_GET_STRING_SIZE (value1),
		    (unsigned char *) DB_GET_STRING (value2),
		    (int) DB_GET_STRING_SIZE (value2));
  c = MR_CMP_RETURN_CODE (c, do_reverse, domain);

  return c;
}

PR_TYPE tp_String = {
  "character varying", DB_TYPE_STRING, 1, sizeof (const char *), 0, 4,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_string,
  mr_initval_string,
  mr_setmem_string,
  mr_getmem_string,
  mr_setval_string,
  mr_lengthmem_string,
  mr_lengthval_string,
  mr_writemem_string,
  mr_readmem_string,
  mr_writeval_string,
  mr_readval_string,
  mr_freemem_string,
  mr_cmpdisk_string,
  mr_cmpval_string
};

PR_TYPE *tp_Type_string = &tp_String;

/*
 * TYPE CHAR
 */

static void
mr_initmem_char (void *memptr)
{
  /* could fill with zeros, punt for now */
}


/*
 * Note! Due to the "within tolerance" comparison of domains used for
 * assignment validation, we may get values in here whose precision is
 * less than the precision of the actual attribute.   This prevents
 * having to copy a string just to coerce a value into a larger precision.
 * Note that the precision of the source value may also come in as -1 here
 * which is used to mean a "floating" precision that is assumed to be
 * compatible with the destination domain as long as the associated value
 * is within tolerance.  This case is generally only seen for string
 * literals that have been produced by the parser.  These literals will
 * not contain blank padding and strlen() or db_get_string_size() can be
 * used to determine the number of significant characters.
 *
 */
static int
mr_setmem_char (void *memptr, TP_DOMAIN * domain, DB_VALUE * value)
{
  int error = NO_ERROR;
  char *src, *mem;
  int src_precision, src_length, mem_length, charset_multiplier, pad;

  if (value == NULL)
    return NO_ERROR;

  /* Get information from the value */
  src = DB_GET_STRING (value);
  src_precision = DB_GET_STRING_PRECISION (value);
  src_length = DB_GET_STRING_SIZE (value);	/* size in bytes */

  if (src == NULL)
    {
      /*
       * Shouldn't see this, could treat as a NULL assignment.
       * If this really is supposed to be NULL, the bound bit for this
       * value will have been set by obj.c.
       */
      return NO_ERROR;
    }

  /* Check for special NTS flag.  This may not be necessary any more. */
  if (src_length < 0)
    src_length = strlen (src);


  /* The only thing we really care about at this point, is the byte
   * length of the string.  The precision could be checked here but it
   * really isn't necessary for this operation.
   * Calculate the maximum number of bytes we have available here.
   * The multiplier is dependent on codeset, since we're only ASCII here, there
   * is no multiplier.
   */
  charset_multiplier = 1;
  mem_length = domain->precision * charset_multiplier;

  if (mem_length < src_length)
    {
      /*
       * should never get here, this is supposed to be caught during domain
       * validation, need a better error message.
       */
      error = ER_OBJ_DOMAIN_CONFLICT;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "");
    }
  else
    {
      /* copy the value into memory */
      mem = (char *) memptr;
      memcpy (mem, src, src_length);

      /*
       * Check for space padding, if this were a national string, we would
       * need to be padding with the appropriate space character !
       */
      pad = mem_length - src_length;
      if (pad)
	{
	  int i;
	  for (i = src_length; i < mem_length; i++)
	    mem[i] = ' ';
	}
    }
  return error;
}


static int
mr_getmem_char (void *mem, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  int mem_length, charset_multiplier;
  char *new_;

  /* since this is ASCII, there is no precision multiplier */
  charset_multiplier = 1;
  mem_length = domain->precision * charset_multiplier;

  if (!copy)
    new_ = (char *) mem;
  else
    {
      new_ = db_private_alloc (NULL, mem_length + 1);
      if (new_ == NULL)
	return er_errid ();
      memcpy (new_, (char *) mem, mem_length);
      /* make sure that all outgoing strings are NULL terminated */
      new_[mem_length] = '\0';
    }

  db_make_char (value, domain->precision, new_, mem_length);
  if (copy)
    value->need_clear = true;

  return NO_ERROR;
}

static int
mr_lengthmem_char (void *memptr, TP_DOMAIN * domain, int disk)
{
  int mem_length, charset_multiplier;

  /* There is no difference between the disk & memory sizes. */

  /* ASCII, no multiplier */
  charset_multiplier = 1;
  mem_length = domain->precision * charset_multiplier;

  return mem_length;
}

static void
mr_writemem_char (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  int mem_length, charset_multiplier;

  /* ASCII, no multiplier */
  charset_multiplier = 1;
  mem_length = domain->precision * charset_multiplier;

  /*
   * We simply dump the memory image to disk, it will already have been padded.
   * If this were a national character string, at this point, we'd have to
   * decide now to perform a character set conversion.
   */
  or_put_data (buf, (char *) mem, mem_length);
}

static void
mr_readmem_char (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
{
  int mem_length, charset_multiplier, padding;

  if (mem == NULL)
    {
      /*
       * If we passed in a size, then use it.  Otherwise, determine the
       * size from the domain.
       */
      if (size > 0)
	{
	  or_advance (buf, size);
	}
      else
	{
	  /* ASCII, no multiplier */
	  charset_multiplier = 1;
	  mem_length = domain->precision * charset_multiplier;
	  or_advance (buf, mem_length);
	}
    }
  else
    {
      /* ASCII, no multiplier */
      charset_multiplier = 1;
      mem_length = domain->precision * charset_multiplier;

      if (size != -1 && mem_length > size)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CORRUPTED, 0);
	  or_abort (buf);
	}
      or_get_data (buf, (char *) mem, mem_length);

      /*
       * We should only see padding if the string is contained within a packed
       * value that had extra padding to ensure alignment.  If we see these,
       * just pop them out of the buffer.  This shouldn't ever happen for the
       * "getmem" function, only for the "getval" function.
       */
      if (size != -1)
	{
	  padding = size - mem_length;
	  if (padding > 0)
	    or_advance (buf, padding);
	}
    }
}

static void
mr_freemem_char (void *memptr)
{
}

static void
mr_initval_char (DB_VALUE * value, int precision, int scale)
{
  DB_DOMAIN_INIT_CHAR (value, precision);
}

static int
mr_setval_char (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  int error = NO_ERROR;
  int src_precision, src_length;
  char *src_string, *new_;

  if (src == NULL || DB_IS_NULL (src))
    DB_DOMAIN_INIT_CHAR (dest, TP_FLOATING_PRECISION_VALUE);
  else
    {
      src_precision = DB_GET_STRING_PRECISION (src);
      if (src_precision == 0)
	src_precision = TP_FLOATING_PRECISION_VALUE;
      DB_DOMAIN_INIT_CHAR (dest, src_precision);
      /* Get information from the value */
      src_string = DB_GET_STRING (src);
      src_length = DB_GET_STRING_SIZE (src);	/* size in bytes */

      /* shouldn't see a NULL string at this point, treat as NULL */
      if (src_string != NULL)
	{
	  if (!copy)
	    db_make_char (dest, src_precision, src_string, src_length);
	  else
	    {
	      /* Check for NTS marker, may not need to do this any more */
	      if (src_length < 0)
		src_length = strlen (src_string);

	      /* make sure the copy gets a NULL terminator */
	      new_ = db_private_alloc (NULL, src_length + 1);
	      if (new_ == NULL)
		error = er_errid ();
	      else
		{
		  memcpy (new_, src_string, src_length);
		  new_[src_length] = '\0';
		  db_make_char (dest, src_precision, new_, src_length);
		  dest->need_clear = true;
		}
	    }
	}
    }
  return error;
}


/*
 * Note that there are some interpretation issues here related to how
 * we store "value" chars whose memory representation is smaller
 * than the domain precision.  Since "writeval" can be used to
 * set attribute values, we should try to make the behavior
 * of lengthval & writeval the same as the lengthmem & writemem.
 * In particular, we will pad out the value with spaces to match the
 * precision (if given).  In order for this to work correctly, the
 * string pointer in the value better not be NULL and the precision had
 * better be set up correctdly.
 *
 * When packing arrays of DB_VALUEs, not as part of an instance representation,
 * we interpret a NULL string pointer to mean a NULL value and the size will
 * be zero.  If we get a value precision of TP_FLOATING_PRECISION_VALUE,
 * this is taken to mean that the precision is "floating" and we store the
 * string exactly as long as it is in the value.
 *
 * Since the domain tag of a "floating" domain can't be used to tell us how
 * long the packed char is, we must store an additional length prefix on
 * CHAR(n) values of this form.
 *
 * Due to the formatting differences between a "floating" domain CHAR(n)
 * and a CHAR(n) stored in the disk representation of an instance, it is
 * mandatory that values which are to be stored in an instance have the exact
 * precision set in their values.  This should be accomplished through
 * coercion on the server.
 */
static int
mr_lengthval_char (DB_VALUE * value, int disk)
{
  int packed_length, src_precision, charset_multiplier;
  char *src;

  src = db_get_string (value);
  if (src == NULL)
    return 0;

  src_precision = db_value_precision (value);
  if (!IS_FLOATING_PRECISION (src_precision))
    {
      /*
       * Assume for now that we're ASCII.
       * Would have to check for charset conversion here !
       */
      charset_multiplier = 1;
      packed_length = src_precision * charset_multiplier;
    }
  else
    {
      /*
       * Precision is "floating", calculate the effective precision based on the
       * string length.
       * Should be rounding this up so it is a proper multiple of the charset
       * width ?
       */
      packed_length = db_get_string_size (value);
      if (packed_length < 0)
	packed_length = strlen (src);

      /* add in storage for a size prefix on the packed value. */
      packed_length += OR_INT_SIZE;
    }

  /*
   * NOTE: We do NOT perform padding here, if this is used in the context
   * of a packed value, the or_put_value() family of functions must handle
   * their own padding, this is because "lengthval" and "writeval" can be
   * used to place values into the disk representation of instances and
   * there can be no padding in there.
   */

  return packed_length;
}


/*
 * See commentary in mr_lengthval_char.
 */
static int
mr_writeval_char (OR_BUF * buf, DB_VALUE * value)
{
  int src_precision, src_length, packed_length, charset_multiplier, pad;
  char *src;
  int rc = NO_ERROR;

  src = db_get_string (value);
  if (src == NULL)
    return rc;

  src_precision = db_value_precision (value);
  src_length = db_get_string_size (value);	/* size in bytes */

  if (src_length < 0)
    src_length = strlen (src);

  if (!IS_FLOATING_PRECISION (src_precision))
    {
      /*
       * Assume for now that we're ASCII.
       * Would have to check for charset conversion here !
       */
      charset_multiplier = 1;
      packed_length = src_precision * charset_multiplier;

      if (packed_length < src_length)
	{
	  /* should have caught this by now, truncate silently */
	  rc = or_put_data (buf, src, packed_length);
	}
      else
	{
	  rc = or_put_data (buf, src, src_length);
	  /*
	   * Check for space padding, if this were a national string, we
	   * would need to be padding with the appropriate space character !
	   */
	  pad = packed_length - src_length;
	  if (pad)
	    {
	      int i;
	      for (i = src_length; i < packed_length; i++)
		rc = or_put_byte (buf, (int) ' ');
	    }
	}
      if (rc != NO_ERROR)
	return rc;
    }
  else
    {
      /*
       * This is a "floating" precision value. Pack what we can based on the
       * string size.  Note that for this to work, this can only be packed as
       * part of a domain tagged value and we must include a length prefix
       * after the domain.
       */
      packed_length = db_get_string_size (value);
      if (packed_length < 0)
	packed_length = strlen (src);

      /* store the size prefix */
      if ((rc = or_put_int (buf, packed_length)) == NO_ERROR)
	{
	  /* store the data */
	  rc = or_put_data (buf, src, packed_length);
	  /* there is no blank padding in this case */
	}
    }
  return rc;
}


static int
mr_readval_char (OR_BUF * buf, DB_VALUE * value, TP_DOMAIN * domain,
		 int disk_size, bool copy, char *copy_buf, int copy_buf_len)
{
  int mem_length, charset_multiplier, padding;
  int str_length, precision;
  char *new_;
  int rc = NO_ERROR;

  precision = domain->precision;

  if (IS_FLOATING_PRECISION (domain->precision))
    {
      /*
       * This is only allowed if we're unpacking a "floating" domain CHAR(n)
       * in which case there will be a byte size prefix.
       * Just copy the bytes in from disk, adding a NULL terminator for now,
       * will need some logic for NCHAR conversion.
       *
       * Note that we force the destination value's domain precision to be
       * TP_FLOATING_PRECISION_VALUE here
       * even though with the IS_FLOATING_PRECISION macro it may have come
       * in as something else. This is only a temporary thing to get past
       * list file columns that are tagged with really large CHAR(n) domains
       * when they actually should be either -1 or true precisions.
       */

      mem_length = or_get_int (buf, &rc);
      if (rc != NO_ERROR)
	return rc;
      if (value == NULL)
	{
	  rc = or_advance (buf, mem_length);
	}
      else if (!copy)
	{
	  db_make_char (value, TP_FLOATING_PRECISION_VALUE, buf->ptr,
			mem_length);
	  value->need_clear = false;
	  rc = or_advance (buf, mem_length);
	}
      else
	{
	  if (copy_buf && copy_buf_len >= mem_length + 1)
	    {
	      /* read buf image into the copy_buf */
	      new_ = copy_buf;
	    }
	  else
	    {
	      /* Allocate storage for the string
	       * including the kludge NULL terminator */
	      new_ = db_private_alloc (NULL, mem_length + 1);
	    }

	  if (new_ == NULL)
	    {
	      /* need to be able to return errors ! */
	      db_value_domain_init (value, domain->type->id,
				    TP_FLOATING_PRECISION_VALUE, 0);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 0);
	      or_abort (buf);
	      return ER_FAILED;
	    }
	  else
	    {
	      if ((rc = or_get_data (buf, new_, mem_length)) != NO_ERROR)
		{
		  if (new_ != copy_buf)
		    db_private_free_and_init (NULL, new_);
		  return rc;
		}
	      new_[mem_length] = '\0';	/* append the kludge NULL terminator */
	      db_make_char (value, TP_FLOATING_PRECISION_VALUE, new_,
			    mem_length);
	      value->need_clear = (new_ != copy_buf) ? true : false;
	    }
	}
    }
  else
    {
      /*
       * Normal fixed width char(n) whose size can be determined by looking at
       * the domain.
       * Assumes ASCII with no charset multiplier.
       * Needs NCHAR work here to separate the dependencies on disk_size and
       * mem_size.
       */
      charset_multiplier = 1;
      mem_length = domain->precision * charset_multiplier;

      if (disk_size != -1 && mem_length > disk_size)
	{
	  /*
	   * If we're low here, we could just read what we have and make a
	   * smaller value.  Still the domain should match at this point.
	   */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CORRUPTED, 0);
	  or_abort (buf);
	  return ER_FAILED;
	}

      if (value == NULL)
	{
	  rc = or_advance (buf, mem_length);
	}
      else if (disk_size == 0)
	{
	  db_value_domain_init (value, DB_TYPE_CHAR, precision, 0);
	}
      else if (!copy)
	{
	  str_length = mem_length;
	  db_make_char (value, precision, buf->ptr, str_length);
	  value->need_clear = false;
	  rc = or_advance (buf, str_length);
	}
      else
	{
	  if (copy_buf && copy_buf_len >= mem_length + 1)
	    {
	      /* read buf image into the copy_buf */
	      new_ = copy_buf;
	    }
	  else
	    {
	      /*
	       * Allocate storage for the string including the kludge NULL
	       * terminator
	       */
	      new_ = db_private_alloc (NULL, mem_length + 1);
	    }
	  if (new_ == NULL)
	    {
	      /* need to be able to return errors ! */
	      db_value_domain_init (value, domain->type->id,
				    domain->precision, 0);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 0);
	      or_abort (buf);
	      return ER_FAILED;
	    }
	  else
	    {
	      if ((rc = or_get_data (buf, new_, mem_length)) != NO_ERROR)
		{
		  if (new_ != copy_buf)
		    db_private_free_and_init (NULL, new_);
		  return rc;
		}
	      new_[mem_length] = '\0';	/* append the kludge NULL terminator */
	      db_make_char (value, domain->precision, new_, mem_length);
	      value->need_clear = (new_ != copy_buf) ? true : false;
	    }
	}

      if (rc == NO_ERROR)
	{
	  /*
	   * We should only see padding if the string is contained within a
	   * packed value that had extra padding to ensure alignment.  If we
	   * see these, just pop them out of the buffer.
	   */
	  if (disk_size != -1)
	    {
	      padding = disk_size - mem_length;
	      if (padding > 0)
		rc = or_advance (buf, padding);
	    }
	}
    }
  return rc;
}

static int
mr_cmpdisk_char (void *mem1, void *mem2,
		 TP_DOMAIN * domain, int do_reverse,
		 int do_coercion, int total_order, int *start_colp)
{
  int mem_length1, mem_length2, charset_multiplier, c;

  if (IS_FLOATING_PRECISION (domain->precision))
    {
      /*
       * This is only allowed if we're unpacking a "floating" domain CHAR(n) in
       * which case there will be a byte size prefix.
       *
       * Note that we force the destination value's domain precision to be
       * TP_FLOATING_PRECISION_VALUE here
       * even though with the IS_FLOATING_PRECISION macro it may have come
       * in as something else. This is only a temporary thing to get past
       * list file columns that are tagged with really large CHAR(n) domains
       * when they actually should be either -1 or true precisions.
       */

      mem_length1 = OR_GET_INT (mem1);
      mem1 = (char *) mem1 + OR_INT_SIZE;
      mem_length2 = OR_GET_INT (mem2);
      mem2 = (char *) mem2 + OR_INT_SIZE;
    }
  else
    {
      /*
       * Normal fixed width char(n) whose size can be determined by looking at
       * the domain.
       * Assumes ASCII with no charset multiplier.
       * Needs NCHAR work here to separate the dependencies on disk_size and
       * mem_size.
       */
      charset_multiplier = 1;
      mem_length1 = mem_length2 = domain->precision * charset_multiplier;
    }

  c = char_compare ((unsigned char *) mem1, mem_length1,
		    (unsigned char *) mem2, mem_length2);
  c = MR_CMP_RETURN_CODE (c, do_reverse, domain);

  return c;
}

static int
mr_cmpval_char (DB_VALUE * value1, DB_VALUE * value2,
		TP_DOMAIN * domain, int do_reverse,
		int do_coercion, int total_order, int *start_colp)
{
  int c;

  c = char_compare ((unsigned char *) DB_GET_STRING (value1),
		    (int) DB_GET_STRING_SIZE (value1),
		    (unsigned char *) DB_GET_STRING (value2),
		    (int) DB_GET_STRING_SIZE (value2));
  c = MR_CMP_RETURN_CODE (c, do_reverse, domain);

  return c;
}

PR_TYPE tp_Char = {
  "character", DB_TYPE_CHAR, 0, 0, 0, 1,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_char,
  mr_initval_char,
  mr_setmem_char,
  mr_getmem_char,
  mr_setval_char,
  mr_lengthmem_char,
  mr_lengthval_char,
  mr_writemem_char,
  mr_readmem_char,
  mr_writeval_char,
  mr_readval_char,
  mr_freemem_char,
  mr_cmpdisk_char,
  mr_cmpval_char
};

PR_TYPE *tp_Type_char = &tp_Char;

/*
 * TYPE NCHAR
 */

static void
mr_initmem_nchar (void *memptr)
{
  /* could fill with zeros, punt for now */
}


/*
 * Due to the "within tolerance" comparison of domains used for
 * assignment validation, we may get values in here whose precision is
 * less than the precision of the actual attribute.   This prevents
 * having to copy a string just to coerce a value into a larger precision.
 * Note that the precision of the source value may also come in as -1 here
 * which is used to mean a "floating" precision that is assumed to be
 * compatible with the destination domain as long as the associated value
 * is within tolerance.  This case is generally only seen for string
 * literals that have been produced by the parser.  These literals will
 * not contain blank padding and strlen() or db_get_string_size() can be
 * used to determine the number of significant characters.
 */
static int
mr_setmem_nchar (void *memptr, TP_DOMAIN * domain, DB_VALUE * value)
{
  int error = NO_ERROR;
  char *src, *mem;
  int src_precision, src_length, mem_length, pad;

  if (value == NULL)
    return NO_ERROR;

  /* Get information from the value */
  src = db_get_string (value);
  src_precision = db_value_precision (value);
  src_length = db_get_string_size (value);	/* size in bytes */

  if (src == NULL)
    {
      /*
       * Shouldn't see this, could treat as a NULL assignment.
       * If this really is supposed to be NULL, the bound bit for this
       * value will have been set by obj.c.
       */
      return NO_ERROR;
    }

  /* Check for special NTS flag.  This may not be necessary any more. */
  if (src_length < 0)
    src_length = strlen (src);


  /*
   * The only thing we really care about at this point, is the byte
   * length of the string.  The precision could be checked here but it
   * really isn't necessary for this operation.
   * Calculate the maximum number of bytes we have available here.
   */
  mem_length = STR_SIZE (domain->precision, domain->codeset);

  if (mem_length < src_length)
    {
      /*
       * should never get here, this is supposed to be caught during domain
       * validation, need a better error message.
       */
      error = ER_OBJ_DOMAIN_CONFLICT;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "");
    }
  else
    {
      /* copy the value into memory */
      mem = (char *) memptr;
      memcpy (mem, src, src_length);

      /*
       * Check for space padding, if this were a national string, we would need
       * to be padding with the appropriate space character !
       */
      pad = mem_length - src_length;
      if (pad)
	{
	  int i;
	  for (i = src_length; i < mem_length; i++)
	    mem[i] = ' ';
	}
    }
  return error;
}


/*
 * We always ensure that the string returned here is NULL terminated
 * if the copy flag is on.  This technically shouldn't be necessary but there
 * is a lot of code scattered about (especially applications) that assumes
 * NULL termination.
 */
static int
mr_getmem_nchar (void *mem, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  int mem_length;
  char *new_;

  mem_length = STR_SIZE (domain->precision, domain->codeset);

  if (!copy)
    new_ = (char *) mem;
  else
    {
      new_ = db_private_alloc (NULL, mem_length + 1);
      if (new_ == NULL)
	return er_errid ();
      memcpy (new_, (char *) mem, mem_length);
      /* make sure that all outgoing strings are NULL terminated */
      new_[mem_length] = '\0';
    }

  db_make_nchar (value, domain->precision, new_, mem_length);
  if (copy)
    value->need_clear = true;

  return NO_ERROR;
}

static int
mr_lengthmem_nchar (void *memptr, TP_DOMAIN * domain, int disk)
{
  /* There is no difference between the disk & memory sizes. */

  return STR_SIZE (domain->precision, domain->codeset);
}

static void
mr_writemem_nchar (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  int mem_length;

  mem_length = STR_SIZE (domain->precision, domain->codeset);

  /*
   * We simply dump the memory image to disk, it will already have been padded.
   * If this were a national character string, at this point, we'd have to
   * decide now to perform a character set conversion.
   */
  or_put_data (buf, (char *) mem, mem_length);
}

static void
mr_readmem_nchar (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
{
  int mem_length, padding;

  if (mem == NULL)
    {
      /*
       * If we passed in a size, then use it.  Otherwise, determine the
       * size from the domain.
       */
      if (size > 0)
	{
	  or_advance (buf, size);
	}
      else
	{
	  mem_length = STR_SIZE (domain->precision, domain->codeset);
	  or_advance (buf, mem_length);
	}
    }
  else
    {
      mem_length = STR_SIZE (domain->precision, domain->codeset);

      if (size != -1 && mem_length > size)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CORRUPTED, 0);
	  or_abort (buf);
	}
      or_get_data (buf, (char *) mem, mem_length);

      /*
       * We should only see padding if the string is contained within a packed
       * value that had extra padding to ensure alignment.  If we see these,
       * just pop them out of the buffer.  This shouldn't ever happen for
       * the "getmem" function, only for the "getval" function.
       */
      if (size != -1)
	{
	  padding = size - mem_length;
	  if (padding > 0)
	    or_advance (buf, padding);
	}
    }
}

static void
mr_freemem_nchar (void *memptr)
{
}

static void
mr_initval_nchar (DB_VALUE * value, int precision, int scale)
{
  db_value_domain_init (value, DB_TYPE_NCHAR, precision, scale);
}

static int
mr_setval_nchar (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  int error = NO_ERROR;
  int src_precision, src_length;
  char *src_string, *new_;

  if (src == NULL || (DB_IS_NULL (src) && db_value_precision (src) == 0))
    db_value_domain_init (dest, DB_TYPE_NCHAR, TP_FLOATING_PRECISION_VALUE,
			  0);
  else if (DB_IS_NULL (src))
    db_value_domain_init (dest, DB_TYPE_NCHAR, db_value_precision (src), 0);
  else
    {
      /* Get information from the value */
      src_string = db_get_string (src);
      src_precision = db_value_precision (src);
      src_length = db_get_string_size (src);	/* size in bytes */

      db_value_domain_init (dest, DB_TYPE_NCHAR, src_precision, 0);

      /* shouldn't see a NULL string at this point, treat as NULL */
      if (src_string != NULL)
	{
	  if (!copy)
	    db_make_nchar (dest, src_precision, src_string, src_length);
	  else
	    {
	      /* Check for NTS marker, may not need to do this any more */
	      if (src_length < 0)
		src_length = strlen (src_string);

	      /* make sure the copy gets a NULL terminator */
	      new_ = db_private_alloc (NULL, src_length + 1);
	      if (new_ == NULL)
		error = er_errid ();
	      else
		{
		  memcpy (new_, src_string, src_length);
		  new_[src_length] = '\0';
		  db_make_nchar (dest, src_precision, new_, src_length);
		  dest->need_clear = true;
		}
	    }
	}
    }
  return error;
}


/*
 * There are some interpretation issues here related to how
 * we store "value" chars whose memory representation is smaller
 * than the domain precision.  Since "writeval" can be used to
 * set attribute values, we should try to make the behavior
 * of lengthval & writeval the same as the lengthmem & writemem.
 * In particular, we will pad out the value with spaces to match the
 * precision (if given).  In order for this to work correctly, the
 * string pointer in the value better not be NULL and the precision had
 * better be set up correctdly.
 *
 * When packing arrays of DB_VALUEs, not as part of an instance representation,
 * we interpret a NULL string pointer to mean a NULL value and the size will
 * be zero.  If we get a value precision of TP_FLOATING_PRECISION_VALUE,
 * this is taken to mean that the precision is "floating" and we store the
 * string exactly as long as it is in the value.
 *
 * Since the domain tag of a "floating" domain can't be used to tell us how
 * long the packed char is, we must store an additional length prefix on
 * CHAR(n) values of this form.
 *
 * Note that due to the formatting differences between a "floating" domain
 * CHAR(n) and a CHAR(n) stored in the disk representation of an instance, it
 * is mandatory that values which are to be stored in an instance have the
 * exact precision set in their values.  This should be accomplished through
 * coercion on the server.
 */
static int
mr_lengthval_nchar (DB_VALUE * value, int disk)
{
  int packed_length, src_precision;
  char *src;
  INTL_CODESET src_codeset = (INTL_CODESET) db_get_string_codeset (value);

  src = db_get_string (value);
  if (src == NULL)
    return 0;

  src_precision = db_value_precision (value);
  if (!IS_FLOATING_PRECISION (src_precision))
    {
      /* Would have to check for charset conversion here ! */
      packed_length = STR_SIZE (src_precision, src_codeset);
    }
  else
    {
      /* Precision is "floating", calculate the effective precision based on the
       * string length.
       * Should be rounding this up so it is a proper multiple of the charset
       * width ?
       */
      packed_length = db_get_string_size (value);
      if (packed_length < 0)
	packed_length = strlen (src);

#if !defined (SERVER_MODE)
      /*
       * If this is a client side string, and the disk representation length
       * is requested,  Need to return the length of a converted string.
       *
       * Note: This is only done to support the 'floating precision' style of
       * fixed strings.  In this case, the string is treated similarly to a
       * variable length string.
       */
      if (!db_on_server && packed_length > 0 && disk &&
	  DO_CONVERSION_TO_SRVR_STR (src_codeset))
	{
	  int unconverted;
	  int char_count = db_get_string_length (value);
	  char *converted_string = db_private_alloc (NULL,
						     STR_SIZE (char_count,
							       src_codeset));
	  intl_convert_charset ((unsigned char *) src, char_count,
				src_codeset,
				(unsigned char *) converted_string,
				lang_server_charset_id (), &unconverted);

	  if (converted_string)
	    {
	      intl_char_size ((unsigned char *) converted_string,
			      (char_count - unconverted),
			      src_codeset, &packed_length);
	      db_private_free_and_init (NULL, converted_string);
	    }
	}
#endif /* !SERVER_MODE */

      /* add in storage for a size prefix on the packed value. */
      packed_length += OR_INT_SIZE;
    }

  /*
   * NOTE: We do NOT perform padding here, if this is used in the context
   * of a packed value, the or_put_value() family of functions must handle
   * their own padding, this is because "lengthval" and "writeval" can be
   * used to place values into the disk representation of instances and
   * there can be no padding in there.
   */

  return packed_length;
}


/*
 * See commentary in mr_lengthval_nchar.
 */
static int
mr_writeval_nchar (OR_BUF * buf, DB_VALUE * value)
{
  int src_precision, src_size, packed_size, pad;
  char *src;
  INTL_CODESET src_codeset;
  int pad_charsize;
  char pad_char[2];
  char *converted_string = NULL;
  int rc = NO_ERROR;

  src = db_get_string (value);
  if (src == NULL)
    return rc;

  src_precision = db_value_precision (value);
  src_size = db_get_string_size (value);	/* size in bytes */
  src_codeset = (INTL_CODESET) db_get_string_codeset (value);

  if (src_size < 0)
    src_size = strlen (src);

#if !defined (SERVER_MODE)
  if (!db_on_server &&
      src_size > 0 && DO_CONVERSION_TO_SRVR_STR (src_codeset))
    {
      int unconverted;
      int char_count = db_get_string_length (value);
      converted_string = (char *)
	db_private_alloc (NULL, STR_SIZE (char_count, src_codeset));
      (void) intl_convert_charset ((unsigned char *) src,
				   char_count,
				   src_codeset,
				   (unsigned char *) converted_string,
				   lang_server_charset_id (), &unconverted);
      /* Reset the 'src' of the string */
      src = converted_string;
      src_precision = src_precision - unconverted;
      intl_char_size ((unsigned char *) converted_string,
		      (char_count - unconverted), src_codeset, &src_size);
      src_codeset = lang_server_charset_id ();
    }
  (void) lang_charset_space_char (src_codeset, pad_char, &pad_charsize);
#else
  /*
   * We need to make the following call on the server.  See misc/ch_set.c
   * for the problem.
   *
   * (void) lang_srvr_space_char(pad_char, &pad_charsize);
   *
   * Until this is resolved, set the pad character to be an ASCII space.
   */
  pad_charsize = 1;
  pad_char[0] = ' ';
  pad_char[1] = ' ';
#endif

  if (!IS_FLOATING_PRECISION (src_precision))
    {
      packed_size = STR_SIZE (src_precision, src_codeset);

      if (packed_size < src_size)
	{
	  /* should have caught this by now, truncate silently */
	  rc = or_put_data (buf, src, packed_size);
	}
      else
	{
	  if ((rc = or_put_data (buf, src, src_size)) == NO_ERROR)
	    {
	      /*
	       * Check for space padding, if this were a national string, we
	       * would need to be padding with the appropriate space character!
	       */
	      pad = packed_size - src_size;
	      if (pad)
		{
		  int i;
		  for (i = src_size; i < packed_size; i += pad_charsize)
		    {
		      rc = or_put_byte (buf, pad_char[0]);
		      if (i + 1 < packed_size && pad_charsize == 2)
			rc = or_put_byte (buf, pad_char[1]);
		    }
		}
	    }
	}
      if (rc != NO_ERROR)
	goto error;
    }
  else
    {
      /*
       * This is a "floating" precision value. Pack what we can based on the
       * string size.  Note that for this to work, this can only be packed as
       * part of a domain tagged value and we must include a size prefix after
       * the domain.
       */

      /* store the size prefix */
      if ((rc = or_put_int (buf, src_size)) == NO_ERROR)
	{
	  /* store the data */
	  rc = or_put_data (buf, src, src_size);
	  /* there is no blank padding in this case */
	}
    }

error:
  if (converted_string)
    db_private_free_and_init (NULL, converted_string);

  return rc;
}

static int
mr_readval_nchar (OR_BUF * buf, DB_VALUE * value,
		  TP_DOMAIN * domain, int disk_size, bool copy,
		  char *copy_buf, int copy_buf_len)
{
  int mem_length, padding;
  char *new_;
  int rc = NO_ERROR;

  if (IS_FLOATING_PRECISION (domain->precision))
    {
      /*
       * This is only allowed if we're unpacking a "floating" domain CHAR(n)
       * in which case there will be a byte size prefix.
       * Just copy the bytes in from disk, adding a NULL terminator for now,
       * will need some logic for NCHAR conversion.
       *
       * Note that we force the destination value's domain precision to be
       * TP_FLOATING_PRECISION_VALUE here
       * even though with the IS_FLOATING_PRECISION macro it may have come
       * in as something else. This is only a temporary thing to get past
       * list file columns that are tagged with really large CHAR(n) domains
       * when they actually should be either -1 or true precisions.
       */

      mem_length = or_get_int (buf, &rc);
      if (rc != NO_ERROR)
	return ER_FAILED;
      if (value == NULL)
	{
	  rc = or_advance (buf, mem_length);
	}
      else if (!copy)
	{
	  db_make_nchar (value, TP_FLOATING_PRECISION_VALUE,
			 buf->ptr, mem_length);
	  value->need_clear = false;
	  rc = or_advance (buf, mem_length);
	}
      else
	{
	  if (copy_buf && copy_buf_len >= mem_length + 1)
	    {
	      /* read buf image into the copy_buf */
	      new_ = copy_buf;
	    }
	  else
	    {
	      /*
	       * Allocate storage for the string including the kludge NULL
	       * terminator
	       */
	      new_ = db_private_alloc (NULL, mem_length + 1);
	    }

	  if (new_ == NULL)
	    {
	      /* need to be able to return errors ! */
	      db_value_domain_init (value, domain->type->id,
				    TP_FLOATING_PRECISION_VALUE, 0);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 0);
	      or_abort (buf);
	      return ER_FAILED;
	    }
	  else
	    {
	      if ((rc = or_get_data (buf, new_, mem_length)) != NO_ERROR)
		{
		  if (new_ != copy_buf)
		    db_private_free_and_init (NULL, new_);
		  return rc;
		}
	      new_[mem_length] = '\0';	/* append the kludge NULL terminator */
	      db_make_nchar (value, TP_FLOATING_PRECISION_VALUE, new_,
			     mem_length);
	      value->need_clear = (new_ != copy_buf) ? true : false;
	    }
	}
    }
  else
    {
      mem_length = STR_SIZE (domain->precision, domain->codeset);

      if (disk_size != -1 && mem_length > disk_size)
	{
	  /*
	   * If we're low here, we could just read what we have and make a
	   * smaller value.  Still the domain should match at this point.
	   */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CORRUPTED, 0);
	  or_abort (buf);
	  return ER_FAILED;
	}

      if (value == NULL)
	{
	  rc = or_advance (buf, mem_length);
	}
      else if (!copy)
	{
	  db_make_nchar (value, domain->precision, buf->ptr, mem_length);
	  value->need_clear = false;
	  rc = or_advance (buf, mem_length);
	}
      else
	{
	  if (copy_buf && copy_buf_len >= mem_length + 1)
	    {
	      /* read buf image into the copy_buf */
	      new_ = copy_buf;
	    }
	  else
	    {
	      /*
	       * Allocate storage for the string including the kludge NULL
	       * terminator
	       */
	      new_ = db_private_alloc (NULL, mem_length + 1);
	    }

	  if (new_ == NULL)
	    {
	      /* need to be able to return errors ! */
	      db_value_domain_init (value, domain->type->id,
				    domain->precision, 0);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 0);
	      or_abort (buf);
	      return ER_FAILED;
	    }
	  else
	    {
	      if ((rc = or_get_data (buf, new_, mem_length)) != NO_ERROR)
		{
		  if (new_ != copy_buf)
		    db_private_free_and_init (NULL, new_);
		  return rc;
		}
	      new_[mem_length] = '\0';	/* append the kludge NULL terminator */
	      db_make_nchar (value, domain->precision, new_, mem_length);
	      value->need_clear = (new_ != copy_buf) ? true : false;
	    }
	}

      if (rc == NO_ERROR)
	{
	  /* We should only see padding if the string is contained within a
	   * packed value that had extra padding to ensure alignment.  If we
	   * see these, just pop them out of the buffer.
	   */
	  if (disk_size != -1)
	    {
	      padding = disk_size - mem_length;
	      if (padding > 0)
		rc = or_advance (buf, padding);
	    }
	}
    }

  /* Check if conversion needs to be done */
#if !defined (SERVER_MODE)
  if (value && !db_on_server &&
      DO_CONVERSION_TO_SQLTEXT (domain->codeset) && !DB_IS_NULL (value))
    {
      int unconverted;
      int char_count;
      char *temp_string = db_get_nchar (value, &char_count);
      if (char_count > 0)
	{
	  new_ = db_private_alloc (NULL,
				   STR_SIZE (char_count, domain->codeset));
	  (void) intl_convert_charset ((unsigned char *) temp_string,
				       char_count,
				       (INTL_CODESET) domain->codeset,
				       (unsigned char *) new_,
				       lang_charset (), &unconverted);
	  db_value_clear (value);
	  db_make_nchar (value, domain->precision, new_,
			 STR_SIZE (char_count, lang_charset ()));
	  value->need_clear = true;
	}
    }
#endif /* !SERVER_MODE */
  return rc;
}

static int
mr_cmpdisk_nchar (void *mem1, void *mem2,
		  TP_DOMAIN * domain, int do_reverse,
		  int do_coercion, int total_order, int *start_colp)
{
  int mem_length1, mem_length2, c;

  if (IS_FLOATING_PRECISION (domain->precision))
    {
      /*
       * This is only allowed if we're unpacking a "floating" domain CHAR(n) in
       * which case there will be a byte size prefix.
       */

      mem_length1 = OR_GET_INT (mem1);
      mem1 = (char *) mem1 + OR_INT_SIZE;
      mem_length2 = OR_GET_INT (mem2);
      mem2 = (char *) mem2 + OR_INT_SIZE;
    }
  else
    {
      mem_length1 = mem_length2 =
	STR_SIZE (domain->precision, domain->codeset);
    }

  c = nchar_compare ((unsigned char *) mem1, mem_length1,
		     (unsigned char *) mem2, mem_length2,
		     (INTL_CODESET) domain->codeset);
  c = MR_CMP_RETURN_CODE (c, do_reverse, domain);

  return c;
}

static int
mr_cmpval_nchar (DB_VALUE * value1, DB_VALUE * value2,
		 TP_DOMAIN * domain, int do_reverse,
		 int do_coercion, int total_order, int *start_colp)
{
  int c;

  c = nchar_compare ((unsigned char *) DB_GET_STRING (value1),
		     (int) DB_GET_STRING_SIZE (value1),
		     (unsigned char *) DB_GET_STRING (value2),
		     (int) DB_GET_STRING_SIZE (value2),
		     (INTL_CODESET) DB_GET_STRING_CODESET (value2));
  c = MR_CMP_RETURN_CODE (c, do_reverse, domain);

  return c;
}

PR_TYPE tp_NChar = {
  "national character", DB_TYPE_NCHAR, 0, 0, 0, 1,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_nchar,
  mr_initval_nchar,
  mr_setmem_nchar,
  mr_getmem_nchar,
  mr_setval_nchar,
  mr_lengthmem_nchar,
  mr_lengthval_nchar,
  mr_writemem_nchar,
  mr_readmem_nchar,
  mr_writeval_nchar,
  mr_readval_nchar,
  mr_freemem_nchar,
  mr_cmpdisk_nchar,
  mr_cmpval_nchar
};

PR_TYPE *tp_Type_nchar = &tp_NChar;

/*
 * TYPE VARNCHAR
 */

static void
mr_initmem_varnchar (void *mem)
{
  *(char **) mem = NULL;
}


/*
 * The main difference between "memory" strings and "value" strings is that
 * the length tag is stored as an in-line prefix in the memory block allocated
 * to hold the string characters.
 */
static int
mr_setmem_varnchar (void *memptr, TP_DOMAIN * domain, DB_VALUE * value)
{
  int error = NO_ERROR;
  char *src, *cur, *new_, **mem;
  int src_precision, src_length, new_length;

  /* get the current memory contents */
  mem = (char **) memptr;
  cur = *mem;

  if (value == NULL || (src = db_get_string (value)) == NULL)
    {
      /* remove the current value */
      if (cur != NULL)
	{
	  db_private_free_and_init (NULL, cur);
	  mr_initmem_varnchar (memptr);
	}
    }
  else
    {
      /*
       * Get information from the value.  Ignore precision for the time being
       * since we really only care about the byte size of the value for
       * varnchar.
       * Whether or not the value "fits" should have been checked by now.
       */
      src_precision = db_value_precision (value);
      src_length = db_get_string_size (value);	/* size in bytes */
      if (src_length < 0)
	src_length = strlen (src);

      /*
       * Currently we NULL terminate the workspace string.
       * Could try to do the single byte size hack like we have in the
       * disk representation.
       */
      new_length = src_length + sizeof (int) + 1;
      new_ = db_private_alloc (NULL, new_length);
      if (new_ == NULL)
	error = er_errid ();
      else
	{
	  if (cur != NULL)
	    db_private_free_and_init (NULL, cur);

	  /* pack in the length prefix */
	  *(int *) new_ = src_length;
	  cur = new_ + sizeof (int);
	  /* store the string */
	  memcpy (cur, src, src_length);
	  /* NULL terminate the stored string for safety */
	  cur[src_length] = '\0';
	  *mem = new_;
	}
    }

  return error;
}

static int
mr_getmem_varnchar (void *memptr, TP_DOMAIN * domain, DB_VALUE * value,
		    bool copy)
{
  int error = NO_ERROR;
  int mem_length;
  char **mem, *cur, *new_;

  /* get to the current value */
  mem = (char **) memptr;
  cur = *mem;

  if (cur == NULL)
    {
      db_value_domain_init (value, DB_TYPE_VARNCHAR, domain->precision, 0);
      value->need_clear = false;
    }
  else
    {
      /* extract the length prefix and the pointer to the actual string data */
      mem_length = *(int *) cur;
      cur += sizeof (int);

      if (!copy)
	{
	  db_make_varnchar (value, domain->precision, cur, mem_length);
	  value->need_clear = false;
	}
      else
	{
	  /* return it with a NULL terminator */
	  new_ = db_private_alloc (NULL, mem_length + 1);
	  if (new_ == NULL)
	    error = er_errid ();
	  else
	    {
	      memcpy (new_, cur, mem_length);
	      new_[mem_length] = '\0';
	      db_make_varnchar (value, domain->precision, new_, mem_length);
	      value->need_clear = true;
	    }
	}
    }
  return error;
}


/*
 * For the disk representation, we may be adding pad bytes
 * to round up to a word boundary.
 *
 * NOTE: We are currently adding a NULL terminator to the disk representation
 * for some code on the server that still manufactures pointers directly into
 * the disk buffer and assumes it is a NULL terminated string.  This terminator
 * can be removed after the server has been updated.  The logic for maintaining
 * the terminator is actuall in the or_put_varchar, family of functions.
 */
static int
mr_lengthmem_varnchar (void *memptr, TP_DOMAIN * domain, int disk)
{
  char **mem, *cur;
  int len;

  len = 0;
  if (!disk)
    {
      len = tp_VarNChar.size;
    }
  else if (memptr != NULL)
    {
      mem = (char **) memptr;
      cur = *mem;
      if (cur != NULL)
	{
	  len = *(int *) cur;
	  len = or_packed_varchar_length (len);
	}
    }

  return len;
}

static void
mr_writemem_varnchar (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
{
  char **mem, *cur;
  int len;

  mem = (char **) memptr;
  cur = *mem;
  if (cur != NULL)
    {
      len = *(int *) cur;
      cur += sizeof (int);
      or_put_varchar (buf, cur, len);
    }
}


/*
 * The amount of memory requested is currently calculated based on the
 * stored size prefix.  If we ever go to a system where we avoid storing
 * the size, then we could use the size argument passed in to this function
 * but that may also include any padding byte that added to bring us up to
 * a word boundary. Might want some way to determine which bytes at the
 * end of a string are padding.
 */
static void
mr_readmem_varnchar (OR_BUF * buf, void *memptr, TP_DOMAIN * domain, int size)
{
  char **mem, *cur, *new_;
  int len;
  int mem_length, pad;
  char *start;
  int rc = NO_ERROR;

  /* Must have an explicit size here - can't be determined from the domain */
  if (size < 0)
    return;

  if (memptr == NULL)
    {
      if (size)
	or_advance (buf, size);
    }
  else
    {
      mem = (char **) memptr;
      cur = *mem;
      /* should we be checking for existing strings ? */
#if 0
      if (cur != NULL)
	db_private_free_and_init (NULL, cur);
#endif

      new_ = NULL;
      if (size)
	{
	  start = buf->ptr;

	  /* KLUDGE, we have some knowledge of how the thing is stored here in
	   * order have some control over the conversion between the packed
	   * length prefix and the full word memory length prefix.
	   * Might want to put this in another specialized or_ function.
	   */

	  /* Get just the length prefix. */
	  len = or_get_varchar_length (buf, &rc);

	  /*
	   * Allocate storage for this string, including our own full word size
	   * prefix and a NULL terminator.
	   */
	  mem_length = len + sizeof (int) + 1;

	  new_ = db_private_alloc (NULL, mem_length);
	  if (new_ == NULL)
	    or_abort (buf);
	  else
	    {
	      /* store the length in our memory prefix */
	      *(int *) new_ = len;
	      cur = new_ + sizeof (int);

	      /* read the string, with the NULL terminator(which is expected) */
	      or_get_data (buf, cur, len + 1);
	      /* align like or_get_varchar */
	      or_get_align32 (buf);
	    }

	  /*
	   * If we were given a size, check to see if for some reason this is
	   * larger than the already word aligned string that we have now
	   * extracted.  This shouldn't be the case but since we've got a
	   * length, we may as well obey it.
	   */
	  pad = size - (int) (buf->ptr - start);
	  if (pad > 0)
	    or_advance (buf, pad);
	}
      *mem = new_;
    }
}

static void
mr_freemem_varnchar (void *memptr)
{
  char *cur;

  if (memptr != NULL)
    {
      cur = *(char **) memptr;
      if (cur != NULL)
	db_private_free_and_init (NULL, cur);
    }
}

static void
mr_initval_varnchar (DB_VALUE * value, int precision, int scale)
{
  db_make_varnchar (value, precision, NULL, 0);
  value->need_clear = false;
}

static int
mr_setval_varnchar (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  int error = NO_ERROR;
  int src_precision, src_length;
  char *src_str, *new_;

  if (src == NULL || (DB_IS_NULL (src) && db_get_string (src) == 0))
    {
      error = db_value_domain_init (dest,
				    DB_TYPE_VARNCHAR, DB_DEFAULT_PRECISION,
				    0);
    }
  else if (DB_IS_NULL (src) || (src_str = db_get_string (src)) == NULL)
    {
      error = db_value_domain_init (dest,
				    DB_TYPE_VARNCHAR,
				    db_value_precision (src), 0);
    }
  else
    {
      /* Get information from the value. */
      src_precision = db_value_precision (src);
      src_length = db_get_string_size (src);
      if (src_length < 0)
	src_length = strlen (src_str);

      /* should we be paying attention to this? it is extremely dangerous */
      if (!copy)
	{
	  error = db_make_varnchar (dest, src_precision, src_str, src_length);
	}
      else
	{
	  new_ = db_private_alloc (NULL, src_length + 1);
	  if (new_ == NULL)
	    {
	      db_value_domain_init (dest, DB_TYPE_VARNCHAR, src_precision, 0);
	      error = er_errid ();
	    }
	  else
	    {
	      memcpy (new_, src_str, src_length);
	      new_[src_length] = '\0';
	      db_make_varnchar (dest, src_precision, new_, src_length);
	      dest->need_clear = true;
	    }
	}
    }

  return error;
}


/*
 * Ignoring precision as byte size is really the only important thing for
 * varnchar.
 */
static int
mr_lengthval_varnchar (DB_VALUE * value, int disk)
{
  int src_length, len;
  const char *str;
#if !defined (SERVER_MODE)
  INTL_CODESET src_codeset = (INTL_CODESET) db_get_string_codeset (value);
#endif

  len = 0;
  if (value != NULL && (str = db_get_string (value)) != NULL)
    {
      src_length = db_get_string_size (value);	/* size in bytes */
      if (src_length < 0)
	src_length = strlen (str);

#if !defined (SERVER_MODE)
      /*
       * If this is a client side string, and the disk representation length
       * is requested,  Need to return the length of a converted string.
       */
      if (!db_on_server && src_length > 0 && disk &&
	  DO_CONVERSION_TO_SRVR_STR (src_codeset))
	{
	  int unconverted;
	  int char_count = db_get_string_length (value);
	  char *converted_string = db_private_alloc (NULL,
						     STR_SIZE (char_count,
							       src_codeset));
	  intl_convert_charset ((unsigned char *) str, char_count,
				src_codeset,
				(unsigned char *) converted_string,
				lang_server_charset_id (), &unconverted);

	  if (converted_string)
	    {
	      intl_char_size ((unsigned char *) converted_string,
			      (char_count - unconverted), src_codeset, &len);
	      db_private_free_and_init (NULL, converted_string);
	    }
	}
#endif
      len = or_packed_varchar_length (src_length);
    }

  return len;
}

static int
mr_writeval_varnchar (OR_BUF * buf, DB_VALUE * value)
{
  int src_size;
  INTL_CODESET src_codeset;
  char *str;
  int rc = NO_ERROR;

  if (value != NULL && (str = db_get_string (value)) != NULL)
    {
      src_size = db_get_string_size (value);	/* size in bytes */
      if (src_size < 0)
	src_size = strlen (str);

      src_codeset = (INTL_CODESET) db_get_string_codeset (value);
#if !defined (SERVER_MODE)
      if (!db_on_server &&
	  src_size > 0 && DO_CONVERSION_TO_SRVR_STR (src_codeset))
	{
	  int unconverted;
	  int char_count = db_get_string_length (value);
	  char *converted_string = db_private_alloc (NULL,
						     STR_SIZE (char_count,
							       src_codeset));
	  (void) intl_convert_charset ((unsigned char *) str, char_count,
				       src_codeset,
				       (unsigned char *) converted_string,
				       lang_server_charset_id (),
				       &unconverted);

	  if (converted_string)
	    {
	      intl_char_size ((unsigned char *) converted_string,
			      (char_count - unconverted),
			      src_codeset, &src_size);
	      or_put_varchar (buf, converted_string, src_size);
	      db_private_free_and_init (NULL, converted_string);
	    }
	}
      else
	rc = or_put_varchar (buf, str, src_size);
#else /* SERVER_MODE */
      rc = or_put_varchar (buf, str, src_size);
#endif /* !SERVER_MODE */
    }
  return rc;
}

static int
mr_readval_varnchar (OR_BUF * buf, DB_VALUE * value,
		     TP_DOMAIN * domain, int size, bool copy,
		     char *copy_buf, int copy_buf_len)
{
  int pad, precision;
#if !defined (SERVER_MODE)
  INTL_CODESET codeset;
#endif
  char *new_, *start = NULL;
  int str_length;
  int rc = NO_ERROR;

  if (value == NULL)
    {
      if (size == -1)
	rc = or_skip_varchar (buf);
      else
	{
	  if (size)
	    rc = or_advance (buf, size);
	}
    }
  else
    {
      /*
       * Allow the domain to be NULL in which case this must be a maximum
       * precision varnchar.  Should be used only by the tfcl.c class
       * object accessors.
       *
       * Since this domain may be a transient domain from tfcl, don't retain
       * pointers to it.
       */
      if (domain != NULL)
	{
	  precision = domain->precision;
#if !defined (SERVER_MODE)
	  codeset = (INTL_CODESET) domain->codeset;
#endif
	}
      else
	{
	  precision = DB_MAX_VARNCHAR_PRECISION;
#if !defined (SERVER_MODE)
	  codeset = lang_charset ();
#endif
	}

      /* Branch according to convention based on size */
      if (!copy)
	{
	  str_length = or_get_varchar_length (buf, &rc);
	  db_make_varnchar (value, precision, buf->ptr, str_length);
	  value->need_clear = false;
	  or_skip_varchar_remainder (buf, str_length);
	}
      else
	{
	  if (size == 0)
	    {
	      /* its NULL */
	      db_value_domain_init (value, DB_TYPE_VARNCHAR, precision, 0);
	    }
	  else
	    {			/* size != 0 */
	      if (size == -1)
		{
		  /* Standard packed varnchar with a size prefix */
		  ;		/* do nothing */
		}
	      else
		{		/* size != -1 */
		  /* Standard packed varnchar within an area of fixed size,
		   * usually this means we're looking at the disk
		   * representation of an attribute.
		   * Just like the -1 case except we advance past the additional
		   * padding.
		   */
		  start = buf->ptr;
		}		/* size != -1 */

	      str_length = or_get_varchar_length (buf, &rc);
	      if (rc != NO_ERROR)
		{
		  return ER_FAILED;
		}

	      if (copy_buf && copy_buf_len >= str_length + 1)
		{
		  /* read buf image into the copy_buf */
		  new_ = copy_buf;
		}
	      else
		{
		  /*
		   * Allocate storage for the string including the kludge
		   * NULL terminator
		   */
		  new_ = (char *) db_private_alloc (NULL, str_length + 1);
		}

	      if (new_ == NULL)
		{
		  /* need to be able to return errors ! */
		  db_value_domain_init (value, domain->type->id,
					TP_FLOATING_PRECISION_VALUE, 0);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 0);
		  or_abort (buf);
		  return ER_FAILED;
		}
	      else
		{
		  /* read the kludge NULL terminator */
		  if ((rc =
		       or_get_data (buf, new_, str_length + 1)) == NO_ERROR)
		    {
		      /* round up to a word boundary */
		      rc = or_get_align32 (buf);
		    }
		  if (rc != NO_ERROR)
		    {
		      if (new_ != copy_buf)
			db_private_free_and_init (NULL, new_);
		      return ER_FAILED;
		    }

		  db_make_varnchar (value, precision, new_, str_length);
		  value->need_clear = (new_ != copy_buf) ? true : false;

		  if (size == -1)
		    {
		      /* Standard packed varnchar with a size prefix */
		      ;		/* do nothing */
		    }
		  else
		    {		/* size != -1 */
		      /* Standard packed varnchar within an area of fixed
		       * size, usually this means we're looking at the disk
		       * representation of an attribute.
		       * Just like the -1 case except we advance past the
		       * additional padding.
		       */
		      pad = size - (int) (buf->ptr - start);
		      if (pad > 0)
			rc = or_advance (buf, pad);
		    }		/* size != -1 */
		}		/* else */
	    }			/* size != 0 */
	}

      /* Check if conversion needs to be done */
#if !defined (SERVER_MODE)
      if (!db_on_server &&
	  DO_CONVERSION_TO_SQLTEXT (codeset) && !DB_IS_NULL (value))
	{
	  int unconverted;
	  int char_count;
	  char *temp_string = db_get_nchar (value, &char_count);
	  if (char_count > 0)
	    {
	      new_ = (char *)
		db_private_alloc (NULL, STR_SIZE (char_count, codeset));
	      (void) intl_convert_charset ((unsigned char *) temp_string,
					   char_count, codeset,
					   (unsigned char *) new_,
					   lang_charset (), &unconverted);
	      db_value_clear (value);
	      db_make_varnchar (value, precision, new_,
				STR_SIZE (char_count, lang_charset ()));
	      value->need_clear = true;
	    }
	}
#endif /* !SERVER_MODE */
    }
  return rc;
}

static int
mr_cmpdisk_varnchar (void *mem1, void *mem2,
		     TP_DOMAIN * domain, int do_reverse,
		     int do_coercion, int total_order, int *start_colp)
{
  int c = DB_UNK;
  int str_length1, str_length2;
  OR_BUF buf1, buf2;
  int rc = NO_ERROR;

  or_init (&buf1, (char *) mem1, 0);
  str_length1 = or_get_varchar_length (&buf1, &rc);
  if (rc == NO_ERROR)
    {

      or_init (&buf2, (char *) mem2, 0);
      str_length2 = or_get_varchar_length (&buf2, &rc);
      if (rc == NO_ERROR)
	{

	  c = varnchar_compare ((unsigned char *) buf1.ptr, str_length1,
				(unsigned char *) buf2.ptr, str_length2,
				(INTL_CODESET) domain->codeset);
	  c = MR_CMP_RETURN_CODE (c, do_reverse, domain);
	  return c;
	}
    }

  return DB_UNK;
}

static int
mr_cmpval_varnchar (DB_VALUE * value1, DB_VALUE * value2,
		    TP_DOMAIN * domain, int do_reverse,
		    int do_coercion, int total_order, int *start_colp)
{
  int c;

  c = varnchar_compare ((unsigned char *) DB_GET_STRING (value1),
			(int) DB_GET_STRING_SIZE (value1),
			(unsigned char *) DB_GET_STRING (value2),
			(int) DB_GET_STRING_SIZE (value2),
			(INTL_CODESET) DB_GET_STRING_CODESET (value2));
  c = MR_CMP_RETURN_CODE (c, do_reverse, domain);

  return c;
}

PR_TYPE tp_VarNChar = {
  "national character varying", DB_TYPE_VARNCHAR, 1, sizeof (const char *), 0,
  4,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_varnchar,
  mr_initval_varnchar,
  mr_setmem_varnchar,
  mr_getmem_varnchar,
  mr_setval_varnchar,
  mr_lengthmem_varnchar,
  mr_lengthval_varnchar,
  mr_writemem_varnchar,
  mr_readmem_varnchar,
  mr_writeval_varnchar,
  mr_readval_varnchar,
  mr_freemem_varnchar,
  mr_cmpdisk_varnchar,
  mr_cmpval_varnchar
};

PR_TYPE *tp_Type_varnchar = &tp_VarNChar;

/*
 * TYPE BIT
 */

static void
mr_initmem_bit (void *memptr)
{
  /* could fill with zeros, punt for now */
}


/*
 * Due to the "within tolerance" comparison of domains used for
 * assignment validation, we may get values in here whose precision is
 * less than the precision of the actual attribute.   This prevents
 * having to copy a string just to coerce a value into a larger precision.
 * Note that the precision of the source value may also come in as -1 here
 * which is used to mean a "floating" precision that is assumed to be
 * compatible with the destination domain as long as the associated value
 * is within tolerance.  This case is generally only seen for string
 * literals that have been produced by the parser.  These literals will
 * not contain blank padding and strlen() or db_get_string_size() can be
 * used to determine the number of significant characters.
 */
static int
mr_setmem_bit (void *memptr, TP_DOMAIN * domain, DB_VALUE * value)
{
  int error = NO_ERROR;
  char *src, *mem;
  int src_precision, src_length, mem_length, pad;

  if (value == NULL)
    return NO_ERROR;

  /* Get information from the value */
  src = db_get_string (value);
  src_precision = db_value_precision (value);
  src_length = db_get_string_size (value);	/* size in bytes */

  if (src == NULL)
    {
      /*
       * Shouldn't see this, could treat as a NULL assignment.
       * If this really is supposed to be NULL, the bound bit for this
       * value will have been set by obj.c.
       */
      return NO_ERROR;
    }

  /*
   * The only thing we really care about at this point, is the byte
   * length of the string.  The precision could be checked here but it
   * really isn't necessary for this operation.
   * Calculate the maximum number of bytes we have available here.
   */
  mem_length = STR_SIZE (domain->precision, domain->codeset);

  if (mem_length < src_length)
    {
      /*
       * should never get here, this is supposed to be caught during domain
       * validation, need a better error message.
       */
      error = ER_OBJ_DOMAIN_CONFLICT;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "");
    }
  else
    {
      /* copy the value into memory */
      mem = (char *) memptr;
      memcpy (mem, src, src_length);

      /* Check for padding */
      pad = mem_length - src_length;
      if (pad)
	{
	  int i;
	  for (i = src_length; i < mem_length; i++)
	    mem[i] = '\0';
	}
    }
  return error;
}

static int
mr_getmem_bit (void *mem, TP_DOMAIN * domain, DB_VALUE * value, bool copy)
{
  int mem_length;
  char *new_;

  mem_length = STR_SIZE (domain->precision, domain->codeset);

  if (!copy)
    new_ = (char *) mem;
  else
    {
      new_ = db_private_alloc (NULL, mem_length + 1);
      if (new_ == NULL)
	return er_errid ();
      memcpy (new_, (char *) mem, mem_length);
    }

  db_make_bit (value, domain->precision, new_, domain->precision);
  if (copy)
    value->need_clear = true;

  return NO_ERROR;
}

static int
mr_lengthmem_bit (void *memptr, TP_DOMAIN * domain, int disk)
{
  /* There is no difference between the disk & memory sizes. */

  return STR_SIZE (domain->precision, domain->codeset);
}

static void
mr_writemem_bit (OR_BUF * buf, void *mem, TP_DOMAIN * domain)
{
  int mem_length;

  mem_length = STR_SIZE (domain->precision, domain->codeset);

  /*
   * We simply dump the memory image to disk, it will already have been padded.
   * If this were a national character string, at this point, we'd have to
   * decide now to perform a character set conversion.
   */
  or_put_data (buf, (char *) mem, mem_length);
}

static void
mr_readmem_bit (OR_BUF * buf, void *mem, TP_DOMAIN * domain, int size)
{
  int mem_length, padding;

  if (mem == NULL)
    {
      /* If we passed in a size, then use it.  Otherwise, determine the
         size from the domain. */
      if (size > 0)
	{
	  or_advance (buf, size);
	}
      else
	{
	  mem_length = STR_SIZE (domain->precision, domain->codeset);
	  or_advance (buf, mem_length);
	}
    }
  else
    {
      mem_length = STR_SIZE (domain->precision, domain->codeset);
      if (size != -1 && mem_length > size)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CORRUPTED, 0);
	  or_abort (buf);
	}
      or_get_data (buf, (char *) mem, mem_length);

      /*
       * We should only see padding if the string is contained within a packed
       * value that had extra padding to ensure alignment.  If we see these,
       * just pop them out of the buffer.  This shouldn't ever happen for the
       * "getmem" function, only for the "getval" function.
       */
      if (size != -1)
	{
	  padding = size - mem_length;
	  if (padding > 0)
	    or_advance (buf, padding);
	}
    }
}

static void
mr_freemem_bit (void *memptr)
{
}

static void
mr_initval_bit (DB_VALUE * value, int precision, int scale)
{
  db_value_domain_init (value, DB_TYPE_BIT, precision, scale);
}

static int
mr_setval_bit (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  int error = NO_ERROR;
  int src_precision, src_length, src_number_of_bits;
  char *src_string, *new_;

  if (src == NULL || (DB_IS_NULL (src) && db_value_precision (src) == 0))
    db_value_domain_init (dest, DB_TYPE_BIT, TP_FLOATING_PRECISION_VALUE, 0);
  else if (DB_IS_NULL (src))
    db_value_domain_init (dest, DB_TYPE_BIT, db_value_precision (src), 0);
  else
    {
      /* Get information from the value */
      src_string = db_get_bit (src, &src_number_of_bits);
      src_precision = db_value_precision (src);
      src_length = db_get_string_size (src);	/* size in bytes */

      db_value_domain_init (dest, DB_TYPE_BIT, src_precision, 0);

      /* shouldn't see a NULL string at this point, treat as NULL */
      if (src_string != NULL)
	{
	  if (!copy)
	    {
	      db_make_bit (dest, src_precision, src_string,
			   src_number_of_bits);
	    }
	  else
	    {

	      /* make sure the copy gets a NULL terminator */
	      new_ = db_private_alloc (NULL, src_length + 1);
	      if (new_ == NULL)
		error = er_errid ();
	      else
		{
		  memcpy (new_, src_string, src_length);
		  db_make_bit (dest, src_precision, new_, src_number_of_bits);
		  dest->need_clear = true;
		}
	    }
	}
    }
  return error;
}


/*
 * There are some interpretation issues here related to how
 * we store "value" chars whose memory representation is smaller
 * than the domain precision.  Since "writeval" can be used to
 * set attribute values, we should try to make the behavior
 * of lengthval & writeval the same as the lengthmem & writemem.
 * In particular, we will pad out the value with spaces to match the
 * precision (if given).  In order for this to work correctly, the
 * string pointer in the value better not be NULL and the precision had
 * better be set up correctdly.
 *
 * When packing arrays of DB_VALUEs, not as part of an instance representation,
 * we interpret a NULL string pointer to mean a NULL value and the size will
 * be zero.  If we get a value precision of TP_FLOATING_PRECISION_VALUE,
 * this is taken to mean that the precision is "floating" and we store the
 * string exactly as long as it is in the value.
 *
 * Since the domain tag of a "floating" domain can't be used to tell us how
 * long  the packed char is, we must store an additional length prefix on
 * BIT(n) values of this form.
 *
 */
static int
mr_lengthval_bit (DB_VALUE * value, int disk)
{
  int packed_length, src_precision;
  char *src;

  src = db_get_string (value);
  if (src == NULL)
    return 0;

  src_precision = db_value_precision (value);
  if (!IS_FLOATING_PRECISION (src_precision))
    {
      packed_length = STR_SIZE (src_precision, db_get_string_codeset (value));
    }
  else
    {
      /*
       * Precision is "floating", calculate the effective precision based on the
       * string length.
       */
      packed_length = db_get_string_size (value);

      /* add in storage for a size prefix on the packed value. */
      packed_length += OR_INT_SIZE;
    }

  /*
   * NOTE: We do NOT perform padding here, if this is used in the context
   * of a packed value, the or_put_value() family of functions must handle
   * their own padding, this is because "lengthval" and "writeval" can be
   * used to place values into the disk representation of instances and
   * there can be no padding in there.
   */

  return packed_length;
}


/*
 * See commentary in mr_lengthval_bit.
 */
static int
mr_writeval_bit (OR_BUF * buf, DB_VALUE * value)
{
  int src_precision, src_length, packed_length, pad;
  char *src;
  int rc = NO_ERROR;

  src = db_get_string (value);
  if (src == NULL)
    return rc;

  src_precision = db_value_precision (value);
  src_length = db_get_string_size (value);	/* size in bytes */

  if (!IS_FLOATING_PRECISION (src_precision))
    {
      /* Would have to check for charset conversion here ! */
      packed_length = STR_SIZE (src_precision, db_get_string_codeset (value));

      if (packed_length < src_length)
	{
	  /* should have caught this by now, truncate silently */
	  or_put_data (buf, src, packed_length);
	}
      else
	{
	  if ((rc = or_put_data (buf, src, src_length)) == NO_ERROR)
	    {
	      /* Check for padding */
	      pad = packed_length - src_length;
	      if (pad)
		{
		  int i;
		  for (i = src_length; i < packed_length; i++)
		    rc = or_put_byte (buf, (int) '\0');
		}
	    }
	}
    }
  else
    {
      /*
       * This is a "floating" precision value. Pack what we can based on the
       * string size.  Note that for this to work, this can only be packed
       * as part of a domain tagged value and we must include a length
       * prefix after the domain.
       */
      packed_length = db_get_string_length (value);

      /* store the size prefix */
      if ((rc = or_put_int (buf, packed_length)) == NO_ERROR)
	{
	  /* store the data */
	  rc = or_put_data (buf, src, BITS_TO_BYTES (packed_length));
	  /* there is no blank padding in this case */
	}
    }
  return rc;
}

static int
mr_readval_bit (OR_BUF * buf, DB_VALUE * value,
		TP_DOMAIN * domain, int disk_size, bool copy,
		char *copy_buf, int copy_buf_len)
{
  int mem_length, padding;
  int bit_length;
  char *new_;
  int rc = NO_ERROR;

  if (IS_FLOATING_PRECISION (domain->precision))
    {
      /*
       * This is only allowed if we're unpacking a "floating" domain BIT(n)
       * in which case there will be a byte size prefix.
       *
       * Note that we force the destination value's domain precision to be
       * TP_FLOATING_PRECISION_VALUE here
       * even though with the IS_FLOATING_PRECISION macro it may have come
       * in as something else. This is only a temporary thing to get past
       * list file columns that are tagged with really large BIT(n) domains when
       * they actually should be either -1 or true precisions.
       */

      bit_length = or_get_int (buf, &rc);
      if (rc != NO_ERROR)
	return ER_FAILED;
      mem_length = BITS_TO_BYTES (bit_length);
      if (value == NULL)
	{
	  rc = or_advance (buf, mem_length);
	}
      else if (!copy)
	{
	  db_make_bit (value, TP_FLOATING_PRECISION_VALUE, buf->ptr,
		       bit_length);
	  value->need_clear = false;
	  rc = or_advance (buf, mem_length);
	}
      else
	{
	  if (copy_buf && copy_buf_len >= mem_length + 1)
	    {
	      /* read buf image into the copy_buf */
	      new_ = copy_buf;
	    }
	  else
	    {
	      /*
	       * Allocate storage for the string including the kludge NULL
	       * terminator
	       */
	      new_ = db_private_alloc (NULL, mem_length + 1);
	    }

	  if (new_ == NULL)
	    {
	      /* need to be able to return errors ! */
	      db_value_domain_init (value, domain->type->id,
				    TP_FLOATING_PRECISION_VALUE, 0);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 0);
	      or_abort (buf);
	      return ER_FAILED;
	    }
	  else
	    {
	      if ((rc = or_get_data (buf, new_, mem_length)) != NO_ERROR)
		{
		  if (new_ != copy_buf)
		    db_private_free_and_init (NULL, new_);
		  return rc;
		}
	      new_[mem_length] = '\0';	/* append the kludge NULL terminator */
	      db_make_bit (value, TP_FLOATING_PRECISION_VALUE, new_,
			   bit_length);
	      value->need_clear = (new_ != copy_buf) ? true : false;
	    }
	}
    }
  else
    {
      mem_length = STR_SIZE (domain->precision, domain->codeset);

      if (disk_size != -1 && mem_length > disk_size)
	{
	  /*
	   * If we're low here, we could just read what we have and make a
	   * smaller value.  Still the domain should match at this point.
	   */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CORRUPTED, 0);
	  or_abort (buf);
	  return ER_FAILED;
	}

      if (value == NULL)
	{
	  rc = or_advance (buf, mem_length);
	}
      else if (!copy)
	{
	  db_make_bit (value, domain->precision, buf->ptr, domain->precision);
	  value->need_clear = false;
	  rc = or_advance (buf, mem_length);
	}
      else
	{
	  if (copy_buf && copy_buf_len >= mem_length + 1)
	    {
	      /* read buf image into the copy_buf */
	      new_ = copy_buf;
	    }
	  else
	    {
	      /*
	       * Allocate storage for the string including the kludge NULL
	       * terminator
	       */
	      new_ = db_private_alloc (NULL, mem_length + 1);
	    }

	  if (new_ == NULL)
	    {
	      /* need to be able to return errors ! */
	      db_value_domain_init (value, domain->type->id,
				    domain->precision, 0);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 0);
	      or_abort (buf);
	      return ER_FAILED;
	    }
	  else
	    {
	      if ((rc = or_get_data (buf, new_, mem_length)) != NO_ERROR)
		{
		  if (new_ != copy_buf)
		    db_private_free_and_init (NULL, new_);
		  return rc;
		}
	      new_[mem_length] = '\0';	/* append the kludge NULL terminator */
	      db_make_bit (value, domain->precision, new_, domain->precision);
	      value->need_clear = (new_ != copy_buf) ? true : false;
	    }
	}
      if (rc == NO_ERROR)
	{
	  /*
	   * We should only see padding if the string is contained within a
	   * packed value that had extra padding to ensure alignment.  If we see
	   * these, just pop them out of the buffer.
	   */
	  if (disk_size != -1)
	    {
	      padding = disk_size - mem_length;
	      if (padding > 0)
		rc = or_advance (buf, padding);
	    }
	}
    }
  return rc;
}

static int
mr_cmpdisk_bit (void *mem1, void *mem2,
		TP_DOMAIN * domain, int do_reverse,
		int do_coercion, int total_order, int *start_colp)
{
  int bit_length1, mem_length1, bit_length2, mem_length2, c;

  if (IS_FLOATING_PRECISION (domain->precision))
    {
      /*
       * This is only allowed if we're unpacking a "floating" domain BIT(n)
       * in which case there will be a byte size prefix.
       *
       * Note that we force the destination value's domain precision to be
       * TP_FLOATING_PRECISION_VALUE here
       * even though with the IS_FLOATING_PRECISION macro it may have come
       * in as something else. This is only a temporary thing to get past
       * list file columns that are tagged with really large BIT(n) domains when
       * they actually should be either -1 or true precisions.
       */

      bit_length1 = OR_GET_INT (mem1);
      mem1 = (char *) mem1 + OR_INT_SIZE;
      mem_length1 = BITS_TO_BYTES (bit_length1);
      bit_length2 = OR_GET_INT (mem2);
      mem2 = (char *) mem2 + OR_INT_SIZE;
      mem_length2 = BITS_TO_BYTES (bit_length2);
    }
  else
    {
      mem_length1 = mem_length2 =
	STR_SIZE (domain->precision, domain->codeset);
    }

  c = bit_compare ((unsigned char *) mem1, mem_length1,
		   (unsigned char *) mem2, mem_length2);
  c = MR_CMP_RETURN_CODE (c, do_reverse, domain);

  return c;
}

static int
mr_cmpval_bit (DB_VALUE * value1, DB_VALUE * value2,
	       TP_DOMAIN * domain, int do_reverse,
	       int do_coercion, int total_order, int *start_colp)
{
  int c;

  c = bit_compare ((unsigned char *) DB_GET_STRING (value1),
		   (int) DB_GET_STRING_SIZE (value1),
		   (unsigned char *) DB_GET_STRING (value2),
		   (int) DB_GET_STRING_SIZE (value2));
  c = MR_CMP_RETURN_CODE (c, do_reverse, domain);

  return c;
}

PR_TYPE tp_Bit = {
  "bit", DB_TYPE_BIT, 0, 0, 0, 1,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_bit,
  mr_initval_bit,
  mr_setmem_bit,
  mr_getmem_bit,
  mr_setval_bit,
  mr_lengthmem_bit,
  mr_lengthval_bit,
  mr_writemem_bit,
  mr_readmem_bit,
  mr_writeval_bit,
  mr_readval_bit,
  mr_freemem_bit,
  mr_cmpdisk_bit,
  mr_cmpval_bit
};

PR_TYPE *tp_Type_bit = &tp_Bit;

/*
 * TYPE VARBIT
 */

static void
mr_initmem_varbit (void *mem)
{
  *(char **) mem = NULL;
}


/*
 * The main difference between "memory" strings and "value" strings is that
 * the length tag is stored as an in-line prefix in the memory block allocated
 * to hold the string characters.
 */
static int
mr_setmem_varbit (void *memptr, TP_DOMAIN * domain, DB_VALUE * value)
{
  int error = NO_ERROR;
  char *src, *cur, *new_, **mem;
  int src_precision, src_length, src_length_bits, new_length;

  /* get the current memory contents */
  mem = (char **) memptr;
  cur = *mem;

  if (value == NULL || (src = db_get_string (value)) == NULL)
    {
      /* remove the current value */
      if (cur != NULL)
	{
	  db_private_free_and_init (NULL, cur);
	  mr_initmem_varbit (memptr);
	}
    }
  else
    {
      /*
       * Get information from the value.  Ignore precision for the time being
       * since we really only care about the byte size of the value for varbit.
       * Whether or not the value "fits" should have been checked by now.
       */
      src_precision = db_value_precision (value);
      src_length = db_get_string_size (value);	/* size in bytes */
      src_length_bits = db_get_string_length (value);	/* size in bits */

      new_length = src_length + sizeof (int);
      new_ = db_private_alloc (NULL, new_length);
      if (new_ == NULL)
	error = er_errid ();
      else
	{
	  if (cur != NULL)
	    db_private_free_and_init (NULL, cur);

	  /* pack in the length prefix */
	  *(int *) new_ = src_length_bits;
	  cur = new_ + sizeof (int);
	  /* store the string */
	  memcpy (cur, src, src_length);
	  *mem = new_;
	}
    }

  return error;
}

static int
mr_getmem_varbit (void *memptr, TP_DOMAIN * domain,
		  DB_VALUE * value, bool copy)
{
  int error = NO_ERROR;
  int mem_bit_length;
  char **mem, *cur, *new_;

  /* get to the current value */
  mem = (char **) memptr;
  cur = *mem;

  if (cur == NULL)
    {
      db_value_domain_init (value, DB_TYPE_VARBIT, domain->precision, 0);
      value->need_clear = false;
    }
  else
    {
      /* extract the length prefix and the pointer to the actual string data */
      mem_bit_length = *(int *) cur;
      cur += sizeof (int);

      if (!copy)
	{
	  db_make_varbit (value, domain->precision, cur, mem_bit_length);
	  value->need_clear = false;
	}
      else
	{
	  /* return it */
	  new_ = (char *)
	    db_private_alloc (NULL, BITS_TO_BYTES (mem_bit_length) + 1);
	  if (new_ == NULL)
	    error = er_errid ();
	  else
	    {
	      memcpy (new_, cur, BITS_TO_BYTES (mem_bit_length));
	      db_make_varbit (value, domain->precision, new_, mem_bit_length);
	      value->need_clear = true;
	    }
	}
    }
  return error;
}


/*
 * For the disk representation, we may be adding pad bytes
 * to round up to a word boundary.
 */
static int
mr_lengthmem_varbit (void *memptr, TP_DOMAIN * domain, int disk)
{
  char **mem, *cur;
  int len;

  len = 0;
  if (!disk)
    len = tp_VarBit.size;
  else if (memptr != NULL)
    {
      mem = (char **) memptr;
      cur = *mem;
      if (cur != NULL)
	{
	  len = *(int *) cur;
	  len = or_packed_varbit_length (len);
	}
    }

  return len;
}


static void
mr_writemem_varbit (OR_BUF * buf, void *memptr, TP_DOMAIN * domain)
{
  char **mem, *cur;
  int bitlen;

  mem = (char **) memptr;
  cur = *mem;
  if (cur != NULL)
    {
      bitlen = *(int *) cur;
      cur += sizeof (int);
      or_put_varbit (buf, cur, bitlen);
    }
}


/*
 * The amount of memory requested is currently calculated based on the
 * stored size prefix.  If we ever go to a system where we avoid storing the
 * size, then we could use the size argument passed in to this function but
 * that may also include any padding byte that added to bring us up to a word
 * boundary. Might want some way to determine which bytes at the end of a
 * string are padding.
 */
static void
mr_readmem_varbit (OR_BUF * buf, void *memptr, TP_DOMAIN * domain, int size)
{
  char **mem, *cur, *new_;
  int bit_len;
  int mem_length, pad;
  char *start;
  int rc = NO_ERROR;

  /* Must have an explicit size here - can't be determined from the domain */
  if (size < 0)
    return;

  if (memptr == NULL)
    {
      if (size)
	or_advance (buf, size);
    }
  else
    {
      mem = (char **) memptr;
      cur = *mem;
      /* should we be checking for existing strings ? */
#if 0
      if (cur != NULL)
	db_private_free_and_init (NULL, cur);
#endif

      new_ = NULL;
      if (size)
	{
	  start = buf->ptr;

	  /* KLUDGE, we have some knowledge of how the thing is stored here in
	   * order have some control over the conversion between the packed
	   * length prefix and the full word memory length prefix.
	   * Might want to put this in another specialized or_ function.
	   */

	  /* Get just the length prefix. */
	  bit_len = or_get_varbit_length (buf, &rc);

	  /*
	   * Allocate storage for this string, including our own full word size
	   * prefix.
	   */
	  mem_length = BITS_TO_BYTES (bit_len) + sizeof (int);

	  new_ = db_private_alloc (NULL, mem_length);
	  if (new_ == NULL)
	    or_abort (buf);
	  else
	    {
	      /* store the length in our memory prefix */
	      *(int *) new_ = bit_len;
	      cur = new_ + sizeof (int);

	      /* read the string */
	      or_get_data (buf, cur, BITS_TO_BYTES (bit_len));
	      /* align like or_get_varchar */
	      or_get_align32 (buf);
	    }

	  /*
	   * If we were given a size, check to see if for some reason this is
	   * larger than the already word aligned string that we have now
	   * extracted.  This shouldn't be the case but since we've got a
	   * length, we may as well obey it.
	   */
	  pad = size - (int) (buf->ptr - start);
	  if (pad > 0)
	    or_advance (buf, pad);
	}
      *mem = new_;
    }
}

static void
mr_freemem_varbit (void *memptr)
{
  char *cur;

  if (memptr != NULL)
    {
      cur = *(char **) memptr;
      if (cur != NULL)
	db_private_free_and_init (NULL, cur);
    }
}

static void
mr_initval_varbit (DB_VALUE * value, int precision, int scale)
{
  db_make_varbit (value, precision, NULL, 0);
  value->need_clear = false;
}

static int
mr_setval_varbit (DB_VALUE * dest, DB_VALUE * src, bool copy)
{
  int error = NO_ERROR;
  int src_precision, src_length, src_bit_length;
  char *src_str, *new_;

  if (src == NULL || (DB_IS_NULL (src) && db_value_precision (src) == 0))
    {
      error = db_value_domain_init (dest,
				    DB_TYPE_VARBIT, DB_DEFAULT_PRECISION, 0);
    }
  else if (DB_IS_NULL (src) || (src_str = db_get_string (src)) == NULL)
    {
      error = db_value_domain_init (dest,
				    DB_TYPE_VARBIT, db_value_precision (src),
				    0);
    }
  else
    {
      /* Get information from the value. */
      src_precision = db_value_precision (src);
      src_length = db_get_string_size (src);
      src_bit_length = db_get_string_length (src);

      /* should we be paying attention to this? it is extremely dangerous */
      if (!copy)
	{
	  error =
	    db_make_varbit (dest, src_precision, src_str, src_bit_length);
	}
      else
	{
	  new_ = db_private_alloc (NULL, src_length + 1);
	  if (new_ == NULL)
	    {
	      db_value_domain_init (dest, DB_TYPE_VARBIT, src_precision, 0);
	      error = er_errid ();
	    }
	  else
	    {
	      memcpy (new_, src_str, src_length);
	      db_make_varbit (dest, src_precision, new_, src_bit_length);
	      dest->need_clear = true;
	    }
	}
    }

  return error;
}

static int
mr_lengthval_varbit (DB_VALUE * value, int disk)
{
  int bit_length, len;
  const char *str;

  len = 0;
  if (value != NULL && (str = db_get_string (value)) != NULL)
    {
      bit_length = db_get_string_length (value);	/* size in bits */

      len = or_packed_varbit_length (bit_length);
    }
  return len;
}

static int
mr_writeval_varbit (OR_BUF * buf, DB_VALUE * value)
{
  int src_bit_length;
  char *str;
  int rc = NO_ERROR;

  if (value != NULL && (str = db_get_string (value)) != NULL)
    {
      src_bit_length = db_get_string_length (value);	/* size in bits */

      rc = or_put_varbit (buf, str, src_bit_length);
    }
  return rc;
}


/*
 * Size can come in as negative here to create a value with a pointer
 * directly to disk.
 *
 * Note that we have a potential conflict with this as -1 is a valid size
 * to use here when the string has been packed with a domain/length prefix
 * and we can determine the size from there.  In current practice, this
 * isn't a problem because due to word alignment, we'll never get a
 * negative size here that is greater than -4.
 */
static int
mr_readval_varbit (OR_BUF * buf, DB_VALUE * value,
		   TP_DOMAIN * domain, int size, bool copy,
		   char *copy_buf, int copy_buf_len)
{
  int pad, precision;
  int str_bit_length, str_length;
  char *new_, *start = NULL;
  int rc = NO_ERROR;

  if (value == NULL)
    {
      if (size == -1)
	rc = or_skip_varbit (buf);
      else
	{
	  if (size)
	    rc = or_advance (buf, size);
	}
    }
  else
    {
      /*
       * Allow the domain to be NULL in which case this must be a maximum
       * precision varbit.  Should be used only by the trcl.c class
       * object accessors.
       */
      if (domain != NULL)
	precision = domain->precision;
      else
	precision = DB_MAX_VARBIT_PRECISION;

      if (!copy)
	{
	  str_bit_length = or_get_varbit_length (buf, &rc);
	  if (rc == NO_ERROR)
	    {
	      db_make_varbit (value, precision, buf->ptr, str_bit_length);
	      value->need_clear = false;
	      rc = or_skip_varbit_remainder (buf, str_bit_length);
	    }
	}
      else
	{
	  if (size == 0)
	    {
	      /* its NULL */
	      db_value_domain_init (value, DB_TYPE_VARBIT, precision, 0);
	    }
	  else
	    {			/* size != 0 */
	      if (size == -1)
		{
		  /* Standard packed varbit with a size prefix */
		  ;		/* do nothing */
		}
	      else
		{		/* size != -1 */
		  /* Standard packed varbit within an area of fixed size,
		   * usually this means we're looking at the disk
		   * representation of an attribute.
		   * Just like the -1 case except we advance past the additional
		   * padding.
		   */
		  start = buf->ptr;
		}		/* size != -1 */

	      str_bit_length = or_get_varbit_length (buf, &rc);
	      if (rc != NO_ERROR)
		{
		  return ER_FAILED;
		}
	      /* get the string byte length */
	      str_length = BITS_TO_BYTES (str_bit_length);

	      if (copy_buf && copy_buf_len >= str_length + 1)
		{
		  /* read buf image into the copy_buf */
		  new_ = copy_buf;
		}
	      else
		{
		  /*
		   * Allocate storage for the string including the kludge NULL
		   * terminator
		   */
		  new_ = db_private_alloc (NULL, str_length + 1);
		}

	      if (new_ == NULL)
		{
		  /* need to be able to return errors ! */
		  db_value_domain_init (value, domain->type->id,
					TP_FLOATING_PRECISION_VALUE, 0);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 0);
		  or_abort (buf);
		  return ER_FAILED;
		}
	      else
		{
		  /* do not read the kludge NULL terminator */
		  if ((rc = or_get_data (buf, new_, str_length)) == NO_ERROR)
		    {
		      /* round up to a word boundary */
		      rc = or_get_align32 (buf);
		    }
		  if (rc != NO_ERROR)
		    {
		      if (new_ != copy_buf)
			db_private_free_and_init (NULL, new_);
		      return ER_FAILED;
		    }

		  new_[str_length] = '\0';	/* append the kludge NULL terminator */
		  db_make_varbit (value, precision, new_, str_bit_length);
		  value->need_clear = (new_ != copy_buf) ? true : false;

		  if (size == -1)
		    {
		      /* Standard packed varbit with a size prefix */
		      ;		/* do nothing */
		    }
		  else
		    {		/* size != -1 */
		      /* Standard packed varbit within an area of fixed size,
		       * usually this means we're looking at the disk
		       * representation of an attribute.
		       * Just like the -1 case except we advance past the
		       * additional padding.
		       */
		      pad = size - (int) (buf->ptr - start);
		      if (pad > 0)
			rc = or_advance (buf, pad);
		    }		/* size != -1 */
		}		/* else */
	    }			/* size != 0 */
	}

    }
  return rc;
}

static int
mr_cmpdisk_varbit (void *mem1, void *mem2,
		   TP_DOMAIN * domain, int do_reverse,
		   int do_coercion, int total_order, int *start_colp)
{
  int bit_length1, bit_length2;
  int mem_length1, mem_length2, c;
  OR_BUF buf1, buf2;

  or_init (&buf1, (char *) mem1, 0);
  bit_length1 = or_get_varbit_length (&buf1, &c);
  mem_length1 = BITS_TO_BYTES (bit_length1);

  or_init (&buf2, (char *) mem2, 0);
  bit_length2 = or_get_varbit_length (&buf2, &c);
  mem_length2 = BITS_TO_BYTES (bit_length2);

  c = varbit_compare ((unsigned char *) buf1.ptr, mem_length1,
		      (unsigned char *) buf2.ptr, mem_length2);
  c = MR_CMP_RETURN_CODE (c, do_reverse, domain);

  return c;
}

static int
mr_cmpval_varbit (DB_VALUE * value1, DB_VALUE * value2,
		  TP_DOMAIN * domain, int do_reverse,
		  int do_coercion, int total_order, int *start_colp)
{
  int c;

  c = varbit_compare ((unsigned char *) DB_GET_STRING (value1),
		      (int) DB_GET_STRING_SIZE (value1),
		      (unsigned char *) DB_GET_STRING (value2),
		      (int) DB_GET_STRING_SIZE (value2));
  c = MR_CMP_RETURN_CODE (c, do_reverse, domain);

  return c;
}

PR_TYPE tp_VarBit = {
  "bit varying", DB_TYPE_VARBIT, 1, sizeof (const char *), 0, 4,
  help_fprint_value,
  help_sprint_value,
  mr_initmem_varbit,
  mr_initval_varbit,
  mr_setmem_varbit,
  mr_getmem_varbit,
  mr_setval_varbit,
  mr_lengthmem_varbit,
  mr_lengthval_varbit,
  mr_writemem_varbit,
  mr_readmem_varbit,
  mr_writeval_varbit,
  mr_readval_varbit,
  mr_freemem_varbit,
  mr_cmpdisk_varbit,
  mr_cmpval_varbit
};

PR_TYPE *tp_Type_varbit = &tp_VarBit;
