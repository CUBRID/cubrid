/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */

/*
 * api_handle.h 
 */

#include "config.h"
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include "api_handle.h"
#include "api_util.h"
#include "error_code.h"

/* ------------------------------------------------------------------------- */
/* BIND HANDLE ROOT IMPLEMENTATION 
 */
typedef struct bh_root_s BH_ROOT;
typedef struct bhifs_node_s BHIFS_NODE;
typedef struct bh_context_fe_s bh_context_fe;
typedef struct bh_context_be_s bh_context_be;
typedef struct sh_context_be_s sh_context_be;

struct bh_root_s
{
  bh_provider provider;
  BIND_HANDLE rrv;
  API_MUTEX mutex;
  int rrid;			/* const after initialization */
  BH_ROOT *free_link;
  BH_INTERFACE *bhifs;
};

struct bhifs_node_s
{
  dlisth head;			/* must be the first member */
  BIND_HANDLE handle;		/* key */
  BH_BIND *bind;
  BHIFS_NODE *parent;		/* parent node */
  dlisth children;		/* head for children node */
};

#define bhifs_node_init(n,h,b) \
  do { \
    dlisth_init(&(n)->head); \
    (n)->handle = (h); \
    (n)->bind = (b); \
    (b)->bptr = (n); \
    (n)->parent = NULL; \
    dlisth_init(&(n)->children); \
  } while (0)

#define bhifs_node_prune(n) \
  do { \
    dlisth_delete((dlisth *)(n)); \
    (n)->parent = NULL; \
  } while (0)

#define bhifs_node_prune_and_register_root(fe,n) \
  do { \
    bhifs_node_prune(n); \
    dlisth_insert_after((dlisth *)(n),&(fe)->root_handles); \
  } while(0)

/* BH_INTERFACE implementation */
struct bh_context_fe_s
{
  BH_INTERFACE bhifs;		/* should be the first member */
  bh_provider *handle_provider;
  dlisth root_handles;
  bh_context_be *be;
};

/* BIND_HANDLE to BHIFS_NODE mapping abstract structure */
struct bh_context_be_s
{
  void (*destroy) (bh_context_be * be);
  int (*lookup) (bh_context_be * be, BIND_HANDLE handle, BHIFS_NODE ** node);
  int (*insert) (bh_context_be * be, BHIFS_NODE * const node);
  int (*delete) (bh_context_be * be, BIND_HANDLE handle, BHIFS_NODE ** node);
};

/* hash table based bh_context_be implementation */
struct sh_context_be_s
{
  bh_context_be be;
  int bucket_sz;
  hash_table *ht;
};

#define RR_LOCK() API_LOCK(&rr_mutex)
#define RR_UNLOCK() API_UNLOCK(&rr_mutex)
#define MAX_NUM_ROOTS 1024
/*
 * bind handle structure (64 bits)
 *  - rid (16 bits) : used to identify resource root
 *  - id (48 bits) : used to identify handle within a resource root
 */
#define BH_RID_BITS        16
#define BH_RID_OFFSET      48
#define BH_RID_MASK        0xffff000000000000ULL
#define BH_RID_FILTER(h)   (0xffffULL & (h))

#define BH_ID_BITS         48
#define BH_ID_OFFSET       0
#define BH_ID_MASK         0x0000ffffffffffffULL
#define BH_ID_FILTER(h)    (0xffffffffffffULL & (h))

#define BH_MAKE(rr_,id_) \
  (((BH_RID_FILTER(rr_)<< BH_RID_OFFSET) & BH_RID_MASK) | \
   ((BH_ID_FILTER(id_)<<BH_ID_OFFSET) & BH_ID_MASK))

#define BH_GET_RID(h) \
  BH_RID_FILTER(((h)&BH_RID_MASK)>>BH_RID_OFFSET)

#define BH_SET_RID(h,r) \
  h = (((r)<<BH_RID_OFFSET)&BH_RID_MASK) | ((h) & ~BH_RID_MASK)

#define BH_GET_ID(h) \
  BH_ID_FILTER(((h)&BH_ID_MASK)>>BH_ID_OFFSET)

#define BH_SET_ID(h,r) \
  h = (((r)<<BH_ID_OFFSET)&BH_ID_MASK) | ((h) & ~BH_ID_MASK)

/* BH_ROOT related functions */
static int rr_next_handle (bh_provider * bh, BIND_HANDLE * rv);
static int rr_lazy_init (void);
static int bri_init_root (void);
static int bri_alloc_root (int *rrid, BH_ROOT ** root);
static int bri_access_root (int rrid, BH_ROOT ** root);

/* BH_INTERFACE implementation */
static int fe_alloc_handle (BH_INTERFACE * bhifs, BH_BIND * bind,
			    BIND_HANDLE * bh);
static int fe_destroy_handle_worker (bh_context_fe * fe, BHIFS_NODE * node);
static int fe_destroy_handle (BH_INTERFACE * bhifs, BIND_HANDLE bh);
static int fe_lookup (BH_INTERFACE * bhifs, BIND_HANDLE bh, BH_BIND ** bind);
static int fe_bind_to_handle (BH_INTERFACE * bhifs, BH_BIND * bind,
			      BIND_HANDLE * bh);
static int fe_bind_get_parent (BH_INTERFACE * bhifs, BH_BIND * bind,
			       BH_BIND ** pbind);
static int fe_bind_prune (BH_INTERFACE * bhifs, BH_BIND * bind);
static int fe_bind_graft (BH_INTERFACE * bhifs, BH_BIND * bind,
			  BH_BIND * pbind);
static int fe_bind_get_first_child (BH_INTERFACE * bhifs, BH_BIND * bind,
				    BH_BIND ** pchild);
static int fe_bind_get_next_sibling (BH_INTERFACE * bhifs, BH_BIND * bind,
				     BH_BIND ** psibling);
static int fe_bind_map_worker (bh_context_fe * fe, BHIFS_NODE * node,
			       bh_mapf mf, void *arg);
static int fe_bind_map (BH_INTERFACE * bhifs, BH_BIND * bind, bh_mapf mf,
			void *arg);
static void fe_destroy (BH_INTERFACE * bhifs);
/* static hash based bh_context_be implementaiton */
static int sh_comparef (void *key1, void *key2, int *rc);
static int sh_hashf (void *key, unsigned int *rv);
static int sh_keyf (void *elem, void **rk);
static void sh_destroy (bh_context_be * be);
static int sh_lookup (bh_context_be * be, BIND_HANDLE handle,
		      BHIFS_NODE ** node);
static int sh_insert (bh_context_be * be, BHIFS_NODE * const node);
static int sh_delete (bh_context_be * be, BIND_HANDLE handle,
		      BHIFS_NODE ** node);
static int be_create_static_hash (bh_context_be ** be, int bucket_sz);

/* static variables */
static API_MUTEX rr_mutex = API_MUTEX_INITIALIZER;
static int rr_initialized = 0;
static BH_ROOT *rr_free_list;
static BH_ROOT Roots[MAX_NUM_ROOTS];
static int rr_alloc_count = 0;

/*
 * rr_next_handle - get next handle value
 *    return: NO_ERROR
 *    bh(in): BH_ROOT pointer
 *    rv(out): next handle value
 */
static int
rr_next_handle (bh_provider * bh, BIND_HANDLE * rv)
{
  BH_ROOT *root;
  assert (bh != NULL);
  assert (rv != NULL);
  root = (BH_ROOT *) bh;
  root->rrv++;
  *rv = BH_MAKE (root->rrid, root->rrv);
  return NO_ERROR;
}

/*
 * rr_lazy_init - lazy initialize resource root module
 *    return: NO_ERROR if successful, error_code otherwise
 */
static int
rr_lazy_init (void)
{
  int res;

  if (rr_initialized)
    return NO_ERROR;

  RR_LOCK ();
  if (rr_initialized)
    {
      RR_UNLOCK ();
      return NO_ERROR;
    }
  if ((res = bri_init_root ()) != NO_ERROR)
    {
      RR_UNLOCK ();
      return res;
    }
  rr_free_list = NULL;
  rr_initialized = 1;
  RR_UNLOCK ();

  return NO_ERROR;
}

/*
 * rr_init_root - init resource root structure
 *    return: void
 *    root(in): BH_ROOT structure to be initialized
 */
static void
rr_init_root (BH_ROOT * root, int i)
{
  root->provider.next_handle = rr_next_handle;
  root->rrv = 0LL;
  API_MUTEX_INIT (&root->mutex);
  root->rrid = i;
  root->free_link = NULL;
  root->bhifs = NULL;
}

/*
 * bri_init_root - init array based root allcation
 *    return: NO_ERROR
 */
static int
bri_init_root (void)
{
  return NO_ERROR;
}

/*
 * bri_alloc_root - allocate new resource root id and BH_ROOT structure
 *    return: NO_ERROR if successful, error code otherwise
 *    rrid(out): resource root id
 *    root(out): BH_ROOT structure allcated
 */
static int
bri_alloc_root (int *rrid, BH_ROOT ** root)
{
  if (rr_alloc_count < MAX_NUM_ROOTS)
    {
      rr_init_root (&Roots[rr_alloc_count], rr_alloc_count);
      *rrid = rr_alloc_count++;
      *root = &Roots[*rrid];
      return NO_ERROR;
    }
  return ER_INTERFACE_TOO_MANY_CONNECTION;
}

/*
 * bri_access_root - get pointer to BH_ROOT indexed by rrid
 *    return: NO_ERROR
 *    rrid(in): resource root id
 *    root(out): pointer to BH_ROOT structure
 */
static int
bri_access_root (int rrid, BH_ROOT ** root)
{
  if (rrid < 0 || rrid >= rr_alloc_count)
    return ER_INTERFACE_INVALID_HANDLE;
  *root = &Roots[rrid];
  return NO_ERROR;
}


/*
 * fe_alloc_handle - allocate a new handle for the given bind
 *    return: NO_ERROR if successful, error code otherwise
 *    bhifs(in): BH_INTERFACE
 *    bind(in): BH_BIND structure
 *    bh(out): handle allcated
 */
static int
fe_alloc_handle (BH_INTERFACE * bhifs, BH_BIND * bind, BIND_HANDLE * bh)
{
  bh_context_fe *fe;
  BHIFS_NODE *node;
  BIND_HANDLE handle;
  int res;

  assert (bhifs != NULL);
  assert (bh != NULL);

  fe = (bh_context_fe *) bhifs;
  if ((res =
       fe->handle_provider->next_handle (fe->handle_provider,
					 &handle)) != NO_ERROR)
    return res;

  if ((node = API_MALLOC (sizeof (*node))) == NULL)
    return ER_INTERFACE_NO_MORE_MEMORY;
  bhifs_node_init (node, handle, bind);

  if ((res = fe->be->insert (fe->be, node)) != NO_ERROR)
    {
      API_FREE (node);
      return res;
    }

  dlisth_insert_after ((dlisth *) node, &fe->root_handles);
  *bh = handle;
  return NO_ERROR;
}

/*
 * fe_destroy_handle_worker - destroy BHIFS_NODE recursively
 *    return: NO_ERROR if successful, error code otherwise
 *    fe(in): bh_context_fe structure
 *    node(in): node to destroy
 */
static int
fe_destroy_handle_worker (bh_context_fe * fe, BHIFS_NODE * node)
{
  BHIFS_NODE *tmp;
  int res;

  while (!dlisth_is_empty (&node->children))
    {
      BHIFS_NODE *n = (BHIFS_NODE *) node->children.next;
      res = fe_destroy_handle_worker (fe, n);
      if (res != NO_ERROR)
	return res;
    }

  if ((res = fe->be->delete (fe->be, node->handle, &tmp)) != NO_ERROR)
    return res;
  assert (tmp == node);

  if (node->bind != NULL && node->bind->dtor != NULL)
    node->bind->dtor (node->bind);

  bhifs_node_prune (node);
  API_FREE (node);

  return NO_ERROR;
}

/*
 * fe_destroy_handle - destroy given handle
 *    return: NO_ERROR if successful, error code otherwise
 *    bhifs(in): BH_INTERFACE
 *    bh(in): handle
 */
static int
fe_destroy_handle (BH_INTERFACE * bhifs, BIND_HANDLE bh)
{
  bh_context_fe *fe;
  BHIFS_NODE *node;
  int res;

  assert (bhifs != NULL);
  fe = (bh_context_fe *) bhifs;

  if ((res = fe->be->lookup (fe->be, bh, &node)) != NO_ERROR)
    return res;

  return fe_destroy_handle_worker (fe, node);
}

/*
 * fe_lookup - lookup BH_BIND * corresponding to BH_HANDLE
 *    return: NO_ERROR if successful, error code otherwise
 *    bhifs(in): BH_INTERFACE pointer
 *    bh(in): BIND_HANDLE
 *    bind(out): BH_BIND pointer
 */
static int
fe_lookup (BH_INTERFACE * bhifs, BIND_HANDLE bh, BH_BIND ** bind)
{
  bh_context_fe *fe;
  BHIFS_NODE *node;
  int res;

  assert (bhifs != NULL);
  assert (bind != NULL);

  fe = (bh_context_fe *) bhifs;
  if ((res = fe->be->lookup (fe->be, bh, &node)) != NO_ERROR)
    return res;

  *bind = node->bind;
  return NO_ERROR;
}

/*
 * fe_bind_to_handle - get BIND_HANDLE from BH_BIND
 *    return: NO_ERROR if successful, error code otherwise
 *    bhifs(in): BH_INTERFACE
 *    bind(in): BH_BIND
 *    bh(out): BIND_HANDLE
 */
static int
fe_bind_to_handle (BH_INTERFACE * bhifs, BH_BIND * bind, BIND_HANDLE * bh)
{
  BHIFS_NODE *node;

  assert (bhifs != NULL);
  assert (bind != NULL);
  assert (bh != NULL);
  assert (bind->bptr != NULL);

  node = (BHIFS_NODE *) bind->bptr;
  *bh = node->handle;
  return NO_ERROR;
}

/*
 * fe_bind_get_parent - get parent BH_BIND of the given BH_BIND
 *    return: NO_ERROR if successful, error code otherwise
 *    bhifs(in): BH_INTERFACE
 *    bind(in): BH_BIND
 *    pbind(out): parent BH_BIND
 */
static int
fe_bind_get_parent (BH_INTERFACE * bhifs, BH_BIND * bind, BH_BIND ** pbind)
{
  BHIFS_NODE *node, *parent_node;

  assert (bhifs != NULL);
  assert (bind != NULL);
  assert (pbind != NULL);

  node = (BHIFS_NODE *) bind->bptr;
  assert (node != NULL);
  parent_node = node->parent;
  if (parent_node)
    *pbind = parent_node->bind;
  else
    *pbind = NULL;
  return NO_ERROR;
}

/*
 * fe_bind_prune - detach BH_BIND from the parent
 *    return: NO_ERROR if successful, error code otherwise
 *    bhifs(in): BH_INTERFACE
 *    bind(in): BH_BIND to prune
 */
static int
fe_bind_prune (BH_INTERFACE * bhifs, BH_BIND * bind)
{
  BHIFS_NODE *node;
  bh_context_fe *fe;

  assert (bhifs != NULL);
  assert (bind != NULL);
  assert (bind->bptr != NULL);

  node = (BHIFS_NODE *) bind->bptr;
  fe = (bh_context_fe *) bhifs;

  bhifs_node_prune_and_register_root ((bh_context_fe *) bhifs, node);
  return NO_ERROR;
}

/*
 * fe_bind_graft - graft BH_BIND to another BH_BIND
 *    return: NO_ERROR if successful, error code otherwise
 *    bhifs(in): BH_INTERFACE
 *    bind(in): child BH_BIND
 *    pbind(in): parent BH_BIND
 */
static int
fe_bind_graft (BH_INTERFACE * bhifs, BH_BIND * bind, BH_BIND * pbind)
{
  BHIFS_NODE *node, *parent_node;
  bh_context_fe *fe;

  assert (bhifs != NULL);
  assert (bind != NULL);
  assert (bind->bptr != NULL);
  assert (pbind != NULL);
  assert (pbind->bptr != NULL);
  assert (bind != pbind);

  fe = (bh_context_fe *) bhifs;
  node = (BHIFS_NODE *) bind->bptr;
  parent_node = (BHIFS_NODE *) pbind->bptr;
  bhifs_node_prune (node);
  dlisth_insert_before ((dlisth *) node, &parent_node->children);
  node->parent = parent_node;
  return NO_ERROR;
}

/*
 * fe_bind_get_first_child - get first child of given bind
 *    return: NO_ERROR if successful, error_code otherwise
 *    bhifs(in): BH_INTERFACE
 *    bind(in): BH_BIND
 *    pchild(out): child BH_BIND
 */
static int
fe_bind_get_first_child (BH_INTERFACE * bhifs, BH_BIND * bind,
			 BH_BIND ** pchild)
{
  BHIFS_NODE *node, *child;

  assert (bhifs != NULL);
  assert (bind != NULL);
  assert (pchild != NULL);

  node = (BHIFS_NODE *) bind->bptr;
  assert (node != NULL);
  if (dlisth_is_empty (&node->children))
    {
      *pchild = NULL;
      return NO_ERROR;
    }
  child = (BHIFS_NODE *) node->children.next;
  assert (child->bind != NULL);
  *pchild = child->bind;
  return NO_ERROR;
}

/*
 * fe_bind_get_next_sibling - get next sibling of the given BH_BIND
 *    return: NO_ERROR if successful, error code otherwise
 *    bhifs(in): BH_INTERFACE
 *    bind(in): BH_BIND
 *    psibling(out): next sibling 
 */
static int
fe_bind_get_next_sibling (BH_INTERFACE * bhifs, BH_BIND * bind,
			  BH_BIND ** psibling)
{
  BHIFS_NODE *node, *sibling;

  assert (bhifs != NULL);
  assert (bind != NULL);
  assert (psibling != NULL);

  node = (BHIFS_NODE *) bind->bptr;
  assert (node != NULL);
  if (node->parent == NULL || node->head.next == &node->parent->children)
    {
      *psibling = NULL;
      return NO_ERROR;
    }
  sibling = (BHIFS_NODE *) node->head.next;
  assert (sibling->bind != NULL);
  *psibling = sibling->bind;
  return NO_ERROR;
}

/*
 * fe_bind_map_worker - worker function for the fe_bind_map()
 *    return: NO_ERROR if successful, error code otherwise
 *    fe(in): BH_INTERFACE
 *    node(in): BH_BIND 
 *    mf(in): map function
 *    arg(in): argument of the map function
 */
static int
fe_bind_map_worker (bh_context_fe * fe, BHIFS_NODE * node, bh_mapf mf,
		    void *arg)
{
  dlisth *h;

  for (h = node->children.next; h != &node->children; h = h->next)
    {
      BHIFS_NODE *n = (BHIFS_NODE *) h;
      int res = fe_bind_map_worker (fe, n, mf, arg);
      if (res != NO_ERROR)
	return res;
    }
  return mf ((BH_INTERFACE *) fe, node->bind, arg);
}

/*
 * fe_bind_map - call map function for each decendenat BH_BIND node.
 *               in post order.
 *    return: NO_ERROR if successful, error code otherwise
 *    bhifs(in): BH_INTERFACE
 *    bind(in): BH_BIND to map
 *    mf(in): map function
 *    arg(in): map function argument
 */
static int
fe_bind_map (BH_INTERFACE * bhifs, BH_BIND * bind, bh_mapf mf, void *arg)
{
  assert (bhifs != NULL);
  assert (bind != NULL);
  assert (mf != NULL);
  assert (bind->bptr != NULL);

  return fe_bind_map_worker ((bh_context_fe *) bhifs,
			     (BHIFS_NODE *) bind->bptr, mf, arg);
}

/*
 * fe_destroy - destroy BH_INTERFACE
 *    return: void
 *    bhifs(in): BH_INTERFACE
 */
static void
fe_destroy (BH_INTERFACE * bhifs)
{
  bh_context_fe *fe;
  dlisth *h;
  int res;

  assert (bhifs != NULL);
  fe = (bh_context_fe *) bhifs;

  while ((h = fe->root_handles.next) != &fe->root_handles)
    {
      BHIFS_NODE *n = (BHIFS_NODE *) h;
      bhifs_node_prune (n);
      res = fe_destroy_handle_worker (fe, n);

      if (res != NO_ERROR)
	continue;
    }

  fe->be->destroy (fe->be);
  API_FREE (fe);
  return;
}


/*
 * sh_comparef - hash compare function
 *    return: NO_ERROR
 *    key1(in): pointer to the key (BIND_HANDLE)
 *    key2(in): pointer to another key (BIND_HANDLE)
 *    rc(out): compare result (1, 0, -1)
 */
static int
sh_comparef (void *key1, void *key2, int *rc)
{
  BIND_HANDLE p1, p2;

  assert (key1 != NULL);
  assert (key2 != NULL);

  p1 = *(BIND_HANDLE *) key1;
  p2 = *(BIND_HANDLE *) key2;

  *rc = (p1 == p2) ? 0 : p1 < p2 ? -1 : 1;
  return NO_ERROR;
}

/*
 * sh_hashf - hash  hash function
 *    return: NO_ERROR
 *    key(in): pointer to the key (BIND_HANDLE)
 *    rv(out): hash value
 */
static int
sh_hashf (void *key, unsigned int *rv)
{
  BIND_HANDLE *p;

  assert (key != NULL);
  assert (rv != NULL);

  p = (BIND_HANDLE *) key;
  *rv = (unsigned int) (*p % UINT_MAX);
  return NO_ERROR;
}

/*
 * sh_keyf - hash key function. get handle fields from BHIFS_NODE
 *    return: NO_ERROR
 *    elem(in): pointer to the element (BHIFS_NODE)
 *    rk(in): pointer to the key
 */
static int
sh_keyf (void *elem, void **rk)
{
  BHIFS_NODE *node;

  assert (elem != NULL);
  assert (rk != NULL);

  node = (BHIFS_NODE *) elem;
  *rk = &node->handle;
  return NO_ERROR;
}

/*
 * sh_destroy - destroy hash based bh_context_be 
 *    return: void
 *    be(in): bh_context_be
 */
static void
sh_destroy (bh_context_be * be)
{
  sh_context_be *sbe;
  assert (be != NULL);
  sbe = (sh_context_be *) be;
  hash_destroy (sbe->ht, NULL);
  API_FREE (sbe);
  return;
}


/*
 * sh_lookup - lookup BHIFS_NODE for the BIND_HANDLE
 *    return: NO_ERROR if successful, error code otherwise
 *    be(in): bh_context_be pointer
 *    handle(in): BIND_HANDLE
 *    node(out): BHIFS_NODE found
 */
static int
sh_lookup (bh_context_be * be, BIND_HANDLE handle, BHIFS_NODE ** node)
{
  sh_context_be *sbe;
  assert (be != NULL);
  sbe = (sh_context_be *) be;
  return hash_lookup (sbe->ht, &handle, (void **) node);
}

/*
 * sh_insert - insert BHIFS_NODE to the bh_context_be
 *    return: NO_ERROR if successful, error code otherwise
 *    be(in): bh_context_be pointer
 *    node(in): BHIFS_NODE to insert
 */
static int
sh_insert (bh_context_be * be, BHIFS_NODE * const node)
{
  sh_context_be *sbe;
  assert (be != NULL);
  sbe = (sh_context_be *) be;
  return hash_insert (sbe->ht, (void *) node);
}

/*
 * sh_delete - delete hash entry for BIND_HANDLE and return BHIFS_NODE
 *             if exists
 *    return: NO_ERROR if successful, error code otherwise
 *    be(in): bh_context_be pointer
 *    handle(in): BIND_HANDLE
 *    node(out): BHIFS_NODE found
 */
static int
sh_delete (bh_context_be * be, BIND_HANDLE handle, BHIFS_NODE ** node)
{
  sh_context_be *sbe;
  assert (be != NULL);
  sbe = (sh_context_be *) be;
  return hash_delete (sbe->ht, &handle, (void **) node);
}

/*
 * be_create_static_hash - create static hash based bh_context_be interface
 *    return: NO_ERROR if successful, error code otherwise
 *    be(out): bh_context_be 
 *    bucket_sz(in): bucket size
 */
static int
be_create_static_hash (bh_context_be ** be, int bucket_sz)
{
  hash_table *ht;
  sh_context_be *sbe;
  int res;

  ht = NULL;
  res = hash_new (bucket_sz, sh_hashf, sh_keyf, sh_comparef, &ht);
  if (res != NO_ERROR)
    return res;

  if ((sbe = API_MALLOC (sizeof (*sbe))) == NULL)
    {
      hash_destroy (ht, NULL);
      return ER_INTERFACE_NO_MORE_MEMORY;
    }
  sbe->be.destroy = sh_destroy;
  sbe->be.lookup = sh_lookup;
  sbe->be.insert = sh_insert;
  sbe->be.delete = sh_delete;
  sbe->bucket_sz = bucket_sz;
  sbe->ht = ht;
  *be = (bh_context_be *) sbe;
  return NO_ERROR;
}

/*
 * bh_get_rid - get root id from BIND_HANDLE
 *    return: NO_ERROR
 *    bh(in): BIND_HANDL
 *    rid(out): root id
 */
int
bh_get_rid (BIND_HANDLE bh, int *rid)
{
  if (rid == NULL)
    return ER_INTERFACE_INVALID_HANDLE;
  *rid = BH_GET_RID (bh);
  return NO_ERROR;
}

/*
 * bh_root_acquire - create a new root id
 *    return: NO_ERROR if successful, error code otherwise
 *    rrid(out): root id
 *    rt(rt): BH_ROOT_TYPE
 */
int
bh_root_acquire (int *rrid, BH_ROOT_TYPE rt)
{
  int res;
  BH_ROOT *rr;
  BH_INTERFACE *bhifs;

  if (rrid == NULL)
    return ER_INTERFACE_INVALID_ARGUMENT;

  if ((res = rr_lazy_init ()) != NO_ERROR)
    return res;

  rr = NULL;
  res = NO_ERROR;

  RR_LOCK ();
  if (rr_free_list)
    {
      rr = rr_free_list;
      rr_free_list = rr_free_list->free_link;
      rr->free_link = NULL;
    }
  else
    res = bri_alloc_root (rrid, &rr);
  RR_UNLOCK ();

  if (res != NO_ERROR)
    return res;

  assert (rr != NULL);
  bhifs = NULL;

  res = create_handle_context ((bh_provider *) rr, rt, &bhifs);
  if (res != NO_ERROR)
    {
      RR_LOCK ();
      rr->free_link = rr_free_list;
      rr_free_list = rr;
      RR_UNLOCK ();
      return res;
    }
  API_LOCK (&rr->mutex);
  rr->bhifs = bhifs;
  *rrid = rr->rrid;
  API_UNLOCK (&rr->mutex);
  return res;
}

/*
 * bh_root_release - release root for the root id
 *    return: NO_ERROR if successful, error code otherwise
 *    rrid(in): root id
 */
int
bh_root_release (int rrid)
{
  BH_ROOT *root;
  BH_INTERFACE *bhifs;
  int res;

  if ((res = bri_access_root (rrid, &root)) != NO_ERROR)
    return res;

  RR_LOCK ();
  API_LOCK (&root->mutex);
  if (root->bhifs == NULL)
    {
      API_UNLOCK (&root->mutex);
      RR_UNLOCK ();
      return ER_INTERFACE_INVALID_HANDLE;
    }
  root->free_link = rr_free_list;
  rr_free_list = root;
  bhifs = root->bhifs;
  root->bhifs = NULL;
  API_UNLOCK (&root->mutex);
  RR_UNLOCK ();

  assert (bhifs != NULL);
  bhifs->destroy (bhifs);
  return NO_ERROR;
}

/*
 * bh_root_lock - try to lock the root identified by root id. if successful
 *                return BH_INTERFACE also via out parameter.
 *    return: NO_ERROR if successful, error code otherwise
 *    rrid(in): root id
 *    bhifs(out): BH_INTERFACE
 */
int
bh_root_lock (int rrid, BH_INTERFACE ** bhifs)
{
  BH_ROOT *root;
  int res;

  if (rr_initialized == 0)
    return ER_INTERFACE_GENERIC;

  if ((res = bri_access_root (rrid, &root)) != NO_ERROR)
    return res;

  res = API_TRYLOCK (&root->mutex);
  if (res != 0)
    {
      if (res == EBUSY)
	return ER_INTERFACE_HANDLE_TIMEOUT;
      else
	return ER_INTERFACE_GENERIC;
    }
  if (root->bhifs == NULL)
    {
      API_UNLOCK (&root->mutex);
      return ER_INTERFACE_INVALID_HANDLE;
    }
  if (bhifs)
    *bhifs = root->bhifs;

  return NO_ERROR;
}

/*
 * bh_root_unlock - unlock root 
 *    return: NO_ERROR if successful, error code otherwise
 *    rrid(in): root id
 */
int
bh_root_unlock (int rrid)
{
  BH_ROOT *root;
  int res;

  if (rr_initialized == 0)
    return ER_INTERFACE_GENERIC;
  if ((res = bri_access_root (rrid, &root)) != NO_ERROR)
    return res;

  API_UNLOCK (&root->mutex);

  return NO_ERROR;
}

/*
 * create_handle_context - create a BH_INTERFACE 
 *    return: NO_ERROR if successful, error code otherwise
 *    prov(in): bh_provider
 *    rt(in): BH_ROOT_TYPE
 *    bhifs(out): BH_INTERFACE created
 */
int
create_handle_context (bh_provider * prov, BH_ROOT_TYPE rt,
		       BH_INTERFACE ** bhifs)
{
  bh_context_fe *fe;
  bh_context_be *be;
  int res;

  assert (prov != NULL);
  assert (bhifs != NULL);

  be = NULL;
  switch (rt)
    {
    case BH_ROOT_TYPE_STATIC_HASH_64:
      res = be_create_static_hash (&be, 64);
      break;
    case BH_ROOT_TYPE_STATIC_HASH_128:
      res = be_create_static_hash (&be, 128);
      break;
    case BH_ROOT_TYPE_STATIC_HASH_1024:
      res = be_create_static_hash (&be, 1024);
      break;
    case BH_ROOT_TYPE_EXTENDIBLE_HASH:
    case BH_ROOT_TYPE_RB_TREE:
    default:
      return ER_INTERFACE_NOT_SUPPORTED_OPERATION;
    }
  if (res != NO_ERROR)
    return res;
  assert (be != NULL);

  if ((fe = API_MALLOC (sizeof (*fe))) == NULL)
    return ER_INTERFACE_NO_MORE_MEMORY;
  fe->bhifs.alloc_handle = fe_alloc_handle;
  fe->bhifs.destroy_handle = fe_destroy_handle;
  fe->bhifs.lookup = fe_lookup;
  fe->bhifs.bind_to_handle = fe_bind_to_handle;
  fe->bhifs.bind_get_parent = fe_bind_get_parent;
  fe->bhifs.bind_prune = fe_bind_prune;
  fe->bhifs.bind_graft = fe_bind_graft;
  fe->bhifs.bind_get_first_child = fe_bind_get_first_child;
  fe->bhifs.bind_get_next_sibling = fe_bind_get_next_sibling;
  fe->bhifs.bind_map = fe_bind_map;
  fe->bhifs.destroy = fe_destroy;
  fe->handle_provider = prov;
  dlisth_init (&fe->root_handles);
  fe->be = be;
  *bhifs = (BH_INTERFACE *) fe;
  return NO_ERROR;
}
