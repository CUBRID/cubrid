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
 * parse_tree.c -
 */

#ident "$Id$"

#include "config.h"

#include <stddef.h>
#include <assert.h>
#include <setjmp.h>
#include <time.h>
#if !defined(WINDOWS)
#include <sys/time.h>
#endif

#include "porting.h"
#include "dbi.h"
#include "parser.h"
#include "jansson.h"
#include "memory_alloc.h"

#if defined(SERVER_MODE)
#include "connection_error.h"
#endif /* SERVER_MODE */

#include "dbtype.h"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

/*
 * this should be big enough for "largish" select statements to print.
 * It is sized at 8192 less enough to let it fit in 2 blocks with some
 * overhead for malloc headers plus string block header.
 */
#define STRINGS_PER_BLOCK (8192-(4*sizeof(long)+sizeof(char *)+40))
#define HASH_NUMBER 128
#define NODES_PER_BLOCK 256

typedef struct parser_node_block PARSER_NODE_BLOCK;
struct parser_node_block
{
  PARSER_NODE_BLOCK *next;
  int parser_id;
  PT_NODE nodes[NODES_PER_BLOCK];
};

typedef struct parser_node_free_list PARSER_NODE_FREE_LIST;
struct parser_node_free_list
{
  PARSER_NODE_FREE_LIST *next;
  PT_NODE *node;
  int parser_id;
};

typedef struct parser_string_block PARSER_STRING_BLOCK;
struct parser_string_block
{
  PARSER_STRING_BLOCK *next;
  int parser_id;
  int last_string_start;
  int last_string_end;
  int block_end;
  union aligned
  {
    double dummy;
    char chars[STRINGS_PER_BLOCK];
  } u;
};

/* Global reserved name table including info for each reserved name */
PT_RESERVED_NAME pt_Reserved_name_table[] = {

  /* record info attributes */
  {"t_pageid", RESERVED_T_PAGEID, DB_TYPE_INTEGER}
  ,
  {"t_slotid", RESERVED_T_SLOTID, DB_TYPE_INTEGER}
  ,
  {"t_volumeid", RESERVED_T_VOLUMEID, DB_TYPE_INTEGER}
  ,
  {"t_offset", RESERVED_T_OFFSET, DB_TYPE_INTEGER}
  ,
  {"t_length", RESERVED_T_LENGTH, DB_TYPE_INTEGER}
  ,
  {"t_rectype", RESERVED_T_REC_TYPE, DB_TYPE_INTEGER}
  ,
  {"t_reprid", RESERVED_T_REPRID, DB_TYPE_INTEGER}
  ,
  {"t_chn", RESERVED_T_CHN, DB_TYPE_INTEGER}
  ,
  {"t_insid", RESERVED_T_MVCC_INSID, DB_TYPE_BIGINT}
  ,
  {"t_delid", RESERVED_T_MVCC_DELID, DB_TYPE_BIGINT}
  ,
  {"t_flags", RESERVED_T_MVCC_FLAGS, DB_TYPE_INTEGER}
  ,
  {"t_prev_version", RESERVED_T_MVCC_PREV_VERSION_LSA, DB_TYPE_INTEGER}

  /* page header info attributes */
  ,
  {"p_class_oid", RESERVED_P_CLASS_OID, DB_TYPE_OBJECT}
  ,
  {"p_prev_pageid", RESERVED_P_PREV_PAGEID, DB_TYPE_INTEGER}
  ,
  {"p_next_pageid", RESERVED_P_NEXT_PAGEID, DB_TYPE_INTEGER}
  ,
  {"p_num_slots", RESERVED_P_NUM_SLOTS, DB_TYPE_INTEGER}
  ,
  {"p_num_records", RESERVED_P_NUM_RECORDS, DB_TYPE_INTEGER}
  ,
  {"p_anchor_type", RESERVED_P_ANCHOR_TYPE, DB_TYPE_INTEGER}
  ,
  {"p_alignment", RESERVED_P_ALIGNMENT, DB_TYPE_INTEGER}
  ,
  {"p_total_free", RESERVED_P_TOTAL_FREE, DB_TYPE_INTEGER}
  ,
  {"p_cont_free", RESERVED_P_CONT_FREE, DB_TYPE_INTEGER}
  ,
  {"p_offset_to_free_area", RESERVED_P_OFFSET_TO_FREE_AREA, DB_TYPE_INTEGER}
  ,
  {"p_is_saving", RESERVED_P_IS_SAVING, DB_TYPE_INTEGER}
  ,
  {"p_update_best", RESERVED_P_UPDATE_BEST, DB_TYPE_INTEGER}

  /* key info attributes */
  ,
  {"key_volumeid", RESERVED_KEY_VOLUMEID, DB_TYPE_INTEGER}
  ,
  {"key_pageid", RESERVED_KEY_PAGEID, DB_TYPE_INTEGER}
  ,
  {"key_slotid", RESERVED_KEY_SLOTID, DB_TYPE_INTEGER}
  ,
  {"key_key", RESERVED_KEY_KEY, DB_TYPE_NULL}	/* Types should be determined at compilation */
  ,
  {"key_oid_count", RESERVED_KEY_OID_COUNT, DB_TYPE_INTEGER}
  ,
  {"key_first_oid", RESERVED_KEY_FIRST_OID, DB_TYPE_OBJECT}
  ,
  {"key_overflow_key", RESERVED_KEY_OVERFLOW_KEY, DB_TYPE_STRING}
  ,
  {"key_overflow_oids", RESERVED_KEY_OVERFLOW_OIDS, DB_TYPE_STRING}

  /* B-tree node info */
  ,
  {"bt_node_volumeid", RESERVED_BT_NODE_VOLUMEID, DB_TYPE_INTEGER}
  ,
  {"bt_node_pageid", RESERVED_BT_NODE_PAGEID, DB_TYPE_INTEGER}
  ,
  {"bt_node_type", RESERVED_BT_NODE_TYPE, DB_TYPE_STRING}
  ,
  {"bt_node_key_count", RESERVED_BT_NODE_KEY_COUNT, DB_TYPE_INTEGER}
  ,
  {"bt_node_first_key", RESERVED_BT_NODE_FIRST_KEY, DB_TYPE_NULL}
  ,
  {"bt_node_last_key", RESERVED_BT_NODE_LAST_KEY, DB_TYPE_NULL}
};

#if defined(SERVER_MODE)
/* this is a kludge because many platforms do not handle extern
 * linking per ANSI. This should be deleted when nodes get used in server.
 */
static pthread_mutex_t blocks_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t free_lists_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t parser_memory_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t parser_id_lock = PTHREAD_MUTEX_INITIALIZER;
#endif /* SERVER_MODE */

static PARSER_NODE_BLOCK *parser_Node_blocks[HASH_NUMBER];
static PARSER_NODE_FREE_LIST *parser_Node_free_lists[HASH_NUMBER];
static PARSER_STRING_BLOCK *parser_String_blocks[HASH_NUMBER];

static int parser_id = 1;

static PT_NODE *parser_create_node_block (const PARSER_CONTEXT * parser);
static void pt_free_node_blocks (const PARSER_CONTEXT * parser);
static PARSER_STRING_BLOCK *parser_create_string_block (const PARSER_CONTEXT * parser, const int length);
static void pt_free_a_string_block (const PARSER_CONTEXT * parser, PARSER_STRING_BLOCK * string_to_free);
static PARSER_STRING_BLOCK *pt_find_string_block (const PARSER_CONTEXT * parser, const char *old_string);
static char *pt_append_string_for (const PARSER_CONTEXT * parser, const char *old_string, const char *new_tail,
				   const int wrap_with_single_quote);
static PARSER_VARCHAR *pt_append_bytes_for (const PARSER_CONTEXT * parser, PARSER_VARCHAR * old_string,
					    const char *new_tail, const int new_tail_length);
static int pt_register_parser (const PARSER_CONTEXT * parser);
static void pt_unregister_parser (const PARSER_CONTEXT * parser);
static void pt_free_string_blocks (const PARSER_CONTEXT * parser);

/*
 * pt_create_node_block () - creates a new block of nodes, links the block
 * on the hash list for the parser, and returns the free list of new nodes
 *   return:
 *   parser(in):
 */
static PT_NODE *
parser_create_node_block (const PARSER_CONTEXT * parser)
{
  int idhash, inode;
  PARSER_NODE_BLOCK *block;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  block = (PARSER_NODE_BLOCK *) malloc (sizeof (PARSER_NODE_BLOCK));

  if (!block)
    {
      if (parser->jmp_env_active)
	{
	  /* long jump back to routine that set up the jump env for clean up and run down. */
	  longjmp (((PARSER_CONTEXT *) parser)->jmp_env, 1);
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (PARSER_NODE_BLOCK));
	  return NULL;
	}
    }

  /* remember which parser allocated this block */
  block->parser_id = parser->id;

  /* link blocks on the hash list for this id */
  idhash = parser->id % HASH_NUMBER;
#if defined(SERVER_MODE)
  rv = pthread_mutex_lock (&blocks_lock);
#endif /* SERVER_MODE */
  block->next = parser_Node_blocks[idhash];
  parser_Node_blocks[idhash] = block;

  /* link nodes for free list */
  for (inode = 1; inode < NODES_PER_BLOCK; inode++)
    {
      block->nodes[inode - 1].next = &block->nodes[inode];
    }
  block->nodes[NODES_PER_BLOCK - 1].next = NULL;

#if defined(SERVER_MODE)
  pthread_mutex_unlock (&blocks_lock);
#endif /* SERVER_MODE */
  /* return head of free list */
  return &block->nodes[0];
}

/*
 * parser_create_node () - creates a new node for a given parser.
 * First it tries the parser's free list. If empty it adds a new
 * block of nodes to the free list
 *   return:
 *   parser(in):
 */
PT_NODE *
parser_create_node (const PARSER_CONTEXT * parser)
{
  int idhash;
  PARSER_NODE_FREE_LIST *free_list;
  PT_NODE *node;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /* find free list for for this id */
  idhash = parser->id % HASH_NUMBER;

#if defined(SERVER_MODE)
  rv = pthread_mutex_lock (&free_lists_lock);
#endif /* SERVER_MODE */

  free_list = parser_Node_free_lists[idhash];
  while (free_list != NULL && free_list->parser_id != parser->id)
    {
      free_list = free_list->next;
    }

#if defined(SERVER_MODE)
  pthread_mutex_unlock (&free_lists_lock);
#endif /* SERVER_MODE */

  if (free_list == NULL)
    {
      /* this is an programming error ! The parser does not exist! */
      return NULL;
    }

  if (free_list->node == NULL)
    {
      /* do not need to use mutex : only used by one parser(and one thread) */
      free_list->node = parser_create_node_block (parser);
      if (free_list->node == NULL)
	{
	  return NULL;
	}
    }

  node = free_list->node;
  free_list->node = free_list->node->next;

  node->parser_id = parser->id;	/* consistency check */

  return node;
}


/*
 * pt_free_node_blocks () - frees all node blocks allocated to this parser
 *   return: none
 *   parser(in):
 */
static void
pt_free_node_blocks (const PARSER_CONTEXT * parser)
{
  int idhash;
  PARSER_NODE_BLOCK *block;
  PARSER_NODE_BLOCK **previous_block;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /* unlink blocks on the hash list for this id */
  idhash = parser->id % HASH_NUMBER;
#if defined(SERVER_MODE)
  rv = pthread_mutex_lock (&blocks_lock);
#endif /* SERVER_MODE */
  previous_block = &parser_Node_blocks[idhash];
  block = *previous_block;

  while (block != NULL)
    {
      if (block->parser_id == parser->id)
	{
	  /* remove it from list, and free it */
	  *previous_block = block->next;
	  free_and_init (block);
	}
      else
	{
	  /* keep it, and move to next block pointer */
	  previous_block = &block->next;
	}
      /* re-establish invariant */
      block = *previous_block;
    }
#if defined(SERVER_MODE)
  pthread_mutex_unlock (&blocks_lock);
#endif /* SERVER_MODE */
}

/*
 * parser_create_string_block () - reates a new block of strings, links the block
 * on the hash list for the parser, and returns the block
 *   return:
 *   parser(in):
 *   length(in):
 */
static PARSER_STRING_BLOCK *
parser_create_string_block (const PARSER_CONTEXT * parser, const int length)
{
  int idhash;
  PARSER_STRING_BLOCK *block;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  if (length < (int) STRINGS_PER_BLOCK)
    {
      block = (PARSER_STRING_BLOCK *) malloc (sizeof (PARSER_STRING_BLOCK));
      if (!block)
	{
	  if (parser->jmp_env_active)
	    {
	      /* long jump back to routine that set up the jump env for clean up and run down. */
	      longjmp (((PARSER_CONTEXT *) parser)->jmp_env, 1);
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (PARSER_STRING_BLOCK));
	      return NULL;
	    }
	}
      block->block_end = STRINGS_PER_BLOCK - 1;
    }
  else
    {
      /* This is an unusually large string. Allocate a special block for it, with space for one string, plus some space
       * for appending to. */
      block = (PARSER_STRING_BLOCK *) malloc (sizeof (PARSER_STRING_BLOCK) + (length + 1001 - STRINGS_PER_BLOCK));
      if (!block)
	{
	  if (parser->jmp_env_active)
	    {
	      /* long jump back to routine that set up the jump env for clean up and run down. */
	      longjmp (((PARSER_CONTEXT *) parser)->jmp_env, 1);
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      (sizeof (PARSER_STRING_BLOCK) + (length + 1001 - STRINGS_PER_BLOCK)));
	      return NULL;
	    }
	}
      block->block_end = CAST_BUFLEN (length + 1001 - 1);
    }

  /* remember which parser allocated this block */
  block->parser_id = parser->id;
  block->last_string_start = -1;
  block->last_string_end = -1;
  block->u.chars[0] = 0;

  /* link blocks on the hash list for this id */
  idhash = parser->id % HASH_NUMBER;
#if defined(SERVER_MODE)
  rv = pthread_mutex_lock (&parser_memory_lock);
#endif /* SERVER_MODE */
  block->next = parser_String_blocks[idhash];
  parser_String_blocks[idhash] = block;
#if defined(SERVER_MODE)
  pthread_mutex_unlock (&parser_memory_lock);
#endif /* SERVER_MODE */

  return block;
}


/*
 * parser_allocate_string_buffer () - creates memory for a given parser
 *   return: allocated memory pointer
 *   parser(in):
 *   length(in):
 *   align(in):
 *
 * Note :
 * First it tries to find length + 1 bytes in the parser's free strings list.
 * If there is no room, it adds a new block of strings to the free
 * strings list, at least large enough to hold new length plus
 * 1 (for a null character). Thus, one can call it by
 * 	copy_of_foo = pt_create_string(parser, strlen(foo));
 */
void *
parser_allocate_string_buffer (const PARSER_CONTEXT * parser, const int length, const int align)
{
  int idhash;
  PARSER_STRING_BLOCK *block;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */


  /* find free string list for for this id */
  idhash = parser->id % HASH_NUMBER;
#if defined(SERVER_MODE)
  rv = pthread_mutex_lock (&parser_memory_lock);
#endif /* SERVER_MODE */
  block = parser_String_blocks[idhash];
  while (block != NULL
	 && (block->parser_id != parser->id
	     || ((block->block_end - block->last_string_end) < (length + (align - 1) + 1))))
    {
      block = block->next;
    }
#if defined(SERVER_MODE)
  pthread_mutex_unlock (&parser_memory_lock);
#endif /* SERVER_MODE */

  if (block == NULL)
    {
      block = parser_create_string_block (parser, length + (align - 1) + 1);
      if (block == NULL)
	{
	  return NULL;
	}
    }

  /* set start to the aligned length */
  block->last_string_start = CAST_BUFLEN ((block->last_string_end + (align - 1) + 1) & ~(align - 1));
  block->last_string_end = CAST_BUFLEN (block->last_string_start + length);
  block->u.chars[block->last_string_start] = 0;

  return &block->u.chars[block->last_string_start];
}


/*
 * pt_free_a_string_block() - finds a string block, removes it from
 * 			    the hash table linked list frees the memory
 *   return:
 *   parser(in):
 *   string_to_free(in):
 */
static void
pt_free_a_string_block (const PARSER_CONTEXT * parser, PARSER_STRING_BLOCK * string_to_free)
{
  PARSER_STRING_BLOCK **previous_string;
  PARSER_STRING_BLOCK *string;
  int idhash;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /* find string holding old_string for for this parse_id */
  idhash = parser->id % HASH_NUMBER;
#if defined(SERVER_MODE)
  rv = pthread_mutex_lock (&parser_memory_lock);
#endif /* SERVER_MODE */
  previous_string = &parser_String_blocks[idhash];
  string = *previous_string;
  while (string != string_to_free)
    {
      previous_string = &string->next;
      string = *previous_string;
    }

  if (string)
    {
      *previous_string = string->next;
      free_and_init (string);
    }
#if defined(SERVER_MODE)
  pthread_mutex_unlock (&parser_memory_lock);
#endif /* SERVER_MODE */
}

/*
 * pt_find_string_block () - finds a string block from same parser that
 * 			    has oldstring as its last string
 *   return:
 *   parser(in):
 *   old_string(in):
 */
static PARSER_STRING_BLOCK *
pt_find_string_block (const PARSER_CONTEXT * parser, const char *old_string)
{
  PARSER_STRING_BLOCK *string;
  int idhash;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /* find string holding old_string for for this parse_id */
  idhash = parser->id % HASH_NUMBER;
#if defined(SERVER_MODE)
  rv = pthread_mutex_lock (&parser_memory_lock);
#endif /* SERVER_MODE */
  string = parser_String_blocks[idhash];
  while (string != NULL
	 && (string->parser_id != parser->id || &(string->u.chars[string->last_string_start]) != old_string))
    {
      string = string->next;
    }
#if defined(SERVER_MODE)
  pthread_mutex_unlock (&parser_memory_lock);
#endif /* SERVER_MODE */

  return string;
}

/*
 * pt_append_string_for () - appends a tail to a string for a given parser
 *   return:
 *   parser(in):
 *   old_string(in/out):
 *   new_tail(in):
 *   wrap_with_single_quote(in):
 *
 * Note :
 * The space allocated is at least their combined lengths plus one
 * (for a null character). The two strings are logically concatenated
 * and copied into the result string. The physical operation is typically
 * more efficient, and conservative of memory.
 * The given old_string is OVERWRITTEN.
 */
static char *
pt_append_string_for (const PARSER_CONTEXT * parser, const char *old_string, const char *new_tail,
		      const int wrap_with_single_quote)
{
  PARSER_STRING_BLOCK *string;
  char *s;
  int new_tail_length;

  /* here, you know you have two non-NULL pointers */
  string = pt_find_string_block (parser, old_string);
  new_tail_length = strlen (new_tail);
  if (wrap_with_single_quote)
    {
      new_tail_length += 2;	/* for opening/closing "'" */
    }

  /* if we did not find old_string at the end of a string buffer, or if there is not room to concatenate the tail, copy
   * both to new string */
  if ((string == NULL) || ((string->block_end - string->last_string_end) < new_tail_length))
    {
      s = (char *) parser_allocate_string_buffer (parser, strlen (old_string) + new_tail_length, sizeof (char));
      if (s == NULL)
	{
	  return NULL;
	}
      strcpy (s, old_string);
      if (wrap_with_single_quote)
	{
	  strcat (s, "'");
	}
      strcat (s, new_tail);
      if (wrap_with_single_quote)
	{
	  strcat (s, "'");
	}

      /* We might be appending to ever-growing buffers. Detect if there was a string found, but it was out of space,
       * and it was the ONLY string in the buffer. If this happened, free it. */
      if (string != NULL
	  /* && (already know there was not room, see above) */
	  && string->last_string_start == 0)
	{
	  /* old_string is the only contents of string, free it. */
	  pt_free_a_string_block (parser, string);
	}
    }
  else
    {
      /* found old_string at end of buffer with enough room concatenate new_tail in place when repeatedly adding to a
       * buffer, eg. print buffer, this will grow the allocation efficiently to the needed size. */
      s = &string->u.chars[string->last_string_end];
      if (wrap_with_single_quote)
	{
	  strcpy (s, "'");
	  strcpy (s + 1, new_tail);
	  strcpy (s + new_tail_length - 1, "'");
	}
      else
	{
	  strcpy (s, new_tail);
	}
      string->last_string_end += new_tail_length;
      s = &string->u.chars[string->last_string_start];
    }

  return s;
}

/*
 * pt_append_bytes_for () - appends a tail to a old_string for a given parser
 *   return:
 *   parser(in):
 *   old_string(in/out):
 *   new_tail(in):
 *   new_tail_length(in):
 *
 * Note :
 * The space allocated is at least their combined lengths plus one
 * (for a null character) plus the size of a long. The two strings are
 * logically concatenated and copied into the result string. The physical
 * operation is typically more efficient, and conservative of memory.
 * The given VARCHAR old_string is OVERWRITTEN.
 *
 * All VARCHAR strings for a parser will be freed by either parser_free_parser
 * or parser_free_strings.
 */
static PARSER_VARCHAR *
pt_append_bytes_for (const PARSER_CONTEXT * parser, PARSER_VARCHAR * old_string, const char *new_tail,
		     const int new_tail_length)
{
  PARSER_STRING_BLOCK *string;
  char *s;

  if (0 < parser->max_print_len && parser->max_print_len < old_string->length)
    {
      return old_string;
    }

  /* here, you know you have two non-NULL pointers */
  string = pt_find_string_block (parser, (char *) old_string);

  /* if we did not find old_string at the end of a string buffer, or if there is not room to concatenate the tail, copy
   * both to new string */
  if ((string == NULL) || ((string->block_end - string->last_string_end) < new_tail_length))
    {
      s = (char *) parser_allocate_string_buffer (parser,
						  offsetof (PARSER_VARCHAR,
							    bytes) + old_string->length + new_tail_length,
						  sizeof (long));
      if (s == NULL)
	{
	  return NULL;
	}

      memcpy (s, old_string, old_string->length + offsetof (PARSER_VARCHAR, bytes));
      old_string = (PARSER_VARCHAR *) s;
      memcpy (&old_string->bytes[old_string->length], new_tail, new_tail_length);
      old_string->length += (int) new_tail_length;
      old_string->bytes[old_string->length] = 0;	/* nul terminate */

      /* We might be appending to ever-growing buffers. Detect if there was a string found, but it was out of space,
       * and it was the ONLY string in the buffer. If this happened, free it. */
      if (string != NULL
	  /* && (already know there was not room, see above) */
	  && string->last_string_start == 0)
	{
	  /* old_string is the only contents of string, free it. */
	  pt_free_a_string_block (parser, string);
	}
    }
  else
    {
      /* found old_string at end of buffer with enough room concatenate new_tail in place when repeatedly adding to a
       * buffer, eg. print buffer, this will grow the allocation efficiently to the needed size. */

      memcpy (&old_string->bytes[old_string->length], new_tail, new_tail_length);
      old_string->length += (int) new_tail_length;
      old_string->bytes[old_string->length] = 0;	/* nul terminate */

      string->last_string_end += (int) new_tail_length;
      s = &string->u.chars[string->last_string_start];
    }

  return old_string;
}


/*
 * pt_register_parser () - registers parser as existing by creating free list
 *   return: NO_ERROR on success, non-zero for ERROR
 *   parser(in):
 */
static int
pt_register_parser (const PARSER_CONTEXT * parser)
{
  int idhash;
  PARSER_NODE_FREE_LIST *free_list;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /* find free list for for this id */
  idhash = parser->id % HASH_NUMBER;
#if defined(SERVER_MODE)
  rv = pthread_mutex_lock (&free_lists_lock);
#endif /* SERVER_MODE */
  free_list = parser_Node_free_lists[idhash];
  while (free_list != NULL && free_list->parser_id != parser->id)
    {
      free_list = free_list->next;
    }

  if (free_list == NULL)
    {
      /* this is the first time this parser allocated a node. */
      /* set up a free list. This will only be done once per parser. */

      free_list = (PARSER_NODE_FREE_LIST *) calloc (sizeof (PARSER_NODE_FREE_LIST), 1);
      if (free_list == NULL)
	{
#if defined(SERVER_MODE)
	  pthread_mutex_unlock (&free_lists_lock);
#endif /* SERVER_MODE */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (PARSER_NODE_FREE_LIST));
	  return ER_FAILED;
	}
      free_list->parser_id = parser->id;
      free_list->next = parser_Node_free_lists[idhash];
      parser_Node_free_lists[idhash] = free_list;
    }
  else
    {
#if defined(SERVER_MODE)
      pthread_mutex_unlock (&free_lists_lock);
#endif /* SERVER_MODE */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_FAILED;
    }
#if defined(SERVER_MODE)
  pthread_mutex_unlock (&free_lists_lock);
#endif /* SERVER_MODE */
  return NO_ERROR;
}

/*
 * pt_unregister_parser () - unregisters parser as existing,
 *                          or registers it as not existing
 *   return: none
 *   parser(in):
 */
static void
pt_unregister_parser (const PARSER_CONTEXT * parser)
{
  int idhash;
  PARSER_NODE_FREE_LIST *free_list;
  PARSER_NODE_FREE_LIST **previous_free_list;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /* find free list for for this id */
  idhash = parser->id % HASH_NUMBER;
#if defined(SERVER_MODE)
  rv = pthread_mutex_lock (&free_lists_lock);
#endif /* SERVER_MODE */
  previous_free_list = &parser_Node_free_lists[idhash];
  free_list = *previous_free_list;
  while (free_list != NULL && free_list->parser_id != parser->id)
    {
      previous_free_list = &free_list->next;
      free_list = *previous_free_list;
    }

  if (free_list)
    {
      /* all is ok, remove the free list from the hash list of free lists. */
      *previous_free_list = free_list->next;
      free_and_init (free_list);
    }
#if defined(SERVER_MODE)
  pthread_mutex_unlock (&free_lists_lock);
#endif /* SERVER_MODE */
}

void
parser_free_node_resources (PT_NODE * node)
{
  /* before we free this node, see if we need to clear a db_value */
  if (node->node_type == PT_VALUE && node->info.value.db_value_is_in_workspace)
    {
      db_value_clear (&node->info.value.db_value);
    }
  if (node->node_type == PT_INSERT_VALUE && node->info.insert_value.is_evaluated)
    {
      db_value_clear (&node->info.insert_value.value);
    }
  if (node->node_type == PT_JSON_TABLE_COLUMN)
    {
      PT_JSON_TABLE_COLUMN_INFO *col = &node->info.json_table_column_info;
      db_value_clear (col->on_empty.m_default_value);
      col->on_empty.m_default_value = NULL;
      db_value_clear (col->on_error.m_default_value);
      col->on_error.m_default_value = NULL;
      // db_values on_empty.m_default_value & on_error.m_default_value are allocated using area_alloc
    }
}

/*
 * parser_free_node () - Return this node to this parser's node memory pool
 *   return:
 *   parser(in):
 *   node(in):
 *
 * Note :
 * This only makes this memory eligible for re-use
 * by the current parser. To return memory to virtual memory pool,
 * pt_free_nodes or parser_free_parser should be called.
 */
void
parser_free_node (const PARSER_CONTEXT * parser, PT_NODE * node)
{
  int idhash;
  PARSER_NODE_FREE_LIST *free_list;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  if (node == NULL)
    {
      assert_release (false);
      return;
    }

  assert_release (node->node_type != PT_LAST_NODE_NUMBER);

  if (node->node_type == PT_SPEC)
    {
      /* prevent same spec_id on a parser tree */
      return;
    }

  /* find free list for for this id */
  idhash = parser->id % HASH_NUMBER;
#if defined(SERVER_MODE)
  rv = pthread_mutex_lock (&free_lists_lock);
#endif /* SERVER_MODE */
  free_list = parser_Node_free_lists[idhash];
  while (free_list != NULL && free_list->parser_id != parser->id)
    {
      free_list = free_list->next;
    }

#if defined(SERVER_MODE)
  /* we can unlock mutex, since this free_list is only used by one parser */
  pthread_mutex_unlock (&free_lists_lock);
#endif /* SERVER_MODE */

  if (free_list == NULL)
    {
      /* this is an programming error ! The parser does not exist! */
      return;
    }

  if (node->parser_id != parser->id)
    {
      /* this is an programming error ! The node is not from this parser. */
      return;
    }

  parser_free_node_resources (node);
  /*
   * Always set the node type to maximum.  This may
   * keep us from doing bad things to the free list if we try to free
   * this structure more than once.  We shouldn't be doing that (i.e.,
   * we should always be building trees, not graphs) but sometimes it
   * does by accident, and if we don't watch for it we wind up with
   * fatal errors.  A common symptom is stack exhaustion during parser_free_tree
   * as we try to recursively walk a cyclic free list.
   */
  node->node_type = PT_LAST_NODE_NUMBER;

  node->next = free_list->node;
  free_list->node = node;
}


/*
 * parser_alloc () - allocate memory for a given parser
 *   return:
 *   parser(in):
 *   length(in):
 *
 * Note :
 * The space allocated is at least length plus 8.
 * The space allocated is double word aligned ( a multiple of 8 ).
 * Thus, one can call it by
 * 	foo_buffer = parser_alloc (parser, strlen(foo));
 * ALL BUFFERS for a parser will be FREED by either parser_free_parser
 * or parser_free_strings.
 */
void *
parser_alloc (const PARSER_CONTEXT * parser, const int length)
{

  void *pointer;

  pointer = parser_allocate_string_buffer (parser, length + sizeof (long), sizeof (double));
  if (pointer)
    memset (pointer, 0, length);

  return pointer;
}

/*
 * pt_append_string () - appends a tail to a string for a given parser
 *   return:
 *   parser(in):
 *   old_string(in/out):
 *   new_tail(in):
 *
 * Note :
 * The space allocated is at least their combined lengths plus one
 * (for a null character). The two strings are logically concatenated
 * and copied into the result string. The physical operation is typically
 * more efficient, and conservative of memory
 *
 * Note :
 * pt_append_string won't modify old_string but it will return it to caller if new_tail is NULL.
 */
char *
pt_append_string (const PARSER_CONTEXT * parser, const char *old_string, const char *new_tail)
{
  char *s;

  if (new_tail == NULL)
    {
      s = CONST_CAST (char *, old_string);	// it is up to caller
    }
  else if (old_string == NULL)
    {
      s = (char *) parser_allocate_string_buffer (parser, strlen (new_tail), sizeof (char));
      if (s == NULL)
	{
	  return NULL;
	}
      strcpy (s, new_tail);
    }
  else
    {
      s = pt_append_string_for (parser, old_string, new_tail, false);
    }

  return s;
}

/*
 * pt_append_bytes () - appends a byte tail to a string for a given parser
 *   return:
 *   parser(in):
 *   old_string(in/out):
 *   new_tail(in):
 *   new_tail_length(in):
 */
PARSER_VARCHAR *
pt_append_bytes (const PARSER_CONTEXT * parser, PARSER_VARCHAR * old_string, const char *new_tail,
		 const int new_tail_length)
{
  PARSER_VARCHAR *s;

  if (old_string == NULL)
    {
      old_string =
	(PARSER_VARCHAR *) parser_allocate_string_buffer ((PARSER_CONTEXT *) parser, offsetof (PARSER_VARCHAR, bytes),
							  sizeof (long));
      if (old_string == NULL)
	{
	  return NULL;
	}
      old_string->length = 0;
      old_string->bytes[0] = 0;
    }

  if (new_tail == NULL)
    {
      s = old_string;
    }
  else
    {
      s = pt_append_bytes_for ((PARSER_CONTEXT *) parser, old_string, new_tail, new_tail_length);
    }

  return s;
}

/*
 * pt_append_varchar () -
 *   return:
 *   parser(in):
 *   old_string(in/out):
 *   new_tail(in):
 */
PARSER_VARCHAR *
pt_append_varchar (const PARSER_CONTEXT * parser, PARSER_VARCHAR * old_string, const PARSER_VARCHAR * new_tail)
{
  if (new_tail == NULL)
    {
      return old_string;
    }

  return pt_append_bytes (parser, old_string, (char *) new_tail->bytes, new_tail->length);
}


/*
 * pt_append_nulstring () - Append a nul terminated string to
 *                         a PARSER_VARCHAR binary string
 *   return:
 *   parser(in):
 *   bstring(in):
 *   nulstring(in):
 */
PARSER_VARCHAR *
pt_append_nulstring (const PARSER_CONTEXT * parser, PARSER_VARCHAR * bstring, const char *nulstring)
{
  if (nulstring == NULL)
    {
      return bstring;
    }

  return pt_append_bytes (parser, bstring, nulstring, strlen (nulstring));
}


/*
 * pt_get_varchar_bytes () - return a PARSER_VARCHAR byte pointer
 *   return:
 *   string(in):
 */
const unsigned char *
pt_get_varchar_bytes (const PARSER_VARCHAR * string)
{
  if (string != NULL)
    {
      return string->bytes;
    }

  return NULL;
}


/*
 * pt_get_varchar_length () - return a PARSER_VARCHAR length
 *   return:
 *   string(in):
 */
int
pt_get_varchar_length (const PARSER_VARCHAR * string)
{
  if (string != NULL)
    {
      return string->length;
    }

  return 0;
}


/*
 * pt_free_string_blocks () - Return parser's string memory pool to virtual memory
 *   return: none
 *   parser(in):
 */
static void
pt_free_string_blocks (const PARSER_CONTEXT * parser)
{
  int idhash;
  PARSER_STRING_BLOCK *block;
  PARSER_STRING_BLOCK **previous_block;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /* unlink blocks on the hash list for this id */
  idhash = parser->id % HASH_NUMBER;
#if defined(SERVER_MODE)
  rv = pthread_mutex_lock (&parser_memory_lock);
#endif /* SERVER_MODE */
  previous_block = &parser_String_blocks[idhash];
  block = *previous_block;

  while (block != NULL)
    {
      if (block->parser_id == parser->id)
	{
	  /* remove it from list, and free it */
	  *previous_block = block->next;
	  free_and_init (block);
	}
      else
	{
	  /* keep it, and move to next block pointer */
	  previous_block = &block->next;
	}
      /* re-establish invariant */
      block = *previous_block;
    }
#if defined(SERVER_MODE)
  pthread_mutex_unlock (&parser_memory_lock);
#endif /* SERVER_MODE */
}


/*
 * parser_create_parser () - creates a parser context
 *      The pointer can be passed to top level
 *      parse functions and then freed by parser_free_parser.
 *   return:
 */
PARSER_CONTEXT *
parser_create_parser (void)
{
  PARSER_CONTEXT *parser;
  struct timeval t;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */
  struct drand48_data rand_buf;

  parser = (PARSER_CONTEXT *) calloc (sizeof (PARSER_CONTEXT), 1);
  if (parser == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (PARSER_CONTEXT));
      return NULL;
    }

#if !defined (SERVER_MODE)
  parser_init_func_vectors ();
#endif /* !SERVER_MODE */

#if defined(SERVER_MODE)
  rv = pthread_mutex_lock (&parser_id_lock);
#endif /* SERVER_MODE */

  parser->id = parser_id++;

#if defined(SERVER_MODE)
  pthread_mutex_unlock (&parser_id_lock);
#endif /* SERVER_MODE */

  if (pt_register_parser (parser) == ER_FAILED)
    {
      free_and_init (parser);
      return NULL;
    }

  parser->execution_values.row_count = -1;

  /* Generate random values for rand() and drand() */
  gettimeofday (&t, NULL);
  srand48_r (t.tv_usec, &rand_buf);
  lrand48_r (&rand_buf, &parser->lrand);
  drand48_r (&rand_buf, &parser->drand);
  db_make_null (&parser->sys_datetime);
  db_make_null (&parser->sys_epochtime);

  /* initialization */
  parser->query_id = NULL_QUERY_ID;
  parser->flag.is_in_and_list = 0;
  parser->flag.is_holdable = 0;
  parser->flag.is_xasl_pinned_reference = 0;
  parser->flag.recompile_xasl_pinned = 0;
  parser->auto_param_count = 0;
  parser->flag.return_generated_keys = 0;
  parser->flag.is_system_generated_stmt = 0;
  parser->flag.has_internal_error = 0;
  parser->max_print_len = 0;
  parser->flag.is_auto_commit = 0;

  return parser;
}


/*
 * parser_free_parser() - clean up all parse structures after they are through
 *      being used. Values which need to be persistent, should have been
 *      copied by now
 *   return:
 *   parser(in):
 */
void
parser_free_parser (PARSER_CONTEXT * parser)
{
  DB_VALUE *hv;
  int i;

  assert (parser != NULL);

  /* free string blocks */
  pt_free_string_blocks (parser);
  /* free node blocks */
  pt_free_node_blocks (parser);
  pt_unregister_parser (parser);

  if (parser->error_buffer)
    {
      free ((char *) parser->error_buffer);
    }

  if (parser->host_variables)
    {
      for (i = 0, hv = parser->host_variables; i < parser->host_var_count + parser->auto_param_count; i++, hv++)
	{
	  db_value_clear (hv);
	}
      free_and_init (parser->host_variables);
    }

  if (parser->host_var_expected_domains)
    {
      free_and_init (parser->host_var_expected_domains);
    }

  parser_free_lcks_classes (parser);

  /* free remaining plan trace string */
  if (parser->query_trace == true && parser->num_plan_trace > 0)
    {
      for (i = 0; i < parser->num_plan_trace; i++)
	{
	  if (parser->plan_trace[i].format == QUERY_TRACE_TEXT)
	    {
	      if (parser->plan_trace[i].trace.text_plan != NULL)
		{
		  free_and_init (parser->plan_trace[i].trace.text_plan);
		}
	    }
	  else if (parser->plan_trace[i].format == QUERY_TRACE_JSON)
	    {
	      if (parser->plan_trace[i].trace.json_plan != NULL)
		{
		  json_object_clear (parser->plan_trace[i].trace.json_plan);
		  json_decref (parser->plan_trace[i].trace.json_plan);
		  parser->plan_trace[i].trace.json_plan = NULL;
		}
	    }
	}

      parser->num_plan_trace = 0;
    }

  free_and_init (parser);
}

/*
 * parser_free_lcks_classes() - free allocated memory in pt_class_pre_fetch()
 *                              and pt_find_lck_classes ()
 *   return: void
 *   parser(in):
 */
void
parser_free_lcks_classes (PARSER_CONTEXT * parser)
{
  int i;

  if (parser->lcks_classes)
    {
      for (i = 0; i < parser->num_lcks_classes; i++)
	{
	  free_and_init (parser->lcks_classes[i]);
	}

      free_and_init (parser->lcks_classes);
      parser->num_lcks_classes = 0;
    }

  return;
}

/*
 * pt_init_assignments_helper() - initialize enumeration of assignments
 *   return: void
 *   parser(in):
 *   helper(in): address of assignments enumeration structure
 *   assignment(in): assignments list to enumerate
 */
void
pt_init_assignments_helper (PARSER_CONTEXT * parser, PT_ASSIGNMENTS_HELPER * helper, PT_NODE * assignment)
{
  helper->parser = parser;
  helper->assignment = assignment;
  helper->lhs = NULL;
  helper->rhs = NULL;
  helper->is_rhs_const = false;
  helper->is_n_column = false;
}

/*
 * pt_get_next_assignment() - get next assignment
 *   return: returns left side of an assignment
 *   ea(in/out): structure used in assignments enumeration.
 *
 * Note :
 * The function fills the ENUM_ASSIGNMENT structure with details related to
 * next assignment. In case there is a multiple assignment of form
 * (i1, i2, i3)=(select * from t) then the left side of assignment is split
 * and each element of it is returned as if there is a separate assignment and
 * the right side is always returned the same. In this case the is_n_columns
 * is set to true.
 */
PT_NODE *
pt_get_next_assignment (PT_ASSIGNMENTS_HELPER * ea)
{
  PT_NODE *lhs = ea->lhs, *rhs = NULL;

  ea->is_rhs_const = false;
  if (lhs != NULL)
    {
      if (lhs->next != NULL)
	{
	  ea->is_n_column = true;
	  ea->lhs = lhs->next;
	  return ea->lhs;
	}
      ea->assignment = ea->assignment->next;
    }

  if (ea->assignment != NULL)
    {
      lhs = ea->assignment->info.expr.arg1;
      ea->rhs = rhs = ea->assignment->info.expr.arg2;
      ea->is_rhs_const = PT_IS_CONST_NOT_HOSTVAR (rhs);
      if (lhs->node_type == PT_NAME)
	{
	  ea->is_n_column = false;
	  ea->lhs = lhs;
	  return ea->lhs;
	}
      else
	{			/* PT_IS_N_COLUMN_UPDATE_EXPR(lhs) == true */
	  ea->is_n_column = true;
	  ea->lhs = lhs->info.expr.arg1;
	  return ea->lhs;
	}
    }
  else
    {
      ea->lhs = NULL;
      ea->rhs = NULL;
      ea->is_n_column = false;
      return NULL;
    }
}

/*
 * pt_count_assignments() - count assignments
 *   return: the number of assignments in the assignments list
 *   parser(in):
 *   assignments(in): assignments to count.
 *
 * Note :
 * Multiple assignments are split and each component is counted.
 */
int
pt_count_assignments (PARSER_CONTEXT * parser, PT_NODE * assignments)
{
  PT_ASSIGNMENTS_HELPER ea;
  int cnt = 0;

  pt_init_assignments_helper (parser, &ea, assignments);

  while (pt_get_next_assignment (&ea))
    {
      cnt++;
    }

  return cnt;
}

bool
pt_is_json_value_type (PT_TYPE_ENUM type)
{
  if (type == PT_TYPE_MAYBE || type == PT_TYPE_NULL)
    {
      return true;
    }

  DB_TYPE converted_type = pt_type_enum_to_db (type);

  return db_is_json_value_type (converted_type);
}

bool
pt_is_json_doc_type (PT_TYPE_ENUM type)
{
  if (type == PT_TYPE_MAYBE || type == PT_TYPE_NULL)
    {
      return true;
    }

  DB_TYPE converted_type = pt_type_enum_to_db (type);

  return db_is_json_doc_type (converted_type);
}
