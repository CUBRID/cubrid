/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * object_domain.c: type, domain and value operations.
 * This module primarily defines support for domain structures.
 */

#ident "$Id$"


#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <math.h>

#include "memory_manager_2.h"
#include "memory_manager_1.h"

#include "object_representation.h"
#include "db.h"

#include "object_primitive.h"
#include "object_domain.h"

#include "work_space.h"
#if !defined (SERVER_MODE)
#include "virtual_object_1.h"
#include "object_accessor.h"
#else /* SERVER_MODE */
#include "object_accessor.h"
#endif /* !SERVER_MODE */
#include "set_object_1.h"

#include "qp_str.h"
#include "cnv.h"
#include "cnverr.h"

#if !defined (SERVER_MODE)
#include "schema_manager_3.h"
#include "locator_cl.h"
#endif /* !SERVER_MODE */

#if defined (SERVER_MODE)
#include "csserror.h"
#include "language_support.h"
#endif /* SERVER_MODE */

#include "server.h"

/* this must be the last header file included!!! */
#include "dbval.h"

#if !defined (SERVER_MODE)
#undef MUTEX_INIT
#undef MUTEX_DESTROY
#undef MUTEX_LOCK
#undef MUTEX_UNLOCK

#define MUTEX_INIT(a, b)
#define MUTEX_DESTROY(a)
#define MUTEX_LOCK(a, b)
#define MUTEX_UNLOCK(a)
#endif /* !SERVER_MODE */

/*
 * used by versant_driver to avoid doing foolish things
 * like au_fetch_instance on DB_TYPE_OBJECT values that
 * contain versant (not CUBRID) mops.
 */

#define ARE_COMPARABLE(typ1, typ2)                        \
    ((typ1 == typ2) ||                                    \
     (QSTR_IS_CHAR(typ1) && QSTR_IS_CHAR(typ2)) ||    \
     (QSTR_IS_NATIONAL_CHAR(typ1) && QSTR_IS_NATIONAL_CHAR(typ2)))

#define DBL_MAX_DIGITS    ((int)ceil(DBL_MAX_EXP * log10((double) FLT_RADIX)))

#define TP_NEAR_MATCH(t1, t2)                                       \
         (((t1) == (t2)) ||                                         \
	  ((t1) == DB_TYPE_CHAR     && (t2) == DB_TYPE_VARCHAR) ||  \
	  ((t1) == DB_TYPE_VARCHAR  && (t2) == DB_TYPE_CHAR) ||     \
	  ((t1) == DB_TYPE_NCHAR    && (t2) == DB_TYPE_VARNCHAR) || \
	  ((t1) == DB_TYPE_VARNCHAR && (t2) == DB_TYPE_VARCHAR) ||  \
	  ((t1) == DB_TYPE_BIT      && (t2) == DB_TYPE_VARBIT) ||   \
	  ((t1) == DB_TYPE_VARBIT   && (t2) == DB_TYPE_BIT))

/*
 * These are arranged to get relative types for symetrical
 * coercion selection. The absolute position is not critical.
 * If two types are mutually coercible, the more general
 * should appear later. Eg. Float should appear after integer.
 */

static const DB_TYPE db_type_rank[] = { DB_TYPE_NULL,
  DB_TYPE_SHORT,
  DB_TYPE_INTEGER,
  DB_TYPE_NUMERIC,
  DB_TYPE_FLOAT,
  DB_TYPE_DOUBLE,
  DB_TYPE_MONETARY,
  DB_TYPE_SET,
  DB_TYPE_SEQUENCE,
  DB_TYPE_MULTISET,
  DB_TYPE_TIME,
  DB_TYPE_DATE,
  DB_TYPE_TIMESTAMP,
  DB_TYPE_OID,
  DB_TYPE_VOBJ,
  DB_TYPE_OBJECT,
  DB_TYPE_CHAR,
  DB_TYPE_VARCHAR,
  DB_TYPE_NCHAR,
  DB_TYPE_VARNCHAR,
  DB_TYPE_BIT,
  DB_TYPE_VARBIT,
  DB_TYPE_ELO,
  DB_TYPE_VARIABLE,
  DB_TYPE_SUB,
  DB_TYPE_POINTER,
  DB_TYPE_ERROR,
  DB_TYPE_DB_VALUE,
  (DB_TYPE) (DB_TYPE_LAST + 1)
};

/*
 * DOMAIN ALLOCATION
 *
 * Domains are used frequently in classes which are not normally
 * freed when the database shuts down.  Class structures are stored
 * in larger workspace blocks that are freed.
 * If we use malloc for domain structures, we get tons of allocation
 * warnings when shutting down because the domains used in classes
 * don't get explicitly freed.
 *
 * To avoid this, use areas.  Areas for these aren't a bad idea anyway
 * since they can potentially be allocated and freed quickly during
 * loads that result in domain caching.
 *
 * This will mean that domains will serve as a root for the garabage
 * collector.  In practice this isn't much of a problem because
 * the objects pointed to by the domain structures are always class objects
 * and these are generally pinned anyway.
 *
 * Note that the tp_ module is shut down AFTER the workspace manager (and
 * initialized before) so we can't be dependent on the workspace for
 * anything in here.
 */
AREA *tp_Domain_area = NULL;

/*
 * tp_Domains
 *    These are the built in domains that map directly to the primitive
 *    data types.
 *
 *    The function of this array has grown since its inception.  It started
 *    being a way to map a DB_TYPE_ constant into a corresponding default
 *    domain.  It is still used for this purpose and the first n elements
 *    of the array must correspond to the default domaims for each of the
 *    basic types.
 *    After the basic type domains however, there can be any number of
 *    additional built-in domains of arbitrary complexity.  These domains
 *    will all be tagged with an integer domain "index" so that references
 *    to these domains in the database can be stored in a single word,
 *    even if the domain is very complex.  Note that because the domain
 *    indexes can be stored in the database, and passed between the client
 *    and the server, the positions of the domains in this array NOT change
 *    unless we're in a position to perform a database migration.  You can
 *    always add new domains to the end of the array but changing the order
 *    of existing domains will invalidate any stored domain indexes.
 *    Note that there is some empty "space" between the last basic type
 *    domain and the first of the built-in domains to make adding new
 *    data types easier.
 *
 *    The domains at the front of the array, which correspond to each of
 *    the basic data types, serve as the root of a linked list of
 *    other domains related to this type.  For non-parameterized types,
 *    such as DB_TYPE_INteger, there will be only one domain in the list.
 *    For others, such as DB_TYPE_NUMERIC, there can be many domains in
 *    the list, each with a different combination of parameters.
 *    The built-in domains that are defined statically in this file, will
 *    be stiched in to their appropriate list during tp_init so that
 *    tp_domain_cache() will be able to find them.
 *
 */

/*
 * Shorthand to initialize a bunch of fields without duplication.
 * Initializes the fields from precision to the end.
 */
#define DOMAIN_INIT                                    \
  0,            /* precision */                        \
  0,            /* scale */                            \
  NULL,         /* class */                            \
  0,            /* self_ref */                         \
  NULL,         /* set domain */                       \
  {-1, -1, -1}, /* class OID */                        \
  0,            /* codeset */                          \
  1,            /* is_cached */                        \
  1,            /* built_in_index (set in tp_init) */  \
  0,            /* is_parameterized */                 \
  false,     /* is_desc */                          \
  0				/* is_visited */

/* Same as above, but leaves off the prec and scale, and sets the codeset */
#define DOMAIN_INIT2(codeset)                          \
  NULL,         /* class */                            \
  0,            /* self_ref */                         \
  NULL,         /* set domain */                       \
  {-1, -1, -1}, /* class OID */                        \
  (codeset),    /* codeset */                          \
  1,            /* is_cached */                        \
  1,            /* built_in_index (set in tp_init) */  \
  1,            /* is_parameterized */                 \
  false,     /* is_desc */                          \
  0				/* is_visited */

/*
 * Same as DOMAIN_INIT but it sets the is_parameterized flag.
 * Used for things that don't have a precision but which are parameterized in
 * other ways.
 */
#define DOMAIN_INIT3                                   \
  0,            /* precision */                        \
  0,            /* scale */                            \
  NULL,         /* class */                            \
  0,            /* self_ref */                         \
  NULL,         /* set domain */                       \
  {-1, -1, -1}, /* class OID */                        \
  0,            /* codeset */                          \
  1,            /* is_cached */                        \
  1,            /* built_in_index (set in tp_init) */  \
  1,            /* is_parameterized */                 \
  false,     /* is_desc */                          \
  0				/* is_visited */

TP_DOMAIN tp_Null_domain = { NULL, NULL, &tp_Null, DOMAIN_INIT };
TP_DOMAIN tp_Integer_domain = { NULL, NULL, &tp_Integer, DOMAIN_INIT };
TP_DOMAIN tp_Float_domain = { NULL, NULL, &tp_Float, DOMAIN_INIT };
TP_DOMAIN tp_Double_domain = { NULL, NULL, &tp_Double, DOMAIN_INIT };
TP_DOMAIN tp_String_domain = { NULL, NULL, &tp_String,
  DB_MAX_VARCHAR_PRECISION, 0,
  DOMAIN_INIT2 (INTL_CODESET_ISO88591)
};
TP_DOMAIN tp_Object_domain = { NULL, NULL, &tp_Object, DOMAIN_INIT3 };
TP_DOMAIN tp_Set_domain = { NULL, NULL, &tp_Set, DOMAIN_INIT3 };

TP_DOMAIN tp_Multiset_domain = {
  NULL, NULL, &tp_Multiset, DOMAIN_INIT3
};

TP_DOMAIN tp_Sequence_domain = {
  NULL, NULL, &tp_Sequence, DOMAIN_INIT3
};
TP_DOMAIN tp_Midxkey_domain = { NULL, NULL, &tp_Midxkey, DOMAIN_INIT3 };
TP_DOMAIN tp_Elo_domain = { NULL, NULL, &tp_Elo, DOMAIN_INIT };
TP_DOMAIN tp_Time_domain = { NULL, NULL, &tp_Time, DOMAIN_INIT };
TP_DOMAIN tp_Utime_domain = { NULL, NULL, &tp_Utime, DOMAIN_INIT };
TP_DOMAIN tp_Date_domain = { NULL, NULL, &tp_Date, DOMAIN_INIT };

TP_DOMAIN tp_Monetary_domain = {
  NULL, NULL, &tp_Monetary, DOMAIN_INIT
};

TP_DOMAIN tp_Variable_domain = {
  NULL, NULL, &tp_Variable, DOMAIN_INIT3
};

TP_DOMAIN tp_Substructure_domain = {
  NULL, NULL, &tp_Substructure, DOMAIN_INIT3
};
TP_DOMAIN tp_Pointer_domain = { NULL, NULL, &tp_Pointer, DOMAIN_INIT };
TP_DOMAIN tp_Error_domain = { NULL, NULL, &tp_Error, DOMAIN_INIT };
TP_DOMAIN tp_Short_domain = { NULL, NULL, &tp_Short, DOMAIN_INIT };
TP_DOMAIN tp_Vobj_domain = { NULL, NULL, &tp_Vobj, DOMAIN_INIT3 };
TP_DOMAIN tp_Oid_domain = { NULL, NULL, &tp_Oid, DOMAIN_INIT3 };

TP_DOMAIN tp_Numeric_domain = { NULL, NULL, &tp_Numeric,
  DB_DEFAULT_NUMERIC_PRECISION, DB_DEFAULT_NUMERIC_SCALE,
  DOMAIN_INIT2 (0)
};
TP_DOMAIN tp_Bit_domain = { NULL, NULL, &tp_Bit,
  TP_FLOATING_PRECISION_VALUE, 0,
  DOMAIN_INIT2 (INTL_CODESET_RAW_BITS)
};

TP_DOMAIN tp_VarBit_domain = { NULL, NULL, &tp_VarBit,
  DB_MAX_VARBIT_PRECISION, 0,
  DOMAIN_INIT2 (INTL_CODESET_RAW_BITS)
};

TP_DOMAIN tp_Char_domain = { NULL, NULL, &tp_Char,
  TP_FLOATING_PRECISION_VALUE, 0,
  DOMAIN_INIT2 (INTL_CODESET_ISO88591)
};

TP_DOMAIN tp_NChar_domain = { NULL, NULL, &tp_NChar,
  TP_FLOATING_PRECISION_VALUE, 0,
  DOMAIN_INIT2 (INTL_CODESET_ISO88591)
};

TP_DOMAIN tp_VarNChar_domain = { NULL, NULL, &tp_VarNChar,
  DB_MAX_VARNCHAR_PRECISION, 0,
  DOMAIN_INIT2 (INTL_CODESET_ISO88591)
};

/* These must be in DB_TYPE order */
TP_DOMAIN *tp_Domains[] = {
  &tp_Null_domain,
  &tp_Integer_domain,
  &tp_Float_domain,
  &tp_Double_domain,
  &tp_String_domain,
  &tp_Object_domain,
  &tp_Set_domain,
  &tp_Multiset_domain,
  &tp_Sequence_domain,
  &tp_Elo_domain,
  &tp_Time_domain,
  &tp_Utime_domain,
  &tp_Date_domain,
  &tp_Monetary_domain,
  &tp_Variable_domain,
  &tp_Substructure_domain,
  &tp_Pointer_domain,
  &tp_Error_domain,
  &tp_Short_domain,
  &tp_Vobj_domain,
  &tp_Oid_domain,		/* does this make sense? shouldn't we share tp_Object_domain */
  &tp_Null_domain,		/* current position of DB_TYPE_DB_VALUE */
  &tp_Numeric_domain,
  &tp_Bit_domain,
  &tp_VarBit_domain,
  &tp_Char_domain,
  &tp_NChar_domain,
  &tp_VarNChar_domain,
  &tp_Null_domain,		/*result set */
  &tp_Midxkey_domain,

  /* beginning of some "padding" built-in domains that can be used as
   * expansion space when new primitive data types are added.
   */
  &tp_Null_domain,
  &tp_Null_domain,
  &tp_Null_domain,
  &tp_Null_domain,
  &tp_Null_domain,
  &tp_Null_domain,
  &tp_Null_domain,
  &tp_Null_domain,
  &tp_Null_domain,
  &tp_Null_domain,
  &tp_Null_domain,
  &tp_Null_domain,
  &tp_Null_domain,
  &tp_Null_domain,
  &tp_Null_domain,
  &tp_Null_domain,

  /* beginning of the built-in, complex domains */


  /* end of built-in domain marker */
  NULL
};

/*
 * These define conversion rules for coercing a value into a domain
 * of multiple types.  For each value that is defined to support coercion,
 * an array of domains is created indicating the priority of that domain.
 * The domain in the destination list that is highest in the conversion
 * array will be selected.
 *
 * The idea is to select a coercion that preserves the data as much as
 * possible.
 *
 * The straightforward conversions are:
 *
 * 	integer 	=> integer (short) float double monetary time
 * 	short   	=> short integer float double monetary time
 * 	float   	=> float double integer short monetary time
 * 	double  	=> double float integer short monetary time
 * 	monetary  	=> monetary double float integer short time
 * 	string		=> string utime time date
 *
 *
 * The set conversions are mostly for completeness.  The ultimate selection
 * will depend on the subdomains as well as the outer set type.  Of course
 * this assumes that we eventually have support for nested sets.
 *
 * 	set		=> set multiset sequence
 * 	multiset	=> multiset sequence
 * 	sequence	=> sequence multiset
 */

static TP_DOMAIN *tp_Integer_conv[] = {
  &tp_Integer_domain, &tp_Short_domain, &tp_Float_domain, &tp_Double_domain,
  &tp_Numeric_domain, &tp_Monetary_domain, &tp_Time_domain, NULL
};

static TP_DOMAIN *tp_Short_conv[] = {
  &tp_Short_domain, &tp_Integer_domain, &tp_Float_domain, &tp_Double_domain,
  &tp_Numeric_domain, &tp_Monetary_domain, &tp_Time_domain, NULL
};

static TP_DOMAIN *tp_Float_conv[] = {
  &tp_Float_domain, &tp_Double_domain, &tp_Numeric_domain, &tp_Integer_domain,
  &tp_Short_domain, &tp_Monetary_domain, &tp_Time_domain, NULL
};

static TP_DOMAIN *tp_Double_conv[] = {
  &tp_Double_domain, &tp_Float_domain, &tp_Numeric_domain, &tp_Integer_domain,
  &tp_Short_domain, &tp_Monetary_domain, &tp_Time_domain, NULL
};

static TP_DOMAIN *tp_Numeric_conv[] = {
  &tp_Numeric_domain, &tp_Double_domain, &tp_Float_domain, &tp_Integer_domain,
  &tp_Short_domain, &tp_Monetary_domain, &tp_Time_domain, NULL
};

static TP_DOMAIN *tp_Monetary_conv[] = {
  &tp_Monetary_domain, &tp_Double_domain, &tp_Float_domain,
  &tp_Integer_domain,
  &tp_Short_domain, &tp_Time_domain, NULL
};

static TP_DOMAIN *tp_String_conv[] = {
  &tp_String_domain, &tp_Char_domain, &tp_VarNChar_domain, &tp_NChar_domain,
  &tp_Utime_domain, &tp_Time_domain, &tp_Date_domain, NULL
};

static TP_DOMAIN *tp_Char_conv[] = {
  &tp_Char_domain, &tp_String_domain, &tp_NChar_domain, &tp_VarNChar_domain,
  &tp_Utime_domain, &tp_Time_domain, &tp_Date_domain, NULL
};

static TP_DOMAIN *tp_NChar_conv[] = {
  &tp_NChar_domain, &tp_VarNChar_domain, &tp_Char_domain, &tp_String_domain,
  &tp_Utime_domain, &tp_Time_domain, &tp_Date_domain, NULL
};

static TP_DOMAIN *tp_VarNChar_conv[] = {
  &tp_VarNChar_domain, &tp_NChar_domain, &tp_String_domain, &tp_Char_domain,
  &tp_Utime_domain, &tp_Time_domain, &tp_Date_domain, NULL
};

static TP_DOMAIN *tp_Bit_conv[] = {
  &tp_Bit_domain, &tp_VarBit_domain, NULL
};

static TP_DOMAIN *tp_VarBit_conv[] = {
  &tp_VarBit_domain, &tp_Bit_domain, NULL
};

static TP_DOMAIN *tp_Set_conv[] = {
  &tp_Set_domain, &tp_Multiset_domain, &tp_Sequence_domain, NULL
};

static TP_DOMAIN *tp_Multiset_conv[] = {
  &tp_Multiset_domain, &tp_Sequence_domain, NULL
};

static TP_DOMAIN *tp_Sequence_conv[] = {
  &tp_Sequence_domain, &tp_Multiset_domain, NULL
};

/*
 * tp_Domain_conversion_matrix
 *    This is the matrix of conversion rules.  It is used primarily
 *    in the coercion of sets.
 */

TP_DOMAIN **tp_Domain_conversion_matrix[] = {
  NULL,				/* DB_TYPE_NULL */
  tp_Integer_conv,
  tp_Float_conv,
  tp_Double_conv,
  tp_String_conv,
  NULL,				/* DB_TYPE_OBJECT */
  tp_Set_conv,
  tp_Multiset_conv,
  tp_Sequence_conv,
  NULL,				/* DB_TYPE_ELO */
  NULL,				/* DB_TYPE_TIME */
  NULL,				/* DB_TYPE_TIMESTAMP */
  NULL,				/* DB_TYPE_DATE */
  tp_Monetary_conv,
  NULL,				/* DB_TYPE_VARIABLE */
  NULL,				/* DB_TYPE_SUBSTRUCTURE */
  NULL,				/* DB_TYPE_POINTER */
  NULL,				/* DB_TYPE_ERROR */
  tp_Short_conv,
  NULL,				/* DB_TYPE_VOBJ */
  NULL,				/* DB_TYPE_OID */
  NULL,				/* DB_TYPE_DB_VALUE */
  tp_Numeric_conv,		/* DB_TYPE_NUMERIC */
  tp_Bit_conv,			/* DB_TYPE_BIT */
  tp_VarBit_conv,		/* DB_TYPE_VARBIT */
  tp_Char_conv,			/* DB_TYPE_CHAR */
  tp_NChar_conv,		/* DB_TYPE_NCHAR */
  tp_VarNChar_conv		/* DB_TYPE_VARNCHAR */
};

#if defined (SERVER_MODE)
/* lock for domain list cache */
static MUTEX_T tp_domain_cache_lock = MUTEX_INITIALIZER;
#endif /* SERVER_MODE */

static void domain_init (TP_DOMAIN * domain, DB_TYPE typeid_);
static int tp_domain_size_internal (const TP_DOMAIN * domain);
static void tp_value_slam_domain (DB_VALUE * value, const DB_DOMAIN * domain);
static TP_DOMAIN *tp_is_domain_cached (TP_DOMAIN * dlist,
				       TP_DOMAIN * transient, TP_MATCH exact,
				       TP_DOMAIN ** ins_pos);
#if !defined (SERVER_MODE)
static void tp_swizzle_oid (TP_DOMAIN * domain);
#endif /* SERVER_MODE */
static int tp_domain_check_class (TP_DOMAIN * domain);
static const TP_DOMAIN *tp_domain_find_compatible (const TP_DOMAIN * src,
						   const TP_DOMAIN * dest);
static int tp_null_terminate (const DB_VALUE * src, char **strp, int str_len,
			      bool * do_alloc);
static int tp_atotime (const DB_VALUE * src, DB_TIME * temp);
static int tp_atodate (const DB_VALUE * src, DB_DATE * temp);
static int tp_atoutime (const DB_VALUE * src, DB_UTIME * temp);
static int tp_atonumeric (const DB_VALUE * src, DB_VALUE * temp);
static int tp_atof (const DB_VALUE * src, double *num_value);
static char *tp_itoa (int value, char *string, int radix);
static int bfmt_print (int bfmt, const DB_VALUE * the_db_bit, char *string,
		       int max_size);
static TP_DOMAIN_STATUS tp_value_cast_internal (const DB_VALUE * src,
						DB_VALUE * dest,
						const TP_DOMAIN *
						desired_domain,
						bool implicit_coercion,
						bool do_domain_select);
static int oidcmp (OID * oid1, OID * oid2);
static void fprint_domain (FILE * fp, TP_DOMAIN * domain);

/*
 * tp_init - Global initialization for this module.
 *    return: none
 */
void
tp_init (void)
{
  TP_DOMAIN *d;
  int i;

  /*
   * we need to add safe guard to prevent any client from calling
   * this initialize function several times during the client's life time
   */
  if (tp_Domain_area != NULL)
    {
      return;
    }

  /* create our allocation area */
#if defined (SERVER_MODE)
  tp_Domain_area = area_create ("Domains", sizeof (TP_DOMAIN), 32, false);
#else /* !SERVER_MODE */
  tp_Domain_area = area_create ("Domains", sizeof (TP_DOMAIN), 1024, false);
#endif /* SERVER_MODE */

  /*
   * Make sure the next pointer on all the built-in domains is clear.
   * Also make sure the built-in domain numbers are assigned consistently.
   * Assign the builtin indexes starting from 1 so we can use zero to mean
   * that the domain isn't built-in.
   */
  for (i = 0; tp_Domains[i] != NULL; i++)
    {
      d = tp_Domains[i];
      d->next_list = NULL;
      d->class_mop = NULL;
      d->self_ref = 0;
      d->setdomain = NULL;
      d->class_oid.volid = d->class_oid.pageid = d->class_oid.slotid = -1;
      d->is_cached = 1;
      d->built_in_index = i + 1;
      d->is_desc = false;

      /* ! need to be adding this to the corresponding list */
    }
}


/*
 * tp_final - Global shutdown for this module.
 *    return: none
 * Note:
 *    Frees all the cached domains.  It isn't absolutely necessary
 *    that we do this since area_final() will destroy the areas but when
 *    leak statistics are enabled, it will dump a bunch of messages
 *    about dangling domain allocations.
 */
void
tp_final (void)
{
  TP_DOMAIN *dlist, *d, *next, *prev;
  int i;

  /*
   * Make sure the next pointer on all the built-in domains is clear.
   * Also make sure the built-in domain numbers are assigned consistently.
   */
  for (i = 0; tp_Domains[i] != NULL; i++)
    {
      dlist = tp_Domains[i];
      /*
       * The first element in the domain array is always a built-in, there
       * can potentially be other built-ins in the list mixed in with
       * allocated domains.
       */
      for (d = dlist->next_list, prev = dlist, next = NULL; d != NULL;
	   d = next)
	{
	  next = d->next_list;
	  if (d->built_in_index)
	    {
	      prev = d;
	    }
	  else
	    {
	      prev->next_list = next;

	      /*
	       * Make sure to turn off the cache bit or else tp_domain_free
	       * will ignore the request.
	       */
	      d->is_cached = 0;
	      tp_domain_free (d);
	    }
	}
    }
}


/*
 * tp_domain_free - free a hierarchical domain structure.
 *    return: none
 *    dom(out): domain to free
 * Note:
 *    This routine can be called for a transient or cached domain.  If
 *    the domain has been cached, the request is ignored.
 *    Note that you can only call this on the root of a domain hierarchy,
 *    you are not allowed to grab pointers into the middle of a hierarchical
 *    domain and free that.
 */
void
tp_domain_free (TP_DOMAIN * dom)
{
  TP_DOMAIN *d, *next;

  if (dom != NULL && !dom->is_cached)
    {

      /* NULL things that might be problems for garbage collection */
      dom->class_mop = NULL;

      /*
       * sub-domains are always completely owned by their root domain,
       * they cannot be cached anywhere else.
       */
      for (d = dom->setdomain, next = NULL; d != NULL; d = next)
	{
	  next = d->next;
	  tp_domain_free (d);
	}

      area_free (tp_Domain_area, dom);
    }
}


/*
 * domain_init - initializes a domain structure to contain reasonable "default"
 * values.
 *    return: none
 *    domain(out): domain structure to initialize
 *    typeid(in): basic type of the domain
 * Note:
 *    Used by tp_domain_new and also in some other places
 *    where we need to quickly synthesize some transient domain structures.
 */
static void
domain_init (TP_DOMAIN * domain, DB_TYPE typeid_)
{
  domain->next = NULL;
  domain->next_list = NULL;
  domain->type = PR_TYPE_FROM_ID (typeid_);
  domain->precision = 0;
  domain->scale = 0;
  domain->class_mop = NULL;
  domain->self_ref = 0;
  domain->setdomain = NULL;
  OID_SET_NULL (&domain->class_oid);
  domain->codeset = 0;		/* is there a better default for this ? */
  domain->is_cached = 0;
  domain->built_in_index = 0;

  /* use the built-in domain template to see if we're parameterized or not */
  domain->is_parameterized = tp_Domains[typeid_]->is_parameterized;

  domain->is_desc = false;

}

/*
 * tp_domain_new - returns a new initialized transient domain.
 *    return: new transient domain
 *    type(in): type id
 * Note:
 *    It is intended for use in places where domains are being created
 *    incrementally for eventual passing to tp_domain_cache.
 *    Only the type id is passed here since that is the only common
 *    piece of information shared by all domains.
 *    The contents of the domain can be filled in by the caller assuming
 *    they obey the rules.
 */
TP_DOMAIN *
tp_domain_new (DB_TYPE type)
{
  TP_DOMAIN *new_;

  new_ = (TP_DOMAIN *) area_alloc (tp_Domain_area);
  if (new_ != NULL)
    domain_init (new_, type);

  return new_;
}


/*
 * tp_domain_construct - create a transient domain object with type, class,
 * precision, scale and setdomain.
 *    return:
 *    domain_type(in): The basic type of the domain
 *    class_obj(in): The class of the domain (for DB_TYPE_OBJECT)
 *    precision(in): The precision of the domain
 *    scale(in): The class of the domain
 *    setdomain(in): The setdomain of the domain
 * Note:
 *    Used in a few places, callers must be aware that there may be more
 *    initializations to do since not all of the domain parameters are
 *    arguments to this function.
 *
 *    The setdomain must also be a transient domain list.
 */
TP_DOMAIN *
tp_domain_construct (DB_TYPE domain_type,
		     DB_OBJECT * class_obj,
		     int precision, int scale, TP_DOMAIN * setdomain)
{
  TP_DOMAIN *new_;

  new_ = tp_domain_new (domain_type);
  if (new_)
    {
      new_->precision = precision;
      new_->scale = scale;
      new_->setdomain = setdomain;

      if (class_obj == (DB_OBJECT *) TP_DOMAIN_SELF_REF)
	{
	  new_->class_mop = NULL;
	  new_->self_ref = 1;
	}
      else
	{
	  new_->class_mop = class_obj;
	  new_->self_ref = 0;
	  /*
	   * For compatibility on the server side, class objects must have
	   * the oid in the domain match the oid in the class object.
	   */
	  if (class_obj)
	    {
	      new_->class_oid = class_obj->oid_info.oid;
	    }
	}

      /*
       * have to leave the class OID uninitialized because we don't know how
       * to get an OID out of a DB_OBJECT on the server.
       * That shouldn't matter since the server side unpackers will use
       * tp_domain_new and set the domain fields directly.
       */
    }
  return new_;
}


/*
 * tp_domain_copy - copy a hierarcical domain structure
 *    return: new domain
 *    dom(in): domain to copy
 *    check_cache(in): if set, return cached instance
 * Note:
 *    If the domain was cached, we simply return a handle to the cached
 *    domain, otherwise we make a full structure copy.
 *    This should only be used in a few places in the schema manager which
 *    maintains separate copies of all the attribute domains during
 *    flattening.  Could be converted to used cached domains perhaps.
 *    But the "self referencing" domain is the problem.
 *
 *    New functionality:  If the check_cache parameter is false, we make
 *    a NEW copy of the parameter domain whether it is cached or not.   This
 *    is used for updating fields of a cached domain.  We don't want to
 *    update a domain that has already been cached because multiple structures
 *    may be pointing to it.
 */
TP_DOMAIN *
tp_domain_copy (const TP_DOMAIN * domain, bool check_cache)
{
  TP_DOMAIN *new_domain, *first, *last;
  const TP_DOMAIN *d;

  if (check_cache && domain->is_cached)
    return (TP_DOMAIN *) domain;

  first = NULL;
  if (domain != NULL)
    {
      last = NULL;

      for (d = domain; d != NULL; d = d->next)
	{
	  new_domain = tp_domain_new (d->type->id);
	  if (new_domain == NULL)
	    {
	      tp_domain_free (first);
	      return NULL;
	    }
	  else
	    {
	      /* copy over the domain parameters */
	      new_domain->class_mop = d->class_mop;
	      new_domain->class_oid = d->class_oid;
	      new_domain->precision = d->precision;
	      new_domain->scale = d->scale;
	      new_domain->codeset = d->codeset;
	      new_domain->self_ref = d->self_ref;
	      new_domain->is_parameterized = d->is_parameterized;

	      if (d->setdomain != NULL)
		{
		  new_domain->setdomain = tp_domain_copy (d->setdomain, true);
		  if (new_domain->setdomain == NULL)
		    {
		      tp_domain_free (first);
		      return NULL;
		    }
		}

	      if (first == NULL)
		first = new_domain;
	      else
		last->next = new_domain;
	      last = new_domain;
	    }
	}
    }
  return (first);
}


/*
 * tp_domain_size_internal - count the number of domains in a domain list
 *    return: number of domains in a domain list
 *    domain(in): a domain list
 */
static int
tp_domain_size_internal (const TP_DOMAIN * domain)
{
  int size = 0;

  while (domain)
    {
      ++size;
      domain = domain->next;
    }
  return size;
}

/*
 * tp_domain_size - count the number of domains in a dlomain list
 *    return: number of domains in a domain list
 *    domain(in): domain
 */
int
tp_domain_size (const TP_DOMAIN * domain)
{
  return tp_domain_size_internal (domain);
}

/*
 * tp_value_slam_domain - alter the domain of an existing DB_VALUE
 *    return: nothing
 *    value(out): value whose domain is to be altered
 *    domain(in): domain descriptor
 * Note:
 * used usually in a context like tp_value_cast where we know that we
 * have a perfectly good fixed-length string that we want tocast as a varchar.
 *
 * This is a dangerous function and should not be exported to users.  use
 * only if you know exactly what you're doing!!!!
 */
static void
tp_value_slam_domain (DB_VALUE * value, const DB_DOMAIN * domain)
{
  switch (domain->type->id)
    {
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      value->domain.char_info.type = domain->type->id;
      value->domain.char_info.length = domain->precision;
      break;
    case DB_TYPE_NUMERIC:
      value->domain.numeric_info.type = domain->type->id;
      value->domain.numeric_info.precision = domain->precision;
      value->domain.numeric_info.scale = domain->scale;
      break;
    default:
      value->domain.general_info.type = domain->type->id;
      break;
    }
}


/*
 * tp_domain_match - examins two domains to see if they are logically same
 *    return: non-zero if the domains are the same
 *    dom1(in): first domain
 *    dom2(in): second domain
 *    exact(in): how tolerant we are of mismatches
 * Note:
 *    This is a helper function used by tp_domain_cache and elsewhere.
 *    The "root" of any domain hierarchy must match exactly.
 *    For domains with sub-domains, (sets, unions), the sub-domains
 *    in the list can appear in any order.
 *
 *    This should be the only place where we have to worry about the
 *    various type specific domain extensions and how thay are compared.
 *
 *    Note, this was written after the tp_domain_find, tp_domain_match
 *    pair and is written with the assumption that top-level domains
 *    are not allowed to have "next" pointers which is the direction
 *    we're heading.
 *
 *    We could potentially replace tp_domain_equal with this function
 *    but note that tp_domain_equal has some stuff in it about ignoreing
 *    precision on things inside sets.  Until its clear what that
 *    is for, keep these separate.
 *
 *    The "exact" flag is set to ensure that the domains are exactly the
 *    same, all parameters must be exact matches.
 *
 *    If "exact" is zero, it is considered a match if dom2 is within the
 *    "tolerance" of dom1.  The exact meaning of tolerance is type dependent
 *    but the most common interpretation is that dom1 has a precision that
 *    is greater than or equal to the precision of dom2.  That means that
 *    any value from dom2 will logically "fit" within dom1 after.
 *    The exact flag should be turned off only if the caller is sure that
 *    the value associated with the domain being checked can be coerced
 *    later by the receiver.  This is intended primarily for check_att_value
 *    in obj.c because the various "setmem" functions are able to perform
 *    coercion on the fly and we can eliminate yet another string copy
 *    if we defer the coercion until the last moment of assignment.
 *    This function should therefore only allow a non-exact match if
 *    the setmem function associated with the type is able to perform this
 *    delayed coercion.
 *
 *    Note that we can't use this for set elements because we use the
 *    "setval" function to assign set values.  As "setval" doesn't take
 *    a TP_DOMAIN structure yet, we have no way of knowing what the real
 *    domain is supposed to be.  This is a problem primarily for the
 *    "floating" domain CHAR types.  Because of this, set_check_domain will
 *    always request exact matches on the domain checks and will perform
 *    always perform coercion up front.  When the dust settles, we need
 *    to sit down and trace the life of strings & sets from language to disk
 *    and try to reduce the amount of times these get copied.
 */
int
tp_domain_match (const TP_DOMAIN * dom1, const TP_DOMAIN * dom2,
		 TP_MATCH exact)
{
  int match = 0;

  /* in the case where their both cached */
  if (dom1 == dom2)
    return 1;

  if ((dom1->type->id != dom2->type->id) &&
      (exact != TP_STR_MATCH
       || !TP_NEAR_MATCH (dom1->type->id, dom2->type->id)))
    return 0;

  /*
   * At this point, either dom1 and dom2 have exactly the same type, or
   * exact_match is TP_STR_MATCH and dom1 and dom2 are a char/varchar
   * (nchar/varnchar, bit/varbit) pair.
   */

  /* check for asc/desc */
  if (dom1->type->id == dom2->type->id
      && tp_valid_indextype (dom1->type->id)
      && dom1->is_desc != dom2->is_desc)
    {
      return 0;
    }

  /* could use the new is_parameterized flag to avoid the switch ? */

  switch (dom1->type->id)
    {

    case DB_TYPE_NULL:
    case DB_TYPE_INTEGER:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_ELO:
    case DB_TYPE_TIME:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_DATE:
    case DB_TYPE_MONETARY:
    case DB_TYPE_SHORT:
      /*
       * these domains have no parameters, they match if the types are the
       * same.
       */
      match = 1;
      break;

    case DB_TYPE_VOBJ:
    case DB_TYPE_OBJECT:
    case DB_TYPE_SUB:

      /*
       * if "exact" is zero, we should be checking the subclass hierarchy of
       * dom1 to see id dom2 is in it !
       */

      /* Always prefer comparison of MOPs */
      if (dom1->class_mop != NULL && dom2->class_mop != NULL)
	{
	  match = (dom1->class_mop == dom2->class_mop);
	}
      else if (dom1->class_mop == NULL && dom2->class_mop == NULL)
	{
	  match = OID_EQ (&dom1->class_oid, &dom2->class_oid);
	}
      else
	{
	  /*
	   * We have a mixture of OID & MOPS, it probably isn't necessary to
	   * be this general but try to avoid assuming the class OIDs have
	   * been set when there is a MOP present.
	   */
	  if (dom1->class_mop == NULL)
	    {
	      match = OID_EQ (&dom1->class_oid, WS_OID (dom2->class_mop));
	    }
	  else
	    {
	      match = OID_EQ (WS_OID (dom1->class_mop), &dom2->class_oid);
	    }
	}

      if (match == 0 && exact == TP_SET_MATCH
	  && dom1->class_mop == NULL && OID_ISNULL (&dom1->class_oid))
	{
	  match = 1;
	}
      break;

    case DB_TYPE_VARIABLE:
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
#if 1
      /* >>>>> NEED MORE CONSIDERATION <<<<<
       * do not check order
       * must be rollback with tp_domain_add() 
       */
      if (dom1->setdomain == dom2->setdomain)
	{
	  match = 1;
	}
      else
	{
	  int dsize;

	  /* don't bother comparing the lists unless the sizes are the same */
	  dsize = tp_domain_size (dom1->setdomain);
	  if (dsize == tp_domain_size (dom2->setdomain))
	    {

	      /* handle the simple single domain case quickly */
	      if (dsize == 1)
		{
		  match = tp_domain_match (dom1->setdomain, dom2->setdomain,
					   exact);
		}
	      else
		{
		  TP_DOMAIN *d1, *d2;

		  match = 1;
		  for (d1 = dom1->setdomain, d2 = dom2->setdomain;
		       d1 != NULL && d2 != NULL; d1 = d1->next, d2 = d2->next)
		    {
		      if (!tp_domain_match (d1, d2, exact))
			{
			  match = 0;
			  break;	/* immediately exit for loop */
			}
		    }		/* for */
		}
	    }			/* if (dsize == tp_domain_size(dom2->setdomain)) */
	}
#else /* 0 */
      if (dom1->setdomain == dom2->setdomain)
	match = 1;

      else
	{
	  int dsize;

	  /* don't bother comparing the lists unless the sizes are the same */
	  dsize = tp_domain_size (dom1->setdomain);
	  if (dsize == tp_domain_size (dom2->setdomain))
	    {

	      /* handle the simple single domain case quickly */
	      if (dsize == 1)
		{
		  match = tp_domain_match (dom1->setdomain, dom2->setdomain,
					   exact);
		}
	      else
		{
		  TP_DOMAIN *d1, *d2;

		  /* clear the visited flag in the second subdomain list */
		  for (d2 = dom2->setdomain; d2 != NULL; d2 = d2->next)
		    d2->is_visited = 0;

		  match = 1;
		  for (d1 = dom1->setdomain;
		       d1 != NULL && match; d1 = d1->next)
		    {
		      for (d2 = dom2->setdomain; d2 != NULL; d2 = d2->next)
			{
			  if (!d2->is_visited
			      && tp_domain_match (d1, d2, exact))
			    break;
			}
		      /* did we find the domain in the other list ? */
		      if (d2 != NULL)
			d2->is_visited = 1;
		      else
			match = 0;
		    }
		}
	    }
	}
#endif /* 1 */

      break;

    case DB_TYPE_MIDXKEY:
      if (dom1->setdomain == dom2->setdomain)
	match = 1;
      else
	{
	  int i, dsize1, dsize2;
	  TP_DOMAIN *element_dom1;
	  TP_DOMAIN *element_dom2;

	  dsize1 = tp_domain_size (dom1->setdomain);
	  dsize2 = tp_domain_size (dom2->setdomain);

	  if (dsize1 == dsize2)
	    {
	      match = 1;
	      element_dom1 = dom1->setdomain;
	      element_dom2 = dom2->setdomain;

	      for (i = 0; i < dsize1; i++)
		{
		  if ((match = tp_domain_match (element_dom1, element_dom2,
						exact)) == 0)
		    break;
		  element_dom1 = element_dom1->next;
		  element_dom2 = element_dom2->next;
		}
	    }
	}
      break;

    case DB_TYPE_VARCHAR:
    case DB_TYPE_VARBIT:
      if (exact == TP_EXACT_MATCH || exact == TP_SET_MATCH)
	match = dom1->precision == dom2->precision;
      else if (exact == TP_STR_MATCH)
	{
	  /*
	   * Allow the match if the precisions would allow us to reuse the
	   * string without modification.
	   */
	  match = dom1->precision >= dom2->precision;
	}
      else
	{
	  /*
	   * Allow matches regardless of precision, let the actual length of the
	   * value determine if it can be assigned.  This is important for
	   * literal strings as their precision will be the maximum but they
	   * can still be assigned to domains with a smaller precision
	   * provided the actual value is within the destination domain
	   * tolerance.
	   */
	  match = 1;
	}
      break;

    case DB_TYPE_CHAR:
    case DB_TYPE_BIT:
      /*
       * Unlike varchar, we have to be a little tighter on domain matches for
       * fixed width char.  Not as much of a problem since these won't be
       * used for literal strings.
       */
      if (exact == TP_EXACT_MATCH || exact == TP_STR_MATCH ||
	  exact == TP_SET_MATCH)
	match = dom1->precision == dom2->precision;
      else
	{
	  /* Recognize a precision of TP_FLOATING_PRECISION_VALUE to indicate
	   * a precision whose coercability must be determined by examing the
	   * value.  This is used primarily by db_coerce() since it must pick
	   * a reasonable CHAR domain for the representation of a literal
	   * string.
	   * Accept zero here too since it seems to creep into domains
	   * sometimes.
	   */
	  match = dom2->precision == 0
	    || dom2->precision == TP_FLOATING_PRECISION_VALUE
	    || dom1->precision >= dom2->precision;
	}
      break;

    case DB_TYPE_NCHAR:
      if (exact == TP_EXACT_MATCH || exact == TP_STR_MATCH ||
	  exact == TP_SET_MATCH)
	match = (dom1->precision == dom2->precision) &&
	  (dom1->codeset == dom2->codeset);
      else
	{
	  /*
	   * see discussion of special domain precision values in the
	   * DB_TYPE_CHAR case above.
	   */
	  match = (dom1->codeset == dom2->codeset) &&
	    (dom2->precision == 0
	     || dom2->precision == TP_FLOATING_PRECISION_VALUE
	     || dom1->precision >= dom2->precision);
	}

      break;

    case DB_TYPE_VARNCHAR:
      if (exact == TP_EXACT_MATCH || exact == TP_STR_MATCH ||
	  exact == TP_SET_MATCH)
	match = (dom1->precision == dom2->precision) &&
	  (dom1->codeset == dom2->codeset);
      else
	/* see notes above under the DB_TYPE_VARCHAR clause */
	match = dom1->codeset == dom2->codeset;
      break;

    case DB_TYPE_NUMERIC:
      /*
       * note that we never allow inexact matches here because the
       * mr_setmem_numeric function is not currently able to perform the
       * defered coercion.
       */
      match = (dom1->precision == dom2->precision) &&
	(dom1->scale == dom2->scale);
      break;

    case DB_TYPE_POINTER:
    case DB_TYPE_ERROR:
    case DB_TYPE_OID:
    case DB_TYPE_DB_VALUE:
      /*
       * These are internal domains, they shouldn't be seen, in case they are,
       * just let them match without parameters.
       */
      match = 1;
      break;

    case DB_TYPE_RESULTSET:
    case DB_TYPE_TABLE:
      break;
      /* don't have a default so we make sure to add clauses for all types */
    }

  return match;
}

/*
 * tp_is_domain_cached - find matching domain from domain list
 *    return: matched domain
 *    dlist(in): domain list
 *    transient(in): transient domain
 *    exact(in): matching level
 *    ins_pos(out): domain found
 * Note:
 * DB_TYPE_VARCHAR, DB_TYPE_VARBIT, DB_TYPE_VARNCHAR : precision's desc order
 *                                             others: precision's asc order
 */
static TP_DOMAIN *
tp_is_domain_cached (TP_DOMAIN * dlist, TP_DOMAIN * transient, TP_MATCH exact,
		     TP_DOMAIN ** ins_pos)
{
  TP_DOMAIN *domain = dlist;
  int match = 0;

  /* in the case where their both cached */
  if (domain == transient)
    return domain;

  if ((domain->type->id != transient->type->id) &&
      (exact != TP_STR_MATCH
       || !TP_NEAR_MATCH (domain->type->id, transient->type->id)))
    return NULL;

  *ins_pos = domain;

  /*
   * At this point, either domain and transient have exactly the same type, or
   * exact_match is TP_STR_MATCH and domain and transient are a char/varchar
   * (nchar/varnchar, bit/varbit) pair.
   */

  /* could use the new is_parameterized flag to avoid the switch ? */
  switch (domain->type->id)
    {

    case DB_TYPE_NULL:
    case DB_TYPE_INTEGER:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_ELO:
    case DB_TYPE_TIME:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_DATE:
    case DB_TYPE_MONETARY:
    case DB_TYPE_SHORT:
      /*
       * these domains have no parameters, they match if asc/desc are the
       * same
       */
      while (domain)
	{
	  if (domain->is_desc == transient->is_desc)
	    match = 1;
	  if (match)
	    break;

	  *ins_pos = domain;
	  domain = domain->next_list;
	}
      break;

    case DB_TYPE_VOBJ:
    case DB_TYPE_OBJECT:
    case DB_TYPE_SUB:

      while (domain)
	{
	  /*
	   * if "exact" is zero, we should be checking the subclass hierarchy
	   * of domain to see id transient is in it !
	   */

	  /* Always prefer comparison of MOPs */
	  if (domain->class_mop != NULL && transient->class_mop != NULL)
	    {
	      match = (domain->class_mop == transient->class_mop);
	    }
	  else if (domain->class_mop == NULL && transient->class_mop == NULL)
	    {
	      match = OID_EQ (&domain->class_oid, &transient->class_oid);
	    }
	  else
	    {
	      /*
	       * We have a mixture of OID & MOPS, it probably isn't necessary
	       * to be this general but try to avoid assuming the class OIDs
	       * have been set when there is a MOP present.
	       */
	      if (domain->class_mop == NULL)
		{
		  match = OID_EQ (&domain->class_oid,
				  WS_OID (transient->class_mop));
		}
	      else
		{
		  match = OID_EQ (WS_OID (domain->class_mop),
				  &transient->class_oid);
		}
	    }

	  if (match == 0 && exact == TP_SET_MATCH
	      && domain->class_mop == NULL && OID_ISNULL (&domain->class_oid))
	    {
	      /* check for asc/desc */
	      if (domain->is_desc == transient->is_desc)
		match = 1;
	    }

	  if (match)
	    break;

	  *ins_pos = domain;
	  domain = domain->next_list;
	}
      break;

    case DB_TYPE_VARIABLE:
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      {
	int dsize2;

	dsize2 = tp_domain_size (transient->setdomain);
	while (domain)
	  {
#if 1
	    /* >>>>> NEED MORE CONSIDERATION <<<<<
	     * do not check order
	     * must be rollback with tp_domain_add() 
	     */
	    if (domain->setdomain == transient->setdomain)
	      {
		match = 1;
	      }
	    else
	      {
		int dsize1;

		/*
		 * don't bother comparing the lists unless the sizes are the
		 * same
		 */
		dsize1 = tp_domain_size (domain->setdomain);
		if (dsize1 > dsize2)
		  break;

		if (dsize1 == dsize2)
		  {

		    /* handle the simple single domain case quickly */
		    if (dsize1 == 1)
		      {
			match = tp_domain_match (domain->setdomain,
						 transient->setdomain, exact);
		      }
		    else
		      {
			TP_DOMAIN *d1, *d2;

			match = 1;
			for (d1 = domain->setdomain, d2 =
			     transient->setdomain; d1 != NULL && d2 != NULL;
			     d1 = d1->next, d2 = d2->next)
			  {
			    if (!tp_domain_match (d1, d2, exact))
			      {
				match = 0;
				break;	/* immediately exit for loop */
			      }
			  }	/* for */
		      }
		  }		/* if (dsize1 == dsize2) */
	      }
#else /* #if 1 */
	    if (domain->setdomain == transient->setdomain)
	      match = 1;

	    else
	      {
		int dsize;

		/*
		 * don't bother comparing the lists unless the sizes are the
		 * same
		 */
		dsize = tp_domain_size (domain->setdomain);
		if (dsize == tp_domain_size (transient->setdomain))
		  {

		    /* handle the simple single domain case quickly */
		    if (dsize == 1)
		      {
			match = tp_domain_match (domain->setdomain,
						 transient->setdomain, exact);
		      }
		    else
		      {
			TP_DOMAIN *d1, *d2;

			/* clear the visited flag of second subdomain list */
			for (d2 = transient->setdomain; d2 != NULL;
			     d2 = d2->next)
			  d2->is_visited = 0;

			match = 1;
			for (d1 = domain->setdomain;
			     d1 != NULL && match; d1 = d1->next)
			  {
			    for (d2 = transient->setdomain; d2 != NULL;
				 d2 = d2->next)
			      {
				if (!d2->is_visited
				    && tp_domain_match (d1, d2, exact))
				  break;
			      }
			    /* did we find the domain in the other list ? */
			    if (d2 != NULL)
			      d2->is_visited = 1;
			    else
			      match = 0;
			  }
		      }
		  }
	      }
#endif /* #if 1 */

	    if (match)
	      break;

	    *ins_pos = domain;
	    domain = domain->next_list;
	  }
      }
      break;

    case DB_TYPE_MIDXKEY:
      {
	int dsize2;

	dsize2 = tp_domain_size (transient->setdomain);
	while (domain)
	  {
	    if (domain->setdomain == transient->setdomain)
	      match = 1;
	    else
	      {
		int i, dsize1;
		TP_DOMAIN *element_dom1;
		TP_DOMAIN *element_dom2;

		dsize1 = tp_domain_size (domain->setdomain);
		if (dsize1 > dsize2)
		  break;

		if (dsize1 == dsize2)
		  {
		    match = 1;
		    element_dom1 = domain->setdomain;
		    element_dom2 = transient->setdomain;

		    for (i = 0; i < dsize1; i++)
		      {
			if ((match = tp_domain_match (element_dom1,
						      element_dom2,
						      exact)) == 0)
			  break;
			element_dom1 = element_dom1->next;
			element_dom2 = element_dom2->next;
		      }
		  }
	      }
	    if (match)
	      break;

	    *ins_pos = domain;
	    domain = domain->next_list;
	  }
      }
      break;

    case DB_TYPE_VARCHAR:
    case DB_TYPE_VARBIT:
      while (domain)
	{
	  if (exact == TP_EXACT_MATCH || exact == TP_SET_MATCH)
	    {
	      /* check for descending order */
	      if (domain->precision < transient->precision)
		break;
	      match = domain->precision == transient->precision;
	    }
	  else if (exact == TP_STR_MATCH)
	    {
	      /*
	       * Allow the match if the precisions would allow us to reuse the
	       * string without modification.
	       */
	      match = domain->precision >= transient->precision;
	    }
	  else
	    {
	      /*
	       * Allow matches regardless of precision, let the actual length
	       * of the value determine if it can be assigned.  This is
	       * important for literal strings as their precision will be the
	       * maximum but they can still be assigned to domains with a
	       * smaller precision provided the actual value is within the
	       * destination domain tolerance.
	       */
	      match = 1;
	    }

	  /* check for asc/desc */
	  if (match)
	    {
	      if (domain->is_desc == transient->is_desc)
		;		/* OK */
	      else
		match = 0;
	    }

	  if (match)
	    break;

	  *ins_pos = domain;
	  domain = domain->next_list;
	}
      break;

    case DB_TYPE_CHAR:
    case DB_TYPE_BIT:
      while (domain)
	{
	  /*
	   * Unlike varchar, we have to be a little tighter on domain matches
	   * for fixed width char.  Not as much of a problem since these won't
	   * be used for literal strings.
	   */
	  if (exact == TP_EXACT_MATCH || exact == TP_STR_MATCH ||
	      exact == TP_SET_MATCH)
	    {
	      if (domain->precision > transient->precision)
		break;
	      match = domain->precision == transient->precision;
	    }
	  else
	    {
	      /*
	       * Recognize a precision of TP_FLOATING_PRECISION_VALUE to
	       * indicate a precision whose coercability must be determined
	       * by examing the value.  This is used primarily by db_coerce()
	       * since it must pick a reasonable CHAR domain for the
	       * representation of a literal string.
	       * Accept zero here too since it seems to creep into domains
	       * sometimes.
	       */
	      match = transient->precision == 0 ||
		transient->precision == TP_FLOATING_PRECISION_VALUE ||
		domain->precision >= transient->precision;
	    }

	  /* check for asc/desc */
	  if (match)
	    {
	      if (domain->is_desc == transient->is_desc)
		;		/* OK */
	      else
		match = 0;
	    }

	  if (match)
	    break;

	  *ins_pos = domain;
	  domain = domain->next_list;
	}
      break;

    case DB_TYPE_NCHAR:
      while (domain)
	{
	  if (exact == TP_EXACT_MATCH || exact == TP_STR_MATCH ||
	      exact == TP_SET_MATCH)
	    {
	      if (domain->precision > transient->precision)
		break;
	      match = (domain->precision == transient->precision) &&
		(domain->codeset == transient->codeset);
	    }
	  else
	    {
	      /*
	       * see discussion of special domain precision values
	       * in the DB_TYPE_CHAR case above.
	       */
	      match = (domain->codeset == transient->codeset) &&
		(transient->precision == 0 ||
		 transient->precision == TP_FLOATING_PRECISION_VALUE ||
		 domain->precision >= transient->precision);
	    }

	  /* check for asc/desc */
	  if (match)
	    {
	      if (domain->is_desc == transient->is_desc)
		;		/* OK */
	      else
		match = 0;
	    }

	  if (match)
	    break;

	  *ins_pos = domain;
	  domain = domain->next_list;
	}

      break;

    case DB_TYPE_VARNCHAR:
      while (domain)
	{
	  if (exact == TP_EXACT_MATCH || exact == TP_STR_MATCH ||
	      exact == TP_SET_MATCH)
	    {
	      /* check for descending order */
	      if (domain->precision < transient->precision)
		break;
	      match = (domain->precision == transient->precision) &&
		(domain->codeset == transient->codeset);
	    }
	  else
	    /* see notes above under the DB_TYPE_VARCHAR clause */
	    match = domain->codeset == transient->codeset;

	  /* check for asc/desc */
	  if (match)
	    {
	      if (domain->is_desc == transient->is_desc)
		;		/* OK */
	      else
		match = 0;
	    }

	  if (match)
	    break;

	  *ins_pos = domain;
	  domain = domain->next_list;
	}
      break;

    case DB_TYPE_NUMERIC:
      while (domain)
	{
	  /*
	   * note that we never allow inexact matches here because
	   * the mr_setmem_numeric function is not currently able
	   * to perform the defered coercion.
	   */
	  match = (domain->precision == transient->precision) &&
	    (domain->scale == transient->scale);

	  /* check for asc/desc */
	  if (match)
	    {
	      if (domain->is_desc == transient->is_desc)
		;		/* OK */
	      else
		match = 0;
	    }

	  if (match)
	    break;

	  *ins_pos = domain;
	  domain = domain->next_list;
	}
      break;

    case DB_TYPE_POINTER:
    case DB_TYPE_ERROR:
    case DB_TYPE_OID:
    case DB_TYPE_DB_VALUE:
      /*
       * These are internal domains, they shouldn't be seen, in case they are,
       * just let them match without parameters.
       */

      while (domain)
	{
	  if (domain->is_desc == transient->is_desc)
	    match = 1;
	  if (match)
	    break;

	  *ins_pos = domain;
	  domain = domain->next_list;
	}
      break;

    case DB_TYPE_RESULTSET:
    case DB_TYPE_TABLE:
      break;

      /* don't have a default so we make sure to add clauses for all types */
    }

  return (match ? domain : NULL);
}

#if !defined (SERVER_MODE)
/*
 * tp_swizzle_oid - swizzle oid of a domain class recursively
 *    return: void
 *    domain(in): domain to swizzle
 * Note:
 *   If the code caching the domain was written for the server, we will
 *   only have the OID of the class here if this is an object domain.  If
 *   the domain table is being shared by the client and server (e.g. in
 *   standalone mode), it is important that we "swizzle" the OID into
 *   a corresponding workspace MOP during the cache.  This ensures that we
 *   never get an object domain entered into the client's domain table that
 *   doesn't have a real DB_OBJECT pointer for the domain class.  There is
 *   a lot of code that expects this to be the case.
 */
static void
tp_swizzle_oid (TP_DOMAIN * domain)
{
  TP_DOMAIN *d;

  if ((domain->type->id == DB_TYPE_OBJECT
       || domain->type->id == DB_TYPE_OID
       || domain->type->id == DB_TYPE_VOBJ)
      && domain->class_mop == NULL && !OID_ISNULL (&domain->class_oid))
    {
      /* swizzle the pointer if we're on the client */
      domain->class_mop = ws_mop (&domain->class_oid, NULL);
    }
  else if (domain->type->id == DB_TYPE_SET
	   || domain->type->id == DB_TYPE_MULTISET
	   || domain->type->id == DB_TYPE_SEQUENCE
	   || domain->type->id == DB_TYPE_LIST)
    {
      for (d = domain->setdomain; d != NULL; d = d->next)
	{
	  tp_swizzle_oid (d);
	}
    }
}
#endif /* !SERVER_MODE */

/*
 * tp_domain_find_noparam - get domain for give type
 *    return: domain
 *    type(in): domain type
 *    is_desc(in): desc order for index key_type
 */
TP_DOMAIN *
tp_domain_find_noparam (DB_TYPE type, bool is_desc)
{
  TP_DOMAIN *dom;

  /* tp_domain_find_with_no_param */
  /* type : DB_TYPE_NULL         DB_TYPE_INTEGER       DB_TYPE_FLOAT
     DB_TYPE_DOUBLE       DB_TYPE_ELO           DB_TYPE_TIME
     DB_TYPE_TIMESTAMP    DB_TYPE_DATE          DB_TYPE_MONETARY
     DB_TYPE_SHORT
   */

  for (dom = tp_Domains[type]; dom != NULL; dom = dom->next_list)
    {
      if (dom->is_desc == is_desc)
	{
	  break;		/* found */
	}
    }

  return dom;
}

/*
 * tp_domain_find_numeric - find domain for given type, precision and scale
 *    return: domain that matches
 *    type(in): DB_TYPE
 *    precision(in): precision
 *    scale(in): scale
 *    is_desc(in): desc order for index key_type
 */
TP_DOMAIN *
tp_domain_find_numeric (DB_TYPE type, int precision, int scale, bool is_desc)
{
  TP_DOMAIN *dom;

  /* tp_domain_find_with_precision_scale */
  /* type : DB_TYPE_NUMERIC */

  /* search the list for a domain that matches */
  for (dom = tp_Domains[type]; dom != NULL; dom = dom->next_list)
    {
      /* we MUST perform exact matches here */
      if (dom->precision == precision && dom->scale == scale
	  && dom->is_desc == is_desc)
	{
	  break;		/* found */
	}
    }

  return dom;
}

/*
 * tp_domain_find_charbit - find domain for given codeset and precision
 *    return: domain that matches
 *    type(in): DB_TYPE
 *    codeset(in): code set
 *    precision(in): precision
 *    is_desc(in): desc order for index key_type
 */
TP_DOMAIN *
tp_domain_find_charbit (DB_TYPE type, int codeset, int precision,
			bool is_desc)
{
  TP_DOMAIN *dom;

  /* tp_domain_find_with_codeset_precision */
  /*
   * type : DB_TYPE_NCHAR   DB_TYPE_VARNCHAR
   * DB_TYPE_CHAR    DB_TYPE_VARCHAR
   * DB_TYPE_BIT     DB_TYPE_VARBIT
   */

  if (type == DB_TYPE_NCHAR || type == DB_TYPE_VARNCHAR)
    {
      /* search the list for a domain that matches */
      for (dom = tp_Domains[type]; dom != NULL; dom = dom->next_list)
	{
	  /* we MUST perform exact matches here */
	  if (dom->codeset == codeset && dom->precision == precision
	      && dom->is_desc == is_desc)
	    {
	      break;		/* found */
	    }
	}
    }
  else
    {
      /* search the list for a domain that matches */
      for (dom = tp_Domains[type]; dom != NULL; dom = dom->next_list)
	{
	  /* we MUST perform exact matches here */
	  if (dom->precision == precision && dom->is_desc == is_desc)
	    {
	      break;		/* found */
	    }
	}
    }

  return dom;
}

/*
 * tp_domain_find_object - find domain for given class OID and class
 *    return: domain that matches
 *    type(in): DB_TYPE
 *    class_oid(in): class oid
 *    class_mop(in): class structure
 *    is_desc(in): desc order for index key_type
 */
TP_DOMAIN *
tp_domain_find_object (DB_TYPE type, OID * class_oid,
		       struct db_object * class_mop, bool is_desc)
{
  TP_DOMAIN *dom;

  /* tp_domain_find_with_classinfo */

  /* search the list for a domain that matches */
  for (dom = tp_Domains[type]; dom != NULL; dom = dom->next_list)
    {
      /* we MUST perform exact matches here */

      /* Always prefer comparison of MOPs */
      if (dom->class_mop != NULL && class_mop != NULL)
	{
	  if (dom->class_mop == class_mop && dom->is_desc == is_desc)
	    {
	      break;		/* found */
	    }
	}
      else if (dom->class_mop == NULL && class_mop == NULL)
	{
	  if (OID_EQ (&dom->class_oid, class_oid) && dom->is_desc == is_desc)
	    {
	      break;		/* found */
	    }
	}
      else
	{
	  /*
	   * We have a mixture of OID & MOPS, it probably isn't necessary to be
	   * this general but try to avoid assuming the class OIDs have been set
	   * when there is a MOP present.
	   */
	  if (dom->class_mop == NULL)
	    {
	      if (OID_EQ (&dom->class_oid, WS_OID (class_mop))
		  && dom->is_desc == is_desc)
		{
		  break;	/* found */
		}
	    }
	  else
	    {
	      if (OID_EQ (WS_OID (dom->class_mop), class_oid)
		  && dom->is_desc == is_desc)
		{
		  break;	/* found */
		}
	    }
	}
    }

  return dom;
}

/*
 * tp_domain_find_set - find domain that matches for given set domain
 *    return: domain that matches
 *    type(in): DB_TYPE
 *    setdomain(in): set domain
 *    is_desc(in): desc order for index key_type
 */
TP_DOMAIN *
tp_domain_find_set (DB_TYPE type, TP_DOMAIN * setdomain, bool is_desc)
{
  TP_DOMAIN *dom;
  int dsize;
  int src_dsize;

  src_dsize = tp_domain_size (setdomain);

  /* tp_domain_find_with_setdomain */

  /* search the list for a domain that matches */
  for (dom = tp_Domains[type]; dom != NULL; dom = dom->next_list)
    {
      /* we MUST perform exact matches here */
      if (dom->setdomain == setdomain)
	{
	  break;
	}

      if (dom->is_desc == is_desc)
	{
	  /* don't bother comparing the lists unless the sizes are the same */
	  dsize = tp_domain_size (dom->setdomain);
	  if (dsize == src_dsize)
	    {
	      /* handle the simple single domain case quickly */
	      if (dsize == 1)
		{
		  if (tp_domain_match (dom->setdomain, setdomain,
				       TP_EXACT_MATCH))
		    {
		      break;
		    }
		}
	      else
		{
		  TP_DOMAIN *d1, *d2;
		  int match, i;

		  if (type == DB_TYPE_SEQUENCE || type == DB_TYPE_MIDXKEY)
		    {
		      if (dsize == src_dsize)
			{
			  match = 1;
			  d1 = dom->setdomain;
			  d2 = setdomain;

			  for (i = 0; i < dsize; i++)
			    {
			      match = tp_domain_match (d1, d2,
						       TP_EXACT_MATCH);
			      if (match == 0)
				{
				  break;
				}
			      d1 = d1->next;
			      d2 = d2->next;
			    }
			  if (match == 1)
			    {
			      break;
			    }
			}
		    }
		  else
		    {
		      /* clear the visited flag in the second subdomain list */
		      for (d2 = setdomain; d2 != NULL; d2 = d2->next)
			d2->is_visited = 0;

		      match = 1;
		      for (d1 = dom->setdomain; d1 != NULL && match;
			   d1 = d1->next)
			{
			  for (d2 = setdomain; d2 != NULL; d2 = d2->next)
			    {
			      if (!d2->is_visited
				  && tp_domain_match (d1, d2, TP_EXACT_MATCH))
				break;
			    }
			  /* did we find the domain in the other list ? */
			  if (d2 != NULL)
			    d2->is_visited = 1;
			  else
			    match = 0;
			}
		      if (match == 1)
			break;
		    }
		}
	    }
	}
    }

  return dom;
}

/*
 * tp_domain_cache - caches a transient domain
 *    return: cached domain
 *    transient(in/out): transient domain
 * Note:
 *    If the domain has already been cached, it is located and returned.
 *    Otherwise, a new domain is cached and returned.
 *    In either case, the transient domain may be freed so you should never
 *    depend on it being valid after this function returns.
 *
 *    Note that if a new domain is added to the list, it is always appended
 *    to the end.  It is vital that the deafult "built-in" domain be
 *    at the head of the domain lists in tp_Domains.
 */
TP_DOMAIN *
tp_domain_cache (TP_DOMAIN * transient)
{
  TP_DOMAIN *domain, **dlist;
  TP_DOMAIN *ins_pos;
#if defined (SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /* guard against a bad transient domain */
  if (transient == NULL || transient->type == NULL)
    return NULL;

  /* return this domain if its already cached */
  if (transient->is_cached)
    return transient;

#if !defined (SERVER_MODE)
  /* see comments for tp_swizzle_oid */
  tp_swizzle_oid (transient);
#endif /* !SERVER_MODE */

  /*
   * first search stage: NO LOCK
   */
  /* locate the root of the cache list for domains of this type */
  dlist = &(tp_Domains[transient->type->id]);

  /* search the list for a domain that matches */
  ins_pos = NULL;
  domain = tp_is_domain_cached (*dlist, transient, (TP_MATCH) 1, &ins_pos);

  if (domain != NULL)
    {
      /*
       * We found one in the cache, free the supplied domain and return
       * the cached one
       */
      tp_domain_free (transient);
    }
  else
    {

      /*
       * second search stage: LOCK
       */
#if defined (SERVER_MODE)
      MUTEX_LOCK (rv, tp_domain_cache_lock);	/* LOCK */

      /* locate the root of the cache list for domains of this type */
      dlist = &(tp_Domains[transient->type->id]);

      /* search the list for a domain that matches */
      ins_pos = NULL;
      domain =
	tp_is_domain_cached (*dlist, transient, (TP_MATCH) 1, &ins_pos);
      if (domain != NULL)
	{
	  /*
	   * We found one in the cache, free the supplied domain and return
	   * the cached one
	   */
	  tp_domain_free (transient);
	  MUTEX_UNLOCK (tp_domain_cache_lock);
	  return domain;
	}
#endif /* SERVER_MODE */

      /*
       * We couldn't find one, install the transient domain that was passed in.
       * Since by far the most common domain match is going to be the built-in
       * domain at the head of the list, append new domains to the end of the
       * list as they are encountered.
       */
      transient->is_cached = 1;

      if (*dlist)
	{
	  if (ins_pos)
	    {
	      TP_DOMAIN *tmp;

	      tmp = ins_pos->next_list;
	      ins_pos->next_list = transient;
	      transient->next_list = tmp;
	    }
	}
      else
	{
	  *dlist = transient;
	}

      domain = transient;

#if defined (SERVER_MODE)
      MUTEX_UNLOCK (tp_domain_cache_lock);
#endif /* SERVER_MODE */
    }

  return domain;
}


/*
 * tp_domain_resolve - Find a domain object that matches the type, class,
 * precision, scale and setdomain.
 *    return: domain found
 *    domain_type(in): The basic type of the domain
 *    class_obj(in): The class of the domain (for DB_TYPE_OBJECT)
 *    precision(in): The precision of the domain
 *    scale(in): The class of the domain
 *    setdomain(in): The setdomain of the domain
 * Note:
 *    Current implementation just creates a new one then returns it.
 */
TP_DOMAIN *
tp_domain_resolve (DB_TYPE domain_type, DB_OBJECT * class_obj,
		   int precision, int scale, TP_DOMAIN * setdomain)
{
  TP_DOMAIN *d;

  d = tp_domain_new (domain_type);
  if (d != NULL)
    {
      d->precision = precision;
      d->scale = scale;
      d->class_mop = class_obj;
      d->setdomain = setdomain;

      d = tp_domain_cache (d);
    }

  return d;
}


/*
 * tp_domain_resolve_default - returns the built-in "default" domain for a
 * given primitive type
 *    return: cached domain
 *    type(in): type id
 * Note:
 *    This is used only in special cases where we need to get quickly to
 *    a built-in domain without worrying about domain parameters.
 *    Note that this relies on the fact that the built-in domain is at
 *    the head of our domain lists.
 */
TP_DOMAIN *
tp_domain_resolve_default (DB_TYPE type)
{
  return tp_Domains[type];
}


/*
 * tp_domain_resolve_value - Find a domain object that describes the type info
 * in the DB_VALUE.
 *    return: domain found
 *    val(in): A DB_VALUE for which we need to obtain a domain
 *    dbuf(out): if not NULL, founded domain initialized on dbuf
 * Note:
 *
 *    For "atomic" types, it synthesizes the domain structure, if necessary,
 *    and then returns it.  For more complex things, like objects and sets,
 *    it just goes to the object and retrieves the domain from there.
 *
 *    NOTE: Eventually, we should be storing a pointer to the domain
 *    directly in the DB_VALUE, eliminiating the need to perform this
 *    synthesis.
 *
 *    Hack, tp_domain_resolve_value can be called with an optional domain
 *    structure to use as a template for domains that have to be constructed.
 *    This can be used in places where we need to briefly synthesize a domain
 *    to associate with a DB_VALUE but we want to avoid the overhead of
 *    allocating a domain structure and caching it.
 *    If the domain required is one of the builtins, we just return a pointer
 *    to it, otherwise we put the necessary information in the supplied buffer.
 *    and return it as the value of the function.
 *    Do NOT call tp_domain_free on the return value!
 *    If the value of this buffer is NULL, we allocate and cache a buffer
 *    as usual.
 *
 *    TODO !! Clean this up, examine callers and try to remove some of the
 *    various band-aids we have in here.
 */
TP_DOMAIN *
tp_domain_resolve_value (DB_VALUE * val, TP_DOMAIN * dbuf)
{
  TP_DOMAIN *domain;
  DB_TYPE value_type;

  domain = NULL;
  value_type = (DB_TYPE) PRIM_TYPE (val);

  if (TP_IS_SET_TYPE (value_type))
    {
      DB_SET *set;
      /*
       * For sets, just return the domain attached to the set since it
       * will already have been cached.
       */
      set = db_get_set (val);
      if (set != NULL)
	domain = set_get_domain (set);
      else
	{
	  /* we need to synthesize a wildcard set domain for this value */
	  domain = tp_Domains[value_type];
	}

    }
  else if (value_type == DB_TYPE_MIDXKEY)
    {
      DB_MIDXKEY *midxkey;
      /* For midxkey type, return the domain attached to the value */
      midxkey = db_get_midxkey (val);
      if (midxkey != NULL)
	{
	  domain = midxkey->domain;
	}
      else
	{
	  domain = tp_Domains[value_type];	/* TODO: is possible ? */
	}
    }
  else
    {
      switch (value_type)
	{
	case DB_TYPE_NULL:
	case DB_TYPE_INTEGER:
	case DB_TYPE_FLOAT:
	case DB_TYPE_DOUBLE:
	case DB_TYPE_ELO:
	case DB_TYPE_TIME:
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_DATE:
	case DB_TYPE_MONETARY:
	case DB_TYPE_SHORT:
	  /* domains without parameters, return the built-in domain */
	  domain = tp_Domains[value_type];
	  break;

	case DB_TYPE_OBJECT:
	  {
	    DB_OBJECT *mop;
	    domain = &tp_Object_domain;
	    mop = db_get_object (val);
	    if ((mop == NULL) || (mop->deleted))
	      {
		/* just let the oid thing stand?, this is a NULL anyway */
	      }
	    else
	      {
		/* this is a virtual mop */
		if (WS_ISVID (mop))
		  {
		    domain = &tp_Vobj_domain;
		  }
	      }
	  }
	  break;

	case DB_TYPE_OID:
	  /*
	   * Rather than try to synthesize a domain that is specific to the
	   * class of the object in the value, just return the wildcard
	   * object domain.  This is probably the right thing and is especially
	   * convenient if we're on the server where its harder to deal with
	   * objects.
	   * Also note that there is no "OID" domain, from a domain perspective
	   * an "object" and an "oid" are the same thing, the only difference
	   * is in physical representation.
	   */
	  domain = &tp_Object_domain;
	  break;

	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:	/* new name for DB_TYPE_STRING */
	case DB_TYPE_BIT:
	case DB_TYPE_VARBIT:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  /* must find one with a matching precision */
	  if (dbuf == NULL)
	    domain = tp_domain_new (value_type);
	  else
	    {
	      domain = dbuf;
	      domain_init (domain, value_type);
	    }
	  domain->codeset = db_get_string_codeset (val);
	  domain->precision = db_value_precision (val);

	  /*
	   * Convert references to the "floating" precisions to actual
	   * precisions.  This may not be necessary or desireable?
	   * Zero seems to pop up occasionally in DB_VALUE precisions, until
	   * this is fixed, treat it as the floater for the variable width
	   * types.
	   */
	  if (domain->type->id == DB_TYPE_VARCHAR)
	    {
	      if (domain->precision == 0 ||
		  domain->precision == TP_FLOATING_PRECISION_VALUE ||
		  domain->precision > DB_MAX_VARCHAR_PRECISION)
		domain->precision = DB_MAX_VARCHAR_PRECISION;
	    }
	  else if (domain->type->id == DB_TYPE_VARBIT)
	    {
	      if (domain->precision == 0 ||
		  domain->precision == TP_FLOATING_PRECISION_VALUE ||
		  domain->precision > DB_MAX_VARBIT_PRECISION)
		domain->precision = DB_MAX_VARBIT_PRECISION;
	    }
	  else if (value_type == DB_TYPE_VARNCHAR)
	    {
	      if (domain->precision == 0 ||
		  domain->precision == TP_FLOATING_PRECISION_VALUE ||
		  domain->precision >= DB_MAX_VARNCHAR_PRECISION)
		domain->precision = DB_MAX_VARNCHAR_PRECISION;
	    }
	  if (dbuf == NULL)
	    domain = tp_domain_cache (domain);
	  break;

	case DB_TYPE_NUMERIC:
	  /* must find one with a matching precision and scale */
	  if (dbuf == NULL)
	    domain = tp_domain_new (value_type);
	  else
	    {
	      domain = dbuf;
	      domain_init (dbuf, value_type);
	    }
	  domain->precision = db_value_precision (val);
	  domain->scale = db_value_scale (val);

	  /*
	   * Hack, precision seems to be commonly -1 DB_VALUES, turn this into
	   * the default "maximum" precision.
	   * This may not be necessary any more.
	   */
	  if (domain->precision == -1)
	    domain->precision = DB_DEFAULT_NUMERIC_PRECISION;
	  if (domain->scale == -1)
	    domain->scale = DB_DEFAULT_NUMERIC_SCALE;

	  if (dbuf == NULL)
	    domain = tp_domain_cache (domain);
	  break;

	case DB_TYPE_POINTER:
	case DB_TYPE_ERROR:
	case DB_TYPE_VOBJ:
	case DB_TYPE_SUB:
	case DB_TYPE_VARIABLE:
	case DB_TYPE_DB_VALUE:
	  /*
	   * These are internal domains, they shouldn't be seen, in case they
	   * are, match to a built-in
	   */
	  domain = tp_Domains[value_type];
	  break;

	  /*
	   * things handled in logic outside the switch, shuts up compiler
	   * warnings
	   */
	case DB_TYPE_SET:
	case DB_TYPE_MULTISET:
	case DB_TYPE_SEQUENCE:
	case DB_TYPE_MIDXKEY:
	  break;
	case DB_TYPE_RESULTSET:
	case DB_TYPE_TABLE:
	  break;
	}
    }

  return domain;
}

/*
 * tp_create_domain_resolve_value - adjust domain of a DB_VALUE with respect to
 * the primitive value of the value
 *    return: domain
 *    val(in): DB_VALUE
 *    domain(in): domain
 * Note: val->domain changes
 */
TP_DOMAIN *
tp_create_domain_resolve_value (DB_VALUE * val, TP_DOMAIN * domain)
{
  DB_TYPE value_type;

  value_type = (DB_TYPE) PRIM_TYPE (val);

  switch (value_type)
    {
    case DB_TYPE_INTEGER:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_TIME:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_DATE:
    case DB_TYPE_MONETARY:
    case DB_TYPE_SHORT:
    case DB_TYPE_OBJECT:
    case DB_TYPE_OID:
      break;

    case DB_TYPE_CHAR:
    case DB_TYPE_BIT:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARCHAR:	/* new name for DB_TYPE_STRING */
    case DB_TYPE_VARBIT:
    case DB_TYPE_VARNCHAR:
      if (db_value_precision (val) == TP_FLOATING_PRECISION_VALUE)
	{
	  /* Check for floating precision. */
	  val->domain.char_info.length = domain->precision;
	}
      else
	{
	  if (domain->precision == TP_FLOATING_PRECISION_VALUE)
	    {
	      ;			/* nop */
	    }
	  else
	    {
	      if (db_value_precision (val) > domain->precision)
		{
		  val->domain.char_info.length = domain->precision;
		}
	    }
	}
      break;

    case DB_TYPE_NUMERIC:
      break;

    case DB_TYPE_NULL:		/* for midxkey elements */
      break;

    default:
      return NULL;
    }

  /* if(domain) return tp_domain_cache(domain); */
  return domain;
}

#if !defined (SERVER_MODE)

/*
 * tp_domain_add - Adds a domain structure to a domain list if it doesn't
 * already exist.
 *    return: error code
 *    dlist(in/out): domain list
 *    domain(in): domain structure
 * Note:
 *    This routine should only be used to construct a transient domain.
 *    Note that there are no error messages if a duplicate isn't added.
 */
int
tp_domain_add (TP_DOMAIN ** dlist, TP_DOMAIN * domain)
{
  int error = NO_ERROR;
  TP_DOMAIN *d, *found, *last;
  DB_TYPE type_id;

  last = NULL;
  type_id = domain->type->id;
  for (d = *dlist, found = NULL; d != NULL && found == NULL; d = d->next)
    {
#if 1
      /* >>>>> NEED MORE CONSIDERATION <<<<<
       * do not check duplication
       * must be rollback with tp_domain_match() 
       */
#else /* 0 */
      if (d->type->id == type_id)
	{
	  switch (type_id)
	    {
	    case DB_TYPE_INTEGER:
	    case DB_TYPE_FLOAT:
	    case DB_TYPE_DOUBLE:
	    case DB_TYPE_ELO:
	    case DB_TYPE_TIME:
	    case DB_TYPE_TIMESTAMP:
	    case DB_TYPE_DATE:
	    case DB_TYPE_MONETARY:
	    case DB_TYPE_SUB:
	    case DB_TYPE_POINTER:
	    case DB_TYPE_ERROR:
	    case DB_TYPE_SHORT:
	    case DB_TYPE_VOBJ:
	    case DB_TYPE_OID:
	    case DB_TYPE_NULL:
	    case DB_TYPE_VARIABLE:
	    case DB_TYPE_SET:
	    case DB_TYPE_MULTISET:
	    case DB_TYPE_SEQUENCE:
	    case DB_TYPE_DB_VALUE:
	    case DB_TYPE_VARCHAR:
	    case DB_TYPE_VARNCHAR:
	    case DB_TYPE_VARBIT:
	      found = d;
	      break;

	    case DB_TYPE_NUMERIC:
	      if ((d->precision == domain->precision) &&
		  (d->scale == domain->scale))
		found = d;
	      break;

	    case DB_TYPE_CHAR:
	    case DB_TYPE_NCHAR:
	    case DB_TYPE_BIT:
	      /*
	       * PR)  1.deficient character related with CHAR & VARCHAR in set.
	       * ==> distinguishing VARCHAR from CHAR.
	       * 2. core dumped & deficient character related with
	       * CONST CHAR & CHAR in set.
	       * ==> In case of CHAR,NCHAR,BIT,  cosidering precision.
	       */
	      if (d->precision == domain->precision)
		found = d;
	      break;

	    case DB_TYPE_OBJECT:
	      if (d->class_mop == domain->class_mop)
		found = d;
	      break;

	    default:
	      break;
	    }
	}
#endif /* 1 */

      last = d;
    }

  if (found == NULL)
    {
      if (last == NULL)
	*dlist = domain;
      else
	last->next = domain;
    }
  else
    {
      /* the domain already existed, free the supplied domain */
      tp_domain_free (domain);
    }

  return (error);
}

/*
 * tp_domain_attach - concatenate two domains
 *    return: concatenated domain
 *    dlist(out): domain 1
 *    domain(in): domain 2
 */
int
tp_domain_attach (TP_DOMAIN ** dlist, TP_DOMAIN * domain)
{
  int error = NO_ERROR;
  TP_DOMAIN *d;

  d = *dlist;

  if (*dlist == NULL)
    *dlist = domain;
  else
    {
      while (d->next)
	d = d->next;

      d->next = domain;
    }

  return error;
}


/*
 * tp_domain_drop - Removes a domain from a list if it was found.
 *    return: non-zero if domain was dropped
 *    dlist(in/out): domain list
 *    domain(in/out):  domain class
 * Note:
 *    This routine should only be used to modify a transient domain.
 */
int
tp_domain_drop (TP_DOMAIN ** dlist, TP_DOMAIN * domain)
{
  TP_DOMAIN *d, *found, *prev;
  int dropped = 0;
  DB_TYPE type_id;

  type_id = domain->type->id;
  for (d = *dlist, prev = NULL, found = NULL; d != NULL && found == NULL;
       d = d->next)
    {
      if (d->type->id == type_id)
	{
	  switch (type_id)
	    {
	    case DB_TYPE_INTEGER:
	    case DB_TYPE_FLOAT:
	    case DB_TYPE_DOUBLE:
	    case DB_TYPE_ELO:
	    case DB_TYPE_TIME:
	    case DB_TYPE_TIMESTAMP:
	    case DB_TYPE_DATE:
	    case DB_TYPE_MONETARY:
	    case DB_TYPE_SUB:
	    case DB_TYPE_POINTER:
	    case DB_TYPE_ERROR:
	    case DB_TYPE_SHORT:
	    case DB_TYPE_VOBJ:
	    case DB_TYPE_OID:
	    case DB_TYPE_NULL:
	    case DB_TYPE_VARIABLE:
	    case DB_TYPE_SET:
	    case DB_TYPE_MULTISET:
	    case DB_TYPE_SEQUENCE:
	    case DB_TYPE_DB_VALUE:
	    case DB_TYPE_VARCHAR:
	    case DB_TYPE_VARNCHAR:
	    case DB_TYPE_VARBIT:
	      found = d;
	      break;

	    case DB_TYPE_NUMERIC:
	      if ((d->precision == domain->precision) &&
		  (d->scale == domain->scale))
		found = d;
	      break;

	    case DB_TYPE_CHAR:
	    case DB_TYPE_NCHAR:
	    case DB_TYPE_BIT:
	      /* PR)  1.deficient character related with CHAR & VARCHAR in set.
	         ==> distinguishing VARCHAR from CHAR.
	         2. core dumped & deficient character related with
	         CONST CHAR & CHAR in set.
	         ==> In case of CHAR,NCHAR,BIT,  cosidering precision.
	       */
	      if (d->precision == domain->precision)
		found = d;
	      break;

	    case DB_TYPE_OBJECT:
	      if (d->class_mop == domain->class_mop)
		found = d;
	      break;

	    default:
	      break;
	    }			/* switch (type_id) */
	}			/* if (d->type->id == type_id) */

      if (found == NULL)
	prev = d;
    }

  if (found != NULL)
    {
      if (prev == NULL)
	*dlist = found->next;
      else
	prev->next = found->next;

      found->next = NULL;
      tp_domain_free (found);

      dropped = 1;
    }
  return (dropped);
}
#endif /* !SERVER_MODE */


/*
 * tp_domain_check_class - Work function for tp_domain_filter_list and
 * sm_filter_domain.
 *    return: non-zero if the domain was modified
 *    domain(in): domain to examine
 * Note:
 *    Check the class in a domain and if it was deleted, downgrade the
 *    domain to "object".
 */
static int
tp_domain_check_class (TP_DOMAIN * domain)
{
  int change = 0;

#if !defined (SERVER_MODE)
  int status;

  if (!db_on_server)
    {
      if (domain != NULL && domain->type == tp_Type_object
	  && domain->class_mop != NULL)
	{
	  /* check for deletion of the domain class, assume just one for now */
	  status = locator_does_exist_object (domain->class_mop,
					      DB_FETCH_READ);

	  if (status == LC_DOESNOT_EXIST)
	    {
	      WS_SET_DELETED (domain->class_mop);
	      domain->class_mop = NULL;
	      change = 1;
	    }
	}
    }
#endif /* !SERVER_MODE */

  return (change);
}


/*
 * tp_domain_filter_list - filter out any domain references to classes that
 * have been deleted or are otherwise invalid from domain list
 *    return: non-zero if changes were made
 *    dlist():  domain list
 * Note:
 *    The semantic for deleted classes is that the domain reverts
 *    to the root "object" domain, thereby allowing all object references.
 *    This could become more sophisticated but not without a lot of extra
 *    bookkeeping in the database.   If a domain is downgraded to "object",
 *    be sure to remove it from the list entirely if there is already an
 *    "object" domain present.
 */
int
tp_domain_filter_list (TP_DOMAIN * dlist)
{
  TP_DOMAIN *d, *prev, *next;
  int has_object, changes;

  has_object = changes = 0;

  for (d = dlist, prev = NULL, next = NULL; d != NULL; d = next)
    {
      next = d->next;
      if (tp_domain_check_class (d))
	{
	  /* domain reverted to "object" */
	  if (!has_object)
	    {
	      has_object = 1;
	      prev = d;
	    }
	  else
	    {
	      /*
	       * redundant "object" domain, remove, prev can't be NULL here,
	       * will always have at least one domain structure at the head of
	       * the list
	       */
	      prev->next = next;
	      d->next = NULL;
	      tp_domain_free (d);
	      changes = 1;
	    }
	}
      else
	{
	  /* domain is still valid, see if its "object" */
	  if (d->type == tp_Type_object && d->class_mop == NULL)
	    {
	      has_object = 1;
	    }
	  else if (pr_is_set_type (d->type->id) && d->setdomain != NULL)
	    {
	      /* recurse on set domain list */
	      changes = tp_domain_filter_list (d->setdomain);
	    }
	  prev = d;
	}
    }
  return (changes);
}

/*
 * tp_domain_name - generate a printed representation for the given domain.
 *    return: non-zero if buffer overflow, -1 for error
 *    domain(in): domain structure
 *    buffer(out): output buffer
 *    maxlen(in): maximum length of buffer
 */
int
tp_domain_name (TP_DOMAIN * domain, char *buffer, int maxlen)
{
  /*
   * need to get more sophisticated here, do full name decomposition and
   * check maxlen
   */
  strncpy (buffer, domain->type->name, maxlen);
  buffer[maxlen - 1] = '\0';
  return (0);
}


/*
 * tp_value_domain_name - generates printed representation of the domain for a
 * given DB_VALUE.
 *    return: non-zero if buffer overflow, -1 if error
 *    value(in): value to examine
 *    buffer(out): output buffer
 *    maxlen(in): maximum length of buffer
 */
int
tp_value_domain_name (DB_VALUE * value, char *buffer, int maxlen)
{
  /* need to get more sophisticated here */

  strncpy (buffer, pr_type_name (DB_VALUE_TYPE (value)), maxlen);
  buffer[maxlen - 1] = '\0';
  return (0);
}

/*
 * tp_domain_find_compatible - two domains are compatible for the purposes of
 * assignment of values.
 *    return: non-zero if domains are compatible
 *    src(in): domain we're wanting to assign
 *    dest(in): domain we're trying to go into
 * Note:
 *    Domains are compatible if they are equal.
 *    Further, domain 1 is compatible with domain 2 if domain 2 is more
 *    general.
 *
 *    This will not properly detect of the domains are compatible due
 *    to a proper subclass superclass relationship between object domains.
 *    It will only check to see if the class matches exactly.
 *
 *    This is the function used to test to see if a particular set domain
 *    is "within" another set domain during assignment of set values to
 *    attributes.  src in this case will be the domain of the set we were
 *    given and dest will be the domain of the attribute.
 *    All of the sub-domains in src must also be found in dest.
 *
 *    This is somewhat different than tp_domain_match because the comparison
 *    of set domains is more of an "is it a subset" operation rather than
 *    an "is it equal to" operation.
 */
static const TP_DOMAIN *
tp_domain_find_compatible (const TP_DOMAIN * src, const TP_DOMAIN * dest)
{
  const TP_DOMAIN *d, *found;

  found = NULL;

  /*
   * If we have a hierarchical domain, perform a lenient "superset" comparison
   * rather than an exact match.
   */
  if (TP_IS_SET_TYPE (src->type->id) || src->type->id == DB_TYPE_VARIABLE)
    {
      for (d = dest; d != NULL && found == NULL; d = d->next)
	{
	  if (src->type->id == d->type->id &&
	      tp_domain_compatible (src->setdomain, dest->setdomain))
	    {
	      found = d;
	    }
	}
    }
  else
    {
      /*
       * Wasn't a hiearchical domain, check for exact match on the type
       * and any parameters.  Note that we could in theory allow "superset"
       * matches for parameters here too, e.g. numeric(12,2) could be said
       * to be compatible with numeric(10,2) since the precision is higher.
       * In practice though, these domain parameters have implications for
       * the physical storage of the value so it isn't in general allowable
       * to just slam a more lenient domain on top of a particular value.
       * NOTE, tp_domain_match has grown pretty general, it may not be
       * an especially effecient thing to be calling here, particularly
       * since this function is going to be called for every set attribute
       * assignment.  Consider optimizing some of the common tests.
       */

      for (d = dest; d != NULL && found == NULL; d = d->next)
	{
	  if (tp_domain_match ((TP_DOMAIN *) src, (TP_DOMAIN *) d,
			       TP_EXACT_MATCH))
	    {
	      /* exact match flag is on */
	      found = d;
	    }
	}

      /*
       * NOTE: tp_domain_match is not going to properly detect if the source
       * domain class is a subset of the destination domain class, which
       * would mean that they are in fact compatible domains.  We could
       * extend tp_domain_match (with exact flag off of course) to allow
       * this but that's a rather risky change to make right now.  We could
       * also stop and test for this here but it doesn't really matter.
       * This function will give a false negative on the domain which is
       * ok for its current use of trying to avoid a full set coercion
       * operation.  The bug that prompted this rewrite was that we were
       * giving a false positive which we must not do.
       */
    }

  return found;
}

/*
 * tp_domain_compatible - check compatibility of src domain w.r.t dest
 *    return: 1 if compatible, 0 otherwise
 *    src(in): src domain
 *    dest(in): dest domain
 */
int
tp_domain_compatible (const TP_DOMAIN * src, const TP_DOMAIN * dest)
{
  const TP_DOMAIN *d;
  int equal = 0;

  if (src != NULL && dest != NULL)
    {
      equal = 1;
      if (src != dest)
	{
	  /*
	   * for every domain in src, make sure we have a compatible one in
	   * dest
	   */
	  for (d = src; equal && d != NULL; d = d->next)
	    {
	      if (tp_domain_find_compatible (d, dest) == NULL)
		equal = 0;
	    }
	}
    }
  return (equal);
}


/*
 * tp_domain_select - select a domain from a list of possible domains that is
 * the exact match (or closest, depending on the value of exact_match) to the
 * supplied value.
 *    return: domain
 *    domain_list(in): list of possible domains
 *    value(in): value of interest
 *    allow_coercion(in): non-zero if coercion will be allowed
 *    exact_match(in): controls tolerance permitted during match
 * Note:
 *    This operation is used for basic domain compatibility checking
 *    as well as value coercion.
 *    If the allow_coercion flag is on, the tp_Domain_conversion_matrix
 *    will be consulted to find an appropriate domain in the case
 *    where there is no exact match.
 *    If an appropriate domain could not be found, NULL is returned.
 *
 *    This is known not to work correctly for nested set domains.  In order
 *    for the best domain to be selected, we must recursively check the
 *    complete set domains here.
 *
 *    The exact_match flag determines if we allow "tolerance" matches when
 *    checking domains for attributes.  See commentary in tp_domain_match
 *    for more information.
 */
TP_DOMAIN *
tp_domain_select (const TP_DOMAIN * domain_list,
		  const DB_VALUE * value,
		  int allow_coercion, TP_MATCH exact_match)
{
  TP_DOMAIN *best, *d;
  TP_DOMAIN **others;
  DB_TYPE vtype;
  int i;

  best = NULL;

  /*
   * NULL values are allowed in any domain, a NULL domain means that any value
   * is allowed, return the first thing on the list.
   */
  if (value == NULL || domain_list == NULL ||
      (vtype = DB_VALUE_TYPE (value)) == DB_TYPE_NULL)
    return (TP_DOMAIN *) domain_list;

  /*
   * If we've been given a value containing an OID, swizzle it into
   * an object if we're on the client.  If we're on the server,
   * we'll trust it for now under the assumption that the appropriate
   * domain check has already been made at a higher level.  We could
   * check it again here but following OIDs on the server should be
   * avoided if possible to reduce hits on the page buffer pool.
   * Note that we swizzle into a temporary value to keep the input value
   * constant.  On the client, we may want to be trusting here too since
   * OIDs can't come in through the API from the user.
   * Need a more generally accessible swizzling function, like the one in set.c
   */

  if (vtype == DB_TYPE_OID)
    {
      if (db_on_server)
	{
	  /*
	   * On the server, just make sure that we have any object domain in
	   * the list.
	   */
	  for (d = (TP_DOMAIN *) domain_list; d != NULL && best == NULL;
	       d = d->next)
	    {
	      if (d->type->id == DB_TYPE_OBJECT)
		best = d;
	    }
	}
#if !defined (SERVER_MODE)
      else
	{
	  /*
	   * On the client, swizzle to an object and fall in to the next
	   * clause
	   */
	  OID *oid;
	  DB_OBJECT *mop;
	  DB_VALUE temp;

	  oid = (OID *) db_get_oid (value);
	  if (OID_ISNULL (oid))
	    /* this is the same as the NULL case above */
	    return (TP_DOMAIN *) domain_list;
	  else
	    {
	      mop = ws_mop (oid, NULL);
	      db_make_object (&temp, mop);
	      /*
	       * we don't have to worry about clearing this since its an
	       * object
	       */
	      value = (const DB_VALUE *) &temp;
	      vtype = DB_TYPE_OBJECT;
	    }
	}
#endif /* !SERVER_MODE */
    }

  /*
   * Handling of object domains is more complex than just comparing the
   * types and parameters.  We have to see if the instance's class is
   * somewhere in the subclass hierarchy of the domain class.
   * This can't be done on the server yet though presumably we could
   * implement something like this using OID chasing.
   */

  if (vtype == DB_TYPE_OBJECT)
    {
      if (db_on_server)
	{
	  /*
	   * we really shouldn't get here but if we do, handle it like the
	   * OID case above, just return the first object domain that we find.
	   */
	  for (d = (TP_DOMAIN *) domain_list; d != NULL && best == NULL;
	       d = d->next)
	    {
	      if (d->type->id == DB_TYPE_OBJECT)
		best = d;
	    }
	  return best;
	}
#if !defined (SERVER_MODE)
      else
	{
	  /*
	   * On the client, check to see if the instance is within the subclass
	   * hierarchy of the object domains.  If there are more than one
	   * acceptable domains, we just pick the first one.
	   */
	  DB_OBJECT *obj = db_get_object (value);

	  for (d = (TP_DOMAIN *) domain_list; d != NULL && best == NULL;
	       d = d->next)
	    {
	      if (d->type->id == DB_TYPE_OBJECT &&
		  sm_check_object_domain (d, obj))
		best = d;
	    }
	}
#endif /* !SERVER_MODE */
    }

#if !defined (SERVER_MODE)
  else if (vtype == DB_TYPE_POINTER)
    {
      /*
       * This is necessary in order to correctly choose an object domain from
       * the domain list when doing an insert nested inside a heterogeneous
       * set, e.g.:
       * create class foo (a int);
       * create class bar (b set_of(string, integer, foo));
       * insert into bar (b) values ({insert into foo values (1)});
       */
      DB_OTMPL *otmpl =
	(DB_OTMPL *) ((DB_OTMPL *) DB_GET_POINTER (value))->classobj;

      for (d = (TP_DOMAIN *) domain_list; d != NULL && best == NULL;
	   d = d->next)
	{
	  if (d->type->id == DB_TYPE_OBJECT
	      && sm_check_class_domain (d, (DB_OBJECT *) otmpl))
	    best = d;
	}
    }
#endif /* !SERVER_MODE */

  else if TP_IS_SET_TYPE
    (vtype)
    {
      /*
       * Now that we cache set domains, there might be a faster way to do
       * this !
       */
      DB_SET *set;
      set = db_get_set (value);
      for (d = (TP_DOMAIN *) domain_list; d != NULL && best == NULL;
	   d = d->next)
	{
	  if (d->type->id == vtype)
	    {
	      if (set_check_domain (set, d) == DOMAIN_COMPATIBLE)
		best = d;
	    }
	}
    }
  else
    {
      /*
       * synthesize a domain for the value and look for a match.
       * Could we be doing this for the set values too ?
       * Hack, since this will be used only for comparison purposes,
       * don't go through the overhead of caching the domain every time,
       * especially for numeric types.  This will be a lot simpler if we
       * store the domain
       * pointer directly in the DB_VALUE.
       */
      TP_DOMAIN temp_domain, *val_domain;
      val_domain = tp_domain_resolve_value ((DB_VALUE *) value, &temp_domain);

      for (d = (TP_DOMAIN *) domain_list; d != NULL && best == NULL;
	   d = d->next)
	{
	  /* hack, try allowing "tolerance" matches of the domain ! */
	  if (tp_domain_match (d, val_domain, exact_match))
	    best = d;
	}
    }

  /*
   * If we have not found an exact match, and we're allowing coercion,
   * Try to select the domain in the list that is most appropriate, if possible.
   * The domains in the conversion matrix are really only used for their
   * basic type id.  We may need to be smarter about selecting
   * from among multiple parameterized domains of the same type, for example
   * if there is a NUMERIC(12, 2) and a NUMERIC(4, 0) and a value with the
   * effect domain of NUMERIC(5, 0), we should make sure we select the larger
   * of the two available domains.  In practice, this can't happen now because
   * we don't allow explicitly specified or union domains to include
   * multiple element domains for any particular basic type.
   *
   * THIS LOGIC IS BROKEN FOR NESTED SETS !
   */
  if (best == NULL && allow_coercion)
    {
      others = tp_Domain_conversion_matrix[vtype];
      if (others != NULL)
	{
	  for (i = 0; others[i] != NULL && best == NULL; i++)
	    {
	      for (d = (TP_DOMAIN *) domain_list; d != NULL && best == NULL;
		   d = d->next)
		{
		  if (d->type == others[i]->type)
		    best = d;
		}
	    }
	}
    }

  return (best);
}

#if !defined (SERVER_MODE)
/*
 * tp_domain_select_type - similar to tp_domain_select except that it does not
 * require the existance of an actual DB_VALUE containing a proposed value.
 *    return: best domain from the list, NULL if none
 *    domain_list(in): domain lis t
 *    type(in): basic data type
 *    class(in): class if type == DB_TYPE_OBJECT
 *    allow_coercion(in): flag to enable coercions
 * Note:
 *    this cannot be used for checking set domains.
 *
 *    Called by ldr.c, think about trying to convert ldr.c
 *    to use the function above or else come up with a common implemenation
 *    for both cases.
 */
TP_DOMAIN *
tp_domain_select_type (const TP_DOMAIN * domain_list,
		       DB_TYPE type, DB_OBJECT * class_mop,
		       int allow_coercion)
{
  const TP_DOMAIN *best, *d;
  TP_DOMAIN **others;
  int i;

  /*
   * NULL values are allowed in any domain, a NULL domain means that any value
   * is allowed, return the first thing on the list
   */
  if (type == DB_TYPE_NULL || domain_list == NULL)
    best = domain_list;

  else
    {
      best = NULL;
      /*
       * loop through the domain elements looking for one the fits,
       * rather than make type comparisons for each element in the loop,
       * do them out here and duplicate the loop
       */

      if (type == DB_TYPE_OBJECT)
	{
	  for (d = domain_list; d != NULL && best == NULL; d = d->next)
	    {
	      if (d->type->id == DB_TYPE_OBJECT &&
		  sm_check_class_domain ((TP_DOMAIN *) d, class_mop))
		best = d;
	    }
	}
      else if (TP_IS_SET_TYPE (type))
	{
	  for (d = domain_list; d != NULL && best == NULL; d = d->next)
	    {
	      if (d->type->id == type)
		/* can't check the actual set domain here, assume its ok */
		best = d;
	    }
	}
      else
	{
	  for (d = domain_list; d != NULL && best == NULL; d = d->next)
	    {
	      if (d->type->id == type || d->type->id == DB_TYPE_VARIABLE)
		best = d;
	    }
	}

      if (best == NULL && allow_coercion)
	{
	  others = tp_Domain_conversion_matrix[type];
	  if (others != NULL)
	    {
	      /*
	       * loop through the allowable conversions until we find
	       * one that appears in the supplied domain list, the
	       * array is ordered in terms of priority,
	       * THIS WILL NOT WORK CORRECTLY FOR NESTED SETS
	       */
	      for (i = 0; others[i] != NULL && best == NULL; i++)
		{
		  for (d = domain_list; d != NULL && best == NULL;
		       d = d->next)
		    {
		      if (d->type == others[i]->type)
			best = d;
		    }
		}
	    }
	}
    }

  return ((TP_DOMAIN *) best);
}
#endif /* !SERVER_MODE */


/*
 * tp_domain_check - does basic validation of a value against a domain.
 *    return: domain status
 *    domain(in): destination domain
 *    value(in): value to look at
 *    exact_match(in): controls the tolerance permitted for the match
 * Note:
 *    It does NOT do coercion.  If the intention is to perform coercion,
 *    them tp_domain_select should be used.
 *    Exact match is used to request a deferred coercion of values that
 *    are within "tolerance" of the destination domain.  This is currently
 *    only specified for assignment of attribute values and will be
 *    recognized only by those types whose "setmem" and "writeval" functions
 *    are able to perform delayed coercion.  Examples are the CHAR types
 *    which will do truncation or blank padding as the values are being
 *    assigned.  See commentary in tp_domain_match for more information.
 */
TP_DOMAIN_STATUS
tp_domain_check (const TP_DOMAIN * domain,
		 const DB_VALUE * value, TP_MATCH exact_match)
{
  TP_DOMAIN_STATUS status;
  TP_DOMAIN *d;

  if (domain == NULL)
    status = DOMAIN_COMPATIBLE;
  else
    {
      d = tp_domain_select (domain, value, 0, exact_match);
      if (d == NULL)
	status = DOMAIN_INCOMPATIBLE;
      else
	status = DOMAIN_COMPATIBLE;
    }

  return (status);
}

/*
 * COERCION
 */


/*
 * tp_can_steal_string - check if the string currently held in "val" can be
 * safely reused
 *    WITHOUT copying.
 *    return: error code
 *    val(in): source (and destination) value
 *    desired_domain(in): desired domain for coerced value
 * Note:
 *    Basically, this holds if
 *       1. the dest precision is "floating", or
 *       2. the dest type is varying and the length of the string is less
 *          than or equal to the dest precision, or
 *       3. the dest type is fixed and the length of the string is exactly
 *          equal to the dest precision.
 *    Since the desired domain is often a varying char, this wins often.
 */
int
tp_can_steal_string (const DB_VALUE * val, const DB_DOMAIN * desired_domain)
{
  DB_TYPE original_type, desired_type;
  int original_length, desired_precision;

  original_type = DB_VALUE_DOMAIN_TYPE (val);
  original_length = DB_GET_STRING_LENGTH (val);
  desired_type = desired_domain->type->id;
  desired_precision = desired_domain->precision;

  if (desired_precision == TP_FLOATING_PRECISION_VALUE)
    desired_precision = original_length;

  switch (desired_type)
    {
    case DB_TYPE_CHAR:
      return (desired_precision == original_length &&
	      (original_type == DB_TYPE_CHAR ||
	       original_type == DB_TYPE_VARCHAR));
    case DB_TYPE_VARCHAR:
      return (desired_precision >= original_length &&
	      (original_type == DB_TYPE_CHAR ||
	       original_type == DB_TYPE_VARCHAR));
    case DB_TYPE_NCHAR:
      return (desired_precision == original_length &&
	      (original_type == DB_TYPE_NCHAR ||
	       original_type == DB_TYPE_VARNCHAR));
    case DB_TYPE_VARNCHAR:
      return (desired_precision >= original_length &&
	      (original_type == DB_TYPE_NCHAR ||
	       original_type == DB_TYPE_VARNCHAR));
    case DB_TYPE_BIT:
      return (desired_precision == original_length &&
	      (original_type == DB_TYPE_BIT ||
	       original_type == DB_TYPE_VARBIT));
    case DB_TYPE_VARBIT:
      return (desired_precision >= original_length &&
	      (original_type == DB_TYPE_BIT ||
	       original_type == DB_TYPE_VARBIT));
    default:
      return 0;
    }
}

/*
 * tp_null_terminate - NULL terminate the given DB_VALUE string.
 *    return: NO_ERROR or error code
 *    src(in): string to null terminate
 *    strp(out): pointer for output
 *    str_len(in): length of 'str'
 *    do_alloc(out): set true if allocation occurred
 * Note:
 *    Don't call this unless src is a string db_value.
 */
static int
tp_null_terminate (const DB_VALUE * src, char **strp, int str_len,
		   bool * do_alloc)
{
  char *str;
  int str_size;

  *do_alloc = false;		/* init */

  str = DB_GET_STRING (src);
  str_size = DB_GET_STRING_SIZE (src);

  if (str[str_size] == '\0')
    {
      /* already NULL terminated */
      *strp = str;

      return NO_ERROR;
    }

  if (str_size >= str_len)
    {
      if ((*strp = (char *) malloc (str_size + 1)) == NULL)
	return ER_OUT_OF_VIRTUAL_MEMORY;

      *do_alloc = true;		/* mark as alloced */
    }

  memcpy (*strp, str, str_size);
  (*strp)[str_size] = '\0';

  return NO_ERROR;
}

/*
 * tp_atotime - coerce a string to a time
 *    return: NO_ERROR or error code
 *    src(in): string DB_VALUE
 *    temp(out): time container
 * Note:
 *    Accepts strings that are not null terminated. Don't call this unless
 *    src is a string db_value.
 */
static int
tp_atotime (const DB_VALUE * src, DB_TIME * temp)
{
  char str[150];
  char *strp = str;
  bool do_alloc;
  int status = NO_ERROR;

  if ((tp_null_terminate (src, &strp, sizeof (str), &do_alloc) != NO_ERROR)
      || (db_string_to_time (strp, temp) != NO_ERROR))
    status = ER_FAILED;

  if (do_alloc)
    free_and_init (strp);

  return status;
}


/*
 * tp_atodate - coerce a string to a date
 *    return: NO_ERROR or error code
 *    src(in): string DB_VALUE
 *    temp(out): date container
 * Note:
 *    Accepts strings that are not null terminated. Don't call this unless
 *    src is a string db_value.
 */
static int
tp_atodate (const DB_VALUE * src, DB_DATE * temp)
{
  char str[150];
  char *strp = str;
  bool do_alloc;
  int status = NO_ERROR;

  if ((tp_null_terminate (src, &strp, sizeof (str), &do_alloc) != NO_ERROR)
      || (db_string_to_date (strp, temp) != NO_ERROR))
    status = ER_FAILED;

  if (do_alloc)
    free_and_init (strp);

  return status;
}				/* tp_atodate */


/*
 * tp_atoutime - coerce a string to a utime.
 *    return: NO_ERROR or error code
 *    src(in): string DB_VALUE
 *    temp(out): utime container
 * Note:
 *    Accepts strings that are not null terminated. Don't call this unless
 *    src is a string db_value.
 */
static int
tp_atoutime (const DB_VALUE * src, DB_UTIME * temp)
{
  char str[150];
  char *strp = str;
  bool do_alloc;
  int status = NO_ERROR;

  if ((tp_null_terminate (src, &strp, sizeof (str), &do_alloc) != NO_ERROR)
      || (db_string_to_utime (strp, temp) != NO_ERROR))
    status = ER_FAILED;

  if (do_alloc)
    free_and_init (strp);

  return status;
}


/*
 * tp_atonumeric - Coerce a string to a numeric.
 *    return: NO_ERROR or error code
 *    src(in): string DB_VALUE
 *    temp(out): numeirc container
 * Note:
 *    Accepts strings that are not null terminated. Don't call this unless
 *    src is a string db_value.
 */
static int
tp_atonumeric (const DB_VALUE * src, DB_VALUE * temp)
{
  char str[150];
  char *strp = str;
  bool do_alloc;
  int status = NO_ERROR;

  if ((tp_null_terminate (src, &strp, sizeof (str), &do_alloc) != NO_ERROR)
      || (numeric_coerce_string_to_num (strp, temp) != NO_ERROR))
    {
      status = ER_FAILED;
    }

  if (do_alloc)
    {
      free_and_init (strp);
    }

  return status;
}


/*
 * tp_atof - Coerce a string to a double.
 *    return: NO_ERROR or error code
 *    src(in): string DB_VALUE
 *    num_value(out): float container
 * Note:
 *    Accepts strings that are not null terminated. Don't call this unless
 *    src is a string db_value.
 */
static int
tp_atof (const DB_VALUE * src, double *num_value)
{
  char str[150];
  char *strp = str;
  bool do_alloc;
  double d;
  char *p;
  int status = NO_ERROR;

  if (tp_null_terminate (src, &strp, sizeof (str), &do_alloc) != NO_ERROR)
    return ER_FAILED;

  /* don't use atof() which cannot detect the error. */
  d = strtod (strp, &p);
  if (*p != 0)			/* all input does not consumed */
    {
      status = ER_FAILED;
    }
  else
    {
      *num_value = d;
    }

  if (do_alloc)
    free_and_init (strp);

  return status;
}


/*
 * tp_itoa - int to string representation for given radix
 *    return: string pointer (given or malloc'd)
 *    value(in): int value
 *    string(in/out): dest buffer or NULL
 *    radix(in): int value between 2 and 36
 */
static char *
tp_itoa (int value, char *string, int radix)
{
  char tmp[33];
  char *tp = tmp;
  int i;
  unsigned v;
  int sign;
  char *sp;

  if (radix > 36 || radix <= 1)
    {
      return 0;
    }

  sign = (radix == 10 && value < 0);
  if (sign)
    v = -value;
  else
    v = (unsigned) value;
  while (v || tp == tmp)
    {
      i = v % radix;
      v = v / radix;
      if (i < 10)
	*tp++ = i + '0';
      else
	*tp++ = i + 'a' - 10;
    }

  if (string == NULL)
    {
      string = (char *) malloc ((tp - tmp) + sign + 1);
      if (string == NULL)
	return string;
    }
  sp = string;

  if (sign)
    *sp++ = '-';
  while (tp > tmp)
    *sp++ = *--tp;
  *sp = '\0';
  return string;
}

/*
 * import from bfmt_print() in cnv.c
 */
#define BITS_IN_BYTE            8
#define HEX_IN_BYTE             2
#define BITS_IN_HEX             4

#define BYTE_COUNT(bit_cnt)     (((bit_cnt)+BITS_IN_BYTE-1)/BITS_IN_BYTE)
#define BYTE_COUNT_HEX(bit_cnt) (((bit_cnt)+BITS_IN_HEX-1)/BITS_IN_HEX)

/*
 * bfmt_print - Change the given string to a representation of the given
 * bit string value in the given format.
 *    return: NO_ERROR or -1 if max_size is too small
 *    bfmt(in): 0: for binary representation or 1: for hex representation
 *    the_db_bit(in): DB_VALUE
 *    string(out): output buffer
 *    max_size(in): size of output buffer
 */
static int
bfmt_print (int bfmt, const DB_VALUE * the_db_bit, char *string, int max_size)
{
  int length = 0;
  int string_index = 0;
  int byte_index;
  int bit_index;
  char *bstring;
  int error = NO_ERROR;
  static const char digits[16] = { '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
  };

  /* Get the buffer and the length from the_db_bit */
  bstring = DB_GET_BIT (the_db_bit, &length);

  switch (bfmt)
    {
    case 0:			/* BIT_STRING_BINARY */
      if (length + 1 > max_size)
	error = -1;
      else
	{
	  for (byte_index = 0; byte_index < BYTE_COUNT (length); byte_index++)
	    {
	      for (bit_index = 7;
		   bit_index >= 0 && string_index < length; bit_index--)
		{
		  *string =
		    digits[((bstring[byte_index] >> bit_index) & 0x1)];
		  string++;
		  string_index++;
		}
	    }
	  *string = '\0';
	}
      break;

    case 1:			/* BIT_STRING_HEX */
      if (BYTE_COUNT_HEX (length) + 1 > max_size)
	error = -1;
      else
	{
	  for (byte_index = 0; byte_index < BYTE_COUNT (length); byte_index++)
	    {
	      *string = digits[((bstring[byte_index] >> BITS_IN_HEX) & 0x0f)];
	      string++;
	      string_index++;
	      if (string_index < BYTE_COUNT_HEX (length))
		{
		  *string = digits[((bstring[byte_index] & 0x0f))];
		  string++;
		  string_index++;
		}
	    }
	  *string = '\0';
	}
      break;

    default:
      break;
    }

  return error;
}


#define ROUND(x)		  (int)((x) > 0 ? ((x) + .5) : ((x) - .5))
#define SECONDS_IN_A_DAY	  (long)(24L * 60L * 60L)
#define TP_IS_CHAR_STRING(db_val_type)					\
    (db_val_type == DB_TYPE_CHAR || db_val_type == DB_TYPE_VARCHAR ||	\
     db_val_type == DB_TYPE_NCHAR || db_val_type == DB_TYPE_VARNCHAR)

#define TP_IS_DATETIME_TYPE(db_val_type)				\
    (db_val_type == DB_TYPE_DATE || db_val_type == DB_TYPE_TIME ||	\
     db_val_type == DB_TYPE_TIMESTAMP)

#define TP_IMPLICIT_COERCION_NOT_ALLOWED(src_type, dest_type)		\
   ((TP_IS_CHAR_STRING(src_type) && !(TP_IS_CHAR_STRING(dest_type) ||	\
				      TP_IS_DATETIME_TYPE(dest_type)))	\
                                 ||					\
    (!TP_IS_CHAR_STRING(src_type) && TP_IS_CHAR_STRING(dest_type)))

#define MAKE_DESIRED_STRING_DB_VALUE(desired_type, desired_domain, new_string, target, status, data_stat) \
          { \
	      DB_VALUE temp; \
	      status = DOMAIN_COMPATIBLE; \
	      switch(desired_type) { \
		case DB_TYPE_CHAR: \
		  db_make_char(&temp, desired_domain->precision, new_string, \
			       strlen(new_string)); \
		  break; \
		case DB_TYPE_NCHAR: \
		  db_make_nchar(&temp, desired_domain->precision, new_string, \
				strlen(new_string)); \
		  break; \
		case DB_TYPE_VARCHAR: \
		  db_make_varchar(&temp, desired_domain->precision, \
				  new_string, strlen(new_string)); \
		  break; \
		case DB_TYPE_VARNCHAR: \
		  db_make_varnchar(&temp, desired_domain->precision, \
				   new_string, strlen(new_string)); \
		  break; \
		default: /* Can't get here.  This just quiets the compiler */ \
		  break; \
	      } \
	      temp.need_clear = true; \
	      if (db_char_string_coerce(&temp,target,&data_stat) != NO_ERROR) \
		  status = DOMAIN_INCOMPATIBLE; \
	      else \
		  status = DOMAIN_COMPATIBLE; \
	      pr_clear_value(&temp); \
	  }

/*
 * tp_value_coerce - Coerce a value into one of another domain.
 *    return: error code
 *    src(in): source value
 *    dest(out): destination value
 *    desired_domain(in): destination domain
 */
TP_DOMAIN_STATUS
tp_value_coerce (const DB_VALUE * src, DB_VALUE * dest,
		 const TP_DOMAIN * desired_domain)
{
  return tp_value_cast (src, dest, desired_domain, true);
}

/*
 * tp_value_coerce_internal - Coerce a value into one of another domain.
 *    return: error code
 *    src(in): source value
 *    dest(out): destination value
 *    desired_domain(in): destination domain
 *    implicit_coercion(in): flag for whether the coercion is implicit
 *    do_domain_select(in): flag for select appropriate domain from
 *                          'desired_domain'
 */
static TP_DOMAIN_STATUS
tp_value_cast_internal (const DB_VALUE * src, DB_VALUE * dest,
			const TP_DOMAIN * desired_domain,
			bool implicit_coercion, bool do_domain_select)
{
  DB_TYPE desired_type, original_type;
  int err;
  TP_DOMAIN_STATUS status;
  TP_DOMAIN *best;
  const DB_MONETARY *v_money;
  DB_UTIME v_utime;
  DB_TIME v_time;
  DB_DATE v_date;
  DB_DATA_STATUS data_stat;
  DB_VALUE temp, *target;
  int hour, minute, second;
  int year, month, day;

  err = NO_ERROR;
  status = DOMAIN_COMPATIBLE;

  /* A NULL src is allowed but destination remains NULL, not desired_domain */
  if (src == NULL || (original_type = DB_VALUE_TYPE (src)) == DB_TYPE_NULL)
    {
      db_make_null (dest);
      return (status);
    }

  /* If more than one destination domain, select the most appropriate */
  if (do_domain_select)
    {
      if (desired_domain->next != NULL)
	{
	  best = tp_domain_select (desired_domain, src, 1, TP_ANY_MATCH);
	  if (best != NULL)
	    desired_domain = best;
	}
    }
  desired_type = desired_domain->type->id;

  if (desired_type == original_type)
    {
      /*
       * If there is an easy to check exact match on a non-parameterized
       * domain, just do a simple clone of the value.
       */
      if (!desired_domain->is_parameterized)
	{
	  if (src != dest)
	    pr_clone_value ((DB_VALUE *) src, dest);
	  return (status);
	}
      else
	{			/* is parameterized domain */
	  switch (desired_type)
	    {
	    case DB_TYPE_NUMERIC:
	      if (desired_domain->precision == DB_VALUE_PRECISION (src) &&
		  desired_domain->scale == DB_VALUE_SCALE (src))
		{
		  if (src != dest)
		    pr_clone_value ((DB_VALUE *) src, dest);
		  return (status);
		}
	      break;
	    case DB_TYPE_OID:
	      if (src != dest)
		{
		  pr_clone_value ((DB_VALUE *) src, dest);
		}
	      return (status);
	    default:
	      /* pr_is_string_type(desired_type) - NEED MORE CONSIDERATION */
	      break;
	    }			/* switch (desired_type) */
	}			/* else */
    }

  /*
   * If the implicit_coercion flag is set, check to see if the original
   * type can be implicitly coerced to the desired_type.
   *
   * (Note: This macro only picks up only coercions that are not allowed
   *        implicitly but are allowed explicitly.)
   */
  if (implicit_coercion)
    {
      if (TP_IMPLICIT_COERCION_NOT_ALLOWED (original_type, desired_type))
	{
	  db_make_null (dest);
	  return DOMAIN_INCOMPATIBLE;
	}
    }

  /*
   * If src == dest, coerce into a temporary variable and
   * handle the conversion before returning.
   */
  if (src == dest)
    target = &temp;
  else
    target = dest;

  /*
   * Initialize the destination domain, important for the
   * nm_ coercion functions thich take domain information inside the
   * destination db value.
   */
  db_value_domain_init (target, desired_type,
			desired_domain->precision, desired_domain->scale);

  switch (desired_type)
    {
    case DB_TYPE_SHORT:
      switch (original_type)
	{
	case DB_TYPE_MONETARY:
	  v_money = DB_GET_MONETARY (src);
	  if (OR_CHECK_SHORT_OVERFLOW (v_money->amount))
	    status = DOMAIN_OVERFLOW;
	  else
	    db_make_short (target, ROUND (v_money->amount));
	  break;
	case DB_TYPE_INTEGER:
	  if (OR_CHECK_SHORT_OVERFLOW (DB_GET_INT (src)))
	    status = DOMAIN_OVERFLOW;
	  else
	    db_make_short (target, DB_GET_INT (src));
	  break;
	case DB_TYPE_FLOAT:
	  if (OR_CHECK_SHORT_OVERFLOW (DB_GET_FLOAT (src)))
	    status = DOMAIN_OVERFLOW;
	  else
	    db_make_short (target, ROUND (DB_GET_FLOAT (src)));
	  break;
	case DB_TYPE_DOUBLE:
	  if (OR_CHECK_SHORT_OVERFLOW (DB_GET_DOUBLE (src)))
	    status = DOMAIN_OVERFLOW;
	  else
	    db_make_short (target, ROUND (DB_GET_DOUBLE (src)));
	  break;
	case DB_TYPE_NUMERIC:
	  status = (TP_DOMAIN_STATUS)
	    numeric_db_value_coerce_from_num ((DB_VALUE *) src, target,
					      &data_stat);
	  if (status != NO_ERROR)
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    double num_value;

	    if (tp_atof (src, &num_value) != NO_ERROR)
	      {
		if (er_errid () != NO_ERROR)	/* i.e, malloc failure */
		  return DOMAIN_ERROR;
		status = DOMAIN_INCOMPATIBLE;	/* conversion error */
		break;
	      }

	    if (OR_CHECK_SHORT_OVERFLOW (num_value))
	      status = DOMAIN_OVERFLOW;
	    else
	      db_make_short (target, ROUND (num_value));
	    break;
	  }
	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_INTEGER:
      switch (original_type)
	{
	case DB_TYPE_SHORT:
	  db_make_int (target, DB_GET_SHORT (src));
	  break;
	case DB_TYPE_MONETARY:
	  v_money = DB_GET_MONETARY (src);
	  if (OR_CHECK_INT_OVERFLOW (v_money->amount))
	    status = DOMAIN_OVERFLOW;
	  else
	    db_make_int (target, ROUND (v_money->amount));
	  break;
	case DB_TYPE_FLOAT:
	  if (OR_CHECK_INT_OVERFLOW (DB_GET_FLOAT (src)))
	    status = DOMAIN_OVERFLOW;
	  else
	    db_make_int (target, ROUND (DB_GET_FLOAT (src)));
	  break;
	case DB_TYPE_DOUBLE:
	  if (OR_CHECK_INT_OVERFLOW (DB_GET_DOUBLE (src)))
	    status = DOMAIN_OVERFLOW;
	  else
	    db_make_int (target, ROUND (DB_GET_DOUBLE (src)));
	  break;
	case DB_TYPE_NUMERIC:
	  status = (TP_DOMAIN_STATUS)
	    numeric_db_value_coerce_from_num ((DB_VALUE *) src, target,
					      &data_stat);
	  if (status != NO_ERROR)
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    double num_value;

	    if (tp_atof (src, &num_value) != NO_ERROR)
	      {
		if (er_errid () != NO_ERROR)	/* i.e, malloc failure */
		  return DOMAIN_ERROR;
		status = DOMAIN_INCOMPATIBLE;	/* conversion error */
		break;
	      }

	    if (OR_CHECK_INT_OVERFLOW (num_value))
	      status = DOMAIN_OVERFLOW;
	    else
	      db_make_int (target, ROUND (num_value));
	    break;
	  }
	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_FLOAT:
      switch (original_type)
	{
	case DB_TYPE_SHORT:
	  db_make_float (target, (float) DB_GET_SHORT (src));
	  break;
	case DB_TYPE_INTEGER:
	  db_make_float (target, (float) DB_GET_INT (src));
	  break;
	case DB_TYPE_DOUBLE:
	  if (OR_CHECK_FLOAT_OVERFLOW (DB_GET_DOUBLE (src)))
	    status = DOMAIN_OVERFLOW;
	  else
	    db_make_float (target, (float) DB_GET_DOUBLE (src));
	  break;
	case DB_TYPE_MONETARY:
	  v_money = DB_GET_MONETARY (src);
	  if (OR_CHECK_FLOAT_OVERFLOW (v_money->amount))
	    status = DOMAIN_OVERFLOW;
	  else
	    db_make_float (target, (float) v_money->amount);
	  break;
	case DB_TYPE_NUMERIC:
	  status = (TP_DOMAIN_STATUS)
	    numeric_db_value_coerce_from_num ((DB_VALUE *) src, target,
					      &data_stat);
	  if (status != NO_ERROR)
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    double num_value;

	    if (tp_atof (src, &num_value) != NO_ERROR)
	      {
		if (er_errid () != NO_ERROR)	/* i.e, malloc failure */
		  return DOMAIN_ERROR;
		status = DOMAIN_INCOMPATIBLE;	/* conversion error */
		break;
	      }

	    if (OR_CHECK_FLOAT_OVERFLOW (num_value))
	      status = DOMAIN_OVERFLOW;
	    else
	      db_make_float (target, (float) num_value);
	    break;
	  }
	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_DOUBLE:
      switch (original_type)
	{
	case DB_TYPE_SHORT:
	  db_make_double (target, (double) DB_GET_SHORT (src));
	  break;
	case DB_TYPE_INTEGER:
	  db_make_double (target, (double) DB_GET_INT (src));
	  break;
	case DB_TYPE_FLOAT:
	  db_make_double (target, (double) DB_GET_FLOAT (src));
	  break;
	case DB_TYPE_MONETARY:
	  v_money = DB_GET_MONETARY (src);
	  db_make_double (target, (double) v_money->amount);
	  break;
	case DB_TYPE_NUMERIC:
	  status = (TP_DOMAIN_STATUS)
	    numeric_db_value_coerce_from_num ((DB_VALUE *) src, target,
					      &data_stat);
	  if (status != NO_ERROR)
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    double num_value;

	    if (tp_atof (src, &num_value) != NO_ERROR)
	      {
		if (er_errid () != NO_ERROR)	/* i.e, malloc failure */
		  return DOMAIN_ERROR;
		status = DOMAIN_INCOMPATIBLE;	/* conversion error */
		break;
	      }

	    db_make_double (target, num_value);
	    break;
	  }
	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_NUMERIC:
      /*
       * Numeric-to-numeric coercion will be handled in the nm_ module.
       * The desired precision & scale is communicated through the destination
       * value.
       */
      switch (original_type)
	{
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    DB_VALUE temp;

	    if (tp_atonumeric (src, &temp) != NO_ERROR)
	      status = DOMAIN_ERROR;
	    else
	      status = tp_value_coerce (&temp, target, desired_domain);
	    break;
	  }
	default:
	  if (numeric_db_value_coerce_to_num ((DB_VALUE *) src,
					      target, &data_stat) != NO_ERROR)
	    status = DOMAIN_INCOMPATIBLE;
	  else if (data_stat == DATA_STATUS_TRUNCATED)
	    status = DOMAIN_OVERFLOW;
	  else
	    status = DOMAIN_COMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_MONETARY:
      switch (original_type)
	{
	case DB_TYPE_SHORT:
	  db_make_monetary (target, DB_CURRENCY_DEFAULT,
			    (double) DB_GET_SHORT (src));
	  break;
	case DB_TYPE_INTEGER:
	  db_make_monetary (target, DB_CURRENCY_DEFAULT,
			    (double) DB_GET_INT (src));
	  break;
	case DB_TYPE_FLOAT:
	  db_make_monetary (target, DB_CURRENCY_DEFAULT,
			    (double) DB_GET_FLOAT (src));
	  break;
	case DB_TYPE_DOUBLE:
	  db_make_monetary (target, DB_CURRENCY_DEFAULT, DB_GET_DOUBLE (src));
	  break;
	case DB_TYPE_NUMERIC:
	  status = (TP_DOMAIN_STATUS)
	    numeric_db_value_coerce_from_num ((DB_VALUE *) src, target,
					      &data_stat);
	  if (status != NO_ERROR)
	    {
	      status = DOMAIN_OVERFLOW;
	    }
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    double num_value;

	    if (tp_atof (src, &num_value) != NO_ERROR)
	      {
		if (er_errid () != NO_ERROR)	/* i.e, malloc failure */
		  return DOMAIN_ERROR;
		status = DOMAIN_INCOMPATIBLE;	/* conversion error */
		break;
	      }

	    db_make_monetary (target, DB_CURRENCY_DEFAULT, num_value);
	    break;
	  }
	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_UTIME:
      switch (original_type)
	{
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  if (tp_atoutime (src, &v_utime) != NO_ERROR)
	    return DOMAIN_ERROR;
	  else
	    db_make_timestamp (target, v_utime);
	  break;

	case DB_TYPE_DATE:
	  db_time_encode (&v_time, 0, 0, 0);
	  status = (TP_DOMAIN_STATUS)
	    db_timestamp_encode (&v_utime, (DB_DATE *) DB_GET_DATE (src),
				 &v_time);
	  if (status == NO_ERROR)
	    db_make_timestamp (target, v_utime);
	  else
	    status = DOMAIN_OVERFLOW;
	  break;

	case DB_TYPE_TIME:
	  status = DOMAIN_INCOMPATIBLE;
	  break;

	default:
	  status =
	    tp_value_coerce ((DB_VALUE *) src, target, &tp_Integer_domain);
	  if (status == DOMAIN_COMPATIBLE)
	    {
	      int tmpint;
	      if ((tmpint = DB_GET_INT (target)) >= 0)
		db_make_timestamp (target, (DB_UTIME) tmpint);
	      else
		status = DOMAIN_INCOMPATIBLE;
	    }
	  break;
	}
      break;

    case DB_TYPE_DATE:
      switch (original_type)
	{
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  if (tp_atodate (src, &v_date) != NO_ERROR)
	    return DOMAIN_ERROR;
	  db_date_decode (&v_date, &month, &day, &year);
	  db_make_date (target, month, day, year);
	  break;

	case DB_TYPE_UTIME:
	  db_utime_decode ((DB_TIME *) DB_GET_UTIME (src), &v_date, NULL);
	  db_date_decode (&v_date, &month, &day, &year);
	  db_make_date (target, month, day, year);
	  break;

	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_TIME:
      switch (original_type)
	{
	case DB_TYPE_UTIME:
	  db_utime_decode ((DB_TIME *) DB_GET_UTIME (src), NULL, &v_time);
	  db_time_decode (&v_time, &hour, &minute, &second);
	  db_make_time (target, hour, minute, second);
	  break;
	case DB_TYPE_SHORT:
	  v_time = DB_GET_SHORT (src) % SECONDS_IN_A_DAY;
	  db_time_decode (&v_time, &hour, &minute, &second);
	  db_make_time (target, hour, minute, second);
	  break;
	case DB_TYPE_INTEGER:
	  v_time = DB_GET_INT (src) % SECONDS_IN_A_DAY;
	  db_time_decode (&v_time, &hour, &minute, &second);
	  db_make_time (target, hour, minute, second);
	  break;
	case DB_TYPE_MONETARY:
	  v_money = DB_GET_MONETARY (src);
	  if (OR_CHECK_INT_OVERFLOW (v_money->amount))
	    status = DOMAIN_OVERFLOW;
	  else
	    {
	      v_time = ROUND (v_money->amount) % SECONDS_IN_A_DAY;
	      db_time_decode (&v_time, &hour, &minute, &second);
	      db_make_time (target, hour, minute, second);
	    }
	  break;
	case DB_TYPE_FLOAT:
	  {
	    float ftmp = DB_GET_FLOAT (src);
	    if (OR_CHECK_INT_OVERFLOW (ftmp))
	      status = DOMAIN_OVERFLOW;
	    else
	      {
		v_time = (ROUND (ftmp)) % SECONDS_IN_A_DAY;
		db_time_decode (&v_time, &hour, &minute, &second);
		db_make_time (target, hour, minute, second);
	      }
	    break;
	  }
	case DB_TYPE_DOUBLE:
	  {
	    double dtmp = DB_GET_DOUBLE (src);
	    if (OR_CHECK_INT_OVERFLOW (dtmp))
	      status = DOMAIN_OVERFLOW;
	    else
	      {
		v_time = (ROUND (dtmp)) % SECONDS_IN_A_DAY;
		db_time_decode (&v_time, &hour, &minute, &second);
		db_make_time (target, hour, minute, second);
	      }
	    break;
	  }
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  if (tp_atotime (src, &v_time) != NO_ERROR)
	    return DOMAIN_ERROR;
	  db_time_decode (&v_time, &hour, &minute, &second);
	  db_make_time (target, hour, minute, second);
	  break;
	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

#if !defined (SERVER_MODE)
    case DB_TYPE_OBJECT:
      {
	DB_OBJECT *v_obj = NULL;
	/* Make sure the domains are compatible.  Coerce view objects to
	   real objects.
	 */
	switch (original_type)
	  {
	  case DB_TYPE_OBJECT:
	    if (!sm_coerce_object_domain ((TP_DOMAIN *) desired_domain,
					  DB_GET_OBJECT (src), &v_obj))
	      status = DOMAIN_INCOMPATIBLE;
	    break;
	  case DB_TYPE_POINTER:
	    /*
	     * This is for nested inserts of the form:
	     * insert into torihikisaki values('0000001', 't_saki1',
	     * {(insert into tokuisaki values('01','tk_sak1')),
	     * (insert into tokuisaki values('02','tk_sak2')),
	     * (insert into tokuisaki values('03','tk_sak3'))});
	     * The set elements will be templates at this point rather
	     * than objects.  tp_value_cast() will be called as part of
	     * domain checking when adding the set attribute to the
	     * template.  We must return a DB_TYPE_POINTER rather than
	     * an object so that the nested inserts will be performed
	     * when the template is finished.
	     */
	    if (!sm_check_class_domain ((TP_DOMAIN *) desired_domain,
					((DB_OTMPL *) DB_GET_POINTER (src))->
					classobj))
	      status = DOMAIN_INCOMPATIBLE;
	    db_make_pointer (target, DB_GET_POINTER (src));
	    break;
	  case DB_TYPE_OID:
	    vid_oid_to_object (src, &v_obj);
	    break;

	  case DB_TYPE_VOBJ:
	    vid_vobj_to_object (src, &v_obj);
	    if (!db_is_vclass (desired_domain->class_mop))
	      {
		v_obj = db_real_instance (v_obj);
	      }
	    break;

	  default:
	    status = DOMAIN_INCOMPATIBLE;
	  }
	if (original_type != DB_TYPE_POINTER)
	  {
	    /* check we got an object in a proper class */
	    if (v_obj && desired_domain->class_mop)
	      {
		DB_OBJECT *obj_class;

		obj_class = db_get_class (v_obj);
		if (obj_class == desired_domain->class_mop)
		  {
		    /* everything is fine */
		  }
		else
		  if (db_is_subclass (obj_class, desired_domain->class_mop))
		  {
		    /* everything is also ok */
		  }
		else if (db_is_vclass (desired_domain->class_mop))
		  {
		    /*
		     * This should still be an error, and the above
		     * code should have constructed a virtual mop.
		     * I'm not sure the rest of the code is consistent
		     * in this regard.
		     */
		  }
		else
		  {
		    status = DOMAIN_INCOMPATIBLE;
		  }
	      }
	    db_make_object (target, v_obj);
	  }
      }
      break;
#endif /* !SERVER_MODE */

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      if (!TP_IS_SET_TYPE (original_type))
	status = DOMAIN_INCOMPATIBLE;
      else
	{
	  SETREF *setref;
	  setref = db_get_set (src);
	  if (setref)
	    {
	      TP_DOMAIN *set_domain;
	      set_domain = setobj_domain (setref->set);
	      if (src == dest
		  && tp_domain_compatible (set_domain, desired_domain))
		{
		  /*
		   * We know that this is a "coerce-in-place" operation, and
		   * we know that no coercion is necessary, so do nothing: we
		   * can use the exact same set without any conversion.
		   * Setting "src" to NULL prevents the wrapup code from
		   * clearing the set; that's important since we haven't made
		   * a copy.
		   */
		  setobj_put_domain (setref->set,
				     (TP_DOMAIN *) desired_domain);
		  src = NULL;
		}
	      else
		{
		  if (tp_domain_compatible (set_domain, desired_domain))
		    {
		      /*
		       * Well, we can't use the exact same set, but we don't
		       * have to do the whole hairy coerce thing either: we
		       * can just make a copy and then take the more general
		       * domain.  setobj_put_domain() guards against null
		       * pointers, there's no need to check first.
		       */
		      setref = set_copy (setref);
		      if (setref)
			setobj_put_domain (setref->set,
					   (TP_DOMAIN *) desired_domain);
		    }
		  else
		    {
		      /*
		       * Well, now we have to use the whole hairy coercion
		       * thing.  Too bad...
		       *
		       * This case will crop up when someone tries to cast a
		       * "set of int" as a "set of float", for example.
		       */
		      setref = set_coerce (setref, (TP_DOMAIN *)
					   desired_domain, implicit_coercion);
		    }
		  if (setref == NULL)
		    err = er_errid ();
		  else if (desired_type == DB_TYPE_SET)
		    err = db_make_set (target, setref);
		  else if (desired_type == DB_TYPE_MULTISET)
		    err = db_make_multiset (target, setref);
		  else
		    err = db_make_sequence (target, setref);

		}
	      if (!setref || err < 0)
		status = DOMAIN_INCOMPATIBLE;
	    }
	}
      break;

    case DB_TYPE_VOBJ:
      if (original_type == DB_TYPE_VOBJ)
	{
	  SETREF *setref;
	  /*
	   * We should try and convert the view of the src to match
	   * the view of the desired_domain. However, the desired
	   * domain generally does not contain this information.
	   * We will detect domain incompatibly later on assignment,
	   * so we treat casting any DB_TYPE_VOBJ to DB_TYPE_VOBJ
	   * as success.
	   */
	  status = DOMAIN_COMPATIBLE;
	  setref = db_get_set (src);
	  if (src != dest || !setref)
	    pr_clone_value ((DB_VALUE *) src, target);
	  else
	    {
	      /*
	       * this is a "coerce-in-place", and no coercion is necessary,
	       * so do nothing: use the same vobj without any conversion. set
	       * "src" to NULL to prevent the wrapup code from clearing dest.
	       */
	      setobj_put_domain (setref->set, (TP_DOMAIN *) desired_domain);
	      src = NULL;
	    }
	}
      else
#if !defined (SERVER_MODE)
      if (original_type == DB_TYPE_OBJECT)
	{
	  if (vid_object_to_vobj (DB_GET_OBJECT (src), target) < 0)
	    status = DOMAIN_INCOMPATIBLE;
	  else
	    status = DOMAIN_COMPATIBLE;
	  break;
	}
      else
#endif /* !SERVER_MODE */
      if (original_type == DB_TYPE_OID || original_type == DB_TYPE_OBJECT)
	{
	  DB_VALUE view_oid;
	  DB_VALUE class_oid;
	  DB_VALUE keys;
	  OID nulloid;
	  DB_SEQ *seq;

	  OID_SET_NULL (&nulloid);
	  DB_MAKE_OID (&class_oid, &nulloid);
	  DB_MAKE_OID (&view_oid, &nulloid);
	  seq = db_seq_create (NULL, NULL, 3);
	  keys = *src;

	  /*
	   * if we are on the server, and get a DB_TYPE_OBJECT,
	   * then its only possible representation is a DB_TYPE_OID,
	   * and it may be treated that way. However, this should
	   * not really be a case that can happen. It may still
	   * for historical reasons, so is not falgged as an error.
	   * On the client, a worskapce based scheme must be used,
	   * which is just above in a conditional compiled section.
	   */

	  if ((db_seq_put (seq, 0, &view_oid) != NO_ERROR) ||
	      (db_seq_put (seq, 1, &class_oid) != NO_ERROR) ||
	      (db_seq_put (seq, 2, &keys) != NO_ERROR))
	    status = DOMAIN_INCOMPATIBLE;
	  else
	    {
	      db_make_sequence (target, seq);
	      db_value_alter_type (target, DB_TYPE_VOBJ);
	      status = DOMAIN_COMPATIBLE;
	    }
	}
      else
	{
	  status = DOMAIN_INCOMPATIBLE;
	}
      break;

    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      switch (original_type)
	{
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  {
	    DB_VALUE temp;
	    char *bit_char_string;
	    int src_size = DB_GET_STRING_SIZE (src);
	    int dst_size = (src_size + 1) / 2;

	    bit_char_string = db_private_alloc (NULL, dst_size + 1);
	    if (bit_char_string)
	      {
		if (qstr_hex_to_bin (bit_char_string, dst_size,
				     DB_GET_STRING (src),
				     src_size) != src_size)
		  {
		    status = DOMAIN_ERROR;
		    db_private_free_and_init (NULL, bit_char_string);
		  }
		else
		  {
		    db_make_bit (&temp, TP_FLOATING_PRECISION_VALUE,
				 bit_char_string, src_size * 4);
		    temp.need_clear = true;
		    if (db_bit_string_coerce (&temp, target, &data_stat) !=
			NO_ERROR)
		      status = DOMAIN_INCOMPATIBLE;
		    else
		      status = DOMAIN_COMPATIBLE;
		    pr_clear_value (&temp);
		  }
	      }
	    else
	      {
		/* Couldn't allocate space for bit_char_string */
		status = DOMAIN_INCOMPATIBLE;
	      }
	  }
	  break;
	default:
	  if (src == dest && tp_can_steal_string (src, desired_domain))
	    {
	      tp_value_slam_domain (dest, desired_domain);
	      /*
	       * Set "src" to NULL to prevent the wrapup code from undoing
	       * our work; since we haven't actually made a copy, we don't
	       * want to clear the original.
	       */
	      src = NULL;
	    }
	  else if (db_bit_string_coerce (src, target, &data_stat) != NO_ERROR)
	    status = DOMAIN_INCOMPATIBLE;
	  else if (data_stat == DATA_STATUS_TRUNCATED && implicit_coercion)
	    {
	      status = DOMAIN_OVERFLOW;
	      db_value_clear (target);
	    }
	  else
	    status = DOMAIN_COMPATIBLE;
	  break;
	}
      break;

    case DB_TYPE_VARCHAR:
    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      switch (original_type)
	{
	case DB_TYPE_VARCHAR:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  if (src == dest && tp_can_steal_string (src, desired_domain))
	    {
	      tp_value_slam_domain (dest, desired_domain);
	      /*
	       * Set "src" to NULL to prevent the wrapup code from undoing
	       * our work; since we haven't actually made a copy, we don't
	       * want to clear the original.
	       */
	      src = NULL;
	    }
	  else if (db_char_string_coerce (src, target, &data_stat) !=
		   NO_ERROR)
	    status = DOMAIN_INCOMPATIBLE;
	  else if (data_stat == DATA_STATUS_TRUNCATED && implicit_coercion)
	    {
	      status = DOMAIN_OVERFLOW;
	      db_value_clear (target);
	    }
	  else
	    status = DOMAIN_COMPATIBLE;
	  break;
	case DB_TYPE_INTEGER:
	case DB_TYPE_SMALLINT:
	  {
	    int max_size = 10 + 2 + 1;
	    char *new_string;

	    new_string = (char *) db_private_alloc (NULL, max_size);
	    if (!new_string)
	      return DOMAIN_ERROR;

	    if (tp_itoa (original_type == DB_TYPE_INTEGER ?
			 DB_GET_INT (src) : DB_GET_SHORT (src), new_string,
			 10))
	      {
		if (db_value_precision (target) < (int) strlen (new_string))
		  {
		    status = DOMAIN_OVERFLOW;
		    db_private_free_and_init (NULL, new_string);
		  }
		else
		  {
		    MAKE_DESIRED_STRING_DB_VALUE (desired_type,
						  desired_domain, new_string,
						  target, status, data_stat);
		  }
	      }
	    else
	      {
		status = DOMAIN_ERROR;
		db_private_free_and_init (NULL, new_string);
	      }
	    break;
	  }
	case DB_TYPE_DOUBLE:
	case DB_TYPE_FLOAT:
	  {
	    int max_size = DBL_MAX_DIGITS + 2 + 1;
	    char *new_string;

	    new_string = (char *) db_private_alloc (NULL, max_size);
	    if (!new_string)
	      return DOMAIN_ERROR;

	    sprintf (new_string, "%.*g", (original_type == DB_TYPE_FLOAT) ? 10 : 17,	/* FIXME - macro */
		     (original_type == DB_TYPE_FLOAT) ?
		     DB_GET_FLOAT (src) : DB_GET_DOUBLE (src));

	    if (db_value_precision (target) < (int) strlen (new_string))
	      {
		status = DOMAIN_OVERFLOW;
		db_private_free_and_init (NULL, new_string);
	      }
	    else
	      {
		MAKE_DESIRED_STRING_DB_VALUE (desired_type, desired_domain,
					      new_string, target, status,
					      data_stat);
	      }

	    break;
	  }
	case DB_TYPE_NUMERIC:
	  {
	    int max_size = 38 + 2 + 1;
	    char *new_string, *ptr;

	    new_string = (char *) db_private_alloc (NULL, max_size);
	    if (!new_string)
	      return DOMAIN_ERROR;

	    ptr = numeric_db_value_print ((DB_VALUE *) src);
	    strcpy (new_string, ptr);

	    if (db_value_precision (target) < (int) strlen (new_string))
	      {
		status = DOMAIN_OVERFLOW;
		db_private_free_and_init (NULL, new_string);
	      }
	    else
	      {
		MAKE_DESIRED_STRING_DB_VALUE (desired_type, desired_domain,
					      new_string, target, status,
					      data_stat);
	      }

	    break;
	  }
	case DB_TYPE_MONETARY:
	  {
	    int max_size = DBL_MAX_DIGITS + 2 + 1 + 1;
	    char *new_string;
	    char *p;

	    new_string = (char *) db_private_alloc (NULL, max_size);
	    if (!new_string)
	      return DOMAIN_ERROR;

	    sprintf (new_string, "%s%.*f",
		     lang_currency_symbol (DB_GET_MONETARY (src)->type),
		     2, DB_GET_MONETARY (src)->amount);

	    p = new_string + strlen (new_string);
	    for (--p; p >= new_string && *p == '0'; p--)
	      {			/* remove trailing zeros */
		*p = '\0';
	      }
	    if (*p == '.')	/* remove point */
	      *p = '\0';

	    if (db_value_precision (target) < (int) strlen (new_string))
	      {
		status = DOMAIN_OVERFLOW;
		db_private_free_and_init (NULL, new_string);
	      }
	    else
	      {
		MAKE_DESIRED_STRING_DB_VALUE (desired_type, desired_domain,
					      new_string, target, status,
					      data_stat);
	      }

	    break;
	  }
	case DB_TYPE_DATE:
	case DB_TYPE_TIME:
	case DB_TYPE_TIMESTAMP:
	  {
	    int max_size = 30;
	    char *new_string;

	    new_string = (char *) db_private_alloc (NULL, max_size);
	    if (!new_string)
	      return DOMAIN_ERROR;

	    if (original_type == DB_TYPE_DATE)
	      {
		db_date_to_string (new_string, max_size,
				   (DB_DATE *) DB_GET_DATE (src));
	      }
	    else if (original_type == DB_TYPE_TIME)
	      {
		db_time_to_string (new_string, max_size,
				   (DB_TIME *) DB_GET_TIME (src));
	      }
	    else
	      {
		db_timestamp_to_string (new_string, max_size,
					(DB_TIMESTAMP *)
					DB_GET_TIMESTAMP (src));
	      }

	    if (db_value_precision (target) < (int) strlen (new_string))
	      {
		status = DOMAIN_OVERFLOW;
		db_private_free_and_init (NULL, new_string);
	      }
	    else
	      {
		MAKE_DESIRED_STRING_DB_VALUE (desired_type, desired_domain,
					      new_string, target, status,
					      data_stat);
	      }

	    break;
	  }
	case DB_TYPE_BIT:
	case DB_TYPE_VARBIT:
	  {
	    int max_size;
	    char *new_string;
	    int convert_error;

	    max_size = ((db_get_string_length (src) + 3) / 4) + 1;
	    new_string = (char *) db_private_alloc (NULL, max_size);
	    if (!new_string)
	      return DOMAIN_ERROR;

	    convert_error = bfmt_print (1 /* BIT_STRING_HEX */ , src,
					new_string, max_size);

	    if (convert_error == NO_ERROR)
	      {
		if (db_value_precision (target) < (int) strlen (new_string))
		  {
		    status = DOMAIN_OVERFLOW;
		    db_private_free_and_init (NULL, new_string);
		  }
		else
		  {
		    MAKE_DESIRED_STRING_DB_VALUE (desired_type,
						  desired_domain, new_string,
						  target, status, data_stat);
		  }
	      }
	    else if (convert_error == -1)
	      {
		status = DOMAIN_OVERFLOW;
		db_private_free_and_init (NULL, new_string);
	      }
	    else
	      {
		status = DOMAIN_ERROR;
		db_private_free_and_init (NULL, new_string);
	      }
	    break;
	  }

	default:
	  status = DOMAIN_INCOMPATIBLE;
	  break;
	}
      break;

    default:
      status = DOMAIN_INCOMPATIBLE;
      break;
    }

  if (err < 0)
    status = DOMAIN_ERROR;

  if (status != DOMAIN_COMPATIBLE)
    {
      if (src != dest)
	/* make sure this doesn't have any partial results */
	db_make_null (dest);
    }
  else if (src == dest)
    {
      /* coercsion successful, transfer the value if src == dest */
      db_value_clear (dest);
      *dest = temp;
    }

  return (status);
}

/*
 * tp_value_cast - Coerce a value into one of another domain.
 *    return: TP_DOMAIN_STATUS
 *    src(in): src DB_VALUE
 *    dest(out): dest DB_VALUE
 *    desired_domain(in):
 *    implicit_coercion(in): flag for the coercion is implicit
 * Note:
 *    This function does select domain from desired_domain
 */
TP_DOMAIN_STATUS
tp_value_cast (const DB_VALUE * src, DB_VALUE * dest,
	       const TP_DOMAIN * desired_domain, bool implicit_coercion)
{
  return tp_value_cast_internal (src, dest, desired_domain, implicit_coercion,
				 true);
}

/*
 * tp_value_cast_no_domain_select - Coerce a value into one of another domain.
 *    return: TP_DOMAIN_STATUS
 *    src(in): src DB_VALUE
 *    dest(out): dest DB_VALUE
 *    desired_domain(in):
 *    implicit_coercion(in): flag for the coercion is implicit
 * Note:
 *    This function does not select domain from desired_domain
 */
TP_DOMAIN_STATUS
tp_value_cast_no_domain_select (const DB_VALUE * src, DB_VALUE * dest,
				const TP_DOMAIN * desired_domain,
				bool implicit_coercion)
{
  return tp_value_cast_internal (src, dest, desired_domain, implicit_coercion,
				 false);
}


/*
 * VALUE COMPARISON
 */


/*
 * oidcmp - Compares two OIDs and returns a DB_ style status code.
 *    return: DB_ comparison status code
 *    oid1(in): first oid
 *    oid2(in): second oid
 * Note:
 *    The underlying oid_compare should be using these so we can avoid
 *    an extra level of indirection.
 */
static int
oidcmp (OID * oid1, OID * oid2)
{
  int status;

  status = oid_compare (oid1, oid2);
  if (status < 0)
    status = DB_LT;
  else if (status > 0)
    status = DB_GT;
  else
    status = DB_EQ;

  return status;
}


/*
 * tp_more_general_type - compares two type with respect to generality
 *    return: 0 if same type,
 *           <0 if type1 less general then type2,
 *           >0 otherwise
 *    type1(in): first type
 *    type2(in): second type
 */
int
tp_more_general_type (const DB_TYPE type1, const DB_TYPE type2)
{
  static int rank[DB_TYPE_LAST + 1];
  static int rank_init = 0;
  int i;

  if (type1 == type2)
    return 0;
  if ((unsigned) type1 > DB_TYPE_LAST)
    {
#if defined (CUBRID_DEBUG)
      printf ("tp_more_general_type: DB type 1 out of range: %d\n", type1);
#endif /* CUBRID_DEBUG */
      return 0;
    }
  if ((unsigned) type2 > DB_TYPE_LAST)
    {
#if defined (CUBRID_DEBUG)
      printf ("tp_more_general_type: DB type 2 out of range: %d\n", type2);
#endif /* CUBRID_DEBUG */
      return 0;
    }
  if (!rank_init)
    {
      /* set up rank so we can do fast table lookup */
      for (i = 0; i <= DB_TYPE_LAST; i++)
	rank[i] = 0;
      for (i = 0; db_type_rank[i] < (DB_TYPE_LAST + 1); i++)
	{
	  rank[db_type_rank[i]] = i;
	}
      rank_init = 1;
    }

  return rank[type1] - rank[type2];
}


/*
 * tp_set_compare - compare two collection
 *    return: zero if equal, <0 if less, >0 if greater
 *    value1(in): first collection value
 *    value2(in): second collection value
 *    do_coercion(in): coercion flag
 *    total_order(in): total order flag
 * Note:
 *    If the total_order flag is set, it will return one of DB_LT, DB_GT, or
 *    SB_SUBSET, DB_SUPERSET, or DB_EQ, it will not return DB_UNK.
 */
int
tp_set_compare (const DB_VALUE * value1, const DB_VALUE * value2,
		int do_coercion, int total_order)
{
  DB_VALUE temp;
  int status, coercion;
  DB_VALUE *v1, *v2;
  DB_TYPE vtype1, vtype2;
  DB_SET *s1, *s2;

  coercion = 0;
  if (value1 == NULL || PRIM_IS_NULL (value1))
    {
      if (value2 == NULL || PRIM_IS_NULL (value2))
	status = (total_order ? DB_EQ : DB_UNK);
      else
	status = (total_order ? DB_LT : DB_UNK);
    }
  else if (value2 == NULL || PRIM_IS_NULL (value2))
    status = (total_order ? DB_GT : DB_UNK);
  else
    {
      v1 = (DB_VALUE *) value1;
      v2 = (DB_VALUE *) value2;

      vtype1 = (DB_TYPE) PRIM_TYPE (v1);
      vtype2 = (DB_TYPE) PRIM_TYPE (v2);
      if (vtype1 != DB_TYPE_SET
	  && vtype1 != DB_TYPE_MULTISET && vtype1 != DB_TYPE_SEQUENCE)
	return DB_NE;

      if (vtype2 != DB_TYPE_SET
	  && vtype2 != DB_TYPE_MULTISET && vtype2 != DB_TYPE_SEQUENCE)
	return DB_NE;

      if (vtype1 != vtype2)
	{
	  if (!do_coercion)
	    {
	      /* types are not comparable */
	      return DB_NE;
	    }
	  else
	    {
	      PRIM_INIT_NULL (&temp);
	      coercion = 1;
	      if (tp_more_general_type (vtype1, vtype2) > 0)
		{
		  /* vtype1 is more general, coerce value 2 */
		  status = tp_value_coerce (v2, &temp, tp_Domains[vtype1]);
		  if (status != DOMAIN_COMPATIBLE)
		    {
		      /*
		       * This is arguably an error condition
		       * but Not Equal is as close as we can come
		       * to reporting it.
		       */
		      pr_clear_value (&temp);
		      return DB_NE;
		    }
		  else
		    {
		      v2 = &temp;
		      vtype2 = DB_VALUE_TYPE (v2);
		    }
		}
	      else
		{
		  /* coerce value1 to value2's type */
		  status = tp_value_coerce (v1, &temp, tp_Domains[vtype2]);
		  if (status != DOMAIN_COMPATIBLE)
		    {
		      /*
		       * This is arguably an error condition
		       * but Not Equal is as close as we can come
		       * to reporting it.
		       */
		      pr_clear_value (&temp);
		      return DB_NE;
		    }
		  else
		    {
		      v1 = &temp;
		      vtype1 = DB_VALUE_TYPE (v1);
		    }
		}
	    }
	}
      /* Here, we have two collections of the same type */

      s1 = db_get_set (v1);
      s2 = db_get_set (v2);

      /*
       * there may ba a call for set_compare returning a total
       * ordering some day.
       */
      status = set_compare (s1, s2, do_coercion);

      if (coercion)
	pr_clear_value (&temp);
    }
  return status;
}


/*
 * tp_value_compare - compares two values
 *    return: zero if equal, <0 if less, >0 if greater
 *    value1(in): first value
 *    value2(in): second value
 *    do_coercion(in): coercion flag
 *    total_order(in): total order flag
 * Note:
 *    There is some implicit conversion going on here, not sure if this
 *    is a good idea because it gives the impression that these have
 *    compatible domains.
 *
 *    If the total_order flag is set, it will return one of DB_LT, DB_GT, or
 *    DB_EQ, it will not return DB_UNK.  For the purposes of the total
 *    ordering, two NULL values are DB_EQ and if only one value is NULL, that
 *    value is less than the non-null value.
 */
int
tp_value_compare (const DB_VALUE * value1, const DB_VALUE * value2,
		  int do_coercion, int total_order)
{
  DB_VALUE temp;
  int status, coercion;
  DB_VALUE *v1, *v2;
  DB_TYPE vtype1, vtype2;
  DB_OBJECT *mop;
  DB_IDENTIFIER *oid;

  status = DB_UNK;
  coercion = 0;

  if (value1 == NULL || PRIM_IS_NULL (value1))
    {
      if (value2 == NULL || PRIM_IS_NULL (value2))
	status = (total_order ? DB_EQ : DB_UNK);
      else
	status = (total_order ? DB_LT : DB_UNK);
    }
  else if (value2 == NULL || PRIM_IS_NULL (value2))
    {
      status = (total_order ? DB_GT : DB_UNK);
    }
  else
    {

      v1 = (DB_VALUE *) value1;
      v2 = (DB_VALUE *) value2;

      vtype1 = (DB_TYPE) PRIM_TYPE (v1);
      vtype2 = (DB_TYPE) PRIM_TYPE (v2);

      /*
       * Hack, DB_TYPE_OID & DB_TYPE_OBJECT are logically the same domain
       * although their physical representations are different.
       * If we see a pair of those, handle it up front before we
       * fall in and try to perform coercion.  Avoid "coercion" between
       * OIDs and OBJECTs because we usually try to keep OIDs unswizzled
       * as long as possible.
       */
      if (vtype1 != vtype2)
	{
	  if (vtype1 == DB_TYPE_OBJECT)
	    {
	      if (vtype2 == DB_TYPE_OID)
		{
		  mop = db_get_object (v1);
		  oid = db_get_oid (v2);
		  return oidcmp (WS_OID (mop), oid);
		}
	    }
	  else if (vtype2 == DB_TYPE_OBJECT)
	    {
	      if (vtype1 == DB_TYPE_OID)
		{
		  mop = db_get_object (v2);
		  oid = db_get_oid (v1);
		  return oidcmp (oid, WS_OID (mop));
		}
	    }

	  /*
	   * If value types aren't exact, try coercion.
	   * May need to be using the domain returned by
	   * tp_domain_resolve_value here ?
	   */
	  if (do_coercion && !ARE_COMPARABLE (vtype1, vtype2))
	    {
	      PRIM_INIT_NULL (&temp);
	      coercion = 1;
	      if (tp_more_general_type (vtype1, vtype2) > 0)
		{
		  /* vtype1 is more general, corce value 2 */
		  status = tp_value_coerce (v2, &temp, tp_Domains[vtype1]);
		  if (status != DOMAIN_COMPATIBLE)
		    {
		      /*
		       * This is arguably an error condition
		       * but Not Equal is as close as we can come
		       * to reporting it.
		       */
		    }
		  else
		    {
		      v2 = &temp;
		      vtype2 = DB_VALUE_TYPE (v2);
		    }
		}
	      else
		{
		  /* coerce value1 to value2's type */
		  status = tp_value_coerce (v1, &temp, tp_Domains[vtype2]);
		  if (status != DOMAIN_COMPATIBLE)
		    {
		      /*
		       * This is arguably an error condition
		       * but Not Equal is as close as we can come
		       * to reporting it.
		       */
		    }
		  else
		    {
		      v1 = &temp;
		      vtype1 = DB_VALUE_TYPE (v1);
		    }
		}
	    }
	}

      if (!ARE_COMPARABLE (vtype1, vtype2))
	{
	  /*
	   * Default status for mismatched types.
	   * Not correct but will be consistent.
	   */
	  if (tp_more_general_type (vtype1, vtype2) > 0)
	    status = DB_GT;
	  else
	    status = DB_LT;
	}
      else
	{
	  PR_TYPE *pr_type;

	  pr_type = PR_TYPE_FROM_ID (vtype1);
	  status = (*(pr_type->cmpval)) (v1, v2,
					 NULL, 0,
					 do_coercion, total_order, NULL);
	  if (status == DB_UNK)
	    {
	      /* safe guard */
	      if (pr_type->id == DB_TYPE_MIDXKEY)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_MR_NULL_DOMAIN,
			  0);
		}
	    }
	}

      if (coercion)
	pr_clear_value (&temp);
    }
  return (status);
}


/*
 * tp_value_equal - compares the contents of two DB_VALUE structures and
 * determines if they are equal
 *    return: non-zero if the values are equal
 *    value1(in): first value
 *    value2(in): second value
 *    do_coercion(): coercion flag
 * Note:
 *    determines if they are equal.  This is a boolean comparison, you
 *    cannot use this for sorting.
 *
 *    This used to be fully implemented, since this got a lot more complicated
 *    with the introduction of parameterized types, and it is doubtfull that
 *    it saved much in performance anyway, it has been reimplemented to simply
 *    call tp_value_compare.  The old function is commented out below in case
 *    this causes problems.  After awhile, it can be removed.
 *
 */
int
tp_value_equal (const DB_VALUE * value1, const DB_VALUE * value2,
		int do_coercion)
{
  return tp_value_compare (value1, value2, do_coercion, 0) == DB_EQ;
}

/*
 * DOMAIN INFO FUNCTIONS
 */


/*
 * tp_domain_disk_size - Caluclate the disk size necessary to store a value
 * for a particular domain.
 *    return: disk size in bytes. -1 if this is a variable width domain
 *    domain(in): domain to consider
 * Note:
 *    This is here because it takes a domain handle.
 *    Since this is going to get called a lot, we might want to just add
 *    this to the TP_DOMAIN structure and calculate it internally when
 *    it is cached.
 */
int
tp_domain_disk_size (TP_DOMAIN * domain)
{
  int size;

  if (domain->type->variable_p)
    {
      return -1;
    }

  /*
   * Use the "lengthmem" function here with a NULL pointer.  The size will
   * not be dependent on the actual value.
   * The decision of whether or not to use the lengthmem function probably
   * should be based on the value of "disksize" ?
   */
  if (domain->type->lengthmem != NULL)
    {
      size = (*(domain->type->lengthmem)) (NULL, domain, 1);
    }
  else
    {
      size = domain->type->disksize;
    }

  return size;
}


/*
 * tp_domain_memory_size - Calculates the "instance memory" size required
 * to hold a value for a particular domain.
 *    return: bytes size
 *    domain(in): domain to consider
 * Note:
 *    This is here because it takes a domain handle, could go in prim.c
 *    but its becoming unclear where the dividing line is anymore.
 *    Compare this with tp_domain_disk_size which calculates the disk size.
 *
 *    The disk size and instance size are often the same but not always.
 */
int
tp_domain_memory_size (TP_DOMAIN * domain)
{
  int size;

  /*
   * Use the "lengthmem" function here with a NULL pointer and a "disk"
   * flag of zero.
   * This will cause it to return the instance memory size.
   */
  if (domain->type->lengthmem != NULL)
    {
      size = (*(domain->type->lengthmem)) (NULL, domain, 0);
    }
  else
    {
      size = domain->type->size;
    }

  return size;
}


/*
 * tp_init_value_domain - initializes the domain information in a DB_VALUE to
 * correspond to the information from a TP_DOMAIN structure.
 *    return: none
 *    domain(out): domain information
 *    value(in): value to initialize
 * Note:
 *    Used primarily by the value unpacking functions.
 *    It uses the "initval" type function.  This needs to be changed
 *    to take a full domain rather than just precision/scale but the
 *    currently behavior will work for now.
 *
 *    Think about the need for "initval" all it really does is call
 *    db_value_domain_init() with the supplied arguments.
 */
void
tp_init_value_domain (TP_DOMAIN * domain, DB_VALUE * value)
{
  if (domain == NULL)
    /* shouldn't happen ? */
    db_value_domain_init (value, DB_TYPE_NULL, DB_DEFAULT_PRECISION,
			  DB_DEFAULT_SCALE);
  else
    (*(domain->type->initval)) (value, domain->precision, domain->scale);
}


/*
 * tp_check_value_size - check a particular variable sized value (e.g.
 * varchar, char, bit) against a destination domain.
 *    return: domain status (ok or overflow)
 *    domain(in): target domain
 *    value(in): value to be assigned
 * Note:
 *    It is assumed that basic domain compatibility has already been
 *    performed and that the supplied domain will match with what is
 *    in the value.
 *    This is used primarily for character data that is allowed to fit
 *    within a domain if the byte size is within tolerance.
 *
 *    This really should be a new type of prim.c vector function as it
 *    is type specific.
 */
TP_DOMAIN_STATUS
tp_check_value_size (TP_DOMAIN * domain, DB_VALUE * value)
{
  TP_DOMAIN_STATUS status;
  int src_precision, src_length;
  DB_TYPE dbtype;
  char *src;

  status = DOMAIN_COMPATIBLE;

  /* if target domain is "floating", its always ok */
  if (domain->precision != TP_FLOATING_PRECISION_VALUE)
    {

      dbtype = domain->type->id;
      switch (dbtype)
	{
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_BIT:
	  /*
	   * The compatibility will be determined by the precision.
	   * A floating precision is determined by the length of the string
	   * value.
	   */
	  src = DB_GET_STRING (value);
	  if (src != NULL)
	    {
	      src_precision = db_value_precision (value);
	      src_length = db_get_string_size (value);
	      if (src_length < 0)
		{
		  if (!TP_IS_BIT_TYPE (dbtype))
		    src_length = strlen (src);
		}

	      /* Check for floating precision. */
	      if (src_precision == TP_FLOATING_PRECISION_VALUE)
		{
		  if (dbtype == DB_TYPE_NCHAR)
		    src_precision = db_get_string_length (value);
		  else
		    src_precision = src_length;
		}

	      if (src_precision > domain->precision)
		status = DOMAIN_OVERFLOW;
	    }
	  break;

	case DB_TYPE_VARCHAR:
	case DB_TYPE_VARNCHAR:
	case DB_TYPE_VARBIT:
	  /*
	   * The compatibility of the value is always determined by the
	   * actual length of the value, not the destination precision.
	   */
	  src = DB_GET_STRING (value);
	  if (src != NULL)
	    {
	      src_length = db_get_string_size (value);
	      if (src_length < 0)
		{
		  if (!TP_IS_BIT_TYPE (dbtype))
		    src_length = strlen (src);
		}

	      /*
	       * Work backwards from the source length into a minimum precision.
	       * This feels like it should be a nice packed utility
	       * function somewhere.
	       */
	      if (dbtype == DB_TYPE_VARNCHAR)
		src_precision = db_get_string_length (value);
	      else
		src_precision = src_length;

	      if (src_precision > domain->precision)
		status = DOMAIN_OVERFLOW;
	    }
	  break;

	default:
	  /*
	   * None of the other types require this form of value dependent domain
	   * precision checking.
	   */
	  break;
	}
    }
  return status;
}

/*
 * fprint_domain - print information of a domain
 *    return: void
 *    fp(out): FILE pointer
 *    domain(in): domain to print
 */
static void
fprint_domain (FILE * fp, TP_DOMAIN * domain)
{
  TP_DOMAIN *d;

  for (d = domain; d != NULL; d = d->next)
    {

      switch (d->type->id)
	{

	case DB_TYPE_OBJECT:
	case DB_TYPE_OID:
	case DB_TYPE_SUB:
	  if (d->type->id == DB_TYPE_SUB)
	    fprintf (fp, "sub(");
#if !defined (SERVER_MODE)
	  if (d->class_mop != NULL)
	    fprintf (fp, "%s", db_get_class_name (d->class_mop));
	  else if (OID_ISNULL (&d->class_oid))
	    fprintf (fp, "object");
	  else
#endif /* !SERVER_MODE */
	    fprintf (fp, "object(%d,%d,%d)",
		     d->class_oid.volid, d->class_oid.pageid,
		     d->class_oid.slotid);
	  if (d->type->id == DB_TYPE_SUB)
	    fprintf (fp, ")");
	  break;

	case DB_TYPE_VARIABLE:
	  fprintf (fp, "union(");
	  fprint_domain (fp, d->setdomain);
	  fprintf (fp, ")");
	  break;

	case DB_TYPE_SET:
	  fprintf (fp, "set(");
	  fprint_domain (fp, d->setdomain);
	  fprintf (fp, ")");
	  break;
	case DB_TYPE_MULTISET:
	  fprintf (fp, "multiset(");
	  fprint_domain (fp, d->setdomain);
	  fprintf (fp, ")");
	  break;
	case DB_TYPE_SEQUENCE:
	  fprintf (fp, "sequence(");
	  fprint_domain (fp, d->setdomain);
	  fprintf (fp, ")");
	  break;

	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_BIT:
	case DB_TYPE_VARBIT:
	  fprintf (fp, "%s(%d)", d->type->name, d->precision);
	  break;

	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  fprintf (fp, "%s(%d) NATIONAL %d", d->type->name, d->precision,
		   d->codeset);
	  break;

	case DB_TYPE_NUMERIC:
	  fprintf (fp, "%s(%d,%d)", d->type->name, d->precision, d->scale);
	  break;

	default:
	  fprintf (fp, "%s", d->type->name);
	  break;
	}

      if (d->next != NULL)
	fprintf (fp, ",");
    }
}

/*
 * tp_dump_domain - fprint_domain to stdout
 *    return: void
 *    domain(in): domain to print
 */
void
tp_dump_domain (TP_DOMAIN * domain)
{
  fprint_domain (stdout, domain);
  fprintf (stdout, "\n");
}

/*
 * tp_domain_print - fprint_domain to stdout
 *    return: void
 *    domain(in): domain to print
 */
void
tp_domain_print (TP_DOMAIN * domain)
{
  fprint_domain (stdout, domain);
}

/*
 * tp_domain_fprint - fprint_domain to stdout
 *    return: void
 *    fp(out): FILE pointer
 *    domain(in): domain to print
 */
void
tp_domain_fprint (FILE * fp, TP_DOMAIN * domain)
{
  fprint_domain (fp, domain);
}

/*
 * tp_valid_indextype - check for valid index type
 *    return: 1 if type is a valid index type, 0 otherwise.
 *    type(in): a database type constant
 */
int
tp_valid_indextype (DB_TYPE type)
{
  switch (type)
    {
    case DB_TYPE_INTEGER:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_STRING:
    case DB_TYPE_OBJECT:
    case DB_TYPE_TIME:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_DATE:
    case DB_TYPE_MONETARY:
    case DB_TYPE_SHORT:
    case DB_TYPE_OID:
    case DB_TYPE_NUMERIC:
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      return 1;
    default:
      return 0;
    }
}


/*
 * tp_domain_references_objects - check if domain is an object domain or a
 * collection domain that might include objects.
 *    return: int (true or false)
 *    dom(in): the domain to be inspected
 */
int
tp_domain_references_objects (const TP_DOMAIN * dom)
{
  switch (dom->type->id)
    {
    case DB_TYPE_OBJECT:
    case DB_TYPE_OID:
    case DB_TYPE_VOBJ:
      return true;
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      dom = dom->setdomain;
      if (dom)
	{
	  /*
	   * If domains are specified, we can assume that the upper levels
	   * have enforced the rule that no value in the collection has a
	   * domain that isn't included in this list.  If this list has no
	   * object domain, then no collection with this domain can include
	   * an object reference.
	   */
	  for (; dom; dom = dom->next)
	    {
	      if (tp_domain_references_objects (dom))
		return true;
	    }
	  return false;
	}
      else
	{
	  /*
	   * We've got hold of one of our fabulous "collection of anything"
	   * attributes.  We've got no choice but to assume that it might
	   * have objects in it.
	   */
	  return true;
	}
    default:
      return false;
    }
}
