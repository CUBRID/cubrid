#include "config.h"
#include <string.h>
#include <assert.h>
#include "api_util.h"
#include "api_common.h"

typedef struct array_indexer_s ARRAY_INDEXER;
typedef struct list_indexer_elem_s LIST_INDEXER_ELEM;
typedef struct list_indexer_s LIST_INDEXER;

/* array based VALUE_INDEXER implementation */
struct array_indexer_s
{
  VALUE_INDEXER indexer;
  int nvalue;
  VALUE_AREA **vs_map;
  API_VALUE **values;
};

/* linked list based VALUE_INDEXER implementation */
struct list_indexer_elem_s
{
  dlisth header;
  VALUE_AREA *va;
  API_VALUE *value;
};

struct list_indexer_s
{
  VALUE_INDEXER indexer;
  int nelems;
  dlisth elems;			/* list of LIST_VAI_ELEM */
  /* speed boost caching */
  int cache_idx;
  LIST_INDEXER_ELEM *cache_elem;
};

/* array based VALUE_INDEXER implementation */
static int ai_api_check (VALUE_INDEXER * indexer, int index,
			 CHECK_PURPOSE pup);
static int ai_api_length (VALUE_INDEXER * indexer, int *len);
static int ai_api_get (VALUE_INDEXER * indexer, int index,
		       VALUE_AREA ** rva, API_VALUE ** rv);
static int ai_api_set (VALUE_INDEXER * indexer, int index,
		       VALUE_AREA * va, API_VALUE * dv);
static int ai_api_map (VALUE_INDEXER * indexer,
		       int (*mapf) (void *, int, VALUE_AREA *, API_VALUE *),
		       void *arg);
static int ai_api_insert (VALUE_INDEXER * indexer, int index,
			  VALUE_AREA * va, API_VALUE * dval);
static int ai_api_delete (VALUE_INDEXER * indexer, int index,
			  VALUE_AREA ** rva, API_VALUE ** dbval);
static void ai_api_destroy (VALUE_INDEXER * indexer,
			    void (*df) (VALUE_AREA * va, API_VALUE * db));

/* linked list based VALUE_INDEXER implementation */
static int li_api_check (VALUE_INDEXER * indexer, int index,
			 CHECK_PURPOSE pup);
static int li_api_length (VALUE_INDEXER * indexer, int *len);
static int li_api_get (VALUE_INDEXER * indexer, int index,
		       VALUE_AREA ** rva, API_VALUE ** rv);
static int li_api_set (VALUE_INDEXER * indexer, int index,
		       VALUE_AREA * va, API_VALUE * val);
static int li_api_map (VALUE_INDEXER * indexer,
		       int (*mapf) (void *, int, VALUE_AREA *, API_VALUE *),
		       void *arg);
static int li_api_insert (VALUE_INDEXER * indexer, int index,
			  VALUE_AREA * va, API_VALUE * dval);
static int li_api_delete (VALUE_INDEXER * indexer, int index,
			  VALUE_AREA ** rva, API_VALUE ** dbval);
static void li_api_destroy (VALUE_INDEXER * indexer,
			    void (*df) (VALUE_AREA * va, API_VALUE * db));

/* ------------------------------------------------------------------------- */
/* array based VALUE_INDEXER implementation */

/*
 * ai_api_check - check if the given index is valid for the CHECK_PURPOSE
 *    return: NO_ERROR if successful, error code otherwise
 *    indexer(in): VALUE_INDEXER
 *    index(in): index
 *    pup(in): CHECK_PURPOSE
 */
static int
ai_api_check (VALUE_INDEXER * indexer, int index, CHECK_PURPOSE pup)
{
  ARRAY_INDEXER *ai = (ARRAY_INDEXER *) indexer;

  assert (ai != NULL);

  if (index < 0 || index >= ai->nvalue)
    {
      return ER_INTERFACE_GENERIC;
    }

  return NO_ERROR;
}

/*
 * ai_api_length - get number of elements in the indexer
 *    return: NO_ERROR if successful, error code otherwise
 *    indexer(in): VALUE_INDEXER
 *    len(out): number of elements
 */
static int
ai_api_length (VALUE_INDEXER * indexer, int *len)
{
  ARRAY_INDEXER *ai = (ARRAY_INDEXER *) indexer;
  assert (ai != NULL);
  assert (len != NULL);
  *len = ai->nvalue;
  return NO_ERROR;
}

/*
 * ai_api_get - get element
 *    return: NO_ERROR if successful, error code otherwise
 *    indexer(in): VALUE_INDEXER
 *    index(in): index of the item
 *    rva(out): VALUE_AREA
 *    rv(out): API_VALUE
 */
static int
ai_api_get (VALUE_INDEXER * indexer, int index,
	    VALUE_AREA ** rva, API_VALUE ** rv)
{
  ARRAY_INDEXER *ai = (ARRAY_INDEXER *) indexer;
  assert (ai != NULL);
  assert (rva != NULL);
  assert (rv != NULL);

  *rva = ai->vs_map[index];
  *rv = ai->values[index];
  return NO_ERROR;
}

/*
 * ai_api_set - set VALUE_AREA and API_VALUE to given index
 *    return: NO_ERROR if successful, error code otherwise
 *    indexer(in): VALUE_INDEXER
 *    index(int): index of the item
 *    va(in): pointer to VALUE_AREA
 *    val(in): pointer to API_VALUE
 */
static int
ai_api_set (VALUE_INDEXER * indexer, int index, VALUE_AREA * va,
	    API_VALUE * val)
{
  ARRAY_INDEXER *ai = (ARRAY_INDEXER *) indexer;
  assert (ai != NULL);
  ai->vs_map[index] = va;
  ai->values[index] = val;
  return NO_ERROR;
}

/*
 * ai_api_map - call mapf for each element in indexer in order
 *    return: NO_ERROR if successful, error code otherwise
 *    indexer(in): VALUE_INDEXER
 *    mapf(in): map function
 *    arg(in): argument of the mapf
 */
static int
ai_api_map (VALUE_INDEXER * indexer,
	    int (*mapf) (void *, int, VALUE_AREA *, API_VALUE *), void *arg)
{
  ARRAY_INDEXER *ai = (ARRAY_INDEXER *) indexer;
  int i, res;
  assert (ai != NULL);
  assert (mapf != NULL);
  for (i = 0; i < ai->nvalue; i++)
    {
      res = mapf (arg, i, ai->vs_map[i], ai->values[i]);
      if (res != NO_ERROR)
	return res;
    }
  return NO_ERROR;
}

/*
 * ai_api_insert - insert a new element after given index position
 *    return: NO_ERROR if successful, error code otherwise
 *    indexer(in): VALUE_INDEXER
 *    index(in): index of element
 *    va(in): pointer to VALUE_AREA
 *    dval(in): pointer to API_VALUE
 * NOTE
 * To insert the element to the top front of position the indexer, index -1 is used.
 */
static int
ai_api_insert (VALUE_INDEXER * indexer, int index, VALUE_AREA * va,
	       API_VALUE * dval)
{
  return ER_INTERFACE_NOT_SUPPORTED_OPERATION;
}

/*
 * ai_api_delete - delete existing element at the given position
 *    return: NO_ERROR if successful, error code otherwise
 *    indexer(in): VALUE_INDEXER
 *    index(in): index of the element
 *    rva(out): pointer to VALUE_AREA
 *    dbval(out): pointer to API_VALUE
 */
static int
ai_api_delete (VALUE_INDEXER * indexer, int index, VALUE_AREA ** rva,
	       API_VALUE ** dbval)
{
  return ER_INTERFACE_NOT_SUPPORTED_OPERATION;
}

/*
 * ai_api_destroy -destroy value indexer
 *    return: void
 *    indexer(in): VALUE_INDEXER
 *    df(in): element destroy function
 * 
 */
static void
ai_api_destroy (VALUE_INDEXER * indexer,
		void (*df) (VALUE_AREA * va, API_VALUE * db))
{
  ARRAY_INDEXER *ai = (ARRAY_INDEXER *) indexer;
  int i;

  assert (ai != NULL);

  if (df != NULL)
    {
      for (i = 0; i < ai->nvalue; i++)
	{
	  df (ai->vs_map[i], ai->values[i]);
	}
    }
  API_FREE (ai->vs_map);
  API_FREE (ai->values);
  API_FREE (ai);
}

static VALUE_INDEXER_IFS ARRAY_INDEXER_IFS_ = {
  ai_api_check,
  ai_api_length,
  ai_api_get,
  ai_api_set,
  ai_api_map,
  ai_api_insert,
  ai_api_delete,
  ai_api_destroy
};



/* ------------------------------------------------------------------------- */
/* linked list based VALUE_INDEXER implementation */

/*
 * li_api_check - check if the given index is valid for the CHECK_PURPOSE
 *    return: NO_ERROR if successful, error code otherwise
 *    indexer(in): VALUE_INDEXER
 *    index(in): index
 *    pup(in): CHECK_PURPOSE
 */
static int
li_api_check (VALUE_INDEXER * indexer, int index, CHECK_PURPOSE pup)
{
  LIST_INDEXER *li = (LIST_INDEXER *) indexer;
  int valid_index = 0;
  assert (li != NULL);

  valid_index = index >= 0 && index < li->nelems;
  if ((pup & CHECK_FOR_INSERT) && !valid_index && index == -1)
    valid_index = 1;

  if (!valid_index)
    return ER_INTERFACE_GENERIC;
  else
    return NO_ERROR;
}

/*
 * li_api_length - get number of elements in the indexer
 *    return: NO_ERROR if successful, error code otherwise
 *    indexer(in): VALUE_INDEXER
 *    len(out): number of elements
 */
static int
li_api_length (VALUE_INDEXER * indexer, int *len)
{
  LIST_INDEXER *li = (LIST_INDEXER *) indexer;
  assert (li != NULL);
  assert (len != NULL);
  *len = li->nelems;
  return NO_ERROR;
}

/*
 * li_getf - get element at the given index position
 *    return: NO_ERROR if successful, error code otherwise
 *    li(in): LIST_INDEXER instance
 *    index(in): index
 * NOTE
 *    cache_idx and cache_elem is used and re-adjusted
 */
static LIST_INDEXER_ELEM *
li_getf (LIST_INDEXER * li, int index)
{
  dlisth *h;
  int ds, dc, de;
  int i, inc;

  assert (li != NULL);
  assert (index >= 0 && index < li->nelems);

  /* fast check */
  if (index == li->cache_idx)
    return li->cache_elem;
  else if (index == 0)
    {
      li->cache_idx = index;
      li->cache_elem = (LIST_INDEXER_ELEM *) (li->elems.next);
      return li->cache_elem;
    }
  else if (index == li->nelems - 1)
    {
      li->cache_idx = index;
      li->cache_elem = (LIST_INDEXER_ELEM *) (li->elems.prev);
      return li->cache_elem;
    }

  /* list traverse */
  ds = index;
  dc = index > li->cache_idx ? index - li->cache_idx : li->cache_idx - index;
  de = li->nelems - index;
  /* 
   * find the minimum element from {ds, dc, de} and 
   * calcuate the direction form the element to index
   */
  if (ds > dc)
    {
      if (de > dc)
	{
	  i = li->cache_idx;
	  inc = index > li->cache_idx ? 1 : -1;
	  h = (dlisth *) li->cache_elem;
	}
      else
	{
	  i = li->nelems - 1;
	  inc = -1;
	  h = li->elems.prev;	/* last elem */
	}
    }
  else
    {
      if (de > ds)
	{
	  i = 0;
	  inc = 1;
	  h = li->elems.next;	/* first elem */
	}
      else
	{
	  i = li->nelems - 1;
	  inc = -1;
	  h = li->elems.prev;
	}
    }
  assert (i > index ? inc == -1 : inc == 1);
  while (i != index)
    {
      if (inc > 0)
	h = h->next;
      else
	h = h->prev;
      i += inc;
    }

  assert (h != &li->elems);
  li->cache_idx = index;
  li->cache_elem = (LIST_INDEXER_ELEM *) h;
  return (LIST_INDEXER_ELEM *) h;
}


/*
 * li_api_get - get element
 *    return: NO_ERROR if successful, error code otherwise
 *    indexer(in): VALUE_INDEXER
 *    index(in): index of the item
 *    rva(out): VALUE_AREA
 *    rv(out): API_VALUE
 */
static int
li_api_get (VALUE_INDEXER * indexer, int index,
	    VALUE_AREA ** rva, API_VALUE ** rv)
{
  LIST_INDEXER *li = (LIST_INDEXER *) indexer;
  LIST_INDEXER_ELEM *e;

  assert (li != NULL);
  assert (index >= 0 && index < li->nelems);
  assert (rva != NULL);
  assert (rv != NULL);

  e = li_getf (li, index);
  assert (e != NULL);
  *rva = e->va;
  *rv = e->value;
  return NO_ERROR;
}

/*
 * li_api_set - set VALUE_AREA and API_VALUE to given index
 *    return: NO_ERROR if successful, error code otherwise
 *    indexer(in): VALUE_INDEXER
 *    index(int): index of the item
 *    va(in): pointer to VALUE_AREA
 *    val(in): pointer to API_VALUE
 */
static int
li_api_set (VALUE_INDEXER * indexer, int index, VALUE_AREA * va,
	    API_VALUE * dval)
{
  LIST_INDEXER *li = (LIST_INDEXER *) indexer;
  LIST_INDEXER_ELEM *e;

  assert (li != NULL);
  assert (index >= 0 && index < li->nelems);

  e = li_getf (li, index);
  assert (e != NULL);
  e->va = va;
  e->value = dval;
  return NO_ERROR;
}

struct li_mapf_arg
{
  int (*mapf) (void *, int, VALUE_AREA *, API_VALUE *);
  void *arg;
  int index;
};

/*
 * li_mapf - this is a helper function of li_api_map (type is dlist_map_func) 
 *    return: NO_ERROR if successful, error code otherwise
 *    h(h): dlisth
 *    arg(in): linked list element
 *    cont(out): continuation marker
 */
static int
li_mapf (dlisth * h, void *arg, int *cont)
{
  LIST_INDEXER_ELEM *e = (LIST_INDEXER_ELEM *) h;
  struct li_mapf_arg *ar = (struct li_mapf_arg *) arg;
  int res;
  assert (e != NULL);
  assert (ar != NULL);
  res = ar->mapf (ar->arg, ar->index, e->va, e->value);
  if (res == NO_ERROR)
    {
      *cont = 1;
      ar->index++;
      return NO_ERROR;
    }
  *cont = 0;
  return res;
}

/*
 * li_api_map - call mapf for each element in indexer in order
 *    return: NO_ERROR if successful, error code otherwise
 *    indexer(in): VALUE_INDEXER
 *    mapf(in): map function
 *    arg(in): argument of the mapf
 */
static int
li_api_map (VALUE_INDEXER * indexer,
	    int (*mapf) (void *, int, VALUE_AREA *, API_VALUE *), void *arg)
{
  LIST_INDEXER *li = (LIST_INDEXER *) indexer;
  struct li_mapf_arg ARG;
  int res;

  assert (li != NULL);
  assert (mapf != NULL);

  ARG.mapf = mapf;
  ARG.arg = arg;
  ARG.index = 0;
  res = dlisth_map (&li->elems, li_mapf, &ARG);
  return res;
}

/*
 * li_api_delete - delete existing element at the given position
 *    return: NO_ERROR if successful, error code otherwise
 *    indexer(in): VALUE_INDEXER
 *    index(in): index of the element
 *    rva(out): pointer to VALUE_AREA
 *    dbval(out): pointer to API_VALUE
 */
static int
li_api_delete (VALUE_INDEXER * indexer, int index, VALUE_AREA ** rva,
	       API_VALUE ** dbval)
{
  LIST_INDEXER *li = (LIST_INDEXER *) indexer;
  LIST_INDEXER_ELEM *e;

  assert (li != NULL);
  assert (li->nelems > 0);
  assert (index >= 0 && index < li->nelems);
  assert (rva != NULL);
  assert (dbval != NULL);

  assert (li->cache_idx >= 0 && li->cache_elem != NULL);
  e = li_getf (li, index);
  assert (e != NULL);

  *rva = e->va;
  *dbval = e->value;
  li->nelems--;
  /* ajust cache */
  if (li->nelems > 0)
    {
      if (index < li->cache_idx)
	{
	  li->cache_idx--;
	}
      else if (index == li->cache_idx)
	{
	  if (index > 0)
	    {
	      li->cache_idx = index - 1;
	      li->cache_elem = (LIST_INDEXER_ELEM *) (((dlisth *) e)->prev);
	    }
	  else
	    {
	      li->cache_elem = (LIST_INDEXER_ELEM *) (((dlisth *) e)->next);
	    }
	}
    }
  else
    {
      li->cache_idx = -1;
      li->cache_elem = NULL;
    }
  dlisth_delete ((dlisth *) e);
  API_FREE (e);
  return NO_ERROR;
}

/*
 * li_api_insert - insert a new element after given index position
 *    return: NO_ERROR if successful, error code otherwise
 *    indexer(in): VALUE_INDEXER
 *    index(in): index of element
 *    va(in): pointer to VALUE_AREA
 *    dval(in): pointer to API_VALUE
 * NOTE
 * To insert the element to the top front of position the indexer, index -1 is used.
 */
static int
li_api_insert (VALUE_INDEXER * indexer, int index, VALUE_AREA * va,
	       API_VALUE * dval)
{
  LIST_INDEXER *li = (LIST_INDEXER *) indexer;
  LIST_INDEXER_ELEM *e;
  dlisth *h;

  assert (li != NULL);
  assert (index >= -1 && index < li->nelems);

  e = API_MALLOC (sizeof (*e));
  if (e == NULL)
    return ER_INTERFACE_NO_MORE_MEMORY;
  dlisth_init (&e->header);
  e->va = va;
  e->value = dval;
  if (index == -1)
    h = &li->elems;
  else
    h = (dlisth *) li_getf (li, index);
  assert (h != NULL);
  dlisth_insert_after ((dlisth *) e, h);
  li->nelems++;
  /* ajust cache */
  if (index == -1 && li->cache_idx == -1)
    {
      li->cache_idx = 0;
      li->cache_elem = e;
    }
  else if (index < li->cache_idx)
    {
      li->cache_idx++;
    }
  return NO_ERROR;
}

/*
 * li_api_destroy - destroy value indexer
 *    return: void
 *    indexer(in): VALUE_INDEXER
 *    df(in): element destroy function
 */
static void
li_api_destroy (VALUE_INDEXER * indexer,
		void (*df) (VALUE_AREA * va, API_VALUE * db))
{
  LIST_INDEXER *li = (LIST_INDEXER *) indexer;

  assert (li != NULL);
  while (!dlisth_is_empty (&li->elems))
    {
      LIST_INDEXER_ELEM *e = (LIST_INDEXER_ELEM *) li->elems.next;
      if (df)
	df (e->va, e->value);
      dlisth_delete ((dlisth *) e);
      API_FREE (e);
    }
  API_FREE (li);
}

static VALUE_INDEXER_IFS LIST_INDEXER_IFS_ = {
  li_api_check,
  li_api_length,
  li_api_get,
  li_api_set,
  li_api_map,
  li_api_insert,
  li_api_delete,
  li_api_destroy
};

/* ------------------------------------------------------------------------- */
/* EXPORTED FUNCTION */

/*
 * array_indexer_create - create a new array indexer
 *    return: NO_ERROR if successful, error code otherwise
 *    nvalue(in): number of elements in the indexer
 *    rvi(out): pointer to VALUE_INDEXER
 * NOTE
 *   array value indexer has fixed elements of (NULL, NULL) VALUE_AREA and
 *   API_VALUE pair initially. insert() and delete() operation is not defined.
 */
int
array_indexer_create (int nvalue, VALUE_INDEXER ** rvi)
{
  ARRAY_INDEXER *ai;

  assert (rvi != NULL);
  assert (nvalue > 0);
  ai = API_MALLOC (sizeof (*ai));
  if (ai == NULL)
    return ER_INTERFACE_NO_MORE_MEMORY;
  ai->indexer.ifs = &ARRAY_INDEXER_IFS_;
  ai->nvalue = nvalue;
  ai->vs_map = API_CALLOC (nvalue, sizeof (void *));
  if (ai->vs_map == NULL)
    {
      API_FREE (ai);
      return ER_INTERFACE_NO_MORE_MEMORY;
    }
  ai->values = API_CALLOC (nvalue, sizeof (API_VALUE *));
  if (ai->values == NULL)
    {
      API_FREE (ai->vs_map);
      API_FREE (ai);
      return ER_INTERFACE_NO_MORE_MEMORY;
    }
  *rvi = (VALUE_INDEXER *) ai;
  return NO_ERROR;
}

/*
 * list_indexer_create - create a new list based indexer
 *    return: NO_ERROR if successful, error code otherwise
 *    rvi(out): pointer to the VALUE_INDEXER
 */
int
list_indexer_create (VALUE_INDEXER ** rvi)
{
  LIST_INDEXER *li;
  assert (rvi != NULL);
  li = API_MALLOC (sizeof (*li));
  if (li == NULL)
    return ER_INTERFACE_NO_MORE_MEMORY;
  li->indexer.ifs = &LIST_INDEXER_IFS_;
  li->nelems = 0;
  dlisth_init (&li->elems);
  li->cache_idx = -1;
  li->cache_elem = NULL;
  *rvi = (VALUE_INDEXER *) li;
  return NO_ERROR;
}
