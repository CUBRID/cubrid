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
 * libcubmemc.c - User defined class method implementation to work with
 *                     CUBRID using libmemcached
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include <libmemcached/memcached.h>

#include "dict/dict.h"
#include "libcubmemc.h"

/* Cache class dictinary entry */
typedef struct cache_dict_entry CACHE_DICT_ENTRY;
struct cache_dict_entry
{
  char *name;			/* name of cache class (key) */
  char *server_list;		/* server list meta data */
  char *behavior;		/* behavior meta data */
  memcached_st *memc;		/* memcached instance */
};

/* Method house keeping structure */
typedef struct method_housekeep METHOD_HOUSEKEEP;
struct method_housekeep
{
  char *name;			/* Cache name (== class name) */
  char *server_list;		/* server_list attribute value of cache class */
  char *behavior;		/* behavior attribute value of cache class */
  CACHE_DICT_ENTRY *entry;	/* cache dictionary entry */
  int error_code;		/* error code */
};

#define METHOD_HOUSEKEEP_CLEANUP(hk)   \
do {                                   \
  (hk)->name = NULL;                   \
  (hk)->server_list = NULL;            \
  (hk)->behavior = NULL;               \
  (hk)->entry = NULL;                  \
  (hk)->error_code = CUBMEMC_OK;  \
} while(0)

#define METHOD_HOUSEKEEP_INIT(hk) METHOD_HOUSEKEEP_CLEANUP(hk)

typedef enum
{
  STORAGE_CMD_SET = 0,
  STORAGE_CMD_ADD,
  STORAGE_CMD_REPLACE,
  STORAGE_CMD_APPEND,
  STORAGE_CMD_PREPEND,
  STORAGE_CMD_CAS
} CUBMEMC_STORAGE_CMD;

typedef enum
{
  INCDEC_CMD_INC = 0,
  INCDEC_CMD_DEC
} CUBMEMC_INCDEC_CMD;

static unsigned int string_hash (const char *k1);
static int check_obj_and_get_attrs (DB_OBJECT * obj, char **name,
				    char **server_list, char **behavior);
static void delete_cache_entry_func (void *d);
static int lazy_init_cache_class_dictionary ();

static int populate_one_server (memcached_st * memc, const char *server,
				const char *port);
static int parse_server_list_with_population (const char *server_list,
					      memcached_st * memc);
static int parse_behavior_with_setting (const char *behavior,
					memcached_st * mc);
static int create_and_init_memcached (memcached_st ** memc,
				      const char *server_list,
				      const char *behavior);
#define GET_FLAG_FORCE   0x01
static int get_cache_entry (const char *name, int flag,
			    const char *server_list, const char *behavior,
			    CACHE_DICT_ENTRY ** e);
static METHOD_HOUSEKEEP *method_housekeep_begin (DB_OBJECT * obj,
						 METHOD_HOUSEKEEP * hk);
static void method_housekeep_end (METHOD_HOUSEKEEP * hk);
static const char *my_strerror (int error_code);
static int get_key_from_dbval (DB_VALUE * db_val, char **val, int *len);
static int get_value_from_dbval (DB_VALUE * db_val, char **val, int *len,
				 CUBMEMC_VALUE_TYPE * type);
static int coerce_value_to_dbval (char **value_p, size_t * value_len,
				  CUBMEMC_VALUE_TYPE value_type,
				  CUBMEMC_VALUE_TYPE target_type,
				  DB_VALUE * db_val);
static void my_cubmemc_get (DB_OBJECT * obj, DB_VALUE * return_arg,
			    DB_VALUE * key, CUBMEMC_VALUE_TYPE type);
static void cubmemc_storage_command (CUBMEMC_STORAGE_CMD cmd, DB_OBJECT * obj,
				     DB_VALUE * return_arg, DB_VALUE * key,
				     DB_VALUE * value, DB_VALUE * expiration,
				     DB_VALUE * cas);
static void cubmemc_incdec_command (CUBMEMC_INCDEC_CMD cmd, DB_OBJECT * obj,
				    DB_VALUE * return_arg, DB_VALUE * key,
				    DB_VALUE * offset);

/* Cache dictionary */
static dict *cache_Class_dictionary = NULL;

/*
 * string_hash () - string hash function for cache_Class_dictionary
 *
 * return: hash value
 * k1 (in): string
 *
 * Note: This implements the algorithm reported by Diniel J. Bernstein's.
 *       Following is the orignal posting.
 *
 * > Thanks for the reference.  Can someone recommend a book that does go into 
 * > all the latest "hot-shot" methods? 
 * Have there been any fundamentally new hashing methods in the last twenty 
 * years? The only one I know of is Pearson's. 
 *
 * What are you really looking for? There are lots of string functions that 
 * are both fast and, in practice, better than random hashing. These days I 
 * start from h = 5381, and set h = h << 5 + h + c mod any power of 2 for 
 * each new character c. Apparently Chris Torek prefers a multiplier of 31: 
 * h << 5 - h + c. These are reliable and extremely fast. You can make them 
 * even faster if you want to hash an entire string at once: just 
 * precompute powers of 31 or 33, etc. 
 *
 * ---Dan
 *
 */
static unsigned int
string_hash (const char *k)
{
  unsigned int hash = 5381;
  int c;

  while ((c = *k++) != 0)
    {
      hash = ((hash << 5) + hash) + c;	/* hash * 33 + c */
    }
  return hash;
}


/*
 * check_obj_and_get_attrs() - check object and get cache meta data attributes
 *
 * return: 0 if successful, Non zero otherwise
 * obj(in): class MOP
 * name(out): class name
 * server_list(out): server_list attribute value
 * behavior(out): behavior attribute value
 *
 */
static int
check_obj_and_get_attrs (DB_OBJECT * obj, char **name, char **server_list,
			 char **behavior)
{
  int ret;
  char *class_name;
  DB_ATTRIBUTE *server_list_attr;
  char *server_list_p;
  DB_ATTRIBUTE *behavior_attr;
  char *behavior_p;
  DB_VALUE *val;

  /* Check object and get cache class name */
  ret = db_is_class (obj);
  if (ret == 0)
    {
      return CUBMEMC_ERROR_NOT_A_CLASS;
    }

  class_name = (char *) db_get_class_name (obj);
  if (class_name == NULL)
    {
      ret = db_error_code ();

      assert (ret != 0);
      return ret;
    }

  /* Check and get the value of server_list attribute */
  server_list_attr = db_get_class_attribute (obj, CACHE_ATTR_SERVER_LIST);
  if (server_list_attr == NULL
      || db_attribute_type (server_list_attr) != DB_TYPE_STRING)
    {
      return CUBMEMC_ERROR_SERVER_LIST;
    }

  val = db_attribute_default (server_list_attr);
  if (DB_VALUE_TYPE (val) != DB_TYPE_STRING
      || (server_list_p = db_get_string (val)) == NULL)
    {
      return CUBMEMC_ERROR_SERVER_LIST;
    }

  /* Check and get the value of  behavior attribute */
  behavior_attr = db_get_class_attribute (obj, CACHE_ATTR_BEHAVIOR);
  if (behavior_attr == NULL
      || db_attribute_type (behavior_attr) != DB_TYPE_STRING)
    {
      return CUBMEMC_ERROR_BEHAVIOR;
    }

  val = db_attribute_default (behavior_attr);
  if (DB_VALUE_TYPE (val) != DB_TYPE_STRING
      || (behavior_p = db_get_string (val)) == NULL)
    {
      return CUBMEMC_ERROR_BEHAVIOR;
    }

  *name = class_name;
  *server_list = server_list_p;
  *behavior = behavior_p;

  return CUBMEMC_OK;
}

/*
 * populate_one_server () - add server to memcached_st
 * return: MEMCACHED_ERROR code
 * memc(in/out): pointer to memcached_st structure
 * server(in): host
 * port(in): port
 *
 * Note: this function does not check the value of 'server' and 'port' argument
 */
static int
populate_one_server (memcached_st * memc, const char *server,
		     const char *port)
{
  memcached_return_t mret;
  int port_id;

  assert (server != NULL);
  assert (port != NULL);

  port_id = atoi (port);

  mret = memcached_server_add (memc, server, port_id);
  if (mret != MEMCACHED_SUCCESS)
    {
      return mret;
    }

  return CUBMEMC_OK;
}

/*
 * parse_server_list_with_population () - Parse server_list string and
 *   populate specified servers into memcached_st 
 *  return: MEMCACHED_ERROR code
 *  server_list(in): server_list string
 *  memc(in/out): pointer to memcached_st structure
 *
 * Note: parse with simple FSM. Use lex/yacc to be more conservative.
 */
static int
parse_server_list_with_population (const char *server_list,
				   memcached_st * memc)
{
  enum
  { SL_SERVER, SL_PORT } fsm_status;
  char server[CACHE_ATTR_SERVER_LIST_MAX_LEN + 1];
  char port[10 + 1];		/* int range */
  char *p, *curr_bufp, *bufp;
  int C, ret;

  server[0] = '\0';
  port[0] = '\0';

  p = (char *) server_list;
  fsm_status = SL_SERVER;
  curr_bufp = &server[0];
  bufp = &server[0];

  /* Skip leading spaces */
  while (isspace (*p))
    {
      p++;
    }

  /* Body portion should have no space in it */
  while ((C = *p) != 0 && !isspace (C))
    {
      p++;
      /* Check separator */
      if (C == ((fsm_status == SL_SERVER) ? ':' : ','))
	{
	  if (curr_bufp - bufp == 0)
	    {
	      return CUBMEMC_ERROR_SERVER_LIST_FORMAT;
	    }

	  /* Close current token */
	  *curr_bufp = '\0';

	  /* State transition */
	  fsm_status = (fsm_status == SL_SERVER) ? SL_PORT : SL_SERVER;

	  if (fsm_status == SL_SERVER)
	    {
	      /* One server spec. parsed */
	      ret = populate_one_server (memc, server, port);
	      if (ret != CUBMEMC_OK)
		{
		  return ret;
		}
	    }

	  /* Switch buffer semantic */
	  if (fsm_status == SL_SERVER)
	    {
	      curr_bufp = &server[0];
	      bufp = &server[0];
	      server[0] = '\0';
	    }
	  else
	    {
	      curr_bufp = &port[0];
	      bufp = &port[0];
	      port[0] = '\0';
	    }
	}
      else if (C == ':' || C == ',')
	{
	  return CUBMEMC_ERROR_SERVER_LIST_FORMAT;
	}
      else
	{
	  if (curr_bufp - bufp >
	      ((fsm_status ==
		SL_SERVER) ? CACHE_ATTR_SERVER_LIST_MAX_LEN : 10))
	    {
	      return CUBMEMC_ERROR_SERVER_LIST_FORMAT;
	    }
	  *curr_bufp++ = (char) C;
	}
    }

  /* Handle opened SL_PORT */
  if (fsm_status == SL_PORT)
    {
      /* Close current token */
      *curr_bufp = '\0';

      ret = populate_one_server (memc, server, port);
      if (ret != CUBMEMC_OK)
	{
	  return ret;
	}
    }
  else
    {
      return CUBMEMC_ERROR_SERVER_LIST_FORMAT;
    }

  /* Skip trailing spaces */
  while (isspace (*p))
    {
      p++;
    }

  /* no remainging stuff */
  if (*p != '\0')
    {
      return CUBMEMC_ERROR_SERVER_LIST_FORMAT;
    }

  return CUBMEMC_OK;
}

/*
 * parse_behavior_with_setting () - Parse behavior string and set behavior
 *                                  as specified
 * return CUBMEMC_ERROR code
 * behavior(in): behavior string
 * mc(in/out): pointer to memcached_st structure
 * 
 */
static int
parse_behavior_with_setting (const char *behavior, memcached_st * mc)
{
  return CUBMEMC_OK;
}

/*
 * create_and_init_memcached () - create a new memcached_st 
 *
 * return: CUBMEMC_ERROR code
 * memc(out): memcached_st structure
 * server_list: server list
 * behavior: behavior string
 */
static int
create_and_init_memcached (memcached_st ** memc, const char *server_list,
			   const char *behavior)
{
  int ret = CUBMEMC_OK;
  memcached_st *mc = NULL;

  mc = memcached_create (NULL);
  if (mc == NULL)
    {
      return CUBMEMC_ERROR_OUT_OF_MEMORY;
    }

  ret = parse_server_list_with_population (server_list, mc);
  if (ret != CUBMEMC_OK)
    {
      goto error;
    }

  ret = parse_behavior_with_setting (behavior, mc);
  if (ret != CUBMEMC_OK)
    {
      goto error;
    }

  *memc = mc;
  return CUBMEMC_OK;

error:
  assert (ret != CUBMEMC_OK);

  if (mc != NULL)
    {
      memcached_free (mc);
    }
  return ret;
}

/*
 * get_cache_entry () - Get cache directory entry. 
 *
 * return: CUBMEMC_ERROR code
 * name(in): cache class name
 * flag(in): Get option
 *           GET_FLAG_FORCE: if not found create new one
 * server_list(in): server list attribute (used when GET_FLAG_FORCE flag is set)
 * behavior(in): behavior attribute (used when GET_FLAG_FORCE flag is set)
 * e(out): cache directory entry
 */
static int
get_cache_entry (const char *name, int flag, const char *server_list,
		 const char *behavior, CACHE_DICT_ENTRY ** e)
{
  CACHE_DICT_ENTRY *entry;

  assert (e != NULL);

  entry = (CACHE_DICT_ENTRY *) dict_search (cache_Class_dictionary, name);
  if (entry != NULL)
    {
      *e = entry;
      return CUBMEMC_OK;
    }

  if ((flag & GET_FLAG_FORCE) == 1)
    {
      int ret;
      char *n, *s, *b;
      memcached_st *memc;

      entry = malloc (sizeof (CACHE_DICT_ENTRY));
      if (entry == NULL)
	{
	  return CUBMEMC_ERROR_OUT_OF_MEMORY;
	}

      /* make entry */
      n = strdup (name);
      s = strdup (server_list);
      b = strdup (behavior);

      if (n == NULL || s == NULL || b == NULL)
	{
	  if (n != NULL)
	    {
	      free (n);
	    }
	  if (s != NULL)
	    {
	      free (s);
	    }
	  if (b != NULL)
	    {
	      free (b);
	    }
	  return CUBMEMC_ERROR_OUT_OF_MEMORY;
	}

      /* create memcached handle and initialize using server_list and 
         behavior */
      memc = NULL;
      ret = create_and_init_memcached (&memc, server_list, behavior);
      if (memc == NULL)
	{
	  free (n);
	  free (s);
	  free (b);
	}

      entry->name = n;
      entry->server_list = s;
      entry->behavior = b;
      entry->memc = memc;

      dict_insert (cache_Class_dictionary, entry->name, entry, 0);
      *e = entry;
    }
  else
    {
      *e = NULL;
    }

  return CUBMEMC_OK;
}

/*
 * delete_cache_entry_func () - directory delete hook function
 * return: None
 * d(in): pointer to cache directory entry
 */
static void
delete_cache_entry_func (void *d)
{
  CACHE_DICT_ENTRY *entry;

  assert (d != NULL);
  entry = (CACHE_DICT_ENTRY *) d;

  assert (entry->name != NULL);
  assert (entry->server_list != NULL);
  assert (entry->behavior != NULL);
  assert (entry->memc != NULL);

  free (entry->name);
  free (entry->server_list);
  free (entry->behavior);
  memcached_free (entry->memc);
  free (entry);
}

/*
 * lazy_init_cache_class_dictionary() - Lazy initialize cache_Class_dictionary
 * return: CUBMEMC_OK if successful, error code otherwise
 */
static int
lazy_init_cache_class_dictionary ()
{
  dict *dct;

  if (cache_Class_dictionary == NULL)
    {
      /* 1021 is the prime number that is nearest to 1024 
         key is entry->name, so we do not provide key delete function.
       */
      dct = hashtable_dict_new ((dict_cmp_func) strcmp,
				(dict_hsh_func) string_hash, NULL,
				delete_cache_entry_func, 1021);
      if (dct == NULL)
	{
	  return CUBMEMC_ERROR_OUT_OF_MEMORY;
	}
      cache_Class_dictionary = dct;
    }

  return CUBMEMC_OK;
}

/*
 * method_housekeep_begin() - setup housekeep structure
 *
 * return: NULL, if error. Otherwise, hk is returned
 * obj(in): class MOP
 * hk(out): house keep structure
 *
 * Note: If this function succeeds then caller should call 
 *       method_housekeep_end() 
 *
 */
static METHOD_HOUSEKEEP *
method_housekeep_begin (DB_OBJECT * obj, METHOD_HOUSEKEEP * hk)
{
  int ret;
  CACHE_DICT_ENTRY *entry;

  assert (obj != NULL);
  assert (hk != NULL);

  /* Check object and get attributes */
  ret =
    check_obj_and_get_attrs (obj, &hk->name, &hk->server_list, &hk->behavior);
  if (ret != CUBMEMC_OK)
    {
      hk->error_code = ret;
      return NULL;
    }

  /* Lazy initialize cache_Class_dictionary */
  ret = lazy_init_cache_class_dictionary ();
  if (ret != CUBMEMC_OK)
    {
      hk->error_code = ret;
      return NULL;
    }

  /* Get cache dictionary entry */
  entry = NULL;
  ret =
    get_cache_entry (hk->name, GET_FLAG_FORCE, hk->server_list, hk->behavior,
		     &entry);
  if (ret != CUBMEMC_OK)
    {
      hk->error_code = ret;
      return NULL;
    }
  assert (entry != NULL);

  /* Check metadata change */
  if (strncmp (hk->server_list, entry->server_list,
	       CACHE_ATTR_SERVER_LIST_MAX_LEN) != 0
      || strncmp (hk->behavior, entry->behavior,
		  CACHE_ATTR_BEHAVIOR_MAX_LEN) != 0)
    {
      /* remove */
      ret = dict_remove (cache_Class_dictionary, hk->name, 1);
      if (ret != 0)
	{
	  hk->error_code = CUBMEMC_ERROR_INTERNAL;
	  return NULL;
	}

      /* force get */
      entry = NULL;
      ret =
	get_cache_entry (hk->name, GET_FLAG_FORCE, hk->server_list,
			 hk->behavior, &entry);
      if (ret != CUBMEMC_OK)
	{
	  hk->error_code = ret;
	  return NULL;
	}
    }
  assert (entry != NULL);

  hk->entry = entry;
  hk->error_code = CUBMEMC_OK;
  return hk;
}


/*
 * method_housekeep_end() -  clean up housekeep structure
 *
 * return: None
 * hk(out): house keep structure
 *
 */
static void
method_housekeep_end (METHOD_HOUSEKEEP * hk)
{
  assert (hk != NULL);

  /* Nothing to do for now */
  return;
}

/*
 * cubmemc_stringerror () - libcubmemc specific error description
 *
 * return: description string
 * error_code: error code
 */
static const char *
my_strerror (int error_code)
{
  assert (error_code <= CUBMEMC_ERROR_START);

  switch (error_code)
    {
    case CUBMEMC_OK:
      return "Success";

    case CUBMEMC_ERROR_NOT_IMPLEMENTED:
      return "Not implemented";

    case CUBMEMC_ERROR_INVALID_ARG:
      return "Invalid argument";

    case CUBMEMC_ERROR_OUT_OF_MEMORY:
      return "Out of virtual memory";

    case CUBMEMC_ERROR_NOT_A_CLASS:
      return "Not a class object";

    case CUBMEMC_ERROR_SERVER_LIST:
      return "There must be non NULL string attribute with name server_list";

    case CUBMEMC_ERROR_BEHAVIOR:
      return "There must be non NULL string attribute with name behavior";

    case CUBMEMC_ERROR_SERVER_LIST_FORMAT:
      return "Invalid server_list value format";

    case CUBMEMC_ERROR_BEHAVIOR_FORMAT:
      return "Invalid behavior value format";

    case CUBMEMC_ERROR_CAS_OUT_OF_RANGE:
      return "Cas value is out of int range";

    case CUBMEMC_ERROR_INVALID_VALUE_TYPE:
      return "Value stored in memcached has unsupported value type";

    case CUBMEMC_ERROR_INTERNAL:
      return "Internal error";

    default:
      return "Unknown libcubmemc error";
    }
}


static int
get_key_from_dbval (DB_VALUE * db_val, char **val, int *len)
{
  char *v;
  int sz;
  int value_type;

  assert (db_val != NULL);
  assert (val != NULL);
  assert (len != NULL);

  v = NULL;
  sz = 0;
  value_type = db_value_type (db_val);
  switch (value_type)
    {
    case DB_TYPE_CHAR:
      {
	v = db_get_char (db_val, &sz);
      }
      break;

    case DB_TYPE_VARCHAR:	/* DB_TYPE_STRING */
      {
	v = db_get_string (db_val);
	if (v != NULL)
	  {
	    sz = strlen (v);
	  }
      }
      break;
    default:
      return CUBMEMC_ERROR_INVALID_ARG;
    }

  *val = v;
  *len = sz;
  return CUBMEMC_OK;
}

static int
get_value_from_dbval (DB_VALUE * db_val, char **val, int *len,
		      CUBMEMC_VALUE_TYPE * type)
{
  char *v;
  int sz;
  CUBMEMC_VALUE_TYPE value_type;

  assert (db_val != NULL);
  assert (val != NULL);
  assert (len != NULL);
  assert (type != NULL);

  v = NULL;
  sz = 0;
  value_type = VALUE_TYPE_NULL;

  switch (db_value_type (db_val))
    {
    case DB_TYPE_CHAR:
      {
	v = db_get_char (db_val, &sz);
	value_type = VALUE_TYPE_STRING;
      }
      break;

    case DB_TYPE_VARCHAR:	/* DB_TYPE_STRING */
      {
	v = db_get_string (db_val);
	if (v != NULL)
	  {
	    sz = strlen (v);
	  }
	value_type = VALUE_TYPE_STRING;
      }
      break;
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      {
	v = db_get_bit (db_val, &sz);
	sz = (sz + 7) / 8;
	value_type = VALUE_TYPE_BINARY;
      }
      break;

    default:
      return CUBMEMC_ERROR_INVALID_ARG;
    }

  *val = v;
  *len = sz;
  *type = value_type;
  return CUBMEMC_OK;
}

/*
 * cubmemc_strerror () - memcached_strerror() wrapper function
 * 
 * return: None
 * obj(in): class MOP
 * return_arg(out): STRING describing error code
 * error_code(in): error code
 */
void
cubmemc_strerror (DB_OBJECT * obj, DB_VALUE * return_arg,
		  DB_VALUE * error_code)
{
  int ret;
  char *msg;

  ret = db_get_int (error_code);
  if (ret < 0)
    {
      /* db_error_string() returns string description of LAST error occurred */
      msg = "CUBRID error";
    }
  else if (ret == 0)
    {
      msg = "Success";
    }
  if (ret < CUBMEMC_ERROR_START)
    {
      msg = (char *) memcached_strerror (NULL, ret);
    }
  else if (ret <= CUBMEMC_ERROR_END)
    {
      msg = (char *) my_strerror (ret);
    }
  else
    {
      msg = "Unknown error";
    }

  DB_MAKE_STRING (return_arg, msg);
  return;
}

/* 
 * coerce_value_to_dbval () - make db_val of type 'target_type' from given 
 *                            raw data of type 'value_type'
 *
 * return: error code
 * value_p(in/out): value. can be changed after coersion
 * value_len(in/out): value length
 * value_type: value type of *value_p
 * target_type: target type
 * db_val: value container
 * 
 * Type conversion rule
 *   VALUE_TYPE_STRING --> DB_TYPE_STRING
 *   VALUE_TYPE_BINARY --> DB_TYPE_VARBIT
 *
 * Value conversion rule: (row: value_type, col: target_type)
 *   +--------+-----------+-------------+-----------+
 *   |        | ASIS      | STRING      | BINARY    |
 *   +--------+-----------+-------------+-----------+
 *   | ASIS   | error     | error       | error     |
 *   | STRING | no change | no change   | no change |
 *   | BINARY | no change | hex format  | no change |
 *   | OTHER  | error     | error       | error     |
 *   +--------+-----------+-------------+-----------+
 *   OTHER type can be set by external application by accident. In this case,
 *   we should return error instead of unspecified behavior 
 */
static int
coerce_value_to_dbval (char **value_p, size_t * value_len,
		       CUBMEMC_VALUE_TYPE value_type,
		       CUBMEMC_VALUE_TYPE target_type, DB_VALUE * db_val)
{
  CUBMEMC_VALUE_TYPE resolved_type;
  char *vp;
  int vlen;
  int ret = CUBMEMC_OK;

  assert (*value_p != NULL);
  assert (*value_len > 0);

  /* check argument */
  if (value_type != VALUE_TYPE_STRING && value_type != VALUE_TYPE_BINARY)
    {
      return CUBMEMC_ERROR_INVALID_VALUE_TYPE;
    }

  if (target_type != VALUE_TYPE_ASIS && target_type != VALUE_TYPE_STRING &&
      target_type != VALUE_TYPE_BINARY)
    {
      assert (0);		/* my fault */
      return CUBMEMC_ERROR_INVALID_VALUE_TYPE;
    }

  vp = *value_p;
  vlen = *value_len;

  /* determine resolved_type */
  resolved_type = target_type;
  if (target_type == VALUE_TYPE_ASIS)
    {
      resolved_type = value_type;
    }

  /* do conversion */
  if (value_type == VALUE_TYPE_BINARY && target_type == VALUE_TYPE_STRING)
    {
      char *tmp;
      int i, j;
      static const char *tbl = "0123456789abcdef";

      tmp = malloc (vlen * 2 + 1);
      if (tmp == NULL)
	{
	  return CUBMEMC_ERROR_OUT_OF_MEMORY;
	}

      for (i = 0, j = 0; i < vlen; i++, j += 2)
	{
	  tmp[j] = tbl[(((unsigned char) vp[i]) >> 4) & 15];
	  tmp[j + 1] = tbl[((unsigned char) vp[i]) & 15];
	}
      tmp[j] = '\0';

      free (vp);
      vp = tmp;
      vlen = vlen * 2;
    }

  /* make db value */
  if (resolved_type == VALUE_TYPE_STRING)
    {
      if (vp[vlen] != '\0')
	{
#if 0
	  /* safety check */
	  vp = realloc (vp, vlen + 1);
	  if (vp == NULL)
	    {
	      ret = CUBMEMC_ERROR_OUT_OF_MEMORY;
	      goto end;
	    }
	  vp[vlen] = '\0';
	  vlen = vlen + 1;
#else
	  vp[vlen - 1] = '\0';
#endif
	}
      ret = db_make_string (db_val, vp);
    }
  else if (resolved_type == VALUE_TYPE_BINARY)
    {
      ret = db_make_varbit (db_val, DB_DEFAULT_PRECISION, vp, vlen * 8);
    }
  else
    {
      ret = CUBMEMC_ERROR_INTERNAL;
    }

  *value_p = vp;
  *value_len = vlen;
  return CUBMEMC_OK;
}

/*
 * my_cubmemc_get () - memcached_get wrapper function
 * 
 * return: None
 * obj(in): class MOP
 * return_arg(out): STRING
 * key(in): key
 * target_type(in): target return type
 */
static void
my_cubmemc_get (DB_OBJECT * obj, DB_VALUE * return_arg,
		DB_VALUE * key, CUBMEMC_VALUE_TYPE target_type)
{
  METHOD_HOUSEKEEP housekeep, *hk;
  memcached_return_t mret;
  int ret;
  char *key_p, *value_p;
  int key_len;
  CUBMEMC_VALUE_TYPE value_type;
  size_t value_len;
  static char *prev_return_value = NULL;

  key_p = NULL;
  key_len = 0;
  ret = get_key_from_dbval (key, &key_p, &key_len);

  if (ret != CUBMEMC_OK)
    {
      db_make_error (return_arg, ret);
      return;
    }

  /* method house keep begin */
  METHOD_HOUSEKEEP_INIT (&housekeep);
  hk = method_housekeep_begin (obj, &housekeep);
  if (hk == NULL)
    {
      db_make_error (return_arg, housekeep.error_code);
      return;
    }

  /* Call memcached_get() */
  value_len = 0;
  value_type = VALUE_TYPE_NULL;
  value_p = memcached_get (hk->entry->memc, key_p, (size_t) key_len,
			   &value_len, &value_type, &mret);


  /*
   *  1. memcached protocol permits data bytes to be zero length
   *  2. MySQL UDF memc_get returns NULL when memcached error
   *
   * Bearing in mind above facts,there is no way to distinguish NULL data
   *  from error. MySQL UDF has this type of pitfall with getter style 
   *  functions; i.e. memc_get/memc_increment/mec_decrement
   *
   * In cubmemc getter functions, error means error.
   */
  if (value_p == NULL)
    {
      if (mret == MEMCACHED_SUCCESS)
	{
	  db_make_null (return_arg);
	}
      else
	{
	  db_make_error (return_arg, (int) mret);
	}
    }
  else
    {
      if (value_type != VALUE_TYPE_STRING && value_type != VALUE_TYPE_BINARY)
	{
	  if (value_p != NULL)
	    {
	      free (value_p);
	    }

	  db_make_error (return_arg, CUBMEMC_ERROR_INVALID_VALUE_TYPE);
	  return;
	}

      ret =
	coerce_value_to_dbval (&value_p, &value_len, value_type, target_type,
			       return_arg);
      if (ret != NO_ERROR)
	{
	  db_make_error (return_arg, ret);
	  free (value_p);
	  goto end;
	}

      /*
       * Note on return_arg life time.
       * return_arg is used in caller function. Data returned from 
       * memcached_get() should be deallocated by free(). So we defer 
       * deallocating the data up to next cubmemc_get () call.
       * This simple scheme works because...
       *   1. method call is broker side operation
       *   2. broker side operation is single-threaded (i.e. one process)
       * If broker side operation is multi-threaded, prev_return_value
       *  should be located in thread specific data(TSD) area.
       */
      if (prev_return_value != NULL)
	{
	  free (prev_return_value);
	  prev_return_value = value_p;
	}
    }

end:
  /* method house keep end */
  method_housekeep_end (hk);
  return;
}

/*
 * cubmemc_get_string () - memcached_get() wrapper function
 * 
 * return: None
 * obj(in): class MOP
 * return_arg(out): Error or STRING value
 * key(in): key
 * 
 */
void
cubmemc_get_string (DB_OBJECT * obj, DB_VALUE * return_arg, DB_VALUE * key)
{
  my_cubmemc_get (obj, return_arg, key, VALUE_TYPE_STRING);
  return;
}

void
cubmemc_get_binary (DB_OBJECT * obj, DB_VALUE * return_arg, DB_VALUE * key)
{
  my_cubmemc_get (obj, return_arg, key, VALUE_TYPE_BINARY);
  return;
}

/*
 * cubmemc_storage_command () - memcached storage function wrapper
 *
 * return: None
 * cmd(in): storage command type
 * obj(int): class MOP
 * return_arg(out): Error code
 * key(in): key
 * value(in): value
 * expiration(in): expiration time specified in seconds which is either unix
 *                 time (>30 days) or offset from current time (<= 30 days)
 * cas(in): cas value (null if 'cmd' is not cas command)
 *
 */
static void
cubmemc_storage_command (CUBMEMC_STORAGE_CMD cmd, DB_OBJECT * obj,
			 DB_VALUE * return_arg, DB_VALUE * key,
			 DB_VALUE * value, DB_VALUE * expiration,
			 DB_VALUE * cas)
{
  METHOD_HOUSEKEEP housekeep, *hk;
  int ret;
  char *key_p, *value_p;
  int key_len, value_len;
  CUBMEMC_VALUE_TYPE value_type;
  int expiration_i;
  uint64_t cas_val = 0;

  key_p = NULL;
  key_len = 0;
  ret = get_key_from_dbval (key, &key_p, &key_len);
  if (ret != CUBMEMC_OK)
    {
      db_make_error (return_arg, ret);
      return;
    }

  value_len = 0;
  value_p = NULL;
  value_type = VALUE_TYPE_NULL;
  if (DB_VALUE_TYPE (value) != DB_TYPE_NULL)
    {
      ret = get_value_from_dbval (value, &value_p, &value_len, &value_type);
      if (ret != CUBMEMC_OK)
	{
	  db_make_error (return_arg, ret);
	  return;
	}
    }

  if (DB_VALUE_TYPE (expiration) != DB_TYPE_INTEGER)
    {
      db_make_error (return_arg, CUBMEMC_ERROR_INVALID_ARG);
      return;
    }
  expiration_i = db_get_int (expiration);

  if (cmd == STORAGE_CMD_CAS)
    {
      assert (cas != NULL);

      if (DB_VALUE_TYPE (cas) != DB_TYPE_INTEGER)
	{
	  db_make_error (return_arg, CUBMEMC_ERROR_INVALID_ARG);
	  return;
	}
      cas_val = (uint64_t) db_get_int (cas);
    }

  /* method house keep begin */
  METHOD_HOUSEKEEP_INIT (&housekeep);
  hk = method_housekeep_begin (obj, &housekeep);
  if (hk == NULL)
    {
      db_make_error (return_arg, housekeep.error_code);
      return;
    }

  /* call storage command */
  switch (cmd)
    {
    case STORAGE_CMD_SET:
      ret =
	memcached_set (hk->entry->memc, key_p, (size_t) key_len, value_p,
		       (size_t) value_len, (time_t) expiration_i, value_type);
      break;
    case STORAGE_CMD_ADD:
      ret =
	memcached_add (hk->entry->memc, key_p, (size_t) key_len, value_p,
		       (size_t) value_len, (time_t) expiration_i, value_type);
      break;
    case STORAGE_CMD_REPLACE:
      ret =
	memcached_replace (hk->entry->memc, key_p, (size_t) key_len,
			   value_p, (size_t) value_len,
			   (time_t) expiration_i, value_type);
      break;
    case STORAGE_CMD_APPEND:
      ret =
	memcached_append (hk->entry->memc, key_p, (size_t) key_len, value_p,
			  (size_t) value_len, (time_t) expiration_i,
			  value_type);
      break;
    case STORAGE_CMD_PREPEND:
      ret =
	memcached_prepend (hk->entry->memc, key_p, (size_t) key_len,
			   value_p, (size_t) value_len,
			   (time_t) expiration_i, value_type);
      break;
    case STORAGE_CMD_CAS:
      ret = memcached_cas (hk->entry->memc, key_p, (size_t) key_len,
			   value_p, (size_t) value_len, (time_t) expiration_i,
			   value_type, cas_val);
    default:
      assert (0);
      ret = CUBMEMC_ERROR_INTERNAL;
    }

  if (ret != MEMCACHED_SUCCESS)
    {
      db_make_error (return_arg, ret);
      goto end;
    }
  else
    {
      db_make_int (return_arg, ret);
    }

end:
  /* method house keep end */
  method_housekeep_end (hk);
  return;
}

/*
 * cubmemc_set () - memcached_set() wrapper function
 * Note:
 * See the comment of cubmemc_storage_command function for detailed description.
 */
void
cubmemc_set (DB_OBJECT * obj, DB_VALUE * return_arg,
	     DB_VALUE * key, DB_VALUE * value, DB_VALUE * expiration)
{
  cubmemc_storage_command (STORAGE_CMD_SET, obj, return_arg, key, value,
			   expiration, NULL);
}

/*
 * cubmemc_delete () - memcached_delete() wrapper function
 * 
 * return: None
 * obj(in): class MOP
 * return_arg(out): return argument (error code)
 * key(in): key
 * expiration(in): expiration time specified in seconds which is either unix 
 *                 time (>30 days) or offset from current time (<= 30 days)
 *
 */
void
cubmemc_delete (DB_OBJECT * obj, DB_VALUE * return_arg,
		DB_VALUE * key, DB_VALUE * expiration)
{
  METHOD_HOUSEKEEP housekeep, *hk;
  int ret;
  char *key_p;
  int key_len;
  int expiration_i;


  ret = get_key_from_dbval (key, &key_p, &key_len);
  if (ret != CUBMEMC_OK)
    {
      db_make_error (return_arg, ret);
      return;
    }

  if (DB_VALUE_TYPE (expiration) != DB_TYPE_INTEGER)
    {
      db_make_error (return_arg, CUBMEMC_ERROR_INVALID_ARG);
      return;
    }

  expiration_i = db_get_int (expiration);

  /* method house keep begin */
  METHOD_HOUSEKEEP_INIT (&housekeep);
  hk = method_housekeep_begin (obj, &housekeep);
  if (hk == NULL)
    {
      db_make_error (return_arg, housekeep.error_code);
      return;
    }

  /* call memcached_set () */
  ret =
    memcached_delete (hk->entry->memc, key_p, (size_t) key_len,
		      (time_t) expiration_i);

  if (ret != MEMCACHED_SUCCESS)
    {
      db_make_error (return_arg, ret);
      goto end;
    }
  else
    {
      db_make_int (return_arg, ret);
    }

end:
  /* method house keep end */
  method_housekeep_end (hk);
  return;
}

/*
 * cubmemc_add() - memcached_add() wrapper function
 * Note:
 * See the comment of cubmemc_storage_command function for detailed description.
 */
void
cubmemc_add (DB_OBJECT * obj, DB_VALUE * return_arg,
	     DB_VALUE * key, DB_VALUE * value, DB_VALUE * expiration)
{
  cubmemc_storage_command (STORAGE_CMD_ADD, obj, return_arg, key, value,
			   expiration, NULL);
  return;
}

/*
 * cubmemc_replace() - memcached_replace() wrapper function
 * Note:
 * See the comment of cubmemc_storage_command function for detailed description.
 */
void
cubmemc_replace (DB_OBJECT * obj, DB_VALUE * return_arg,
		 DB_VALUE * key, DB_VALUE * value, DB_VALUE * expiration)
{
  cubmemc_storage_command (STORAGE_CMD_REPLACE, obj, return_arg, key, value,
			   expiration, NULL);
  return;
}

/*
 * cubmemc_append() - memcached_append() wrapper function
 * Note:
 * See the comment of cubmemc_storage_command function for detailed description.
 */
void
cubmemc_append (DB_OBJECT * obj, DB_VALUE * return_arg,
		DB_VALUE * key, DB_VALUE * value, DB_VALUE * expiration)
{
  cubmemc_storage_command (STORAGE_CMD_APPEND, obj, return_arg, key, value,
			   expiration, NULL);
  return;
}

/*
 * cubmemc_prepend() - memcached_prepend() wrapper function
 * Note:
 * See the comment of cubmemc_storage_command function for detailed description.
 */
void
cubmemc_prepend (DB_OBJECT * obj, DB_VALUE * return_arg,
		 DB_VALUE * key, DB_VALUE * value, DB_VALUE * expiration)
{
  cubmemc_storage_command (STORAGE_CMD_PREPEND, obj, return_arg, key, value,
			   expiration, NULL);
  return;
}

#if 0				/* It is buggy */
/*
 * cubmemc_cas() - memcached_cas() wrapper function
 * Note:
 * See the comment of cubmemc_storage_command function for detailed description.
 */
void
cubmemc_cas (DB_OBJECT * obj, DB_VALUE * return_arg,
	     DB_VALUE * key, DB_VALUE * value,
	     DB_VALUE * expiration, DB_VALUE * cas)
{
  cubmemc_storage_command (STORAGE_CMD_CAS, obj, return_arg, key, value,
			   expiration, cas);
  return;
}
#endif

/*
 * cubmemc_incdec_command - memcached cas increment/decrement function wrapper
 * 
 * return: None
 * cmd(in): CAS command
 * obj(in): class MOP
 * return_arg(out): Error code or cas value (integer)
 * key(in): key
 * offset(in): offset
 *
 */
static void
cubmemc_incdec_command (CUBMEMC_INCDEC_CMD cmd, DB_OBJECT * obj,
			DB_VALUE * return_arg, DB_VALUE * key,
			DB_VALUE * offset)
{
  METHOD_HOUSEKEEP housekeep, *hk;
  int ret;
  char *key_p;
  int key_len;
  int offset_val;
  uint64_t cas_val;

  key_p = NULL;
  key_len = 0;
  ret = get_key_from_dbval (key, &key_p, &key_len);
  if (ret != CUBMEMC_OK)
    {
      db_make_error (return_arg, ret);
      return;
    }

  assert (offset != NULL);
  if (DB_VALUE_TYPE (offset) != DB_TYPE_INTEGER)
    {
      db_make_error (return_arg, CUBMEMC_ERROR_INVALID_ARG);
      return;
    }
  offset_val = db_get_int (offset);

  /* method house keep begin */
  METHOD_HOUSEKEEP_INIT (&housekeep);
  hk = method_housekeep_begin (obj, &housekeep);
  if (hk == NULL)
    {
      db_make_error (return_arg, housekeep.error_code);
      return;
    }

  /* call storage command */
  switch (cmd)
    {
    case INCDEC_CMD_INC:
      ret =
	memcached_increment (hk->entry->memc, key_p, (size_t) key_len,
			     offset_val, &cas_val);
      break;
    case INCDEC_CMD_DEC:
      ret =
	memcached_decrement (hk->entry->memc, key_p, (size_t) key_len,
			     offset_val, &cas_val);
      break;
    default:
      assert (0);
      ret = CUBMEMC_ERROR_INTERNAL;
    }

  if (ret != MEMCACHED_SUCCESS)
    {
      db_make_error (return_arg, ret);
      goto end;
    }
  else
    {
      if (cas_val > UINT_MAX)
	{
	  db_make_error (return_arg, CUBMEMC_ERROR_CAS_OUT_OF_RANGE);
	  goto end;
	}
      db_make_int (return_arg, (int) cas_val);
    }

end:
  /* method house keep end */
  method_housekeep_end (hk);
  return;
}

/*
 * cubmemc_increment() - memcached_decrement wrapper function
 * Note:
 * See the comment of cubmemc_incdec_command function for detailed description
 */
void
cubmemc_increment (DB_OBJECT * obj, DB_VALUE * return_arg,
		   DB_VALUE * key, DB_VALUE * offset)
{
  cubmemc_incdec_command (INCDEC_CMD_INC, obj, return_arg, key, offset);
  return;
}

/*
 * cubmemc_decrement() - memcached_decrement wrapper function
 * Note:
 * See the comment of cubmemc_incdec_command function for detailed description
 */
void
cubmemc_decrement (DB_OBJECT * obj, DB_VALUE * return_arg,
		   DB_VALUE * key, DB_VALUE * offset)
{
  cubmemc_incdec_command (INCDEC_CMD_DEC, obj, return_arg, key, offset);
  return;
}
