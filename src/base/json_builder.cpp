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

#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <utility>
#include <vector>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/*
 * Memory model
 *
 * Node data lives in one pool per thread, an arena. A node this interface hands
 * back is owned by the caller until a container takes it over or the caller
 * releases it. The arena is freed once nothing on the thread is owned any more,
 * which is what cub_json_decref () counts.
 *
 * The consequence is that one node that is neither stored nor released keeps the
 * arena open for as long as the thread lives, and every later tree built on that
 * thread piles up in it. The interface is built so that cannot happen by
 * accident: every entry point that can fail releases what it was given, and
 * arena_get () refuses to grow the arena past a bound rather than let it run
 * away unnoticed.
 */

namespace
{
  typedef rapidjson::MemoryPoolAllocator<rapidjson::CrtAllocator> pool_type;

  /* A trace of a very large query runs to a few thousand nodes. This is the
   * point where the only sane reading is that a node went missing, so refuse to
   * grow instead of holding memory the arena can no longer reclaim. */
  const size_t ARENA_NODE_LIMIT = 1 << 18;

  struct json_arena;

  /*
   * One node of the tree the callers build.
   *
   * A node starts out holding its own storage. When a container takes it over,
   * RapidJSON moves the data into the container, and the node remembers where it
   * landed - the owning node plus the member or element index - rather than a
   * pointer to it.
   *
   * That indirection is the point. RapidJSON grows a container's member and
   * element arrays by reallocating them, and its pool allocator copies the array
   * to a fresh block rather than freeing the old one. A pointer taken at
   * insertion time therefore goes stale as soon as the container gains its
   * seventeenth entry - kDefaultObjectCapacity is 16 - and a write through it
   * would land in the abandoned block and be silently dropped. Resolving through
   * the owner instead is always correct and costs one hop per level of nesting.
   */
  struct node_impl
  {
    rapidjson::Value own;	/* storage while nothing has taken this node over */
    node_impl *owner;		/* the container that did take it over, or NULL */
    rapidjson::SizeType slot;	/* member or element index inside the owner */
    json_arena *arena;
    bool is_owned_by_caller;

    rapidjson::Value *value ();
  };

  struct json_arena
  {
    pool_type pool;
    // *INDENT-OFF*
    std::vector<node_impl *> nodes;
    // *INDENT-ON*
    long owned = 0;
  };

  thread_local json_arena *tl_arena = NULL;

  rapidjson::Value *
  node_impl::value ()
  {
    if (owner == NULL)
      {
	return &own;
      }

    rapidjson::Value *container = owner->value ();
    if (container->IsObject ())
      {
	assert (slot < container->MemberCount ());
	return & (container->MemberBegin () + slot)->value;
      }

    assert (container->IsArray () && slot < container->Size ());
    return & (*container)[slot];
  }

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
    a->owned = 0;
    a->pool.Clear ();
  }

  node_impl *
  node_new (rapidjson::Type t)
  {
    json_arena *a = arena_get ();

    if (a->nodes.size () >= ARENA_NODE_LIMIT)
      {
	/* a node was neither stored nor released somewhere upstream */
	assert (false);
	return NULL;
      }

    node_impl *n = new node_impl ();
    n->own.SetNull ();
    if (t != rapidjson::kNullType)
      {
	n->own = rapidjson::Value (t);
      }
    n->owner = NULL;
    n->slot = 0;
    n->arena = a;
    n->is_owned_by_caller = true;
    a->nodes.push_back (n);
    a->owned++;
    return n;
  }

  /* the caller stops owning the node once a container has taken its data */
  void
  node_attached (node_impl *n, node_impl *owner, rapidjson::SizeType slot)
  {
    n->owner = owner;
    n->slot = slot;
    if (n->is_owned_by_caller)
      {
	n->is_owned_by_caller = false;
	n->arena->owned--;
      }
  }

  /* Bytes that are not UTF-8 cannot go into a JSON string: RapidJSON's writer
   * copies them through unchanged with the default flags, which would leave the
   * whole trace unparseable for a client. Reject them here, the way jansson
   * did, so the member is dropped and the document stays valid. */
  bool
  is_valid_utf8 (const char *s)
  {
    const unsigned char *p = (const unsigned char *) s;

    while (*p != '\0')
      {
	unsigned int cp;
	int trailing;

	if (*p < 0x80)
	  {
	    p++;
	    continue;
	  }
	else if ((*p & 0xE0) == 0xC0)
	  {
	    trailing = 1;
	    cp = *p & 0x1F;
	  }
	else if ((*p & 0xF0) == 0xE0)
	  {
	    trailing = 2;
	    cp = *p & 0x0F;
	  }
	else if ((*p & 0xF8) == 0xF0)
	  {
	    trailing = 3;
	    cp = *p & 0x07;
	  }
	else
	  {
	    return false;
	  }

	p++;
	for (int i = 0; i < trailing; i++, p++)
	  {
	    if ((*p & 0xC0) != 0x80)
	      {
		return false;
	      }
	    cp = (cp << 6) | (*p & 0x3F);
	  }

	/* overlong, surrogate and out of range encodings are not valid UTF-8 */
	if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
	  {
	    return false;
	  }
	if ((trailing == 1 && cp < 0x80) || (trailing == 2 && cp < 0x800) || (trailing == 3 && cp < 0x10000))
	  {
	    return false;
	  }
      }

    return true;
  }

  // *INDENT-OFF*
  inline node_impl *
  as_node (cub_json_t *p)
  {
    return reinterpret_cast<node_impl *> (p);
  }

  inline cub_json_t *
  as_handle (node_impl *n)
  {
    return reinterpret_cast<cub_json_t *> (n);
  }
  // *INDENT-ON*

  cub_json_t *
  scalar_new (rapidjson::Value &&v)
  {
    node_impl *n = node_new (rapidjson::kNullType);
    if (n == NULL)
      {
	return NULL;
      }
    n->own = std::move (v);
    return as_handle (n);
  }
}				// namespace

cub_json_t *
cub_json_object (void)
{
  node_impl *n = node_new (rapidjson::kObjectType);
  return n == NULL ? NULL : as_handle (n);
}

cub_json_t *
cub_json_array (void)
{
  node_impl *n = node_new (rapidjson::kArrayType);
  return n == NULL ? NULL : as_handle (n);
}

cub_json_t *
cub_json_true (void)
{
  return scalar_new (rapidjson::Value (true));
}

cub_json_t *
cub_json_false (void)
{
  return scalar_new (rapidjson::Value (false));
}

cub_json_t *
cub_json_boolean (int value)
{
  return scalar_new (rapidjson::Value (value != 0));
}

cub_json_t *
cub_json_integer (cub_json_int_t value)
{
  rapidjson::Value v;
  v.SetInt64 ((int64_t) value);
  return scalar_new (std::move (v));
}

cub_json_t *
cub_json_real (double value)
{
  if (!std::isfinite (value))
    {
      /* JSON has no NaN and no infinity. Letting one through would make the
       * writer stop in the middle of the document and hand back a fragment. */
      return NULL;
    }
  return scalar_new (rapidjson::Value (value));
}

cub_json_t *
cub_json_string (const char *value)
{
  if (value == NULL || !is_valid_utf8 (value))
    {
      return NULL;
    }

  node_impl *n = node_new (rapidjson::kNullType);
  if (n == NULL)
    {
      return NULL;
    }
  n->own.SetString (value, (rapidjson::SizeType) strlen (value), n->arena->pool);
  return as_handle (n);
}

int
cub_json_object_set_new (cub_json_t *object, const char *key, cub_json_t *value)
{
  if (value == NULL)
    {
      return -1;
    }

  node_impl *v = as_node (value);

  if (object == NULL || key == NULL)
    {
      cub_json_decref (value);
      return -1;
    }

  if (object == value)
    {
      /* the container and the value are the same node, so there is nothing to
       * take over; releasing it here would pull the ground out from under the
       * handle the caller still holds */
      return -1;
    }

  node_impl *o = as_node (object);

  if (o->arena != v->arena)
    {
      /* the two were built on different threads, so the value's data sits in a
       * pool this container has no say over; splicing it in would leave the
       * container pointing at storage that can go away under it */
      assert (false);
      cub_json_decref (value);
      return -1;
    }

  rapidjson::Value *container = o->value ();
  if (!container->IsObject ())
    {
      /* Turning the container into an object here would throw away whatever it
       * holds. The multi-spec SCAN of a class hierarchy hands an array to code
       * that goes on to set members on it, so this is reachable. */
      cub_json_decref (value);
      return -1;
    }

  pool_type &pool = o->arena->pool;
  rapidjson::Value k (key, (rapidjson::SizeType) strlen (key), pool);

  /* A key set twice replaces what was there. Appending it instead would emit
   * the same key twice, which is not what a client reading the trace expects,
   * and the lookup costs a few percent of the time this whole walk takes. */
  rapidjson::Value::MemberIterator member = container->FindMember (k);
  if (member != container->MemberEnd ())
    {
      member->value = *v->value ();
      node_attached (v, o, (rapidjson::SizeType) (member - container->MemberBegin ()));
      return 0;
    }

  container->AddMember (k, *v->value (), pool);
  node_attached (v, o, container->MemberCount () - 1);
  return 0;
}

int
cub_json_array_append_new (cub_json_t *array, cub_json_t *value)
{
  if (value == NULL)
    {
      return -1;
    }

  node_impl *v = as_node (value);

  if (array == NULL)
    {
      cub_json_decref (value);
      return -1;
    }

  if (array == value)
    {
      /* the container and the value are the same node, so there is nothing to
       * take over; releasing it here would pull the ground out from under the
       * handle the caller still holds */
      return -1;
    }

  node_impl *a = as_node (array);

  if (a->arena != v->arena)
    {
      /* the two were built on different threads, so the value's data sits in a
       * pool this container has no say over; splicing it in would leave the
       * container pointing at storage that can go away under it */
      assert (false);
      cub_json_decref (value);
      return -1;
    }

  rapidjson::Value *container = a->value ();
  if (!container->IsArray ())
    {
      cub_json_decref (value);
      return -1;
    }

  container->PushBack (*v->value (), a->arena->pool);
  node_attached (v, a, container->Size () - 1);
  return 0;
}

void
cub_json_decref (cub_json_t *node)
{
  if (node == NULL)
    {
      return;
    }

  node_impl *n = as_node (node);
  json_arena *a = n->arena;

  if (n->is_owned_by_caller)
    {
      n->is_owned_by_caller = false;
      a->owned--;
    }
  if (a->owned <= 0)
    {
      arena_release (a);
    }
}

long
cub_json_owned_count (void)
{
  return tl_arena == NULL ? 0 : tl_arena->owned;
}

char *
cub_json_dumps (const cub_json_t *node)
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

  if (!as_node (const_cast<cub_json_t *> (node))->value ()->Accept (writer))
    {
      /* the writer refused a value; what is in the buffer is not a document */
      return NULL;
    }

  return strdup (sb.GetString ());
}

cub_json_t *
cub_json_loads (const char *text)
{
  if (text == NULL)
    {
      return NULL;
    }

  node_impl *n = node_new (rapidjson::kNullType);
  if (n == NULL)
    {
      return NULL;
    }

  /* parse into the arena, so the parsed data outlives this call */
  rapidjson::Document doc (&n->arena->pool);
  doc.Parse (text);
  if (doc.HasParseError ())
    {
      cub_json_decref (as_handle (n));
      return NULL;
    }

  n->own.Swap (doc);
  return as_handle (n);
}

cub_json_t *
cub_json_pack (const char *fmt, ...)
{
  if (fmt == NULL || *fmt != '{')
    {
      return NULL;
    }

  node_impl *root = node_new (rapidjson::kObjectType);
  if (root == NULL)
    {
      return NULL;
    }

  /* Every node this walk creates or is handed goes in here, so that a format it
   * cannot follow does not leave one behind. Releasing a node the setters have
   * already taken over is a no-op, so the list can be walked unconditionally. */
  // *INDENT-OFF*
  std::vector<cub_json_t *> taken;
  // *INDENT-ON*
  const char *p = fmt + 1;
  bool ok = true;

  va_list ap;
  va_start (ap, fmt);

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
      if (key == NULL || *p != ':')
	{
	  ok = false;
	  break;
	}
      p++;

      /* each case leaves p on the last format character it consumed */
      cub_json_t *v = NULL;

      switch (*p)
	{
	case 'o':
	  v = va_arg (ap, cub_json_t *);
	  break;

	case 's':
	  v = cub_json_string (va_arg (ap, const char *));
	  break;

	case 'i':
	  v = cub_json_integer ((cub_json_int_t) va_arg (ap, int));
	  break;

	case 'I':
	  v = cub_json_integer (va_arg (ap, cub_json_int_t));
	  break;

	case 'f':
	  v = cub_json_real (va_arg (ap, double));
	  break;

	case 'b':
	  v = cub_json_boolean (va_arg (ap, int));
	  break;

	case '[':
	{
	  node_impl *arr = node_new (rapidjson::kArrayType);
	  if (arr == NULL)
	    {
	      ok = false;
	      break;
	    }
	  v = as_handle (arr);

	  p++;
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

	      cub_json_t *element = va_arg (ap, cub_json_t *);
	      taken.push_back (element);
	      if (cub_json_array_append_new (v, element) != 0)
		{
		  ok = false;
		  break;
		}
	      p++;
	    }
	  if (ok && *p != ']')
	    {
	      ok = false;
	    }
	  break;
	}

	default:
	  ok = false;
	  break;
	}

      if (v != NULL)
	{
	  taken.push_back (v);
	}
      if (!ok)
	{
	  break;
	}

      if (cub_json_object_set_new (as_handle (root), key, v) != 0)
	{
	  ok = false;
	  break;
	}
      p++;
    }

  va_end (ap);

  if (ok && *p != '}')
    {
      ok = false;
    }

  if (!ok)
    {
      /* the root goes last: while it is still owned the arena cannot be
       * released, so none of these handles can go stale mid-loop */
      for (size_t i = 0; i < taken.size (); i++)
	{
	  cub_json_decref (taken[i]);
	}
      cub_json_decref (as_handle (root));
      return NULL;
    }

  return as_handle (root);
}
