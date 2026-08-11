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
 * json_builder.cpp - JSON tree building for execution plan output, over RapidJSON
 */

#include "json_builder.h"

#include "db_rapidjson.hpp"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"

#include <cstdarg>
#include <cstring>
#include <vector>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/*
 * Memory model
 *
 * Node data lives in one pool per thread, an arena. A node created by this
 * interface is a root until it is inserted somewhere; insertion moves the data
 * into the parent and re-points the handle at the member that now holds it, so
 * the caller's pointer keeps working. That matters because the plan dump inserts
 * an empty object into its parent and then keeps adding members through the
 * original pointer.
 *
 * The arena is released when the last root on the thread is released, which is
 * what json_decref () counts. Nodes that were inserted are not roots any more and
 * are reclaimed with the arena.
 */

namespace
{
  typedef rapidjson::MemoryPoolAllocator<rapidjson::CrtAllocator> pool_type;

  struct json_arena;

  struct node_impl
  {
    rapidjson::Value own;         /* storage while this node is still a root */
    rapidjson::Value *held;       /* own, or the member it was moved into */
    json_arena *arena;
    bool is_root;
  };

  struct json_arena
  {
    pool_type pool;
    // *INDENT-OFF*
    std::vector<node_impl *> nodes;
    // *INDENT-ON*
    long roots = 0;
  };

  thread_local json_arena *tl_arena = NULL;

  json_arena *
  arena_get ()
  {
    if (tl_arena == NULL)
      {
	tl_arena = new json_arena ();
      }
    return tl_arena;
  }

  void
  arena_release (json_arena *a)
  {
    for (size_t i = 0; i < a->nodes.size (); i++)
      {
	delete a->nodes[i];
      }
    a->nodes.clear ();
    a->roots = 0;
    a->pool.Clear ();
  }

  node_impl *
  node_new (rapidjson::Type t)
  {
    json_arena *a = arena_get ();
    node_impl *n = new node_impl ();
    n->own.SetNull ();
    if (t != rapidjson::kNullType)
      {
	n->own = rapidjson::Value (t);
      }
    n->held = &n->own;
    n->arena = a;
    n->is_root = true;
    a->nodes.push_back (n);
    a->roots++;
    return n;
  }

  /* the node stops being a root once its data has been moved into a parent */
  void
  node_attached (node_impl *n, rapidjson::Value *where)
  {
    n->held = where;
    if (n->is_root)
      {
	n->is_root = false;
	n->arena->roots--;
      }
  }

  // *INDENT-OFF*
  inline node_impl *
  N (json_t *p)
  {
    return reinterpret_cast<node_impl *> (p);
  }

  inline const node_impl *
  N (const json_t *p)
  {
    return reinterpret_cast<const node_impl *> (p);
  }

  inline json_t *
  H (node_impl *n)
  {
    return reinterpret_cast<json_t *> (n);
  }
  // *INDENT-ON*
}				// namespace

json_t *
json_object (void)
{
  return H (node_new (rapidjson::kObjectType));
}

json_t *
json_array (void)
{
  return H (node_new (rapidjson::kArrayType));
}

json_t *
json_null (void)
{
  return H (node_new (rapidjson::kNullType));
}

json_t *
json_true (void)
{
  node_impl *n = node_new (rapidjson::kNullType);
  n->held->SetBool (true);
  return H (n);
}

json_t *
json_false (void)
{
  node_impl *n = node_new (rapidjson::kNullType);
  n->held->SetBool (false);
  return H (n);
}

json_t *
json_boolean (int value)
{
  node_impl *n = node_new (rapidjson::kNullType);
  n->held->SetBool (value != 0);
  return H (n);
}

json_t *
json_integer (json_int_t value)
{
  node_impl *n = node_new (rapidjson::kNullType);
  n->held->SetInt64 ((int64_t) value);
  return H (n);
}

json_t *
json_real (double value)
{
  node_impl *n = node_new (rapidjson::kNullType);
  n->held->SetDouble (value);
  return H (n);
}

json_t *
json_string (const char *value)
{
  node_impl *n = node_new (rapidjson::kNullType);
  if (value != NULL)
    {
      n->held->SetString (value, (rapidjson::SizeType) strlen (value), n->arena->pool);
    }
  return H (n);
}

int
json_object_set_new (json_t *object, const char *key, json_t *value)
{
  if (object == NULL || key == NULL || value == NULL)
    {
      return -1;
    }

  node_impl *o = N (object);
  node_impl *v = N (value);
  pool_type &pool = o->arena->pool;

  if (!o->held->IsObject ())
    {
      o->held->SetObject ();
    }

  rapidjson::Value k (key, (rapidjson::SizeType) strlen (key), pool);
  o->held->AddMember (k, *v->held, pool);
  node_attached (v, & (o->held->MemberEnd () - 1)->value);
  return 0;
}

int
json_array_append_new (json_t *array, json_t *value)
{
  if (array == NULL || value == NULL)
    {
      return -1;
    }

  node_impl *a = N (array);
  node_impl *v = N (value);

  if (!a->held->IsArray ())
    {
      a->held->SetArray ();
    }

  a->held->PushBack (*v->held, a->arena->pool);
  node_attached (v, & (*a->held)[a->held->Size () - 1]);
  return 0;
}

int
json_object_clear (json_t *object)
{
  if (object == NULL)
    {
      return -1;
    }
  if (N (object)->held->IsObject ())
    {
      N (object)->held->RemoveAllMembers ();
    }
  return 0;
}

void
json_decref (json_t *node)
{
  if (node == NULL)
    {
      return;
    }

  node_impl *n = N (node);
  json_arena *a = n->arena;

  if (n->is_root)
    {
      n->is_root = false;
      a->roots--;
    }
  if (a->roots <= 0)
    {
      arena_release (a);
    }
}

void
json_delete (json_t *node)
{
  json_decref (node);
}

char *
json_dumps (const json_t *node, size_t flags)
{
  if (node == NULL)
    {
      return NULL;
    }

  rapidjson::StringBuffer sb;
  // *INDENT-OFF*
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer (sb);
  // *INDENT-ON*
  writer.SetIndent (' ', 2);
  N (node)->held->Accept (writer);

  return strdup (sb.GetString ());
}

json_t *
json_loads (const char *text, size_t flags, void *error)
{
  if (text == NULL)
    {
      return NULL;
    }

  node_impl *n = node_new (rapidjson::kNullType);
  /* parse into the arena, so the parsed data outlives this call */
  rapidjson::Document doc (&n->arena->pool);
  doc.Parse (text);
  if (doc.HasParseError ())
    {
      json_decref (H (n));
      return NULL;
    }
  n->held->Swap (doc);
  return H (n);
}

json_t *
json_pack (const char *fmt, ...)
{
  if (fmt == NULL || *fmt != '{')
    {
      return NULL;
    }

  va_list ap;
  va_start (ap, fmt);

  node_impl *root = node_new (rapidjson::kObjectType);
  pool_type &pool = root->arena->pool;
  const char *p = fmt + 1;
  bool ok = true;

  while (ok && *p != '\0' && *p != '}')
    {
      if (*p == ',' || *p == ' ')
	{
	  p++;
	  continue;
	}
      if (*p != 's')
	{
	  ok = false;
	  break;
	}
      p++;
      const char *key = va_arg (ap, const char *);
      if (*p != ':')
	{
	  ok = false;
	  break;
	}
      p++;

      rapidjson::Value k (key != NULL ? key : "", (rapidjson::SizeType) (key != NULL ? strlen (key) : 0), pool);

      switch (*p)
	{
	case 'o':
	{
	  json_t *arg = va_arg (ap, json_t *);
	  if (arg != NULL)
	    {
	      node_impl *v = N (arg);
	      root->held->AddMember (k, *v->held, pool);
	      node_attached (v, & (root->held->MemberEnd () - 1)->value);
	    }
	  else
	    {
	      rapidjson::Value nul (rapidjson::kNullType);
	      root->held->AddMember (k, nul, pool);
	    }
	  p++;
	  break;
	}
	case 's':
	{
	  const char *s = va_arg (ap, const char *);
	  rapidjson::Value sv (s != NULL ? s : "", (rapidjson::SizeType) (s != NULL ? strlen (s) : 0), pool);
	  root->held->AddMember (k, sv, pool);
	  p++;
	  break;
	}
	case 'i':
	{
	  rapidjson::Value v (va_arg (ap, int));
	  root->held->AddMember (k, v, pool);
	  p++;
	  break;
	}
	case 'I':
	{
	  rapidjson::Value v;
	  v.SetInt64 ((int64_t) va_arg (ap, json_int_t));
	  root->held->AddMember (k, v, pool);
	  p++;
	  break;
	}
	case 'f':
	{
	  rapidjson::Value v (va_arg (ap, double));
	  root->held->AddMember (k, v, pool);
	  p++;
	  break;
	}
	case 'b':
	{
	  rapidjson::Value v (va_arg (ap, int) != 0);
	  root->held->AddMember (k, v, pool);
	  p++;
	  break;
	}
	case '[':
	{
	  p++;
	  rapidjson::Value arr (rapidjson::kArrayType);
	  while (*p != '\0' && *p != ']')
	    {
	      if (*p == ',' || *p == ' ')
		{
		  p++;
		  continue;
		}
	      if (*p != 'o')
		{
		  ok = false;
		  break;
		}
	      json_t *arg = va_arg (ap, json_t *);
	      if (arg != NULL)
		{
		  node_impl *v = N (arg);
		  arr.PushBack (*v->held, pool);
		  node_attached (v, &arr[arr.Size () - 1]);
		}
	      else
		{
		  arr.PushBack (rapidjson::Value (rapidjson::kNullType), pool);
		}
	      p++;
	    }
	  if (!ok)
	    {
	      break;
	    }
	  if (*p == ']')
	    {
	      p++;
	    }
	  root->held->AddMember (k, arr, pool);
	  break;
	}
	default:
	  ok = false;
	  break;
	}
    }

  va_end (ap);

  if (!ok)
    {
      json_decref (H (root));
      return NULL;
    }
  return H (root);
}

void
json_set_alloc_funcs (void * (*malloc_fn) (size_t), void (*free_fn) (void *))
{
  /* RapidJSON manages its own allocation */
  (void) malloc_fn;
  (void) free_fn;
}
