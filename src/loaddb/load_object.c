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
 * load_object.c: simplified object descriptions.
 */

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
#include "db_value_printer.hpp"
#include "network_interface_cl.h"
#include "printer.hpp"

#include "message_catalog.h"
#include "string_opfunc.h"
#if defined(WINDOWS)
#include "porting.h"
#endif

#define MIGRATION_CHUNK 4096
static char migration_buffer[MIGRATION_CHUNK];

static int object_disk_size (DESC_OBJ * obj, int *offset_size_ptr);
static void put_varinfo (OR_BUF * buf, DESC_OBJ * obj, int offset_size);
static void put_attributes (OR_BUF * buf, DESC_OBJ * obj);
static void get_desc_current (OR_BUF * buf, SM_CLASS * class_, DESC_OBJ * obj, int bound_bit_flag, int offset_size,
			      bool is_unloaddb);
static SM_ATTRIBUTE *find_current_attribute (SM_CLASS * class_, int id);
static void get_desc_old (OR_BUF * buf, SM_CLASS * class_, int repid, DESC_OBJ * obj, int bound_bit_flag,
			  int offset_size, bool is_unloaddb);
static void init_load_err_filter (void);
static void default_clear_err_filter (void);

#if (MAJOR_VERSION >= 11) || (MAJOR_VERSION == 10 && MINOR_VERSION >= 1)
extern int data_readval_string (OR_BUF * buf, DB_VALUE * value, TP_DOMAIN * domain, int size, bool copy, char *copy_buf,
				int copy_buf_len);
#endif

/*
 * make_desc_obj - Makes an object descriptor for a particular class.
 *    return: object descriptor
 *    class(in): class structure
 *    string_buf_size(in): Specifies the size of the memory space to copy the data of the varchar column
 */
DESC_OBJ *
make_desc_obj (SM_CLASS * class_, int pre_alloc_varchar_size)
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
  obj->dbvalue_buf_ptr = NULL;

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

      // Until now, string_buf_size is set to -1 when calling from compatdb.
      if (pre_alloc_varchar_size > 0)
	{
	  obj->dbvalue_buf_ptr = (DBVALUE_BUF *) calloc (class_->att_count, sizeof (DBVALUE_BUF));
	}

      for (i = 0, att = class_->attributes; i < class_->att_count; i++, att = (SM_ATTRIBUTE *) att->header.next)
	{
	  db_make_null (&obj->values[i]);
	  obj->atts[i] = att;

	  if (obj->dbvalue_buf_ptr && att->type->get_id () == DB_TYPE_VARCHAR)
	    {
	      INT64 byte_sz = pre_alloc_varchar_size;
	      assert (pre_alloc_varchar_size <= DB_MAX_VARCHAR_PRECISION);
	      assert (att->domain != NULL);

	      if (att->domain->precision > 0 && att->domain->precision <= pre_alloc_varchar_size)
		{
		  byte_sz = att->domain->precision;
		}

	      /* Using INTL_CODESET_MULT() causes significant memory waste. 
	       * Therefore, realistic character lengths are used.
	       * (byte_sz * INTL_CODESET_MULT (att->domain->codeset));
	       */
	      if (att->domain->codeset == INTL_CODESET_UTF8)
		byte_sz *= 3;
	      else if (att->domain->codeset == INTL_CODESET_KSC5601_EUC)
		byte_sz *= 2;

	      if (byte_sz < OR_MINIMUM_STRING_LENGTH_FOR_COMPRESSION)
		{
		  /* When using dbvalue_buf_ptr, the PEEK method will be specified to read.
		   * In the PEEK method, a buffer is needed only when the string is compressed.    
		   */
		  continue;
		}

	      obj->dbvalue_buf_ptr[i].buf = (char *) malloc (byte_sz + 1);
	      obj->dbvalue_buf_ptr[i].buf_size = (obj->dbvalue_buf_ptr[i].buf == NULL) ? 0 : byte_sz;
	    }
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

  if (obj->dbvalue_buf_ptr != NULL)
    {
      for (i = 0; i < obj->count; i++)
	{
	  if (obj->dbvalue_buf_ptr[i].buf != NULL)
	    {
	      free (obj->dbvalue_buf_ptr[i].buf);
	    }
	}
      free (obj->dbvalue_buf_ptr);
    }

  if (obj->count && obj->values != NULL)
    {
      for (i = 0; i < obj->count; i++)
	{
	  pr_clear_value (&obj->values[i]);
	}
      free (obj->values);
    }
  if (obj->atts != NULL)
    {
      free (obj->atts);
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
  int a, i;
  volatile int size;
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
  int expected_disk_size;
  int offset_size;
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
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_TEMPORARY_CLASS_OID));
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
get_desc_current (OR_BUF * buf, SM_CLASS * class_, DESC_OBJ * obj, int bound_bit_flag, int offset_size,
		  bool is_unloaddb)
{
  SM_ATTRIBUTE *att;
  int *vars = NULL;
  int i, j, offset, offset2, pad;
  char *bits, *start;
  int rc = NO_ERROR;
  bool do_copy = is_unloaddb ? false : true;
  int zvar[32];

  /* need nicer way to store these */
  if (class_->variable_count)
    {
      if (class_->variable_count <= (sizeof (zvar) / sizeof (zvar[0])))
	{
	  vars = zvar;
	}
      else
	{
	  vars = (int *) malloc (sizeof (int) * class_->variable_count);
	  if (vars == NULL)
	    {
	      return;
	    }
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
	  att->type->data_readval (buf, &obj->values[i], att->domain, -1, do_copy, NULL, 0);
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
#if (MAJOR_VERSION >= 11) || (MAJOR_VERSION == 10 && MINOR_VERSION >= 1)
	  if (is_unloaddb && obj->dbvalue_buf_ptr && att->type->get_id () == DB_TYPE_VARCHAR)
	    {
	      data_readval_string (buf, &obj->values[i], att->domain, vars[j], false, obj->dbvalue_buf_ptr[i].buf,
				   obj->dbvalue_buf_ptr[i].buf_size);
	    }
	  else
#endif
	    {
	      att->type->data_readval (buf, &obj->values[i], att->domain, vars[j], do_copy, NULL, 0);
	    }
	}

      if (vars != zvar)
	{
	  free (vars);
	}
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
get_desc_old (OR_BUF * buf, SM_CLASS * class_, int repid, DESC_OBJ * obj, int bound_bit_flag, int offset_size,
	      bool is_unloaddb)
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
  int storage_order;
  bool do_copy = is_unloaddb ? false : true;
  int zvar[32];

  oldrep = classobj_find_representation (class_, repid);
  if (oldrep == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_INVALID_REPRESENTATION, 1, sm_ch_name ((MOBJ) class_));
      return;
    }

  if (oldrep->variable_count)
    {
      /* need nicer way to store these */
      if (class_->variable_count <= (sizeof (zvar) / sizeof (zvar[0])))
	{
	  vars = zvar;
	}
      else
	{
	  vars = (int *) malloc (sizeof (int) * oldrep->variable_count);
	  if (vars == NULL)
	    {
	      goto abort_on_error;
	    }
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
      type = pr_type_from_id (rat->typeid_);
      if (type == NULL)
	{
	  goto abort_on_error;
	}

      if (attmap[i] == NULL)
	{
	  /* its gone, skip over it */
	  type->data_readval (buf, NULL, rat->domain, -1, do_copy, NULL, 0);
	}
      else
	{
	  /* its real, get it into the proper value */
	  type->data_readval (buf, &obj->values[attmap[i]->storage_order], rat->domain, -1, do_copy, NULL, 0);
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
      type = pr_type_from_id (rat->typeid_);
      if (type == NULL)
	{
	  goto abort_on_error;
	}

      att_index = i + oldrep->fixed_count;
      if (attmap[att_index] == NULL)
	{
	  /* its null, skip over it */
	  type->data_readval (buf, NULL, rat->domain, vars[i], do_copy, NULL, 0);
	}
      else
	{
	  /* read it into the proper value */
	  storage_order = attmap[att_index]->storage_order;
#if (MAJOR_VERSION >= 11) || (MAJOR_VERSION == 10 && MINOR_VERSION >= 1)
	  if (is_unloaddb && obj->dbvalue_buf_ptr && type->get_id () == DB_TYPE_VARCHAR)
	    {
	      data_readval_string (buf, &obj->values[storage_order], rat->domain, vars[i], false,
				   obj->dbvalue_buf_ptr[storage_order].buf,
				   obj->dbvalue_buf_ptr[storage_order].buf_size);
	    }
	  else
#endif
	    {
	      type->data_readval (buf, &obj->values[storage_order], rat->domain, vars[i], do_copy, NULL, 0);
	    }
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

  if (vars && vars != zvar)
    {
      free (vars);
    }

  obj->updated_flag = 1;
  return;
abort_on_error:
  if (attmap != NULL)
    {
      free (attmap);
    }

  if (vars && vars != zvar)
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
desc_disk_to_obj (MOP classop, SM_CLASS * class_, RECDES * record, DESC_OBJ * obj, bool is_unloaddb)
{
  volatile int error = NO_ERROR;
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

      i = OR_INT_SIZE;		/* skip chn */
      if (mvcc_flags & OR_MVCC_FLAG_VALID_INSID)
	{
	  i += OR_MVCCID_SIZE;	/* skip insert id */
	}
      if (mvcc_flags & OR_MVCC_FLAG_VALID_DELID)
	{
	  i += OR_MVCCID_SIZE;	/* skip delete id */
	}
      if (mvcc_flags & OR_MVCC_FLAG_VALID_PREV_VERSION)
	{
	  i += OR_MVCC_PREV_VERSION_LSA_SIZE;	/* skip prev version lsa */
	}

      or_advance (buf, i);

      bound_bit_flag = repid_bits & OR_BOUND_BIT_FLAG;
      if (repid == class_->repid)
	{
	  get_desc_current (buf, class_, obj, bound_bit_flag, offset_size, is_unloaddb);
	}
      else
	{
	  get_desc_old (buf, class_, repid, obj, bound_bit_flag, offset_size, is_unloaddb);
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

static bool filter_ignore_errors[-ER_LAST_ERROR] = {
  false,
};

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

// TODO: this is specific to loader. I don't think it belongs here.
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
      if (ignore_warning && er_get_severity () == ER_WARNING_SEVERITY)
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

/* *INDENT-OFF* */
void
get_ignored_errors (std::vector<int> &vec)
{
  for (int i = 0; i < -ER_LAST_ERROR; i++)
    {
      if (filter_ignore_errors[i])
	{
	  vec.push_back (-i);
	}
    }
}
/* *INDENT-ON* */
