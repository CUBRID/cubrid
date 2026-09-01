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
 * which is what trace_json_decref () counts.
 *
 * The consequence is that one node that is neither stored nor released keeps the
 * arena open for as long as the thread lives, and every later tree built on that
 * thread piles up in it. The interface is built so that cannot happen by
 * accident: an entry point that fails with a node it was given to store
 * releases it, and node_new () refuses to grow the arena past a bound rather
 * than let it run away unnoticed.
 *
 * The exception is a node the caller never owned in the first place - the
 * container itself, or one another container has already taken. Those are
 * refused and left where they are, since there is nothing to release.
 */

namespace
{
  typedef rapidjson::MemoryPoolAllocator<rapidjson::CrtAllocator> pool_type;

  /* A trace runs to a few thousand nodes, so reaching this usually means one
   * went missing upstream - but a plan over enough partitioned tables gets here
   * honestly, so it bounds the memory rather than asserting. The trace comes
   * out short. */
  const size_t ARENA_NODE_LIMIT = 1 << 18;

  /* An object past this many members carries an index of its keys, so that
   * setting one stops being a scan of the whole container. Below it the scan
   * wins and the index would only cost an allocation. */
  const rapidjson::SizeType OBJECT_INDEX_THRESHOLD = 128;

  /*
   * The index is an open-addressed table of member positions - nothing else.
   * It stores no key: on a probe it compares against the member's own name in
   * the container, which is where the key already lives. So it copies no
   * strings, allocates once per growth rather than once per key, and stays in
   * one cache-friendly run of memory.
   *
   * A cell holds the member position plus one, so that zero can mean empty.
   */
  struct key_index
  {
    // *INDENT-OFF*
    std::vector<rapidjson::SizeType> cells;
    // *INDENT-ON*
    size_t used = 0;
  };

  size_t
  key_hash (const char *key, size_t len)
  {
    /* FNV-1a */
    size_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < len; i++)
      {
	h ^= (unsigned char) key[i];
	h *= 1099511628211ULL;
      }
    return h;
  }

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
    key_index *index;		/* built once this object outgrows the scan */

    ~node_impl ()
    {
      delete index;
    }

    rapidjson::Value *value ();
  };

  struct json_arena
  {
    pool_type pool;
    // *INDENT-OFF*
    std::vector<node_impl *> nodes;
    // *INDENT-ON*
    long owned = 0;

    ~json_arena ()
    {
      /* a thread that ends without having released every tree still gives its
       * nodes back here, rather than leaving them to the process */
      for (size_t i = 0; i < nodes.size (); i++)
	{
	  delete nodes[i];
	}
    }
  };

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
    /* one pool per thread, released when the thread ends */
    thread_local json_arena arena;
    return &arena;
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
    n->index = NULL;
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

  const rapidjson::SizeType INDEX_NOT_FOUND = (rapidjson::SizeType) -1;

  /* Where key sits in container, or INDEX_NOT_FOUND. free_cell comes back as
   * the cell an insert of that key would take. */
  rapidjson::SizeType
  index_probe (const key_index *ix, const rapidjson::Value *container, const char *key, size_t len, size_t *free_cell)
  {
    size_t mask = ix->cells.size () - 1;
    size_t i = key_hash (key, len) & mask;

    while (ix->cells[i] != 0)
      {
	rapidjson::SizeType slot = ix->cells[i] - 1;
	const rapidjson::Value &name = (container->MemberBegin () + slot)->name;
	if (name.GetStringLength () == len && memcmp (name.GetString (), key, len) == 0)
	  {
	    return slot;
	  }
	i = (i + 1) & mask;
      }

    *free_cell = i;
    return INDEX_NOT_FOUND;
  }

#if !defined (NDEBUG)
  /* what index_probe () has to agree with: where a plain scan finds the key */
  rapidjson::SizeType
  index_debug_scan (const rapidjson::Value *container, const char *key, size_t len)
  {
    rapidjson::SizeType slot = 0;
    for (rapidjson::Value::ConstMemberIterator m = container->MemberBegin (); m != container->MemberEnd (); ++m, ++slot)
      {
	if (m->name.GetStringLength () == len && memcmp (m->name.GetString (), key, len) == 0)
	  {
	    return slot;
	  }
      }
    return INDEX_NOT_FOUND;
  }
#endif /* !NDEBUG */

  void
  index_rebuild (key_index *ix, const rapidjson::Value *container, size_t cells)
  {
    ix->cells.assign (cells, 0);
    ix->used = 0;

    rapidjson::SizeType slot = 0;
    for (rapidjson::Value::ConstMemberIterator m = container->MemberBegin (); m != container->MemberEnd (); ++m, ++slot)
      {
	size_t free_cell = 0;
	if (index_probe (ix, container, m->name.GetString (), m->name.GetStringLength (), &free_cell)
	    == INDEX_NOT_FOUND)
	  {
	    ix->cells[free_cell] = slot + 1;
	  }
	/* A repeat has no cell of its own; the first position stays the answer.
	 * It still counts towards the load, so used keeps matching the member
	 * count. Only trace_json_loads () can produce one - JSON text may. */
	ix->used++;
      }
  }

  // *INDENT-OFF*
  inline node_impl *
  as_node (trace_json_t *p)
  {
    return reinterpret_cast<node_impl *> (p);
  }

  inline trace_json_t *
  as_handle (node_impl *n)
  {
    return reinterpret_cast<trace_json_t *> (n);
  }
  // *INDENT-ON*

  trace_json_t *
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

trace_json_t *
trace_json_object (void)
{
  node_impl *n = node_new (rapidjson::kObjectType);
  return n == NULL ? NULL : as_handle (n);
}

trace_json_t *
trace_json_array (void)
{
  node_impl *n = node_new (rapidjson::kArrayType);
  return n == NULL ? NULL : as_handle (n);
}

trace_json_t *
trace_json_true (void)
{
  return scalar_new (rapidjson::Value (true));
}

trace_json_t *
trace_json_false (void)
{
  return scalar_new (rapidjson::Value (false));
}

trace_json_t *
trace_json_boolean (int value)
{
  return scalar_new (rapidjson::Value (value != 0));
}

trace_json_t *
trace_json_integer (trace_json_int_t value)
{
  rapidjson::Value v;
  v.SetInt64 ((int64_t) value);
  return scalar_new (std::move (v));
}

trace_json_t *
trace_json_real (double value)
{
  if (!std::isfinite (value))
    {
      /* JSON has no NaN and no infinity. Letting one through would make the
       * writer stop in the middle of the document and hand back a fragment. */
      return NULL;
    }
  return scalar_new (rapidjson::Value (value));
}

trace_json_t *
trace_json_string (const char *value)
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
trace_json_object_set_new (trace_json_t *object, const char *key, trace_json_t *value)
{
  if (value == NULL)
    {
      return -1;
    }

  node_impl *v = as_node (value);

  if (object == NULL || key == NULL)
    {
      trace_json_decref (value);
      return -1;
    }

  if (object == value)
    {
      /* the container and the value are the same node, so there is nothing to
       * take over; releasing it here would pull the ground out from under the
       * handle the caller still holds */
      return -1;
    }

  if (v->owner != NULL)
    {
      /* A container has already taken this node. Taking it over again moves the
       * data - RapidJSON stores a member by moving it - so the first container
       * would be left holding a null. Nothing to release either: the node
       * stopped being the caller's when that container took it. */
      return -1;
    }

  node_impl *o = as_node (object);

  if (o->arena != v->arena)
    {
      /* the two were built on different threads, so the value's data sits in a
       * pool this container has no say over; splicing it in would leave the
       * container pointing at storage that can go away under it */
      assert (false);
      trace_json_decref (value);
      return -1;
    }

  rapidjson::Value *container = o->value ();
  if (!container->IsObject ())
    {
      /* Turning the container into an object here would throw away whatever it
       * holds. The multi-spec SCAN of a class hierarchy hands an array to code
       * that goes on to set members on it, so this is reachable. */
      trace_json_decref (value);
      return -1;
    }

  /* A key set twice replaces what was there, the way it used to. RapidJSON
   * keeps no index of the keys, so finding it is a scan - the cheaper of the
   * two while the object is small, quadratic once it is not. Past a threshold
   * the object carries an index and the scan stops. */
  size_t len = strlen (key);
  size_t free_cell = 0;
  rapidjson::SizeType slot = INDEX_NOT_FOUND;

  if (o->index == NULL && container->MemberCount () >= OBJECT_INDEX_THRESHOLD)
    {
      o->index = new key_index ();
      index_rebuild (o->index, container, 4 * (size_t) OBJECT_INDEX_THRESHOLD);
    }

  if (o->index != NULL)
    {
      /* One cell is filled per member, so this is the cheap way to notice that
       * the container has been changed behind the index's back - by a member
       * being removed, say, which nothing does today. */
      assert (o->index->used == container->MemberCount ());

      slot = index_probe (o->index, container, key, len, &free_cell);

      /* And this is the thorough way: the index has to answer what the scan it
       * replaced would have answered. It costs a scan, so only a debug build
       * pays for it, but every run of the unit tests checks it. */
      assert (slot == index_debug_scan (container, key, len));
    }
  else
    {
      rapidjson::Value::MemberIterator member = container->FindMember (key);
      if (member != container->MemberEnd ())
	{
	  slot = (rapidjson::SizeType) (member - container->MemberBegin ());
	}
    }

  if (slot != INDEX_NOT_FOUND)
    {
      (container->MemberBegin () + slot)->value = *v->value ();
      node_attached (v, o, slot);
      return 0;
    }

  /* the key is copied into the pool only once it is known to be a new one */
  pool_type &pool = o->arena->pool;
  rapidjson::Value k (key, (rapidjson::SizeType) len, pool);
  container->AddMember (k, *v->value (), pool);
  slot = container->MemberCount () - 1;

  if (o->index != NULL)
    {
      /* keep the table under a 0.7 load, so a probe stays short */
      if ((o->index->used + 1) * 10 >= o->index->cells.size () * 7)
	{
	  index_rebuild (o->index, container, o->index->cells.size () * 2);
	}
      else
	{
	  o->index->cells[free_cell] = slot + 1;
	  o->index->used++;
	}
    }

  node_attached (v, o, slot);
  return 0;
}

int
trace_json_array_append_new (trace_json_t *array, trace_json_t *value)
{
  if (value == NULL)
    {
      return -1;
    }

  node_impl *v = as_node (value);

  if (array == NULL)
    {
      trace_json_decref (value);
      return -1;
    }

  if (array == value)
    {
      /* the container and the value are the same node, so there is nothing to
       * take over; releasing it here would pull the ground out from under the
       * handle the caller still holds */
      return -1;
    }

  if (v->owner != NULL)
    {
      /* already taken over by a container; see trace_json_object_set_new () */
      return -1;
    }

  node_impl *a = as_node (array);

  if (a->arena != v->arena)
    {
      /* the two were built on different threads, so the value's data sits in a
       * pool this container has no say over; splicing it in would leave the
       * container pointing at storage that can go away under it */
      assert (false);
      trace_json_decref (value);
      return -1;
    }

  rapidjson::Value *container = a->value ();
  if (!container->IsArray ())
    {
      trace_json_decref (value);
      return -1;
    }

  container->PushBack (*v->value (), a->arena->pool);
  node_attached (v, a, container->Size () - 1);
  return 0;
}

void
trace_json_decref (trace_json_t *node)
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
trace_json_owned_count (void)
{
  return arena_get ()->owned;
}

char *
trace_json_dumps (const trace_json_t *node)
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

  if (!as_node (const_cast<trace_json_t *> (node))->value ()->Accept (writer))
    {
      /* the writer refused a value; what is in the buffer is not a document */
      return NULL;
    }

  return strdup (sb.GetString ());
}

trace_json_t *
trace_json_loads (const char *text)
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

  bool parsed;

  {
    /* Parse into the arena, so the parsed data outlives this call.
     *
     * The text is untrusted - plan_string is whatever was last written to the
     * trace_plan session variable - so neither flag is tuning. Iterative keeps
     * the nesting off the C stack, which the default parser overflows.
     * Validate refuses a string that is not UTF-8, the same rule
     * trace_json_string () applies on the way in. */
    rapidjson::Document doc (&n->arena->pool);
    doc.Parse<rapidjson::kParseIterativeFlag | rapidjson::kParseValidateEncodingFlag> (text);
    parsed = !doc.HasParseError ();
    if (parsed)
      {
	n->own.Swap (doc);
      }
  }

  if (!parsed)
    {
      /* releasing the node can clear the pool the document parsed into, so it
       * has to happen once the document is out of scope */
      trace_json_decref (as_handle (n));
      return NULL;
    }

  return as_handle (n);
}

trace_json_t *
trace_json_pack (const char *fmt, ...)
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
  std::vector<trace_json_t *> taken;
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
      trace_json_t *v = NULL;

      switch (*p)
	{
	case 'o':
	  v = va_arg (ap, trace_json_t *);
	  break;

	case 's':
	  v = trace_json_string (va_arg (ap, const char *));
	  break;

	case 'i':
	  v = trace_json_integer ((trace_json_int_t) va_arg (ap, int));
	  break;

	case 'I':
	  v = trace_json_integer (va_arg (ap, trace_json_int_t));
	  break;

	case 'f':
	  v = trace_json_real (va_arg (ap, double));
	  break;

	case 'b':
	  v = trace_json_boolean (va_arg (ap, int));
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

	      trace_json_t *element = va_arg (ap, trace_json_t *);
	      taken.push_back (element);
	      if (trace_json_array_append_new (v, element) != 0)
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

      if (trace_json_object_set_new (as_handle (root), key, v) != 0)
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
	  trace_json_decref (taken[i]);
	}
      trace_json_decref (as_handle (root));
      return NULL;
    }

  return as_handle (root);
}
