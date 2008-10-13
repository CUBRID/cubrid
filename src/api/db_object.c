#include "config.h"
#include <assert.h>
#include <stdlib.h>
#include "dbi.h"
#include "db_stub.h"
#include "api_util.h"

typedef struct object_rm_bind_s OBJECT_RM_BIND;
typedef struct object_res_bind_s OBJECT_RES_BIND;
typedef struct object_resultset_s_ OBJECT_RESULTSET;
typedef struct object_resultset_pool_s OBJECT_RESULTSET_POOL;

struct object_rm_bind_s
{
  API_RESULTSET_META rm;
  OBJECT_RESULTSET *or;
};

struct object_res_bind_s
{
  API_RESULTSET res;
  OBJECT_RESULTSET *or;
};

struct object_resultset_s_
{
  OID oid;
  BH_INTERFACE *bh_ifs;
  OBJECT_RES_BIND *res_bind;
  OBJECT_RM_BIND *rm_bind;
  int deleted;
  DB_OBJECT *obj;
  DB_OBJECT *clz;
  int nattrs;
  DB_ATTRIBUTE **attr_index;
  DB_ATTRIBUTE *attrs;
  BIND_HANDLE conn;
  VALUE_BIND_TABLE *vt;
};

struct object_resultset_pool_s
{
  API_OBJECT_RESULTSET_POOL pool;
  BH_INTERFACE *bh_ifs;
  BIND_HANDLE conn;
  hash_table *ht;
};

/* result set meta api */
static int rm_api_get_count (API_RESULTSET_META * impl, int *count);
static int rm_api_get_info (API_RESULTSET_META * impl, int index,
			    CI_RMETA_INFO_TYPE type, void *arg, size_t size);
static void or_rm_bind_destroyf (BH_BIND * bind);
static int
or_rm_bind_create (OBJECT_RESULTSET * or, OBJECT_RM_BIND ** rrm_bind);
static void or_rm_bind_destroy (OBJECT_RM_BIND * rm_bind);

/* result set api */
static int res_api_get_resultset_metadata (API_RESULTSET * impl,
					   API_RESULTSET_META ** rimpl);
static int res_api_fetch (API_RESULTSET * impl, int offset,
			  CI_FETCH_POSITION pos);
static int res_api_tell (API_RESULTSET * impl, int *offset);
static int res_api_clear_updates (API_RESULTSET * impl);
static int res_api_delete_row (API_RESULTSET * impl);
static int res_api_get_value (API_RESULTSET * impl, int index,
			      CI_TYPE type, void *addr, size_t len,
			      size_t * outlen, bool * is_null);
static int res_api_get_value_by_name (API_RESULTSET * impl,
				      const char *name, CI_TYPE type,
				      void *addr, size_t len,
				      size_t * outlen, bool * isnull);
static int res_api_update_value (API_RESULTSET * impl, int index,
				 CI_TYPE type, void *addr, size_t len);
static int res_api_apply_update (API_RESULTSET * impl);
static void res_api_destroy (API_RESULTSET * impl);
static void or_res_bind_destroyf (BH_BIND * bind);
static int or_res_bind_create (OBJECT_RESULTSET * or,
			       OBJECT_RES_BIND ** rres_bind);
static void or_res_bind_destroy (OBJECT_RES_BIND * res_bind);

/* value bind table interface */
static int vt_api_get_index_by_name (void *impl, const char *name, int *ri);
static int vt_api_get_db_value (void *impl, int index, DB_VALUE * dbval);
static int vt_api_set_db_value (void *impl, int index, DB_VALUE * dbval);
static int vt_api_init_domain (void *impl, int index, DB_VALUE * value);
/* object resultset */
static void or_destroy (OBJECT_RESULTSET * or);
static int or_create (OID * oid, BIND_HANDLE conn, BH_INTERFACE * bh_ifs,
		      OBJECT_RESULTSET ** ror);

/* object result set pool */
static int orp_ht_comparef (void *key1, void *key2, int *r);
static int orp_ht_hashf (void *key, unsigned int *rv);
static int orp_ht_keyf (void *elem, void **rk);
static int orp_api_get_object_resultset (API_OBJECT_RESULTSET_POOL *
					 pool, CI_OID * oid,
					 API_RESULTSET ** rref);
static int orp_oid_delete (API_OBJECT_RESULTSET_POOL * pool, CI_OID * oid);
static int
orp_oid_get_classname (API_OBJECT_RESULTSET_POOL * pool, CI_OID * xoid,
		       char *name, size_t size);
static void orp_api_destroy (API_OBJECT_RESULTSET_POOL * pool);
static int apif_write_glo (DB_OBJECT * obj, char *buf, size_t sz);
static int apif_fill_glo (DB_OBJECT * glo, FILE * fp);
static int orp_api_glo_create (API_OBJECT_RESULTSET_POOL * pool,
			       CI_CONNECTION conn, const char *file_path,
			       const char *init_file_path, CI_OID * glo);
static int orp_api_glo_get_path_name (API_OBJECT_RESULTSET_POOL * pool,
				      CI_OID * glo, char *buf,
				      size_t bufsz, bool full_path);
static int orp_api_glo_do_compaction (API_OBJECT_RESULTSET_POOL * pool,
				      CI_OID * glo);
static int orp_api_glo_insert (API_OBJECT_RESULTSET_POOL * pool,
			       CI_OID * glo, const char *buf, size_t bufsz);
static int orp_api_glo_delete (API_OBJECT_RESULTSET_POOL * pool,
			       CI_OID * glo, size_t sz, size_t * ndeleted);
static int orp_api_glo_read (API_OBJECT_RESULTSET_POOL * pool,
			     CI_OID * glo, char *buf, size_t bufsz,
			     size_t * nread);
static int orp_api_glo_write (API_OBJECT_RESULTSET_POOL * pool,
			      CI_OID * glo, const char *buf, size_t sz);
static int orp_api_glo_truncate (API_OBJECT_RESULTSET_POOL * pool,
				 CI_OID * glo, size_t * ndeleted);
static int orp_api_glo_seek (API_OBJECT_RESULTSET_POOL * pool,
			     CI_OID * glo, long offset, int whence);
static int orp_api_glo_tell (API_OBJECT_RESULTSET_POOL * pool,
			     CI_OID * glo, long *offset);
static int orp_api_glo_like_search (API_OBJECT_RESULTSET_POOL * pool,
				    CI_OID * glo, const char *str,
				    size_t strsz, bool * found);
static int orp_api_glo_reg_search (API_OBJECT_RESULTSET_POOL * pool,
				   CI_OID * glo, const char *str,
				   size_t strsz, bool * found);
static int orp_api_glo_bin_search (API_OBJECT_RESULTSET_POOL * pool,
				   CI_OID * glo, const char *str,
				   size_t strsz, bool * found);
static int orp_api_glo_copy (API_OBJECT_RESULTSET_POOL * pool, CI_OID * to,
			     CI_OID * from);



/*
 * rm_api_get_count - 
 *    return:
 *    impl():
 *    count():
 */
static int
rm_api_get_count (API_RESULTSET_META * impl, int *count)
{
  OBJECT_RESULTSET *or;
  assert (impl != NULL);
  assert (count != NULL);

  or = ((OBJECT_RM_BIND *) impl)->or;
  *count = or->nattrs;
  return NO_ERROR;
}

/*
 * rm_api_get_info - 
 *    return:
 *    impl():
 *    index():
 *    type():
 *    arg():
 *    size():
 */
static int
rm_api_get_info (API_RESULTSET_META * impl, int index,
		 CI_RMETA_INFO_TYPE type, void *arg, size_t size)
{
  OBJECT_RESULTSET *or;
  DB_ATTRIBUTE *attr;
  DB_DOMAIN *domain;
  int res;

  assert (impl != NULL);

  /* convert to zero based index */
  index--;

  or = ((OBJECT_RM_BIND *) impl)->or;
  if (index < 0 || index >= or->nattrs)
    {
      return ER_INTERFACE_INVALID_ARGUMENT;	/* index out of range */
    }
  if (arg == NULL || size <= 0)
    {
      return ER_INTERFACE_INVALID_ARGUMENT;
    }
  attr = or->attr_index[index];

  assert (attr != NULL);

  switch (type)
    {
    case CI_RMETA_INFO_COL_LABEL:
      /* col label is not defined. return null string */
      *(char *) arg = '\0';
      return NO_ERROR;

    case CI_RMETA_INFO_COL_NAME:
      {
	size_t namelen;
	const char *attr_name;

	attr_name = db_attribute_name (attr);

	assert (attr_name != NULL);

	namelen = strlen (attr_name) + 1;
	if (namelen > size)
	  {
	    return ER_INTERFACE_INVALID_ARGUMENT;	/* size insufficient */
	  }
	strcpy ((char *) arg, attr_name);
	return NO_ERROR;
      }
    case CI_RMETA_INFO_COL_TYPE:
      if (size < sizeof (CI_TYPE))
	{
	  return ER_INTERFACE_INVALID_ARGUMENT;
	}

      domain = db_attribute_domain (attr);
      if (domain == NULL)
	{
	  return ER_INTERFACE_GENERIC;
	}

      res = db_type_to_type (db_domain_type (domain), (CI_TYPE *) arg);
      return res;

    case CI_RMETA_INFO_PRECISION:
      if (size < sizeof (int))
	{
	  return ER_INTERFACE_INVALID_ARGUMENT;
	}

      domain = db_attribute_domain (attr);
      if (domain == NULL)
	{
	  return ER_INTERFACE_GENERIC;
	}

      *(int *) arg = db_domain_precision (domain);
      return NO_ERROR;

    case CI_RMETA_INFO_SCALE:
      if (size < sizeof (int))
	{
	  return ER_INTERFACE_INVALID_ARGUMENT;
	}

      domain = db_attribute_domain (attr);
      if (domain == NULL)
	{
	  return ER_INTERFACE_GENERIC;
	}

      *(int *) arg = db_domain_scale (domain);
      return NO_ERROR;

    case CI_RMETA_INFO_TABLE_NAME:
      {
	const char *tbl_name;
	size_t sz;

	assert (or->clz != NULL);

	tbl_name = db_get_class_name (or->clz);
	if (tbl_name == NULL)
	  {
	    return ER_INTERFACE_GENERIC;
	  }

	sz = strlen (tbl_name) + 1;
	if (sz > size)
	  {
	    return ER_INTERFACE_INVALID_ARGUMENT;
	  }
	strcpy ((char *) arg, tbl_name);
	return NO_ERROR;
      }
    case CI_RMETA_INFO_IS_AUTO_INCREMENT:
      if (size < sizeof (int))
	{
	  return ER_INTERFACE_INVALID_ARGUMENT;
	}

      *(int *) arg = db_attribute_is_auto_increment (attr);
      return NO_ERROR;

    case CI_RMETA_INFO_IS_NULLABLE:
      if (size < sizeof (int))
	{
	  return ER_INTERFACE_INVALID_ARGUMENT;
	}

      *(int *) arg = db_attribute_is_non_null (attr) ? 0 : 1;
      return NO_ERROR;

    case CI_RMETA_INFO_IS_WRITABLE:
      if (size < sizeof (int))
	{
	  return ER_INTERFACE_INVALID_ARGUMENT;
	}

      if (or->obj->lock == X_LOCK || or->obj->lock == U_LOCK)
	{
	  *(int *) arg = 1;
	}
      else
	{
	  *(int *) arg = 0;
	}
      return NO_ERROR;

    default:
      return ER_INTERFACE_INVALID_ARGUMENT;
    }

  assert (0);
  return ER_INTERFACE_GENERIC;
}

static API_RESULTSET_META_IFS RM_IFS_ = {
  rm_api_get_count,
  rm_api_get_info
};

/*
 * or_rm_bind_destroyf - 
 *    return:
 *    bind():
 */
static void
or_rm_bind_destroyf (BH_BIND * bind)
{
  OBJECT_RM_BIND *rm_bind = (OBJECT_RM_BIND *) bind;
  assert (rm_bind != NULL);
  assert (rm_bind->or->rm_bind == rm_bind);
  rm_bind->or->rm_bind = NULL;
  or_rm_bind_destroy (rm_bind);
}

/*
 * or_rm_bind_create - 
 *    return:
 *    or():
 *    rrm_bind():
 */
static int
or_rm_bind_create (OBJECT_RESULTSET * or, OBJECT_RM_BIND ** rrm_bind)
{
  OBJECT_RM_BIND *rm_bind;
  assert (or != NULL);
  assert (rrm_bind != NULL);
  rm_bind = API_MALLOC (sizeof (*rm_bind));
  if (rm_bind == NULL)
    return ER_INTERFACE_NO_MORE_MEMORY;
  rm_bind->rm.bind.dtor = or_rm_bind_destroyf;
  rm_bind->rm.handle_type = HANDLE_TYPE_RMETA;
  rm_bind->rm.ifs = &RM_IFS_;
  rm_bind->or = or;
  *rrm_bind = rm_bind;
  return NO_ERROR;
}

/*
 * or_rm_bind_destroy - 
 *    return:
 *    rm_bind():
 */
static void
or_rm_bind_destroy (OBJECT_RM_BIND * rm_bind)
{
  assert (rm_bind != NULL);
  API_FREE (rm_bind);
}

/* ------------------------------------------------------------------------- */
/* OBJECT RESULTSET IMPLEMENTATION */

/*
 * res_api_get_resultset_metadata - 
 *    return:
 *    impl():
 *    rimpl():
 */
static int
res_api_get_resultset_metadata (API_RESULTSET * impl,
				API_RESULTSET_META ** rimpl)
{
  OBJECT_RESULTSET *or;
  assert (impl != NULL);
  assert (rimpl != NULL);
  or = ((OBJECT_RES_BIND *) impl)->or;
  *rimpl = (API_RESULTSET_META *) or->rm_bind;
  return NO_ERROR;
}

/*
 * res_api_fetch - 
 *    return:
 *    impl():
 *    offset():
 *    pos():
 */
static int
res_api_fetch (API_RESULTSET * impl, int offset, CI_FETCH_POSITION pos)
{
  OBJECT_RESULTSET *or;
  int idx;
  assert (impl != NULL);
  or = ((OBJECT_RES_BIND *) impl)->or;
  if (pos == CI_FETCH_POSITION_CURRENT || pos == CI_FETCH_POSITION_FIRST)
    idx = offset;
  else if (pos == CI_FETCH_POSITION_LAST)
    idx = or->nattrs + offset;
  else
    return ER_INTERFACE_INVALID_ARGUMENT;
  if (idx != 0)
    return ER_INTERFACE_INVALID_ARGUMENT;	/* index out of range */

  if (or->deleted == 1)
    return ER_INTERFACE_GENERIC;	/* already deleted */
  return NO_ERROR;
}

/*
 * res_api_tell - 
 *    return:
 *    impl():
 *    offset():
 */
static int
res_api_tell (API_RESULTSET * impl, int *offset)
{
  OBJECT_RESULTSET *or;
  assert (impl != NULL);
  assert (offset != NULL);
  or = ((OBJECT_RES_BIND *) impl)->or;
  *offset = 1;
  return NO_ERROR;
}

/*
 * res_api_clear_updates - 
 *    return:
 *    impl():
 */
static int
res_api_clear_updates (API_RESULTSET * impl)
{
  OBJECT_RESULTSET *or;
  int res;

  assert (impl != NULL);
  or = ((OBJECT_RES_BIND *) impl)->or;

  if (or->deleted == 1)
    return ER_INTERFACE_GENERIC;	/* already deleted */
  res = or->vt->ifs->reset (or->vt);
  return res;
}

/*
 * res_api_delete_row - 
 *    return:
 *    impl():
 */
static int
res_api_delete_row (API_RESULTSET * impl)
{
  OBJECT_RESULTSET *or;
  int res;

  assert (impl != NULL);
  or = ((OBJECT_RES_BIND *) impl)->or;
  if (or->deleted == 1)
    return ER_INTERFACE_GENERIC;	/* already deleted */
  res = db_drop (or->obj);
  if (res != NO_ERROR)
    return ER_INTERFACE_GENERIC;
  or->vt->ifs->destroy (or->vt);
  or->vt = NULL;
  or->deleted = 1;
  return NO_ERROR;
}

/*
 * res_api_get_value - 
 *    return:
 *    impl():
 *    index():
 *    type():
 *    addr():
 *    len():
 *    outlen():
 *    is_null():
 */
static int
res_api_get_value (API_RESULTSET * impl, int index, CI_TYPE type,
		   void *addr, size_t len, size_t * outlen, bool * is_null)
{
  OBJECT_RESULTSET *or;
  int res;

  assert (impl != NULL);

  /* convert to zero based index */
  index--;

  or = ((OBJECT_RES_BIND *) impl)->or;
  if (index < 0 || index >= or->nattrs)
    return ER_INTERFACE_INVALID_ARGUMENT;
  if (or->deleted)
    return ER_INTERFACE_GENERIC;	/* value table deleted */
  res =
    or->vt->ifs->get_value (or->vt, index, type, addr, len, outlen, is_null);
  return res;
}

/*
 * res_api_get_value_by_name - 
 *    return:
 *    impl():
 *    name():
 *    type():
 *    addr():
 *    len():
 *    outlen():
 *    isnull():
 */
static int
res_api_get_value_by_name (API_RESULTSET * impl, const char *name,
			   CI_TYPE type, void *addr, size_t len,
			   size_t * outlen, bool * isnull)
{
  OBJECT_RESULTSET *or;
  int res;

  assert (impl != NULL);
  or = ((OBJECT_RES_BIND *) impl)->or;
  if (name == NULL)
    return ER_INTERFACE_INVALID_ARGUMENT;
  if (or->deleted)
    return ER_INTERFACE_GENERIC;	/* value table deleted */
  res =
    or->vt->ifs->get_value_by_name (or->vt, name, type, addr, len, outlen,
				    isnull);
  return res;
}

/*
 * res_api_update_value - 
 *    return:
 *    impl():
 *    index():
 *    type():
 *    addr():
 *    len():
 */
static int
res_api_update_value (API_RESULTSET * impl, int index,
		      CI_TYPE type, void *addr, size_t len)
{
  OBJECT_RESULTSET *or;
  int res;

  assert (impl != NULL);

  /* convert to zero based index */
  index--;

  or = ((OBJECT_RES_BIND *) impl)->or;
  if (index < 0 || index >= or->nattrs)
    return ER_INTERFACE_INVALID_ARGUMENT;
  if (or->deleted)
    return ER_INTERFACE_GENERIC;	/* value table deleted */
  res = or->vt->ifs->set_value (or->vt, index, type, addr, len);
  return res;
}

/*
 * res_api_apply_update - 
 *    return:
 *    impl():
 */
static int
res_api_apply_update (API_RESULTSET * impl)
{
  OBJECT_RESULTSET *or;
  int res;

  assert (impl != NULL);
  or = ((OBJECT_RES_BIND *) impl)->or;
  if (or->deleted)
    return ER_INTERFACE_GENERIC;	/* value table deleted */
  res = or->vt->ifs->apply_updates (or->vt);
  return res;
}

/*
 * res_api_destroy - 
 *    return:
 *    impl():
 */
static void
res_api_destroy (API_RESULTSET * impl)
{
  OBJECT_RESULTSET *or;
  assert (impl != NULL);
  or = ((OBJECT_RES_BIND *) impl)->or;
  or_destroy (or);
}

static API_RESULTSET_IFS RES_IFS_ = {
  res_api_get_resultset_metadata,
  res_api_fetch,
  res_api_tell,
  res_api_clear_updates,
  res_api_delete_row,
  res_api_get_value,
  res_api_get_value_by_name,
  res_api_update_value,
  res_api_apply_update,
  res_api_destroy
};

/*
 * or_res_bind_destroyf - 
 *    return:
 *    bind():
 */
static void
or_res_bind_destroyf (BH_BIND * bind)
{
  OBJECT_RES_BIND *res_bind = (OBJECT_RES_BIND *) bind;
  assert (res_bind != NULL);
  assert (res_bind->or->res_bind == res_bind);
  res_bind->or->res_bind = NULL;
  or_res_bind_destroy (res_bind);
}


/*
 * or_res_bind_create - 
 *    return:
 *    or():
 *    rres_bind():
 */
static int
or_res_bind_create (OBJECT_RESULTSET * or, OBJECT_RES_BIND ** rres_bind)
{
  OBJECT_RES_BIND *res_bind;

  assert (or != NULL);
  assert (rres_bind != NULL);
  res_bind = API_MALLOC (sizeof (*res_bind));
  if (res_bind == NULL)
    return ER_INTERFACE_NO_MORE_MEMORY;
  res_bind->res.bind.dtor = or_res_bind_destroyf;
  res_bind->res.handle_type = HANDLE_TYPE_RESULTSET;
  res_bind->res.ifs = &RES_IFS_;
  res_bind->or = or;
  *rres_bind = res_bind;
  return NO_ERROR;
}

/*
 * or_res_bind_destroy - 
 *    return:
 *    res_bind():
 */
static void
or_res_bind_destroy (OBJECT_RES_BIND * res_bind)
{
  assert (res_bind != NULL);
  API_FREE (res_bind);
}

/*
 * vt_api_get_index_by_name - 
 *    return:
 *    impl():
 *    name():
 *    ri():
 */
static int
vt_api_get_index_by_name (void *impl, const char *name, int *ri)
{
  OBJECT_RESULTSET *or = (OBJECT_RESULTSET *) impl;
  int i;
  for (i = 0; i < or->nattrs; i++)
    {
      const char *attr_name = db_attribute_name (or->attr_index[i]);
      assert (attr_name != NULL);
      if (strcmp (name, attr_name) == 0)
	{
	  assert (ri != NULL);
	  *ri = i;
	  return NO_ERROR;
	}
    }
  return ER_INTERFACE_GENERIC;	/* NO SUCH ATTRIBUTE */
}

/*
 * vt_api_get_db_value - 
 *    return:
 *    impl():
 *    index():
 *    dbval():
 */
static int
vt_api_get_db_value (void *impl, int index, DB_VALUE * dbval)
{
  OBJECT_RESULTSET *or = (OBJECT_RESULTSET *) impl;
  DB_ATTRIBUTE *attr;
  int res;
  assert (or != NULL);
  attr = or->attr_index[index];
  assert (attr != NULL);
  res = db_get (or->obj, attr->header.name, dbval);
  if (res != NO_ERROR)
    return ER_INTERFACE_GENERIC;
  return NO_ERROR;
}

/*
 * vt_api_set_db_value - 
 *    return:
 *    impl():
 *    index():
 *    dbval():
 */
static int
vt_api_set_db_value (void *impl, int index, DB_VALUE * dbval)
{
  OBJECT_RESULTSET *or = (OBJECT_RESULTSET *) impl;
  DB_ATTRIBUTE *attr;
  int res;

  assert (or != NULL);

  attr = or->attr_index[index];
  assert (attr != NULL);

  res = db_put (or->obj, attr->header.name, dbval);
  if (res != NO_ERROR)
    {
      return ER_INTERFACE_GENERIC;
    }

  return NO_ERROR;
}

/*
 * vt_api_init_domain - 
 *    return:
 *    impl():
 *    index():
 *    value():
 */
static int
vt_api_init_domain (void *impl, int index, DB_VALUE * value)
{
  OBJECT_RESULTSET *or = (OBJECT_RESULTSET *) impl;
  DB_ATTRIBUTE *attr;
  DB_DOMAIN *domain;
  DB_TYPE dbt;
  int p, s;
  int res;
  assert (or != NULL);
  attr = or->attr_index[index];
  assert (attr != NULL);
  domain = db_attribute_domain (attr);
  assert (domain != NULL);
  dbt = db_domain_type (domain);
  p = db_domain_precision (domain);
  s = db_domain_scale (domain);
  res = db_value_domain_init (value, dbt, p, s);
  return NO_ERROR;
}

/*
 * or_destroy - 
 *    return:
 *    or():
 */
static void
or_destroy (OBJECT_RESULTSET * or)
{
  BIND_HANDLE res_h;
  int res;

  assert (or != NULL);
  assert (or->res_bind != NULL);
  assert (or->rm_bind != NULL);
  res =
    or->bh_ifs->bind_to_handle (or->bh_ifs, (BH_BIND *) or->res_bind, &res_h);
  assert (res == NO_ERROR);
  res = or->bh_ifs->destroy_handle (or->bh_ifs, res_h);
  assert (res == NO_ERROR);
  assert (or->res_bind == NULL);
  assert (or->rm_bind == NULL);
  if (or->attr_index != NULL)
    {
      API_FREE (or->attr_index);
    }
  if (or->vt != NULL)
    {
      or->vt->ifs->destroy (or->vt);
    }
  API_FREE (or);
}

/*
 * or_create - 
 *    return:
 *    oid():
 *    conn():
 *    bh_ifs():
 *    ror():
 */
static int
or_create (OID * oid, BIND_HANDLE conn, BH_INTERFACE * bh_ifs,
	   OBJECT_RESULTSET ** ror)
{
  OBJECT_RESULTSET *or;
  int res = NO_ERROR;
  BIND_HANDLE res_h, rm_h;

  assert (oid != NULL);
  assert (ror != NULL);

  /* create structure */
  or = API_CALLOC (1, sizeof (*or));
  if (or == NULL)
    {
      return ER_INTERFACE_NO_MORE_MEMORY;
    }
  or->oid = *oid;
  or->bh_ifs = bh_ifs;
  res = or_res_bind_create (or, &or->res_bind);
  if (res != NO_ERROR)
    {
      API_FREE (or);
      return res;
    }
  res = or_rm_bind_create (or, &or->rm_bind);
  if (res != NO_ERROR)
    {
      or_res_bind_destroy (or->res_bind);
      API_FREE (or);
      return res;
    }
  /* realize handles and setup dependency between them */
  res = bh_ifs->alloc_handle (bh_ifs, (BH_BIND *) or->res_bind, &res_h);
  if (res != NO_ERROR)
    {
      or_res_bind_destroy (or->res_bind);
      or_rm_bind_destroy (or->rm_bind);
      API_FREE (or);
      return res;
    }
  res = bh_ifs->alloc_handle (bh_ifs, (BH_BIND *) or->rm_bind, &rm_h);
  if (res != NO_ERROR)
    {
      bh_ifs->destroy_handle (bh_ifs, res_h);
      or_rm_bind_destroy (or->rm_bind);
      API_FREE (or);
      return res;
    }
  res =
    bh_ifs->bind_graft (bh_ifs, (BH_BIND *) or->rm_bind,
			(BH_BIND *) or->res_bind);
  if (res != NO_ERROR)
    {
      bh_ifs->destroy_handle (bh_ifs, res_h);
      bh_ifs->destroy_handle (bh_ifs, rm_h);
      API_FREE (or);
      return res;
    }

  or->obj = db_object (oid);
  if (or->obj == NULL)
    {
      or_destroy (or);
      return ER_INTERFACE_GENERIC;
    }
  or->clz = db_get_class (or->obj);
  if (or->clz == NULL)
    {
      or_destroy (or);
      return ER_INTERFACE_GENERIC;
    }
  /* create object value bind table */
  do
    {
      int i = 0;
      DB_ATTRIBUTE *attrs;

      or->deleted = 0;
      attrs = db_get_attributes (or->obj);
      if (attrs == NULL)
	{
	  or_destroy (or);
	  return ER_INTERFACE_GENERIC;
	}
      or->attrs = attrs;

      while (attrs)
	{
	  i++;
	  attrs = db_attribute_next (attrs);
	}
      or->nattrs = i;
      or->attr_index = API_CALLOC (i, sizeof (DB_ATTRIBUTE *));
      if (or->attr_index == NULL)
	{
	  or_destroy (or);
	  return ER_INTERFACE_NO_MORE_MEMORY;
	}
      attrs = or->attrs;

      i = 0;
      while (attrs)
	{
	  or->attr_index[i] = attrs;
	  attrs = db_attribute_next (attrs);
	  i++;
	}
      or->conn = conn;
      res = create_db_value_bind_table (i, or, 0, conn,
					vt_api_get_index_by_name,
					vt_api_get_db_value,
					vt_api_set_db_value,
					vt_api_init_domain, &or->vt);
      if (res != NO_ERROR)
	{
	  or_destroy (or);
	  return res;
	}
    }
  while (0);

  *ror = or;
  return NO_ERROR;
}

/*
 * orp_ht_comparef - 
 *    return:
 *    key1():
 *    key2():
 *    r():
 */
static int
orp_ht_comparef (void *key1, void *key2, int *r)
{
  OID *oid1 = (OID *) key1;
  OID *oid2 = (OID *) key2;

  assert (oid1 != NULL);
  assert (oid2 != NULL);
  *r = OID_EQ (oid1, oid2) ? 0 : OID_GT (oid1, oid2) ? 1 : -1;
  return NO_ERROR;
}

/*
 * orp_ht_hashf - 
 *    return:
 *    key():
 *    rv():
 */
static int
orp_ht_hashf (void *key, unsigned int *rv)
{
  OID *oid = (OID *) key;

  assert (oid != NULL);
  assert (rv != NULL);
  *rv = OID_PSEUDO_KEY (oid);
  return NO_ERROR;
}

/*
 * orp_ht_keyf - 
 *    return:
 *    elem():
 *    rk():
 */
static int
orp_ht_keyf (void *elem, void **rk)
{
  OBJECT_RESULTSET *or = (OBJECT_RESULTSET *) elem;
  assert (or != NULL);
  assert (rk != NULL);
  *rk = &or->oid;
  return NO_ERROR;
}

/*
 * orp_api_get_object_resultset - 
 *    return:
 *    pool():
 *    xoid():
 *    rref():
 */
static int
orp_api_get_object_resultset (API_OBJECT_RESULTSET_POOL * pool,
			      CI_OID * xoid, API_RESULTSET ** rref)
{
  OBJECT_RESULTSET_POOL *p;
  void *r;
  int res;
  OID oid;
  OBJECT_RESULTSET *or;

  assert (pool != NULL);
  assert (xoid != NULL);
  assert (rref != NULL);

  p = (OBJECT_RESULTSET_POOL *) pool;
  r = NULL;
  xoid2oid (xoid, &oid);
  res = hash_lookup (p->ht, &oid, &r);
  if (res != NO_ERROR)
    return res;
  if (r)
    {
      *rref = (API_RESULTSET *) r;
      return NO_ERROR;
    }
  res = or_create (&oid, p->conn, p->bh_ifs, &or);
  if (res != NO_ERROR)
    return res;
  *rref = (API_RESULTSET *) or->res_bind;
  return NO_ERROR;
}

/*
 * orp_oid_delete - 
 *    return:
 *    pool():
 *    xoid():
 */
static int
orp_oid_delete (API_OBJECT_RESULTSET_POOL * pool, CI_OID * xoid)
{
  OBJECT_RESULTSET_POOL *p;
  OID oid;
  int res;
  void *r;

  assert (pool != NULL);
  assert (xoid != NULL);
  p = (OBJECT_RESULTSET_POOL *) pool;
  /* delete object resultset */
  xoid2oid (xoid, &oid);
  res = hash_lookup (p->ht, &oid, &r);
  if (res != NO_ERROR)
    return res;
  if (r)
    {
      OBJECT_RESULTSET *or = (OBJECT_RESULTSET *) r;
      DB_OBJECT *obj = or->obj;
      assert (obj != NULL);
      assert (or->res_bind != NULL);
      or->res_bind->res.ifs->destroy ((API_RESULTSET *) or->res_bind);
      (void) db_drop (obj);
      return NO_ERROR;
    }
  return ER_INTERFACE_INVALID_HANDLE;
}

/*
 * orp_oid_get_classname - 
 *    return:
 *    pool():
 *    xoid():
 *    name():
 *    size():
 */
static int
orp_oid_get_classname (API_OBJECT_RESULTSET_POOL * pool, CI_OID * xoid,
		       char *name, size_t size)
{
  OBJECT_RESULTSET_POOL *p;
  OID oid;
  int res;
  void *r;
  DB_OBJECT *obj;

  assert (pool != NULL);
  assert (xoid != NULL);
  p = (OBJECT_RESULTSET_POOL *) pool;
  /* delete object resultset */
  xoid2oid (xoid, &oid);
  res = hash_lookup (p->ht, &oid, &r);
  if (res != NO_ERROR)
    return res;

  obj = ws_mop (&oid, NULL);
  if (obj)
    {
      char *tmp;
      tmp = (char *) db_get_class_name (obj);
      if (tmp)
	{
	  strncpy (name, tmp, size);
	  return NO_ERROR;
	}
    }

  strncpy (name, "NULL", size);

  return NO_ERROR;
}

/*
 * orp_api_destroy - 
 *    return:
 *    pool():
 */
static void
orp_api_destroy (API_OBJECT_RESULTSET_POOL * pool)
{
  OBJECT_RESULTSET_POOL *p;
  assert (pool != NULL);
  p = (OBJECT_RESULTSET_POOL *) pool;
  hash_destroy (p->ht, NULL);
  API_FREE (p);
}

/*
 * apif_write_glo - 
 *    return:
 *    obj():
 *    buf():
 *    sz():
 */
static int
apif_write_glo (DB_OBJECT * obj, char *buf, size_t sz)
{
  DB_VALUE len;
  DB_VALUE buff;
  DB_VALUE ret;
  int res;

  assert (obj != NULL);
  assert (buf != NULL);
  assert (sz > 0);

  db_make_int (&len, (int) sz);
  res = db_make_varchar (&buff, DB_MAX_VARCHAR_PRECISION, buf, sz);
  if (res != NO_ERROR)
    {
      return ER_INTERFACE_GENERIC;
    }

  res = db_send (obj, "write_data", &ret, &len, &buff);
  if (res != NO_ERROR || sz != (size_t) db_get_int (&ret))
    {
      return ER_INTERFACE_GENERIC;
    }

  return NO_ERROR;
}

/*
 * apif_fill_glo - 
 *    return:
 *    glo():
 *    fp():
 */
static int
apif_fill_glo (DB_OBJECT * glo, FILE * fp)
{
  char readbuff[4096];
  size_t nread;
  int res;

  assert (glo != NULL);
  assert (fp != NULL);

  while ((nread = fread (readbuff, 1, 4096, fp)) > 0)
    {
      res = apif_write_glo (glo, readbuff, nread);
      if (res != NO_ERROR)
	return res;
    }
  if (ferror (fp) != 0)
    return ER_INTERFACE_GENERIC;
  return NO_ERROR;
}

#define ORP_APIF_GET_OBJECT_RETURN_ERROR(glo_,obj_,e_) \
  do{ \
    DB_IDENTIFIER oid_; \
    xoid2oid((glo_),&oid_); \
    obj_ = db_object(&oid_); \
    if(obj_ == NULL) \
      return e_; \
  } while (0)

/*
 * orp_api_glo_create - 
 *    return:
 *    pool():
 *    conn():
 *    file_path():
 *    init_file_path():
 *    glo():
 */
static int
orp_api_glo_create (API_OBJECT_RESULTSET_POOL * pool, CI_CONNECTION conn,
		    const char *file_path, const char *init_file_path,
		    CI_OID * glo)
{
  int res;
  FILE *fp;
  DB_OBJECT *glo_class;
  DB_OBJECT *obj;
  DB_VALUE ret;
  DB_IDENTIFIER *oid;

  assert (glo != NULL);
  fp = NULL;
  if (init_file_path)
    {
      fp = fopen (init_file_path, "r");
      if (fp == NULL)
	goto error_return;
    }
  glo_class = db_find_class ("glo");
  if (glo_class == NULL)
    goto error_return;

  db_make_null (&ret);
  if (file_path)
    {
      DB_VALUE arg1;
      db_make_null (&arg1);
      res =
	db_make_varchar (&arg1, DB_MAX_VARCHAR_PRECISION, (char *) file_path,
			 strlen (file_path));
      if (res != NO_ERROR)
	goto error_return;

      res = db_send (glo_class, "new_fbo", &ret, &arg1);
      if (res != NO_ERROR)
	goto error_return;
    }
  else
    {
      res = db_send (glo_class, "new_lo", &ret);
      if (res != NO_ERROR)
	goto error_return;
    }
  obj = db_get_object (&ret);
  if (obj == NULL)
    goto error_return;
  oid = db_identifier (obj);
  if (oid == NULL)
    goto error_return;
  oid2xoid (oid, conn, glo);
  if (fp)
    {
      res = apif_fill_glo (obj, fp);
      if (res != NO_ERROR)
	goto error_return;
      fclose (fp);
    }
  return NO_ERROR;

error_return:
  res = ER_INTERFACE_GENERIC;
  if (fp)
    fclose (fp);
  return res;
}


/*
 * orp_api_glo_get_path_name - 
 *    return:
 *    pool():
 *    glo():
 *    buf():
 *    bufsz():
 *    full_path():
 */
static int
orp_api_glo_get_path_name (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo,
			   char *buf, size_t bufsz, bool full_path)
{
  DB_OBJECT *obj;
  int res;
  DB_VALUE ret;
  char *path;
  size_t len;

  assert (glo != NULL);

  ORP_APIF_GET_OBJECT_RETURN_ERROR (glo, obj, ER_INTERFACE_GENERIC);
  if (full_path)
    {
      res = db_send (obj, "glo_full_pathname", &ret);
    }
  else
    {
      res = db_send (obj, "glo_pathname", &ret);
    }

  if (res != NO_ERROR)
    {
      return ER_INTERFACE_GENERIC;
    }

  path = db_get_string (&ret);
  if (path == NULL)
    {
      *buf = '\0';
    }
  else
    {
      len = strlen (path) + 1;	/* +1 for null termination */
      len = (len > bufsz) ? bufsz - 1 : len;
      strncpy (buf, path, len - 1);
      buf[len - 1] = '\0';
      db_value_clear (&ret);
    }

  return NO_ERROR;
}

/*
 * orp_api_glo_do_compaction - 
 *    return:
 *    pool():
 *    glo():
 */
static int
orp_api_glo_do_compaction (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo)
{
  DB_OBJECT *obj;
  int res;
  DB_VALUE ret;

  assert (glo != NULL);

  ORP_APIF_GET_OBJECT_RETURN_ERROR (glo, obj, ER_INTERFACE_GENERIC);

  res = db_send (obj, "compress_data", &ret);
  if (res == NO_ERROR && db_get_int (&ret) == 0)
    {
      return NO_ERROR;
    }

  return res;
}

/*
 * orp_api_glo_insert - 
 *    return:
 *    pool():
 *    glo():
 *    buf():
 *    bufsz():
 */
static int
orp_api_glo_insert (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo,
		    const char *buf, size_t bufsz)
{
  DB_OBJECT *obj;
  int res;
  DB_VALUE ret, arg1, arg2;

  assert (glo != NULL);
  assert (buf != NULL);
  assert (bufsz > 0);

  ORP_APIF_GET_OBJECT_RETURN_ERROR (glo, obj, ER_INTERFACE_GENERIC);

  db_make_int (&arg1, bufsz);
  db_make_varchar (&arg2, DB_MAX_VARCHAR_PRECISION, (char *) buf, bufsz);

  res = db_send (obj, "insert_data", &ret, &arg1, &arg2);
  if (res == NO_ERROR && ((size_t) db_get_int (&ret)) == bufsz)
    {
      return NO_ERROR;
    }

  return res;
}

/*
 * orp_api_glo_delete - 
 *    return:
 *    pool():
 *    glo():
 *    sz():
 *    ndeleted():
 */
static int
orp_api_glo_delete (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo,
		    size_t sz, size_t * ndeleted)
{
  DB_OBJECT *obj;
  int res;
  DB_VALUE ret, arg1;

  assert (glo != NULL);
  assert (sz > 0);
  assert (ndeleted != NULL);

  ORP_APIF_GET_OBJECT_RETURN_ERROR (glo, obj, ER_INTERFACE_GENERIC);

  db_make_int (&arg1, sz);

  res = db_send (obj, "delete_data", &ret, &arg1);
  if (res == NO_ERROR)
    {
      *ndeleted = db_get_int (&ret);
      return NO_ERROR;
    }

  return res;
}

/*
 * orp_api_glo_read - 
 *    return:
 *    pool():
 *    glo():
 *    buf():
 *    bufsz():
 *    nread():
 */
static int
orp_api_glo_read (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo,
		  char *buf, size_t bufsz, size_t * nread)
{
  DB_OBJECT *obj;
  int res;
  DB_VALUE ret, arg1, arg2;

  assert (glo != NULL);
  assert (buf != NULL);
  assert (bufsz > 0);
  assert (nread != NULL);

  ORP_APIF_GET_OBJECT_RETURN_ERROR (glo, obj, ER_INTERFACE_GENERIC);

  db_make_int (&arg1, bufsz);
  db_make_varchar (&arg2, DB_MAX_VARCHAR_PRECISION, buf, bufsz);

  res = db_send (obj, "read_data", &ret, &arg1, &arg2);
  if (res == NO_ERROR)
    {
      *nread = db_get_int (&ret);
      return NO_ERROR;
    }

  return res;
}

/*
 * orp_api_glo_write - 
 *    return:
 *    pool():
 *    glo():
 *    buf():
 *    sz():
 */
static int
orp_api_glo_write (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo,
		   const char *buf, size_t sz)
{
  DB_OBJECT *obj;

  ORP_APIF_GET_OBJECT_RETURN_ERROR (glo, obj, ER_INTERFACE_GENERIC);
  return apif_write_glo (obj, (char *) buf, sz);
}

/*
 * orp_api_glo_truncate - 
 *    return:
 *    pool():
 *    glo():
 *    ndeleted():
 */
static int
orp_api_glo_truncate (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo,
		      size_t * ndeleted)
{
  DB_OBJECT *obj;
  int res;
  DB_VALUE ret;

  assert (glo != NULL);
  assert (ndeleted != NULL);

  ORP_APIF_GET_OBJECT_RETURN_ERROR (glo, obj, ER_INTERFACE_GENERIC);

  res = db_send (obj, "truncate_data", &ret);
  if (res == NO_ERROR)
    {
      *ndeleted = db_get_int (&ret);
      return NO_ERROR;
    }

  return res;
}

/*
 * apif_tell - 
 *    return:
 *    obj():
 *    pos():
 */
static int
apif_tell (DB_OBJECT * obj, int *pos)
{
  DB_VALUE ret;
  int res;

  assert (obj != NULL);
  assert (pos != NULL);

  res = db_send (obj, "data_pos", &ret);
  if (res == NO_ERROR)
    {
      *pos = db_get_int (&ret);
      return NO_ERROR;
    }

  return res;
}

/*
 * apif_last_pos - 
 *    return:
 *    obj():
 *    pos():
 */
static int
apif_last_pos (DB_OBJECT * obj, int *pos)
{
  DB_VALUE ret;
  int res;

  assert (obj != NULL);
  assert (pos != NULL);

  res = db_send (obj, "data_size", &ret);
  if (res == NO_ERROR)
    {
      *pos = db_get_int (&ret);
      return NO_ERROR;
    }

  return res;
}

/*
 * orp_api_glo_seek - 
 *    return:
 *    pool():
 *    glo():
 *    offset():
 *    whence():
 */
static int
orp_api_glo_seek (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo,
		  long offset, int whence)
{
  DB_OBJECT *obj;
  int res;
  int base;
  DB_VALUE ret, arg1;

  ORP_APIF_GET_OBJECT_RETURN_ERROR (glo, obj, ER_INTERFACE_GENERIC);
  if (whence == 0)		/* from current */
    {
      res = apif_tell (obj, &base);
      if (res != NO_ERROR)
	return res;
    }
  else if (whence < 0)
    {
      res = apif_last_pos (obj, &base);
      if (res != NO_ERROR)
	return res;
    }
  else
    {
      base = 0;
    }

  db_make_int (&arg1, (int) (base + offset));
  res = db_send (obj, "data_seek", &ret, &arg1);

  return res;
}

/*
 * orp_api_glo_tell - 
 *    return:
 *    pool():
 *    glo():
 *    offset():
 */
static int
orp_api_glo_tell (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo,
		  long *offset)
{
  DB_OBJECT *obj;
  int res, pos;

  ORP_APIF_GET_OBJECT_RETURN_ERROR (glo, obj, ER_INTERFACE_GENERIC);

  res = apif_tell (obj, &pos);
  if (res == NO_ERROR)
    {
      *offset = pos;
    }

  return res;
}

/*
 * orp_api_glo_like_search - 
 *    return:
 *    pool():
 *    glo():
 *    str():
 *    strsz():
 *    found():
 */
static int
orp_api_glo_like_search (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo,
			 const char *str, size_t strsz, bool * found)
{
  DB_OBJECT *obj;
  int res;
  DB_VALUE ret, arg1;

  assert (glo != NULL);
  assert (str != NULL);
  assert (strsz > 0);
  assert (found != NULL);

  ORP_APIF_GET_OBJECT_RETURN_ERROR (glo, obj, ER_INTERFACE_GENERIC);

  db_make_varchar (&arg1, DB_MAX_VARCHAR_PRECISION, (char *) str, strsz);

  res = db_send (obj, "like_search", &ret, &arg1);
  if (res == NO_ERROR)
    {
      *found = (db_get_int (&ret) >= 0);
      return NO_ERROR;
    }

  return res;
}

/*
 * orp_api_glo_reg_search - 
 *    return:
 *    pool():
 *    glo():
 *    str():
 *    strsz():
 *    found():
 */
static int
orp_api_glo_reg_search (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo,
			const char *str, size_t strsz, bool * found)
{
  DB_OBJECT *obj;
  int res;
  DB_VALUE ret, arg1;

  assert (glo != NULL);
  assert (str != NULL);
  assert (strsz > 0);
  assert (found != NULL);

  ORP_APIF_GET_OBJECT_RETURN_ERROR (glo, obj, ER_INTERFACE_GENERIC);

  db_make_varchar (&arg1, DB_MAX_VARCHAR_PRECISION, (char *) str, strsz);

  res = db_send (obj, "reg_search", &ret, &arg1);
  if (res == NO_ERROR)
    {
      *found = (db_get_int (&ret) >= 0);
      return NO_ERROR;
    }

  return res;
}

/*
 * orp_api_glo_bin_search - 
 *    return:
 *    pool():
 *    glo():
 *    str():
 *    strsz():
 *    found():
 */
static int
orp_api_glo_bin_search (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo,
			const char *str, size_t strsz, bool * found)
{
  DB_OBJECT *obj;
  int res;
  DB_VALUE ret, arg1;

  assert (glo != NULL);
  assert (str != NULL);
  assert (strsz > 0);
  assert (found != NULL);

  ORP_APIF_GET_OBJECT_RETURN_ERROR (glo, obj, ER_INTERFACE_GENERIC);

  db_make_varchar (&arg1, DB_MAX_VARCHAR_PRECISION, (char *) str, strsz);

  res = db_send (obj, "binary_search", &ret, &arg1);
  if (res == NO_ERROR)
    {
      *found = (db_get_int (&ret) >= 0);
      return NO_ERROR;
    }

  return res;
}

/*
 * orp_api_glo_copy - 
 *    return:
 *    pool():
 *    to():
 *    from():
 */
static int
orp_api_glo_copy (API_OBJECT_RESULTSET_POOL * pool, CI_OID * to,
		  CI_OID * from)
{
  DB_OBJECT *obj1, *obj2;
  int res;
  DB_VALUE ret, arg1;

  ORP_APIF_GET_OBJECT_RETURN_ERROR (to, obj1, ER_INTERFACE_GENERIC);
  ORP_APIF_GET_OBJECT_RETURN_ERROR (from, obj2, ER_INTERFACE_GENERIC);

  db_make_object (&arg1, obj2);

  res = db_send (obj1, "copy_to", &ret, &arg1);
  if (res == NO_ERROR)
    {
      return NO_ERROR;
    }

  return res;
}

/* ------------------------------------------------------------------------- */
/* EXPORTED FUNCTION */


/*
 * api_object_resultset_pool_create - 
 *    return:
 *    ifs():
 *    conn():
 *    rpool():
 */
int
api_object_resultset_pool_create (BH_INTERFACE * ifs, BIND_HANDLE conn,
				  API_OBJECT_RESULTSET_POOL ** rpool)
{
  OBJECT_RESULTSET_POOL *pool;
  hash_table *ht;
  int res;

  assert (ifs != NULL);
  assert (rpool != NULL);

  pool = API_MALLOC (sizeof (*pool));
  if (pool == NULL)
    {
      return ER_INTERFACE_NO_MORE_MEMORY;
    }

  ht = NULL;

  res = hash_new (64, orp_ht_hashf, orp_ht_keyf, orp_ht_comparef, &ht);
  if (res != NO_ERROR)
    {
      API_FREE (pool);
      return ER_INTERFACE_NO_MORE_MEMORY;
    }

  pool->pool.destroy = orp_api_destroy;
  pool->pool.oid_delete = orp_oid_delete;
  pool->pool.oid_get_classname = orp_oid_get_classname;
  pool->pool.get_object_resultset = orp_api_get_object_resultset;
  pool->pool.glo_create = orp_api_glo_create;
  pool->pool.glo_get_path_name = orp_api_glo_get_path_name;
  pool->pool.glo_do_compaction = orp_api_glo_do_compaction;
  pool->pool.glo_insert = orp_api_glo_insert;
  pool->pool.glo_delete = orp_api_glo_delete;
  pool->pool.glo_read = orp_api_glo_read;
  pool->pool.glo_write = orp_api_glo_write;
  pool->pool.glo_truncate = orp_api_glo_truncate;
  pool->pool.glo_seek = orp_api_glo_seek;
  pool->pool.glo_tell = orp_api_glo_tell;
  pool->pool.glo_like_search = orp_api_glo_like_search;
  pool->pool.glo_reg_search = orp_api_glo_reg_search;
  pool->pool.glo_bin_search = orp_api_glo_bin_search;
  pool->pool.glo_copy = orp_api_glo_copy;
  pool->bh_ifs = ifs;
  pool->conn = conn;
  pool->ht = ht;
  *rpool = (API_OBJECT_RESULTSET_POOL *) pool;

  return NO_ERROR;
}
