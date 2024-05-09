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

//
//  XASL stream - common interface for xasl_to_stream and stream_to_xasl
//

#include "xasl_stream.hpp"

#include "memory_alloc.h"
#include "object_representation.h"
#include "xasl.h"
#include "xasl_unpack_info.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if !defined(SERVER_MODE)
static int stx_Xasl_errcode = NO_ERROR;
#endif /* !SERVER_MODE */

/*
 * stx_get_xasl_errcode () -
 *   return:
 */
int
stx_get_xasl_errcode (THREAD_ENTRY *thread_p)
{
#if defined(SERVER_MODE)
  return thread_p->xasl_errcode;
#else /* SERVER_MODE */
  return stx_Xasl_errcode;
#endif /* SERVER_MODE */
}

/*
 * stx_set_xasl_errcode () -
 *   return:
 *   errcode(in)        :
 */
void
stx_set_xasl_errcode (THREAD_ENTRY *thread_p, int errcode)
{
#if defined(SERVER_MODE)
  thread_p->xasl_errcode = errcode;
#else /* SERVER_MODE */
  stx_Xasl_errcode = errcode;
#endif /* SERVER_MODE */
}

/*
 * stx_init_xasl_unpack_info () -
 *   return:
 *   xasl_stream(in)    : pointer to xasl stream
 *   xasl_stream_size(in)       :
 *
 * Note: initialize the xasl pack information.
 */
int
stx_init_xasl_unpack_info (THREAD_ENTRY *thread_p, char *xasl_stream, int xasl_stream_size)
{
  size_t n;
  XASL_UNPACK_INFO *unpack_info;
  int head_offset, body_offset;

#define UNPACK_SCALE 3		/* TODO: assume */

  head_offset = sizeof (XASL_UNPACK_INFO);
  head_offset = xasl_stream_make_align (head_offset);
  body_offset = xasl_stream_size * UNPACK_SCALE;
  body_offset = xasl_stream_make_align (body_offset);
  unpack_info = (XASL_UNPACK_INFO *) db_private_alloc (thread_p, head_offset + body_offset);
  set_xasl_unpack_info_ptr (thread_p, unpack_info);
  if (unpack_info == NULL)
    {
      return ER_FAILED;
    }
  unpack_info->packed_xasl = xasl_stream;
  unpack_info->packed_size = xasl_stream_size;
  for (n = 0; n < MAX_PTR_BLOCKS; ++n)
    {
      unpack_info->ptr_blocks[n] = (STX_VISITED_PTR *) 0;
      unpack_info->ptr_lwm[n] = 0;
      unpack_info->ptr_max[n] = 0;
    }
  unpack_info->alloc_size = xasl_stream_size * UNPACK_SCALE;
  unpack_info->alloc_buf = (char *) unpack_info + head_offset;
  unpack_info->additional_buffers = NULL;
  unpack_info->track_allocated_bufers = 0;
#if defined (SERVER_MODE)
  unpack_info->thrd = thread_p;
#endif /* SERVER_MODE */

  return NO_ERROR;
}

/*
 * stx_mark_struct_visited () -
 *   return: if successful, return NO_ERROR, otherwise
 *           ER_FAILED and error code is set to xasl_errcode
 *   ptr(in)    : pointer constant to be marked visited
 *   str(in)    : where the struct pointed by 'ptr' is stored
 *
 * Note: mark the given pointer constant as visited to avoid
 * duplicated storage of a struct which is pointed by more than one node
 */
int
stx_mark_struct_visited (THREAD_ENTRY *thread_p, const void *ptr, void *str)
{
  int new_lwm;
  int block_no;
  XASL_UNPACK_INFO *xasl_unpack_info = get_xasl_unpack_info_ptr (thread_p);

  block_no = xasl_stream_get_ptr_block (ptr);
  new_lwm = xasl_unpack_info->ptr_lwm[block_no];

  if (xasl_unpack_info->ptr_max[block_no] == 0)
    {
      xasl_unpack_info->ptr_max[block_no] = START_PTR_PER_BLOCK;
      xasl_unpack_info->ptr_blocks[block_no] =
	      (STX_VISITED_PTR *) db_private_alloc (thread_p, sizeof (STX_VISITED_PTR) * xasl_unpack_info->ptr_max[block_no]);
    }
  else if (xasl_unpack_info->ptr_max[block_no] <= new_lwm)
    {
      xasl_unpack_info->ptr_max[block_no] *= 2;
      xasl_unpack_info->ptr_blocks[block_no] =
	      (STX_VISITED_PTR *) db_private_realloc (thread_p, xasl_unpack_info->ptr_blocks[block_no],
		  sizeof (STX_VISITED_PTR) * xasl_unpack_info->ptr_max[block_no]);
    }

  if (xasl_unpack_info->ptr_blocks[block_no] == (STX_VISITED_PTR *) NULL)
    {
      stx_set_xasl_errcode (thread_p, ER_OUT_OF_VIRTUAL_MEMORY);
      return ER_FAILED;
    }

  xasl_unpack_info->ptr_blocks[block_no][new_lwm].ptr = ptr;
  xasl_unpack_info->ptr_blocks[block_no][new_lwm].str = str;

  xasl_unpack_info->ptr_lwm[block_no]++;

  return NO_ERROR;
}

/*
 * stx_get_struct_visited_ptr () -
 *   return: if the ptr is already visited, the offset of
 *           position where the node pointed by 'ptr' is stored,
 *           otherwise, ER_FAILED (xasl_errcode is NOT set)
 *   ptr(in)    : pointer constant to be checked if visited or not
 *
 * Note: check if the node pointed by `ptr` is already stored or
 * not to avoid multiple store of the same node
 */
void *
stx_get_struct_visited_ptr (THREAD_ENTRY *thread_p, const void *ptr)
{
  int block_no;
  int element_no;
  XASL_UNPACK_INFO *xasl_unpack_info = get_xasl_unpack_info_ptr (thread_p);

  block_no = xasl_stream_get_ptr_block (ptr);

  if (xasl_unpack_info->ptr_lwm[block_no] <= 0)
    {
      return NULL;
    }

  for (element_no = 0; element_no < xasl_unpack_info->ptr_lwm[block_no]; element_no++)
    {
      if (ptr == xasl_unpack_info->ptr_blocks[block_no][element_no].ptr)
	{
	  return (xasl_unpack_info->ptr_blocks[block_no][element_no].str);
	}
    }

  return NULL;
}

/*
 * stx_free_visited_ptrs () -
 *   return:
 *
 * Note: free memory allocated to manage visited ptr constants
 */
void
stx_free_visited_ptrs (THREAD_ENTRY *thread_p)
{
  XASL_UNPACK_INFO *xasl_unpack_info = get_xasl_unpack_info_ptr (thread_p);

  for (size_t i = 0; i < MAX_PTR_BLOCKS; i++)
    {
      xasl_unpack_info->ptr_lwm[i] = 0;
      xasl_unpack_info->ptr_max[i] = 0;
      if (xasl_unpack_info->ptr_blocks[i])
	{
	  db_private_free_and_init (thread_p, xasl_unpack_info->ptr_blocks[i]);
	  xasl_unpack_info->ptr_blocks[i] = (STX_VISITED_PTR *) 0;
	}
    }
}

/*
 * stx_alloc_struct () -
 *   return:
 *   size(in)   : # of bytes of the node
 *
 * Note: allocate storage for structures pointed to from the xasl tree.
 */
char *
stx_alloc_struct (THREAD_ENTRY *thread_p, int size)
{
  char *ptr;
  XASL_UNPACK_INFO *xasl_unpack_info = get_xasl_unpack_info_ptr (thread_p);

  if (!size)
    {
      return NULL;
    }

  size = xasl_stream_make_align (size);	/* alignment */
  if (size > xasl_unpack_info->alloc_size)
    {
      /* need to alloc */
      int p_size;

      p_size = MAX (size, xasl_unpack_info->packed_size);
      p_size = xasl_stream_make_align (p_size);	/* alignment */
      ptr = (char *) db_private_alloc (thread_p, p_size);
      if (ptr == NULL)
	{
	  return NULL;		/* error */
	}
      xasl_unpack_info->alloc_size = p_size;
      xasl_unpack_info->alloc_buf = ptr;
      if (xasl_unpack_info->track_allocated_bufers)
	{
	  UNPACK_EXTRA_BUF *add_buff = NULL;
	  add_buff = (UNPACK_EXTRA_BUF *) db_private_alloc (thread_p, sizeof (UNPACK_EXTRA_BUF));
	  if (add_buff == NULL)
	    {
	      db_private_free_and_init (thread_p, ptr);
	      return NULL;
	    }
	  add_buff->buff = ptr;
	  add_buff->next = NULL;

	  if (xasl_unpack_info->additional_buffers == NULL)
	    {
	      xasl_unpack_info->additional_buffers = add_buff;
	    }
	  else
	    {
	      add_buff->next = xasl_unpack_info->additional_buffers;
	      xasl_unpack_info->additional_buffers = add_buff;
	    }
	}
    }

  /* consume alloced buffer */
  ptr = xasl_unpack_info->alloc_buf;
  xasl_unpack_info->alloc_size -= size;
  xasl_unpack_info->alloc_buf += size;

  return ptr;
}

char *
stx_build_db_value (THREAD_ENTRY *thread_p, char *ptr, DB_VALUE *value)
{
  ptr = or_unpack_db_value (ptr, value);

  return ptr;
}

char *
stx_build_string (THREAD_ENTRY *thread_p, char *ptr, char *string)
{
  int offset;

  ptr = or_unpack_int (ptr, &offset);
  assert_release (offset > 0);

  (void) memcpy (string, ptr, offset);
  ptr += offset;

  return ptr;
}

char *
stx_restore_string (THREAD_ENTRY *thread_p, char *&ptr)
{
#if !defined (CS_MODE)
  char *string;
  int length;
  int offset = 0;

  ptr = or_unpack_int (ptr, &offset);
  if (offset == 0)
    {
      return NULL;
    }

  char *bufptr = &get_xasl_unpack_info_ptr (thread_p)->packed_xasl[offset];
  if (ptr == NULL)
    {
      return NULL;
    }

  string = (char *) stx_get_struct_visited_ptr (thread_p, bufptr);
  if (string != NULL)
    {
      return string;
    }

  length = OR_GET_INT (bufptr);

  if (length == -1)
    {
      /* unpack null-string */
      assert (string == NULL);
    }
  else
    {
      assert_release (length > 0);

      string = (char *) stx_alloc_struct (thread_p, length);
      if (string == NULL)
	{
	  stx_set_xasl_errcode (thread_p, ER_OUT_OF_VIRTUAL_MEMORY);
	  return NULL;
	}

      if (stx_mark_struct_visited (thread_p, bufptr, string) == ER_FAILED
	  || stx_build_string (thread_p, bufptr, string) == NULL)
	{
	  return NULL;
	}
    }

  return string;
#else   // CS_MODE
  int dummy;
  ptr = or_unpack_int (ptr, &dummy);
  return NULL;
#endif  // CS_MODE
}

char *
stx_build (THREAD_ENTRY *thread_p, char *ptr, cubxasl::json_table::column &jtc)
{
  int temp_int;
  XASL_UNPACK_INFO *xasl_unpack_info = get_xasl_unpack_info_ptr (thread_p);

  ptr = or_unpack_int (ptr, &temp_int);
  jtc.m_function = (json_table_column_function) temp_int;

  stx_restore (thread_p, ptr, jtc.m_output_value_pointer);

  if (jtc.m_function == JSON_TABLE_ORDINALITY)
    {
      jtc.m_domain = &tp_Integer_domain;
      return ptr;
    }

  ptr = or_unpack_domain (ptr, &jtc.m_domain, NULL);

  jtc.m_path = stx_restore_string (thread_p, ptr);
  jtc.m_column_name = stx_restore_string (thread_p, ptr);

  if (jtc.m_function == JSON_TABLE_EXISTS)
    {
      return ptr;
    }

  ptr = stx_unpack (thread_p, ptr, jtc.m_on_error);
  ptr = stx_unpack (thread_p, ptr, jtc.m_on_empty);

  return ptr;
}

char *
stx_build (THREAD_ENTRY *thread_p, char *ptr, cubxasl::json_table::node &jtn)
{
  int temp_int = 0;

  jtn.m_iterator = nullptr;

  jtn.m_path = stx_restore_string (thread_p, ptr);

  ptr = or_unpack_int (ptr, &temp_int);
  jtn.m_output_columns_size = (size_t) temp_int;
  if (jtn.m_output_columns_size > 0)
    {
      jtn.m_output_columns =
	      (json_table_column *) stx_alloc_struct (thread_p, (int) (sizeof (json_table_column) * jtn.m_output_columns_size));
      for (size_t i = 0; i < jtn.m_output_columns_size; ++i)
	{
	  jtn.m_output_columns[i].init ();
	  ptr = stx_build (thread_p, ptr, jtn.m_output_columns[i]);
	}
    }

  ptr = or_unpack_int (ptr, &temp_int);
  jtn.m_nested_nodes_size = (size_t) temp_int;
  if (jtn.m_nested_nodes_size > 0)
    {
      jtn.m_nested_nodes =
	      (json_table_node *) stx_alloc_struct (thread_p, (int) (sizeof (json_table_node) * jtn.m_nested_nodes_size));
      for (size_t i = 0; i < jtn.m_nested_nodes_size; ++i)
	{
	  jtn.m_nested_nodes[i].init ();
	  ptr = stx_build (thread_p, ptr, jtn.m_nested_nodes[i]);
	}
    }

  ptr = or_unpack_int (ptr, &temp_int);
  jtn.m_id = (size_t) temp_int;

  ptr = or_unpack_int (ptr, &temp_int);
  jtn.m_is_iterable_node = (bool) temp_int;

  return ptr;
}

char *
stx_build (THREAD_ENTRY *thread_p, char *ptr, cubxasl::json_table::spec_node &json_table_spec)
{
  json_table_spec.init ();

  int node_count;
  ptr = or_unpack_int (ptr, &node_count);
  json_table_spec.m_node_count = (size_t) (node_count);

  stx_restore (thread_p, ptr, json_table_spec.m_json_reguvar);

  stx_alloc (thread_p, json_table_spec.m_root_node);
  assert (json_table_spec.m_root_node != NULL);

  json_table_spec.m_root_node->init ();
  ptr = stx_build (thread_p, ptr, *json_table_spec.m_root_node);

  return ptr;
}

char *
stx_build (THREAD_ENTRY *thread_p, char *ptr, db_value &val)
{
  return stx_build_db_value (thread_p, ptr, &val);
}

char *
stx_unpack (THREAD_ENTRY *thread_p, char *ptr, json_table_column_behavior &behavior)
{
  int temp;

  ptr = or_unpack_int (ptr, &temp);
  behavior.m_behavior = (json_table_column_behavior_type) temp;

  if (behavior.m_behavior == JSON_TABLE_DEFAULT_VALUE)
    {
      behavior.m_default_value = (DB_VALUE *) stx_alloc_struct (thread_p, sizeof (DB_VALUE));
      ptr = stx_build (thread_p, ptr, *behavior.m_default_value);
    }

  return ptr;
}

bool
xasl_stream_compare (const cubxasl::json_table::column &first, const cubxasl::json_table::column &second)
{
  if (first.m_function != second.m_function)
    {
      return false;
    }

  return true;
}

bool
xasl_stream_compare (const cubxasl::json_table::node &first, const cubxasl::json_table::node &second)
{
  if (first.m_output_columns_size != second.m_output_columns_size)
    {
      return false;
    }

  if (first.m_nested_nodes_size != second.m_nested_nodes_size)
    {
      return false;
    }

  if (first.m_id != second.m_id)
    {
      return false;
    }

  if (first.m_is_iterable_node != second.m_is_iterable_node)
    {
      return false;
    }

  return true;
}

bool
xasl_stream_compare (const cubxasl::json_table::spec_node &first, const cubxasl::json_table::spec_node &second)
{
  if (first.m_node_count != second.m_node_count)
    {
      return false;
    }
  return true;
}
