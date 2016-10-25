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
 * load_object.c: simplified object descriptions.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <fcntl.h>
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
#include "object_print.h"
#include "network_interface_cl.h"

#include "message_catalog.h"
#include "string_opfunc.h"
#if defined(WINDOWS)
#include "porting.h"
#endif

/* this must be the last header file included!!! */
#include "dbval.h"

#define MIGRATION_CHUNK 4096
static char migration_buffer[MIGRATION_CHUNK];

static int object_disk_size (DESC_OBJ * obj, int *offset_size_ptr);
static void put_varinfo (OR_BUF * buf, DESC_OBJ * obj, int offset_size);
static void put_attributes (OR_BUF * buf, DESC_OBJ * obj);
static void get_desc_current (OR_BUF * buf, SM_CLASS * class_, DESC_OBJ * obj, int bound_bit_flag, int offset_size);
static SM_ATTRIBUTE *find_current_attribute (SM_CLASS * class_, int id);
static void get_desc_old (OR_BUF * buf, SM_CLASS * class_, int repid, DESC_OBJ * obj, int bound_bit_flag,
			  int offset_size);
static void fprint_set (FILE * fp, DB_SET * set);
static int fprint_special_set (TEXT_OUTPUT * tout, DB_SET * set);
static int bfmt_print (int bfmt, const DB_VALUE * the_db_bit, char *string, int max_size);
static char *strnchr (char *str, char ch, int nbytes);
static int print_quoted_str (TEXT_OUTPUT * tout, char *str, int len, int max_token_len);
static void itoa_strreverse (char *begin, char *end);
static int itoa_print (TEXT_OUTPUT * tout, DB_BIGINT value, int base);
static int fprint_special_strings (TEXT_OUTPUT * tout, DB_VALUE * value);
static void init_load_err_filter (void);
static void default_clear_err_filter (void);


/*
 * make_desc_obj - Makes an object descriptor for a particular class.
 *    return: object descriptor
 *    class(in): class structure
 */
DESC_OBJ *
make_desc_obj (SM_CLASS * class_)
{
  DESC_OBJ *obj;
  SM_ATTRIBUTE *att;
  int i;

  if ((obj = (DESC_OBJ *) malloc (sizeof (DESC_OBJ))) == NULL)
    {
      return NULL;
    }
  if (class_ == NULL)
    {
      return obj;
    }

  obj->classop = NULL;
  obj->class_ = class_;
  obj->updated_flag = 0;
  obj->count = class_->att_count;
  obj->atts = NULL;
  obj->values = NULL;
  if (class_->att_count)
    {
      obj->values = (DB_VALUE *) malloc (sizeof (DB_VALUE) * class_->att_count);
      if (obj->values == NULL)
	{
	  free_and_init (obj);
	  return NULL;
	}
      obj->atts = (SM_ATTRIBUTE **) malloc (sizeof (SM_ATTRIBUTE *) * class_->att_count);
      if (obj->atts == NULL)
	{
	  free_and_init (obj->values);
	  free_and_init (obj);
	  return NULL;
	}
      for (i = 0, att = class_->attributes; i < class_->att_count; i++, att = (SM_ATTRIBUTE *) att->header.next)
	{
	  DB_MAKE_NULL (&obj->values[i]);
	  obj->atts[i] = att;
	}
    }
  return obj;
}


/*
 * desc_free - Frees the storage for an object descriptor.
 *    return: none
 *    obj(out): object descriptor
 */
void
desc_free (DESC_OBJ * obj)
{
  int i;

  if (obj == NULL)
    {
      return;
    }

  if (obj->count && obj->values != NULL)
    {
      for (i = 0; i < obj->count; i++)
	{
	  pr_clear_value (&obj->values[i]);
	}
      free_and_init (obj->values);
    }
  if (obj->atts != NULL)
    {
      free_and_init (obj->atts);
    }
  free_and_init (obj);
}


/*
 * object_disk_size - Calculates the total number of bytes required for the
 * storage of an object as defined with an object descriptor structure.
 *    return: size in bytes
 *    obj(in): object descriptor
 */
static int
object_disk_size (DESC_OBJ * obj, int *offset_size_ptr)
{
  SM_ATTRIBUTE *att;
  SM_CLASS *class_;
  int a, size, i;

  *offset_size_ptr = OR_BYTE_SIZE;

  class_ = obj->class_;

re_check:
  size = OR_MVCC_INSERT_HEADER_SIZE + class_->fixed_size + OR_BOUND_BIT_BYTES (class_->fixed_count);

  if (class_->variable_count)
    {
      size += OR_VAR_TABLE_SIZE_INTERNAL (class_->variable_count, *offset_size_ptr);
      for (a = class_->fixed_count; a < class_->att_count; a++)
	{
	  att = &class_->attributes[a];

	  for (i = 0; i < obj->count; i++)
	    {
	      if (obj->atts[i] == att)
		{
		  break;
		}
	    }
	  if (i < obj->count)
	    {
	      if (att->type->variable_p)
		{
		  size += pr_data_writeval_disk_size (&obj->values[i]);
		}
	      else
		{
		  size += tp_domain_disk_size (att->domain);
		}
	    }
	  else
	    {
	      if (att->type->variable_p)
		{
		  if (!DB_IS_NULL (&att->default_value.value))
		    {
		      size += pr_data_writeval_disk_size (&att->default_value.value);
		    }
		}
	      else
		{
		  size += tp_domain_disk_size (att->domain);
		}
	    }
	}
    }

  if (*offset_size_ptr == OR_BYTE_SIZE && size > OR_MAX_BYTE)
    {
      *offset_size_ptr = OR_SHORT_SIZE;	/* 2byte */
      goto re_check;
    }
  if (*offset_size_ptr == OR_SHORT_SIZE && size > OR_MAX_SHORT)
    {
      *offset_size_ptr = BIG_VAR_OFFSET_SIZE;	/* 4byte */
      goto re_check;
    }
  return (size);
}


/*
 * put_varinfo - Writes the variable offset table for an object defined by
 * an object descriptor structure.
 *    return: void
 *    buf(out): transformer buffer
 *    obj(in): object
 */
static void
put_varinfo (OR_BUF * buf, DESC_OBJ * obj, int offset_size)
{
  SM_ATTRIBUTE *att;
  SM_CLASS *class_;
  int a, offset, len, i;

  class_ = obj->class_;

  if (class_->variable_count)
    {
      /* compute the variable offsets relative to the end of the header (beginning of variable table) */
      offset =
	OR_VAR_TABLE_SIZE_INTERNAL (class_->variable_count,
				    offset_size) + class_->fixed_size + OR_BOUND_BIT_BYTES (class_->fixed_count);

      for (a = class_->fixed_count; a < class_->att_count; a++)
	{
	  att = &class_->attributes[a];

	  for (i = 0; i < obj->count; i++)
	    {
	      if (obj->atts[i] == att)
		{
		  break;
		}
	    }
	  len = 0;
	  if (i < obj->count)
	    {
	      if (att->type->variable_p)
		{
		  len = pr_data_writeval_disk_size (&obj->values[i]);
		}
	      else
		{
		  len = tp_domain_disk_size (att->domain);
		}
	    }
	  else
	    {
	      if (att->type->variable_p)
		{
		  if (!DB_IS_NULL (&att->default_value.value))
		    {
		      len = pr_data_writeval_disk_size (&att->default_value.value);
		    }
		}
	      else
		{
		  len = tp_domain_disk_size (att->domain);
		}
	    }
	  or_put_offset_internal (buf, offset, offset_size);
	  offset += len;
	}
      or_put_offset_internal (buf, offset, offset_size);
      buf->ptr = PTR_ALIGN (buf->ptr, INT_ALIGNMENT);
    }
}


/*
 * put_attributes - Writes the attribute values for an object defined by
 * an object descriptor structure.
 *    return: void
 *    buf(out): transformer buffer
 *    obj(in): object descriptor
 */
static void
put_attributes (OR_BUF * buf, DESC_OBJ * obj)
{
  SM_ATTRIBUTE *att;
  int i, bsize, pad;
  char *bits, *start;

  /* allocate bound bit array */
  bits = NULL;
  bsize = OR_BOUND_BIT_BYTES (obj->class_->fixed_count);
  if (bsize)
    {
      bits = (char *) malloc (bsize);
      if (bits == NULL)
	{
	  goto error;
	}
      else
	{
	  memset (bits, 0, bsize);
	}
    }

  /* 
   * Write fixed attribute values, if unbound, leave zero or garbage
   * it doesn't matter, if the attribute is bound, set the appropriate
   * bit in the bound bit array
   */
  start = buf->ptr;
  for (att = obj->class_->attributes; att != NULL && !att->type->variable_p; att = (SM_ATTRIBUTE *) att->header.next)
    {
      for (i = 0; i < obj->count && obj->atts[i] != att; i++);

      if (i < obj->count)
	{
	  if (DB_IS_NULL (&obj->values[i]))
	    {
	      or_pad (buf, tp_domain_disk_size (att->domain));
	    }
	  else
	    {
	      pr_data_writeval (buf, &obj->values[i]);
	      if (bits != NULL)
		{
		  OR_ENABLE_BOUND_BIT (bits, att->storage_order);
		}
	    }
	}
      else
	{
	  /* no value, use default if one exists */
	  if (DB_IS_NULL (&att->default_value.value))
	    {
	      or_pad (buf, tp_domain_disk_size (att->domain));
	    }
	  else
	    {
	      pr_data_writeval (buf, &att->default_value.value);
	      if (bits != NULL)
		{
		  OR_ENABLE_BOUND_BIT (bits, att->storage_order);
		}
	    }
	}
    }

  /* bring the end of the fixed width block up to proper alignment */
  pad = (int) (buf->ptr - start);
  if (pad < obj->class_->fixed_size)
    {
      or_pad (buf, obj->class_->fixed_size - pad);
    }
  else if (pad > obj->class_->fixed_size)
    {
      /* mismatched fixed block calculations */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CORRUPTED, 0);
      goto error;
    }

  /* now write the bound bits if we have any */
  if (bits != NULL)
    {
      or_put_data (buf, bits, bsize);
      /* 
       * We do not need the bits array anymore, lets free it now.
       * the pr_data_writeval() function can perform a longjmp()
       * back to the calling function if we get an overflow,
       * and bits will not be freed.
       */
      free_and_init (bits);
    }

  /* finaly do the variable width attributes */
  for (; att != NULL; att = (SM_ATTRIBUTE *) att->header.next)
    {
      for (i = 0; i < obj->count && obj->atts[i] != att; i++);

      if (i < obj->count)
	{
	  if (!DB_IS_NULL (&obj->values[i]))
	    {
	      pr_data_writeval (buf, &obj->values[i]);
	    }
	}
      else
	{
	  if (!DB_IS_NULL (&att->default_value.value))
	    {
	      pr_data_writeval (buf, &att->default_value.value);
	    }
	}
    }

  return;

error:
  if (bits != NULL)
    {
      free_and_init (bits);
    }
  or_abort (buf);
}


/*
 * text_print_flush - flush TEXT_OUTPUT contents to file
 *    return: NO_ERROR if successful, ER_IO_WRITE if file I/O error occurred
 *    tout(in/out): TEXT_OUTPUT structure
 */
int
text_print_flush (TEXT_OUTPUT * tout)
{
  /* flush to disk */
  if (tout->count != (int) fwrite (tout->buffer, 1, tout->count, tout->fp))
    {
      return ER_IO_WRITE;
    }

  /* re-init */
  tout->ptr = tout->buffer;
  tout->count = 0;

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


/*
 * desc_obj_to_disk - transforms the object into a disk record for eventual
 * storage.
 *    return: size in bytes (negative if buffer overflow)
 *    obj(in): object descriptor
 *    record(out): disk record
 *    index_flag(in): set to non-zero if object has indexed attributes
 * Note:
 *    This is functionally similar to tf_mem_to_disk except that
 *    the object is defined using an object descriptor rather than
 *    a workspace object.
 *    If it returns a number less than zero, there was not enough room
 *    in the buffer and the transformation was aborted.
 */
int
desc_obj_to_disk (DESC_OBJ * obj, RECDES * record, bool * index_flag)
{
  OR_BUF orep, *buf;
  int error, status;
  bool has_index = false;
  unsigned int repid_bits;
  volatile int expected_disk_size;
  volatile int offset_size;

  buf = &orep;
  or_init (buf, record->data, record->area_size);
  buf->error_abort = 1;

  expected_disk_size = object_disk_size (obj, &offset_size);
  if (record->area_size < (expected_disk_size + (OR_MVCC_MAX_HEADER_SIZE - OR_MVCC_INSERT_HEADER_SIZE)))
    {
      record->length = -expected_disk_size;

      *index_flag = false;
      return (1);
    }

  status = setjmp (buf->env);
  if (status == 0)
    {
      error = 0;

      if (OID_ISTEMP (WS_OID (obj->classop)))
	{
	  printf (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_TEMPORARY_CLASS_OID));
	  return (1);
	}

      /* header */

      repid_bits = obj->class_->repid;
      if (obj->class_->fixed_count)
	{
	  repid_bits |= OR_BOUND_BIT_FLAG;
	}

      /* offset size */
      OR_SET_VAR_OFFSET_SIZE (repid_bits, offset_size);

      repid_bits |= (OR_MVCC_FLAG_VALID_INSID << OR_MVCC_FLAG_SHIFT_BITS);
      or_put_int (buf, repid_bits);
      or_put_int (buf, 0);	/* CHN, fixed size */
      or_put_bigint (buf, MVCCID_NULL);	/* MVCC insert id */

      /* variable info block */
      put_varinfo (buf, obj, offset_size);

      /* attributes, fixed followed by bound bits, followed by variable */
      put_attributes (buf, obj);

      record->length = (int) (buf->ptr - buf->buffer);

      /* see if there are any indexes */
      has_index = classobj_class_has_indexes (obj->class_);
    }
  else
    {
      assert (false);		/* impossible case */

      /* 
       * error, currently can only be from buffer overflow
       * might be nice to store the "size guess" from the class
       * SHOULD BE USING TF_STATUS LIKE tf_mem_to_disk, need to
       * merge these two programs !
       */
      record->length = -expected_disk_size;
      error = 1;
      has_index = false;
    }

  *index_flag = has_index;
  return (error);
}


/*
 * get_desc_current - reads the disk representation of an object and constructs
 * an object descriptor.
 *    return: void
 *    buf(in/out): transformer buffer
 *    class(in): class structure
 *    obj(out): object descriptor
 *    bound_bit_flag(in): non-zero if we're using bound bits
 * Note:
 *    The most current representation of the class is expected.
 */
static void
get_desc_current (OR_BUF * buf, SM_CLASS * class_, DESC_OBJ * obj, int bound_bit_flag, int offset_size)
{
  SM_ATTRIBUTE *att;
  int *vars = NULL;
  int i, j, offset, offset2, pad;
  char *bits, *start;
  int rc = NO_ERROR;

  /* need nicer way to store these */
  if (class_->variable_count)
    {
      vars = (int *) malloc (sizeof (int) * class_->variable_count);
      if (vars == NULL)
	{
	  return;
	}
      /* get the offsets relative to the end of the header (beginning of variable table) */
      offset = or_get_offset_internal (buf, &rc, offset_size);
      for (i = 0; i < class_->variable_count; i++)
	{
	  offset2 = or_get_offset_internal (buf, &rc, offset_size);
	  vars[i] = offset2 - offset;
	  offset = offset2;
	}
      buf->ptr = PTR_ALIGN (buf->ptr, INT_ALIGNMENT);
    }

  bits = NULL;
  if (bound_bit_flag)
    {
      /* assume that the buffer is in contiguous memory and that we can seek ahead to the bound bits.  */
      bits = (char *) buf->ptr + obj->class_->fixed_size;
    }

  att = class_->attributes;
  start = buf->ptr;
  for (i = 0; i < class_->fixed_count; i++, att = (SM_ATTRIBUTE *) att->header.next)
    {

      if (bits != NULL && !OR_GET_BOUND_BIT (bits, i))
	{
	  /* its a NULL value, skip it */
	  db_value_put_null (&obj->values[i]);
	  or_advance (buf, tp_domain_disk_size (att->domain));
	}
      else
	{
	  /* read the disk value into the db_value */
	  (*(att->type->data_readval)) (buf, &obj->values[i], att->domain, -1, true, NULL, 0);
	}
    }

  /* round up to a to the end of the fixed block */
  pad = (int) (buf->ptr - start);
  if (pad < obj->class_->fixed_size)
    {
      or_advance (buf, obj->class_->fixed_size - pad);
    }

  /* skip over the bound bits */
  if (bound_bit_flag)
    {
      or_advance (buf, OR_BOUND_BIT_BYTES (obj->class_->fixed_count));
    }

  /* variable */
  if (vars != NULL)
    {
      for (i = class_->fixed_count, j = 0; i < class_->att_count && j < class_->variable_count;
	   i++, j++, att = (SM_ATTRIBUTE *) att->header.next)
	{
	  (*(att->type->data_readval)) (buf, &obj->values[i], att->domain, vars[j], true, NULL, 0);
	}

      free_and_init (vars);
    }
}


/*
 * find_current_attribute - locates an attribute definition in a class.
 *    return: attribute structure
 *    class(in): class structure
 *    id(in): attribute id
 */
static SM_ATTRIBUTE *
find_current_attribute (SM_CLASS * class_, int id)
{
  SM_ATTRIBUTE *att;

  for (att = class_->attributes; att != NULL; att = (SM_ATTRIBUTE *) att->header.next)
    {
      if (att->id == id)
	{
	  return att;
	}
    }
  return NULL;
}


/*
 * get_desc_old - loads the disk representation of an object into an object
 * descriptor structure.
 *    return: void
 *    buf(in/out): transformer buffer
 *    class(in): class strcuture
 *    repid(in): repid of the stored instance
 *    obj(out): object descriptor being built
 *    bound_bit_flag(in): non-zero if using bound bits
 * Note:
 *    This loads the disk representation of an object into an object
 *    descriptor structure.  This function will handle the transformation
 *    of an obsolete representation of the object into its
 *    newest representation.
 */
static void
get_desc_old (OR_BUF * buf, SM_CLASS * class_, int repid, DESC_OBJ * obj, int bound_bit_flag, int offset_size)
{
  SM_REPRESENTATION *oldrep;
  SM_REPR_ATTRIBUTE *rat, *found;
  SM_ATTRIBUTE *att;
  PR_TYPE *type;
  int *vars = NULL;
  int i, offset, offset2, total, bytes, att_index, padded_size, fixed_size;
  SM_ATTRIBUTE **attmap = NULL;
  char *bits, *start;
  int rc = NO_ERROR;

  oldrep = classobj_find_representation (class_, repid);

  if (oldrep == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_INVALID_REPRESENTATION, 1, sm_ch_name ((MOBJ) class_));
    }
  else
    {
      if (oldrep->variable_count)
	{
	  /* need nicer way to store these */
	  vars = (int *) malloc (sizeof (int) * oldrep->variable_count);
	  if (vars == NULL)
	    {
	      goto abort_on_error;
	    }
	  /* compute the variable offsets relative to the end of the header (beginning of variable table) */
	  offset = or_get_offset_internal (buf, &rc, offset_size);
	  for (i = 0; i < oldrep->variable_count; i++)
	    {
	      offset2 = or_get_offset_internal (buf, &rc, offset_size);
	      vars[i] = offset2 - offset;
	      offset = offset2;
	    }
	  buf->ptr = PTR_ALIGN (buf->ptr, INT_ALIGNMENT);
	}

      /* calculate an attribute map */
      total = oldrep->fixed_count + oldrep->variable_count;
      attmap = (SM_ATTRIBUTE **) malloc (sizeof (SM_ATTRIBUTE *) * total);
      if (attmap == NULL)
	{
	  goto abort_on_error;
	}

      memset (attmap, 0, sizeof (SM_ATTRIBUTE *) * total);

      for (rat = oldrep->attributes, i = 0; rat != NULL; rat = rat->next, i++)
	attmap[i] = find_current_attribute (class_, rat->attid);

      rat = oldrep->attributes;

      /* fixed */
      start = buf->ptr;
      for (i = 0; i < oldrep->fixed_count && rat != NULL; i++, rat = rat->next)
	{
	  type = PR_TYPE_FROM_ID (rat->typeid_);
	  if (type == NULL)
	    {
	      goto abort_on_error;
	    }

	  if (attmap[i] == NULL)
	    {
	      /* its gone, skip over it */
	      (*(type->data_readval)) (buf, NULL, rat->domain, -1, true, NULL, 0);
	    }
	  else
	    {
	      /* its real, get it into the proper value */
	      (*(type->data_readval)) (buf, &obj->values[attmap[i]->storage_order], rat->domain, -1, true, NULL, 0);
	    }
	}

      fixed_size = (int) (buf->ptr - start);
      padded_size = DB_ATT_ALIGN (fixed_size);
      or_advance (buf, (padded_size - fixed_size));


      /* 
       * sigh, we now have to process the bound bits in much the same way as the
       * attributes above, it would be nice if these could be done in parallel
       * but we don't have the fixed size of the old representation so we
       * can't easily sneak the buffer pointer forward, work on this someday
       */
      if (bound_bit_flag && oldrep->fixed_count)
	{
	  bits = buf->ptr;
	  bytes = OR_BOUND_BIT_BYTES (oldrep->fixed_count);
	  if ((buf->ptr + bytes) > buf->endptr)
	    {
	      or_overflow (buf);
	    }

	  rat = oldrep->attributes;
	  for (i = 0; i < oldrep->fixed_count && rat != NULL; i++, rat = rat->next)
	    {
	      if (attmap[i] != NULL)
		{
		  if (!OR_GET_BOUND_BIT (bits, i))
		    {
		      DB_VALUE *v = &obj->values[attmap[i]->storage_order];
		      db_value_clear (v);
		      db_value_put_null (v);
		    }
		}
	    }
	  or_advance (buf, bytes);
	}

      /* variable */
      for (i = 0; i < oldrep->variable_count && rat != NULL; i++, rat = rat->next)
	{
	  type = PR_TYPE_FROM_ID (rat->typeid_);
	  if (type == NULL)
	    {
	      goto abort_on_error;
	    }

	  att_index = i + oldrep->fixed_count;
	  if (attmap[att_index] == NULL)
	    {
	      /* its null, skip over it */
	      (*(type->data_readval)) (buf, NULL, rat->domain, vars[i], true, NULL, 0);
	    }
	  else
	    {
	      /* read it into the proper value */
	      (*(type->data_readval)) (buf, &obj->values[attmap[att_index]->storage_order], rat->domain, vars[i], true,
				       NULL, 0);
	    }
	}

      /* 
       * initialize new values
       */
      for (i = 0, att = class_->attributes; att != NULL; i++, att = (SM_ATTRIBUTE *) att->header.next)
	{
	  found = NULL;
	  for (rat = oldrep->attributes; rat != NULL && found == NULL; rat = rat->next)
	    {
	      if (rat->attid == att->id)
		{
		  found = rat;
		}
	    }
	  if (found == NULL)
	    {
	      /* 
	       * formerly used copy_value which converted MOP values to OID
	       * values, is this really necessary ?
	       */
	      pr_clone_value (&att->default_value.original_value, &obj->values[i]);
	    }
	}

      if (attmap != NULL)
	{
	  free_and_init (attmap);
	}
      if (vars != NULL)
	{
	  free_and_init (vars);
	}

      obj->updated_flag = 1;
    }
  return;

abort_on_error:
  if (attmap != NULL)
    {
      free (attmap);
    }
  if (vars != NULL)
    {
      free (vars);
    }
  or_abort (buf);
}


/*
 * desc_disk_to_obj - similar to tf_disk_to_mem except that it builds an
 * object descriptor structure rather than a workspace object.
 *    return: NO_ERROR if successful, error code otherwise
 *    classop(in): class MOP
 *    class(in): class structure
 *    record(in): disk record
 *    obj(out): object descriptor
 */
int
desc_disk_to_obj (MOP classop, SM_CLASS * class_, RECDES * record, DESC_OBJ * obj)
{
  int error = NO_ERROR;
  OR_BUF orep, *buf;
  int repid, status;
  unsigned int repid_bits;
  int bound_bit_flag;
  int save;
  int i;
  int rc = NO_ERROR;
  int offset_size;

  if (obj == NULL)
    {
      return ER_FAILED;
    }
  else
    {
      /* clear previous values */
      for (i = 0; i < obj->count; i++)
	{
	  pr_clear_value (&obj->values[i]);
	}
    }

  /* Kludge, make sure we don't upgrade objects to OID'd during the reading */
  save = pr_Inhibit_oid_promotion;
  pr_Inhibit_oid_promotion = 1;

  buf = &orep;
  or_init (buf, record->data, record->length);
  buf->error_abort = 1;

  obj->classop = classop;

  status = setjmp (buf->env);
  if (status == 0)
    {
      char mvcc_flags;

      /* offset size */
      offset_size = OR_GET_OFFSET_SIZE (buf->ptr);

      /* in case of MVCC, repid_bits contains MVCC flags */
      repid_bits = or_mvcc_get_repid_and_flags (buf, &rc);
      repid = repid_bits & OR_MVCC_REPID_MASK;

      mvcc_flags = (char) ((repid_bits >> OR_MVCC_FLAG_SHIFT_BITS) & OR_MVCC_FLAG_MASK);

      /* skip chn */
      or_advance (buf, OR_INT_SIZE);

      if (mvcc_flags & OR_MVCC_FLAG_VALID_INSID)
	{
	  /* skip insert id */
	  or_advance (buf, OR_MVCCID_SIZE);
	}

      if (mvcc_flags & OR_MVCC_FLAG_VALID_DELID)
	{
	  /* skip delete id */
	  or_advance (buf, OR_MVCCID_SIZE);
	}

      if (mvcc_flags & OR_MVCC_FLAG_VALID_PREV_VERSION)
	{
	  /* skip prev version lsa */
	  or_advance (buf, OR_MVCC_PREV_VERSION_LSA_SIZE);
	}

      bound_bit_flag = repid_bits & OR_BOUND_BIT_FLAG;

      if (repid == class_->repid)
	{
	  get_desc_current (buf, class_, obj, bound_bit_flag, offset_size);
	}
      else
	{
	  get_desc_old (buf, class_, repid, obj, bound_bit_flag, offset_size);
	}
    }
  else
    {
      error = ER_TF_BUFFER_UNDERFLOW;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
    }

  pr_Inhibit_oid_promotion = save;

  return error;
}


/*
 * fprint_set - Print the contents of a real DB_SET (not a set descriptor).
 *    return: void
 *    fp(in): file pointer
 *    set(in): set reference
 */
static void
fprint_set (FILE * fp, DB_SET * set)
{
  DB_VALUE element_value;
  int len, i;

  len = set_size (set);
  fprintf (fp, "{");
  for (i = 0; i < len; i++)
    {
      if (set_get_element (set, i, &element_value) == NO_ERROR)
	{
	  desc_value_fprint (fp, &element_value);
	  if (i < len - 1)
	    {
	      fprintf (fp, ", ");
	    }
	}
    }
  fprintf (fp, "}");
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
  char *bstring;
  int error = NO_ERROR;
  static char digits[16] = { '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
  };

  /* Get the buffer and the length from the_db_bit */
  bstring = DB_GET_BIT (the_db_bit, &length);

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
static char *
strnchr (char *str, char ch, int nbytes)
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
print_quoted_str (TEXT_OUTPUT * tout, char *str, int len, int max_token_len)
{
  int error = NO_ERROR;
  char *p, *end;
  int partial_len, write_len, left_nbytes;
  char *internal_quote_p;

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

exit_on_end:
  return error;

exit_on_error:
  CHECK_EXIT_ERROR (error);
  goto exit_on_end;
}

#define INTERNAL_BUFFER_SIZE (400)	/* bigger than DBL_MAX_DIGITS */

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
  DB_TYPE type;
  int len;
  DB_TIMETZ *time_tz;
  DB_DATETIMETZ *dt_tz;
  DB_TIMESTAMPTZ *ts_tz;

  type = DB_VALUE_TYPE (value);
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
	}
      CHECK_PRINT_ERROR (itoa_print (tout, DB_GET_BIGINT (value), 10 /* base */ ));
      break;
    case DB_TYPE_INTEGER:
      if (tout->iosize - tout->count < INTERNAL_BUFFER_SIZE)
	{
	  /* flush remaining buffer */
	  CHECK_PRINT_ERROR (text_print_flush (tout));
	}
      CHECK_PRINT_ERROR (itoa_print (tout, DB_GET_INTEGER (value), 10 /* base */ ));
      break;
    case DB_TYPE_SMALLINT:
      if (tout->iosize - tout->count < INTERNAL_BUFFER_SIZE)
	{
	  /* flush remaining buffer */
	  CHECK_PRINT_ERROR (text_print_flush (tout));
	}
      CHECK_PRINT_ERROR (itoa_print (tout, DB_GET_SMALLINT (value), 10 /* base */ ));
      break;

    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
      {
	char *pos;

	pos = tout->ptr;
	CHECK_PRINT_ERROR (text_print
			   (tout, NULL, 0, "%.*g", (type == DB_TYPE_FLOAT) ? 10 : 17,
			    (type == DB_TYPE_FLOAT) ? DB_GET_FLOAT (value) : DB_GET_DOUBLE (value)));

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
	}
      CHECK_PRINT_ERROR (itoa_print (tout, DB_GET_ENUM_SHORT (value), 10 /* base */ ));
      break;

    case DB_TYPE_DATE:
      db_date_to_string (buf, MAX_DISPLAY_COLUMN, DB_GET_DATE (value));
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "date '%s'", buf));
      break;

    case DB_TYPE_TIME:
      db_time_to_string (buf, MAX_DISPLAY_COLUMN, DB_GET_TIME (value));
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "time '%s'", buf));
      break;

    case DB_TYPE_TIMELTZ:
      db_timeltz_to_string (buf, MAX_DISPLAY_COLUMN, DB_GET_TIME (value));
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "timeltz '%s'", buf));
      break;

    case DB_TYPE_TIMETZ:
      time_tz = DB_GET_TIMETZ (value);
      db_timetz_to_string (buf, MAX_DISPLAY_COLUMN, &time_tz->time, &time_tz->tz_id);
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "timetz '%s'", buf));
      break;

    case DB_TYPE_TIMESTAMP:
      db_timestamp_to_string (buf, MAX_DISPLAY_COLUMN, DB_GET_UTIME (value));
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "timestamp '%s'", buf));
      break;

    case DB_TYPE_TIMESTAMPLTZ:
      db_timestampltz_to_string (buf, MAX_DISPLAY_COLUMN, DB_GET_UTIME (value));
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "timestampltz '%s'", buf));
      break;

    case DB_TYPE_TIMESTAMPTZ:
      ts_tz = DB_GET_TIMESTAMPTZ (value);
      db_timestamptz_to_string (buf, MAX_DISPLAY_COLUMN, &ts_tz->timestamp, &ts_tz->tz_id);
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "timestamptz '%s'", buf));
      break;

    case DB_TYPE_DATETIME:
      db_datetime_to_string (buf, MAX_DISPLAY_COLUMN, DB_GET_DATETIME (value));
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "datetime '%s'", buf));
      break;

    case DB_TYPE_DATETIMELTZ:
      db_datetimeltz_to_string (buf, MAX_DISPLAY_COLUMN, DB_GET_DATETIME (value));
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "datetimeltz '%s'", buf));
      break;

    case DB_TYPE_DATETIMETZ:
      dt_tz = DB_GET_DATETIMETZ (value);
      db_datetimetz_to_string (buf, MAX_DISPLAY_COLUMN, &dt_tz->datetime, &dt_tz->tz_id);
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "datetimetz '%s'", buf));
      break;

    case DB_TYPE_MONETARY:
      /* Always print symbol before value, even if for turkish lira the user format is after value :
       * intl_get_currency_symbol_position */
      CHECK_PRINT_ERROR (text_print
			 (tout, NULL, 0, "%s%.*f", intl_get_money_esc_ISO_symbol (DB_GET_MONETARY (value)->type), 2,
			  DB_GET_MONETARY (value)->amount));
      break;

    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      CHECK_PRINT_ERROR (text_print (tout, "N", 1, NULL));
      /* fall through */
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
      ptr = DB_PULL_STRING (value);

      len = db_get_string_size (value);
      if (len < 0)
	{
	  len = strlen (ptr);
	}

      CHECK_PRINT_ERROR (print_quoted_str (tout, ptr, len, MAX_DISPLAY_COLUMN));
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
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "%d", DB_GET_ERROR (value)));
      break;

    case DB_TYPE_POINTER:
      CHECK_PRINT_ERROR (text_print (tout, NULL, 0, "%lx", (unsigned long) DB_GET_POINTER (value)));
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
      CHECK_PRINT_ERROR (fprint_special_set (tout, DB_GET_SET (value)));
      break;

    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
      printf (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_CANT_PRINT_ELO));
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
 * desc_value_fprint - Print a description of the given value.
 *    return: void
 *    fp(in): file pointer
 *    value(in): value container
 * Note:
 *    This is based on db_value_print() but has extensions for the
 *    handling of set descriptors, and ELO's used by the desc_ module.
 *    String printing is also hacked for "unprintable" characters.
 */
void
desc_value_fprint (FILE * fp, DB_VALUE * value)
{
  switch (DB_VALUE_TYPE (value))
    {
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      fprint_set (fp, DB_GET_SET (value));
      break;

    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
      printf (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_CANT_PRINT_ELO));
      break;

    default:
      db_value_fprint (fp, value);
      break;
    }
}

#if defined (CUBRID_DEBUG)
/*
 * desc_value_print - Prints the description of a value to standard output.
 *    return: void
 *    value(in): value container
 */
void
desc_value_print (DB_VALUE * value)
{
  desc_value_fprint (stdout, value);
}
#endif

static bool filter_ignore_errors[-ER_LAST_ERROR] = { false, };

static bool filter_ignore_init = false;
/*
 * init_load_err_filter - init error filter array
 *    return: void
 */
static void
init_load_err_filter (void)
{
  int ifrom, ito;
  memset (filter_ignore_errors, false, sizeof (filter_ignore_errors));
  filter_ignore_errors[0] = true;	/* ER_WARNING_SEVERITY */
  for (ifrom = -ER_DISK_ALMOST_OUT_OF_SPACE, ito = -ER_DISK_TEMP_LAST_ALMOST_OUT_OF_SPACE; ifrom <= ito; ifrom++)
    {
      filter_ignore_errors[ifrom] = true;
    }
  filter_ignore_errors[-ER_BO_NOTIFY_AUTO_VOLEXT] = true;
  filter_ignore_errors[-ER_LOG_MAX_ARCHIVES_HAS_BEEN_EXCEEDED] = true;
  filter_ignore_init = true;
}

/*
 * default_clear_err_filter - clear error filter array
 *    return: void
 */
static void
default_clear_err_filter (void)
{
  int ifrom, ito;
  filter_ignore_errors[0] = false;	/* ER_WARNING_SEVERITY */
  for (ifrom = -ER_DISK_ALMOST_OUT_OF_SPACE, ito = -ER_DISK_TEMP_LAST_ALMOST_OUT_OF_SPACE; ifrom <= ito; ifrom++)
    {
      filter_ignore_errors[ifrom] = false;
    }
  filter_ignore_errors[-ER_BO_NOTIFY_AUTO_VOLEXT] = false;
}

/*
 * init_load_err_filter - loads error codes from an external file.
 *    return: number of error filter set
 */
int
er_filter_fileset (FILE * ef)
{
  int i, num_ignore_error_list;
  int set_count = 0, errcode;
  char rdbuf[32];
  int ignore_error_list[-ER_LAST_ERROR];

  if (ef == NULL)
    {
      return set_count;
    }
  if (!filter_ignore_init)
    {
      init_load_err_filter ();
    }
  memset (rdbuf, 0, sizeof (rdbuf));
  while (feof (ef) == 0)
    {
      if (fgets (rdbuf, 30, ef) == NULL)
	{
	  break;
	}
      if (rdbuf[0] == '#')
	{
	  continue;
	}
      if (rdbuf[0] != '-' && rdbuf[0] != '+')
	{
	  continue;
	}
      if (strncmp (rdbuf, "+DEFAULT", 8) == 0)
	{
	  default_clear_err_filter ();
	}
      else
	{
	  errcode = atoi (&rdbuf[1]);
	  if (errcode <= 0 || errcode >= -ER_LAST_ERROR)
	    {
	      continue;
	    }
	  if (rdbuf[0] == '-')
	    {
	      filter_ignore_errors[errcode] = true;
	    }
	  else
	    {
	      filter_ignore_errors[errcode] = false;
	    }
	  set_count++;
	}
    }

  /* set ws_ignore_error_list */
  for (i = 1, num_ignore_error_list = 0; i < -ER_LAST_ERROR; i++)
    {
      if (filter_ignore_errors[i] == true)
	{
	  ignore_error_list[num_ignore_error_list] = -i;
	  num_ignore_error_list++;
	}
    }

  if (num_ignore_error_list > 0)
    {
      ws_set_ignore_error_list_for_mflush (num_ignore_error_list, ignore_error_list);
    }

  return set_count;
}


/*
 * er_filter_errid - check for ignorable errid
 *    return: NO_ERROR if ignorable errid. otherwise, error-code clear
 *    ignorable errid
 *    ignore_warning(in): true to filter out warnings. otherwise, false
 */
int
er_filter_errid (bool ignore_warning)
{
  int errcode = er_errid (), erridx;

  if (errcode == NO_ERROR)
    {				/* don't have to check */
      return NO_ERROR;
    }

  if (!filter_ignore_init)
    {				/* need to init */
      init_load_err_filter ();
    }

  erridx = (errcode < 0) ? -errcode : errcode;
  if (filter_ignore_errors[erridx])
    {				/* ignore error if ignorable */
      goto clear_errid;
    }

  if (filter_ignore_errors[0])
    {
      if (ignore_warning && er_severity () == ER_WARNING_SEVERITY)
	{
	  goto clear_errid;
	}
    }

exit_on_end:
  return errcode;

clear_errid:
  if (errcode != NO_ERROR)
    {				/* clear ignorable errid */
      er_clearid ();
      errcode = NO_ERROR;
    }
  goto exit_on_end;
}
