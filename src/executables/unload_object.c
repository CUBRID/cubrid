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
 * unload_object.c: Utility that emits database object definitions in database
 *               object loader format.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <assert.h>

#include <sys/stat.h>
#if defined(WINDOWS)
#include <sys/timeb.h>
#include <time.h>
#include <direct.h>
#define	SIGALRM	14
#endif /* WINDOWS */

#include "authenticate.h"
#include "utility.h"
#include "load_object.h"
#include "log_lsa.hpp"
#include "file_hash.h"
#include "db.h"
#include "memory_hash.h"
#include "memory_alloc.h"
#include "locator_cl.h"
#include "schema_manager.h"
#include "locator.h"
#include "transform_cl.h"
#include "object_accessor.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "set_object.h"

#include "message_catalog.h"
#include "server_interface.h"
#include "porting.h"
#include "unloaddb.h"

#include "system_parameter.h"
#include "schema_system_catalog_constants.h"
#include "execute_schema.h"
#include "network_interface_cl.h"
#include "transaction_cl.h"
#include "dbtype.h"

#include <thread>
#include "error_context.hpp"

#define MARK_CLASS_REQUESTED(cl_no) \
  (class_requested[cl_no / 8] |= 1 << cl_no % 8)
#define MARK_CLASS_REFERENCED(cl_no) \
  (class_referenced[cl_no / 8] |= 1 << cl_no % 8)
#define MARK_CLASS_PROCESSED(cl_no) \
  (class_processed[cl_no / 8] |= 1 << cl_no % 8)
#define IS_CLASS_REQUESTED(cl_no) \
  (class_requested[cl_no / 8] & 1 << cl_no % 8)
#define IS_CLASS_REFERENCED(cl_no) \
  (class_referenced[cl_no / 8] & 1 << cl_no % 8)
#define IS_CLASS_PROCESSED(cl_no) \
  (class_processed[cl_no / 8] & 1 << cl_no % 8)

#define GAUGE_INTERVAL	1

static int output_number = 0;

static FH_TABLE *obj_table = NULL;
static FH_TABLE *cl_table = NULL;

static char *class_requested = NULL;
static char *class_referenced = NULL;
static char *class_processed = NULL;

static OID null_oid;

static const char *prohibited_classes[] = {
  CT_AUTHORIZATIONS_NAME,	/* old name for db_root */
  CT_ROOT_NAME,
  CT_USER_NAME,
  CT_AUTHORIZATION_NAME,
  CT_PASSWORD_NAME,
  CT_TRIGGER_NAME,
  CT_SERIAL_NAME,
  CT_HA_APPLY_INFO_NAME,
  /* catalog classes */
  CT_CLASS_NAME,
  CT_ATTRIBUTE_NAME,
  CT_DOMAIN_NAME,
  CT_METHOD_NAME,
  CT_METHSIG_NAME,
  CT_METHARG_NAME,
  CT_METHFILE_NAME,
  CT_QUERYSPEC_NAME,
  CT_RESOLUTION_NAME,		/* currently, not implemented */
  CT_INDEX_NAME,
  CT_INDEXKEY_NAME,
  CT_CLASSAUTH_NAME,
  CT_DATATYPE_NAME,
  CT_STORED_PROC_NAME,
  CT_STORED_PROC_ARGS_NAME,
  CT_PARTITION_NAME,
  CT_COLLATION_NAME,
  CT_CHARSET_NAME,
  CT_DUAL_NAME,
  CT_DB_SERVER_NAME,
  CT_SYNONYM_NAME,
  /* catalog vclasses */
  CTV_CLASS_NAME,
  CTV_SUPER_CLASS_NAME,
  CTV_VCLASS_NAME,
  CTV_ATTRIBUTE_NAME,
  CTV_ATTR_SD_NAME,
  CTV_METHOD_NAME,
  CTV_METHARG_NAME,
  CTV_METHARG_SD_NAME,
  CTV_METHFILE_NAME,
  CTV_INDEX_NAME,
  CTV_INDEXKEY_NAME,
  CTV_AUTH_NAME,
  CTV_TRIGGER_NAME,
  CTV_STORED_PROC_NAME,
  CTV_STORED_PROC_ARGS_NAME,
  CTV_PARTITION_NAME,
  CTV_DB_COLLATION_NAME,
  CTV_DB_CHARSET_NAME,
  CTV_DB_SERVER_NAME,
  CTV_SYNONYM_NAME,
  NULL
};

static int64_t class_objects = 0;
static int64_t total_objects = 0;
static int failed_objects = 0;

static int64_t approximate_class_objects = 0;
static char *gauge_class_name;
static int64_t total_approximate_class_objects = 0;


#define OBJECT_SUFFIX "_objects"

#define HEADER_FORMAT 	"-------------------------------+--------------------------------\n""    %-25s  |  %23s \n""-------------------------------+--------------------------------\n"
#define MSG_FORMAT 		"    %-25s  |  %10ld (%3d%% / %5d%%)"
static FILE *unloadlog_file = NULL;


#if defined(SUPPORT_THREAD_UNLOAD)
//#define DEBUG_PRINT(args...) fprintf(stdout, ##args); fflush(stdout)
#define DEBUG_PRINT(args...)

typedef struct _lc_copyarea_node LC_COPYAREA_NODE;
struct _lc_copyarea_node
{
  LC_COPYAREA *lc_copy_area;
  LC_COPYAREA_NODE *next;
};

class copyarea_list
{
private:
  pthread_mutex_t m_cs_lock;
  LC_COPYAREA_NODE *m_root;
  LC_COPYAREA_NODE *m_tail;

  LC_COPYAREA_NODE *m_free;

  int m_max;
  int m_used;

public:
    copyarea_list (int max = 64)
  {
    pthread_mutex_init (&m_cs_lock, NULL);
    m_root = m_tail = NULL;
    m_free = NULL;
    m_max = max;
    m_used = 0;
  }
   ~copyarea_list ()
  {
    LC_COPYAREA_NODE *pt;
    while (m_root)
      {
	pt = m_root;
	m_root = m_root->next;
	if (pt->lc_copy_area)
	  {
	    locator_free_copy_area (pt->lc_copy_area);
	  }
	free (pt);
      }
    m_root = m_tail = NULL;

    clear_freelist ();

    (void) pthread_mutex_destroy (&m_cs_lock);
  }

  void set_max_list (int n)
  {
    m_max = n;
  }

  void clear_freelist ()
  {
    LC_COPYAREA_NODE *pt;
    LC_COPYAREA_NODE *pt_free;

    pthread_mutex_lock (&m_cs_lock);
    pt_free = m_free;
    m_free = NULL;
    pthread_mutex_unlock (&m_cs_lock);

    while (pt_free)
      {
	pt = pt_free;
	pt_free = pt_free->next;

	locator_free_copy_area (pt->lc_copy_area);
	free (pt);
      }
  }

  void add_freelist (LC_COPYAREA_NODE * node)
  {
    pthread_mutex_lock (&m_cs_lock);
    if (m_free == NULL)
      node->next = NULL;
    else
      node->next = m_free;

    m_free = node;
    pthread_mutex_unlock (&m_cs_lock);
  }

  void add (LC_COPYAREA * fetch_area, bool try_clear)
  {
    LC_COPYAREA_NODE *pt;

    if (try_clear)
      {
	clear_freelist ();
      }

    pt = (LC_COPYAREA_NODE *) malloc (sizeof (LC_COPYAREA_NODE));
    pt->next = NULL;
    pt->lc_copy_area = fetch_area;

    pthread_mutex_lock (&m_cs_lock);
    while (m_max <= m_used)
      {
	pthread_mutex_unlock (&m_cs_lock);
	std::this_thread::yield ();	// usleep (10); 
	pthread_mutex_lock (&m_cs_lock);
      }

    if (m_root == NULL)
      {
	m_root = pt;
	m_tail = m_root;
      }
    else
      {
	assert (m_tail);
	m_tail->next = pt;
	m_tail = pt;
      }
    m_used++;
    pthread_mutex_unlock (&m_cs_lock);
  }

  LC_COPYAREA_NODE *get ()
  {
    LC_COPYAREA_NODE *pt = NULL;

    pthread_mutex_lock (&m_cs_lock);
    if (m_root != NULL)
      {
	pt = m_root;
	m_root = m_root->next;
	if (m_root == NULL)
	  {
	    m_tail = NULL;
	  }
	m_used--;
      }
    pthread_mutex_unlock (&m_cs_lock);
    return pt;
  }
};


typedef struct _unloaddb_class_info UNLD_CLASS_PARAM;
struct _unloaddb_class_info
{
  HFID *hfid;
  DB_OBJECT *class_;
  OID *class_oid;
  SM_CLASS *class_ptr;
  int referenced_class;
  bool is_terminate;
  extract_context *pctxt;

  pthread_mutex_t mtx;
  pthread_cond_t cond;

  class copyarea_list *cparea_lst_ref;
};

typedef struct _unloaddb_thread_param UNLD_THR_PARAM;
struct _unloaddb_thread_param
{
  int thread_idx;
  pthread_t tid;

  int64_t class_objects;
  int64_t total_objects;
  int failed_objects;

  TEXT_OUTPUT text_output;

  UNLD_CLASS_PARAM *uci;
};

UNLD_THR_PARAM *thr_param = NULL;

static bool open_object_file (TEXT_OUTPUT * obj_out, extract_context & ctxt, const char *output_dirname,
			      const char *class_name, int seqno);
static void close_all_object_files (bool close_only);
static void alloc_text_output (TEXT_OUTPUT * obj_out, const char *output_dirname);
#endif // #if defined(SUPPORT_THREAD_UNLOAD)

static int get_estimated_objs (HFID * hfid, int64_t * est_objects);
static int set_referenced_subclasses (DB_OBJECT * class_);
static bool check_referenced_domain (DB_DOMAIN * dom_list, bool set_cls_ref, int *num_cls_refp);
static void extractobjects_cleanup ();
static void extractobjects_term_handler (int sig);
static bool mark_referenced_domain (SM_CLASS * class_ptr, int *num_set);
static void gauge_alarm_handler (int sig);
static int process_class (extract_context & ctxt, int cl_no, TEXT_OUTPUT * obj_out);
static int process_object (DESC_OBJ * desc_obj, OID * obj_oid, int referenced_class, TEXT_OUTPUT * obj_out);
static int process_set (DB_SET * set, TEXT_OUTPUT * obj_out);
static int process_value (DB_VALUE * value, TEXT_OUTPUT * obj_out);
static void update_hash (OID * object_oid, OID * class_oid, int *data);
static DB_OBJECT *is_class (OID * obj_oid, OID * class_oid);
static int all_classes_processed (void);
static int create_filename (const char *output_dirname, const char *output_prefix, const char *suffix,
			    char *output_filename_p, const size_t filename_size);

/*
 * get_estimated_objs - get the estimated number of object reside in file heap
 *    return: NO_ERROR if success, error code otherwise
 *    hfid(in): file heap id
 *    est_objects(out): estimated number of object
 */
static int
get_estimated_objs (HFID * hfid, int64_t * est_objects)
{
  int ignore_npages;
  int nobjs = 0;
  int error = NO_ERROR;

  error = heap_get_class_num_objects_pages (hfid, 1, &nobjs, &ignore_npages);
  if (error < 0)
    return error;

  *est_objects += nobjs;

  return 0;
}

/*
 * set_referenced_subclasses - set class as referenced
 *    return: NO_ERROR, if successful, error number, if not successful.
 *    class(in): root class
 * Note:
 *    CURRENTLY, ALWAYS RETURN NO_ERROR
 */
static int
set_referenced_subclasses (DB_OBJECT * class_)
{
  int error = NO_ERROR;
  int *cls_no_ptr;
  SM_CLASS *class_ptr;
  DB_OBJLIST *u;
  bool check_reference_chain = false;
  int num_set;
  int error_code;

  error_code = fh_get (cl_table, ws_oid (class_), (FH_DATA *) (&cls_no_ptr));
  if (error_code == NO_ERROR && cls_no_ptr != NULL)
    {
      if (input_filename)
	{
	  if (include_references || is_req_class (class_))
	    {
	      if (!IS_CLASS_REFERENCED (*cls_no_ptr) && !IS_CLASS_REQUESTED (*cls_no_ptr))
		{
		  check_reference_chain = true;
		}
	      MARK_CLASS_REFERENCED (*cls_no_ptr);
	    }
	}
      else
	{
	  if (!IS_CLASS_REFERENCED (*cls_no_ptr) && !IS_CLASS_REQUESTED (*cls_no_ptr))
	    {
	      check_reference_chain = true;
	    }
	  MARK_CLASS_REFERENCED (*cls_no_ptr);
	}
    }
  else
    {
#if defined(CUBRID_DEBUG) || defined(CUBRID_DEBUG_TEST)
      fprintf (stdout, "cls_no_ptr is NULL\n");
#endif /* CUBRID_DEBUG */
    }

  ws_find (class_, (MOBJ *) (&class_ptr));
  if (class_ptr == NULL)
    {
      goto exit_on_error;
    }

  if (check_reference_chain)
    {
      mark_referenced_domain (class_ptr, &num_set);
    }

  /* dive to the bottom */
  for (u = class_ptr->users; u != NULL && error == NO_ERROR; u = u->next)
    {
      error = set_referenced_subclasses (u->op);
    }

exit_on_end:
  return error;

exit_on_error:
  CHECK_EXIT_ERROR (error);
  goto exit_on_end;
}


/*
 * check_referenced_domain - check for OBJECT domain as referenced
 *    return: true, if found referened OBJECT domain. false, if not
 *            found referenced OBJECT domain.
 *    dom_list(in): domain list of class attributes
 *    set_cls_ref(in): for true, do checking. for false, do marking
 *    num_cls_refp(out): number of referenced classes.
 * Note:
 *    for referenced CLASS domain, mark the CLASS and set the number of
 *    referenced classes.
 */
static bool
check_referenced_domain (DB_DOMAIN * dom_list, bool set_cls_ref, int *num_cls_refp)
{
  bool found_object_dom;
  DB_DOMAIN *dom;
  DB_TYPE type;
  DB_OBJECT *class_;

  found_object_dom = false;	/* init */

  for (dom = dom_list; dom && !found_object_dom; dom = db_domain_next (dom))
    {
      type = TP_DOMAIN_TYPE (dom);
      switch (type)
	{
	case DB_TYPE_OBJECT:
	  class_ = db_domain_class (dom);
	  if (class_ == NULL)
	    {
	      return true;	/* found object domain */
	    }

	  *num_cls_refp += 1;	/* increase number of reference to class */

	  if (set_cls_ref)
	    {
	      if (set_referenced_subclasses (class_) != NO_ERROR)
		{
		  /* cause error - currently, not happened */
		  return true;
		}
	    }
	  break;
	case DB_TYPE_SET:
	case DB_TYPE_MULTISET:
	case DB_TYPE_SEQUENCE:
	  found_object_dom = check_referenced_domain (db_domain_set (dom), set_cls_ref, num_cls_refp);
	  break;
	default:
	  break;
	}
    }
  return found_object_dom;
}


/*
 * extractobjects_cleanup - do cleanup task
 *    return: void
 */
static void
extractobjects_cleanup ()
{
#if defined(SUPPORT_THREAD_UNLOAD)
  if (thr_param)
    {
      close_all_object_files (false);
      free_and_init (thr_param);

#if defined(SUPPORT_THREAD_UNLOAD)
      g_obj_out->fd = INVALID_FILE_NO;
#else
      g_obj_out->fp = NULL;
#endif
    }
#endif

  if (g_obj_out)
    {
      if (g_obj_out->buffer != NULL)
	free_and_init (g_obj_out->buffer);
#if defined(SUPPORT_THREAD_UNLOAD)
      if (g_obj_out->fd != INVALID_FILE_NO)
	close (g_obj_out->fd);
#else
      if (g_obj_out->fp != NULL)
	fclose (g_obj_out->fp);
#endif
    }

  if (obj_table != NULL)
    {
      if (debug_flag)
	{
	  fh_dump (obj_table);
	}
      fh_destroy (obj_table);
    }
  if (cl_table != NULL)
    fh_destroy (cl_table);

  free_and_init (class_requested);
  free_and_init (class_referenced);
  free_and_init (class_processed);
  return;
}

/*
 * extractobjects_term_handler - extractobject terminate handler
 *    return: void
 *    sig(in): not used
 */
static void
extractobjects_term_handler (int sig)
{
  extractobjects_cleanup ();
  /* terminate a program */
  _exit (1);
}

/*
 * mark_referenced_domain - mark given SM_CLASS closure
 *    return: true if no error, false otherwise
 *    class_ptr(in): SM_CLASS
 *    num_set(out): amortized marking number of SM_CLASS closure
 */
static bool
mark_referenced_domain (SM_CLASS * class_ptr, int *num_set)
{
  SM_ATTRIBUTE *attribute;

  if (class_ptr == NULL)
    return true;

  for (attribute = class_ptr->shared; attribute != NULL; attribute = (SM_ATTRIBUTE *) attribute->header.next)
    {
      if (check_referenced_domain (attribute->domain, true /* do marking */ , num_set))
	{
	  return false;
	}
    }

  for (attribute = class_ptr->class_attributes; attribute != NULL; attribute = (SM_ATTRIBUTE *) attribute->header.next)
    {
      if (check_referenced_domain (attribute->domain, true /* do marking */ , num_set))
	{
	  return false;
	}
    }

  for (attribute = class_ptr->ordered_attributes; attribute; attribute = attribute->order_link)
    {
      if (attribute->header.name_space != ID_ATTRIBUTE)
	{
	  continue;
	}
      if (check_referenced_domain (attribute->domain, true /* do marking */ , num_set))
	{
	  return false;
	}
    }
  return true;
}

static void
get_output_file_name (extract_context & ctxt, const char *output_dirname, const char *class_name, int seqno,
		      char *name_buf)
{
#if defined(SUPPORT_THREAD_UNLOAD)
  char tmp_name[DB_MAX_IDENTIFIER_LENGTH];
  char *name_ptr = tmp_name;

  if (g_modular > 1)
    {
      assert (class_name && *class_name);
      if (seqno > 0)
	{
	  sprintf (tmp_name, "p%d(%d)_%s_t%d(%d)", g_accept, g_modular, class_name, seqno, thread_count);
	}
      else
	{
	  sprintf (tmp_name, "p%d(%d)_%s", g_accept, g_modular, class_name);
	}
    }
  else if (seqno > 0)
    {
      assert (class_name && *class_name);
      sprintf (tmp_name, "%s_t%d(%d)", class_name, seqno, thread_count);
    }
  else
    {
      name_ptr = (char *) class_name;
    }
#endif

  if (class_name && *class_name)
    {
#if defined(SUPPORT_THREAD_UNLOAD)
      snprintf (name_buf, PATH_MAX - 1, "%s/%s_%s%s", output_dirname, ctxt.output_prefix, name_ptr, OBJECT_SUFFIX);
#else
      snprintf (name_buf, PATH_MAX - 1, "%s/%s_%s%s", output_dirname, ctxt.output_prefix, class_name, OBJECT_SUFFIX);
#endif
    }
  else
    {
      snprintf (name_buf, PATH_MAX - 1, "%s/%s%s", output_dirname, ctxt.output_prefix, OBJECT_SUFFIX);
    }
}

static bool
open_object_file (TEXT_OUTPUT * obj_out, extract_context & ctxt, const char *output_dirname, const char *class_name,
		  int seqno)
{
  char out_fname[PATH_MAX];

  out_fname[0] = '\0';
  get_output_file_name (ctxt, output_dirname, class_name, seqno, out_fname);

#if defined(SUPPORT_THREAD_UNLOAD)
  obj_out->fd = open (out_fname, O_WRONLY | O_CREAT | O_TRUNC, OPEN_MODE_VALUE);
  if (obj_out->fd == INVALID_FILE_NO)
#else
  obj_out->fp = fopen_ex (out_fname, "wb");
  if (obj_out->fp == NULL)
#endif
    {
      fprintf (stderr, "%s: %s.\n\n", ctxt.exec_name, strerror (errno));
      return false;
    }
  return true;
}

#if defined(SUPPORT_THREAD_UNLOAD)
static void
close_all_object_files (bool close_only)
{
  int i;

  for (i = 0; i < thread_count; i++)
    {
      if (close_only == false && thr_param[i].text_output.buffer != NULL)
	{
	  free_and_init (thr_param[i].text_output.buffer);
	}
#if defined(SUPPORT_THREAD_UNLOAD)
      if (thr_param[i].text_output.fd != INVALID_FILE_NO)
	{
	  close (thr_param[i].text_output.fd);
	  thr_param[i].text_output.fd = INVALID_FILE_NO;
	}
#else
      if (thr_param[i].text_output.fp)
	{
	  fclose (thr_param[i].text_output.fp);
	  thr_param[i].text_output.fp = NULL;
	}
#endif
    }
}

static bool
fulsh_all_object_files (bool is_close)
{
  int i;
  bool bret = true;

  for (i = 0; i < thread_count; i++)
    {
#if defined(SUPPORT_THREAD_UNLOAD)
      if (thr_param[i].text_output.fd != INVALID_FILE_NO)
	{
	  if (text_print_flush (&(thr_param[i].text_output)) != NO_ERROR)
	    {
	      bret = false;
	    }

	  if (is_close)
	    {
	      close (thr_param[i].text_output.fd);
	      thr_param[i].text_output.fd = INVALID_FILE_NO;
	    }
	}
#else
      if (thr_param[i].text_output.fp)
	{
	  if (text_print_flush (&(thr_param[i].text_output)) != NO_ERROR)
	    {
	      bret = false;
	    }

	  if (is_close)
	    {
	      fclose (thr_param[i].text_output.fp);
	      thr_param[i].text_output.fp = NULL;
	    }
	}
#endif
    }

  return bret;
}



//  Merge and delete multiple divided files
static bool
merge_and_remove_files (extract_context & ctxt, const char *output_dirname, const char *class_name)
{
  int i, alloc_sz;
  int newfd, oldfd;
  char new_fname[PATH_MAX];
  char old_fname[PATH_MAX];
  char *buf = NULL;
  off_t length, offset;
  ssize_t size, r_size, w_size;

  if (thread_count <= 0)
    {
      return true;
    }


#if 1				// time check
  struct timeval startTime, endTime;
  double diffTime;

  gettimeofday (&startTime, NULL);
#endif

  // rename
  old_fname[0] = new_fname[0] = '\0';
  get_output_file_name (ctxt, output_dirname, class_name, 1, old_fname);
  get_output_file_name (ctxt, output_dirname, class_name, -1, new_fname);
  if (rename (old_fname, new_fname) != 0)
    {
      return false;
    }

  if (thread_count > 1)
    {
      newfd = open (new_fname, O_WRONLY | O_APPEND, OPEN_MODE_VALUE);
      if (newfd == INVALID_FILE_NO)
	{
	  return false;
	}
    }

  alloc_sz = 16 * 1024 * 1000;
  buf = (char *) malloc (alloc_sz);

  for (i = 1; i < thread_count; i++)
    {
      get_output_file_name (ctxt, output_dirname, class_name, i + 1, old_fname);
      oldfd = open (old_fname, O_RDONLY, OPEN_MODE_VALUE);
      if (oldfd == INVALID_FILE_NO)
	{
	  free (buf);
	  return false;
	}
      //struct stat stbuf;
      //if (stat (old_fname, &stbuf) == -1)      
#if defined(WINDOWS)
      length = _lseeki64 (oldfd, 0, SEEK_END);
      _lseeki64 (oldfd, 0, SEEK_SET);
#else
      length = lseek64 (oldfd, 0, SEEK_END);
      lseek64 (oldfd, 0, SEEK_SET);
#endif

      offset = 0;
      while (offset < length)
	{
	  size = length - offset;
	  if (size > alloc_sz)
	    size = alloc_sz;

	  r_size = read (oldfd, buf, size);
	  w_size = write (newfd, buf, size);
	  offset += size;
	}

      close (oldfd);
      unlink (old_fname);
    }

  close (newfd);
  free (buf);

#if 1
  gettimeofday (&endTime, NULL);
  diffTime = (endTime.tv_sec - startTime.tv_sec) + ((double) (endTime.tv_usec - startTime.tv_usec) / 1000000);
  printf ("Merge time= %.6f s\n", diffTime);
#endif


  return true;
}
#endif // #if defined(SUPPORT_THREAD_UNLOAD)


static bool
open_output_file (extract_context & ctxt, const char *output_dirname, const char *class_name)
{
#if defined(SUPPORT_THREAD_UNLOAD)
  if (thread_count > 0)
    {
      assert (datafile_per_class && class_name);
      for (int i = 0; i < thread_count; i++)
	{
	  if (open_object_file (&(thr_param[i].text_output), ctxt, output_dirname, class_name, i + 1) == false)
	    {
	      return false;
	    }
	}

      g_obj_out = &(thr_param[0].text_output);
    }
  else
#endif
  if (open_object_file (g_obj_out, ctxt, output_dirname, class_name, -1) == false)
    {
      return false;
    }

  return true;
}

static bool
close_output_file ()
{
  bool bret = true;

#if defined(SUPPORT_THREAD_UNLOAD)
  if (thread_count > 0)
    {
      bret = fulsh_all_object_files (false);
      close_all_object_files (true);
    }
  else
#endif
    {
      if (text_print_flush (g_obj_out) != NO_ERROR)
	{
	  bret = false;
	}
#if defined(SUPPORT_THREAD_UNLOAD)
      close (g_obj_out->fd);
      g_obj_out->fd = INVALID_FILE_NO;
#else
      fclose (g_obj_out->fp);
      g_obj_out->fp = NULL;
#endif
    }

  return bret;
}

static void
alloc_text_output (TEXT_OUTPUT * obj_out, const char *output_dirname)
{
  struct stat stbuf;
  int blksize;

  blksize = 4096;		/* init */
  if (stat (output_dirname, &stbuf) == -1)
    {
      ;				/* nop */
    }
  else
    {
#if defined (WINDOWS)
      blksize = 4096;
#else /* !WINDOWS */
      blksize = stbuf.st_blksize;
#endif /* WINDOWS */
    }

  /*
   * Determine the IO buffer size by specifying a multiple of the
   * natural block size for the device.
   * NEED FUTURE OPTIMIZATION
   */
  obj_out->iosize = 1024 * 1024;	/* 1 Mbyte */
  obj_out->iosize -= (obj_out->iosize % blksize);

  obj_out->buffer = (char *) malloc (obj_out->iosize);

  obj_out->ptr = obj_out->buffer;	/* init */
  obj_out->count = 0;		/* init */
}

/*
 * extract_objects - dump the database in loader format.
 *    return: 0 for success. 1 for error
 *    exec_name(in): utility name
 */
int
extract_objects (extract_context & ctxt, const char *output_dirname)
{
  int i, error;
  HFID *hfid;
  int64_t est_objects = 0;
  int cache_size;
  SM_CLASS *class_ptr;
  const char **cptr;
  int status = 0;
  int num_unload_classes = 0;
  DB_OBJECT **unload_class_table = NULL;
  bool has_obj_ref;
  int num_cls_ref;
  SM_ATTRIBUTE *attribute;
  void (*prev_intr_handler) (int sig);
  void (*prev_term_handler) (int sig);
#if !defined (WINDOWS)
  void (*prev_quit_handler) (int sig);
#endif
  LOG_LSA lsa;
  char unloadlog_filename[PATH_MAX];
  char owner_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
  char *class_name = NULL;
  char owner_str[DB_MAX_USER_LENGTH + 4] = { '\0' };

  /* register new signal handlers */
  prev_intr_handler = os_set_signal_handler (SIGINT, extractobjects_term_handler);
  prev_term_handler = os_set_signal_handler (SIGTERM, extractobjects_term_handler);
#if !defined(WINDOWS)
  prev_quit_handler = os_set_signal_handler (SIGQUIT, extractobjects_term_handler);
#endif

  if (cached_pages <= 0)
    {
      fprintf (stderr,
	       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_UNLOADDB, UNLOADDB_MSG_INVALID_CACHED_PAGES));
      return 1;
    }
  if (page_size < (ssize_t) (sizeof (OID) + sizeof (int)))
    {
      fprintf (stderr,
	       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_UNLOADDB, UNLOADDB_MSG_INVALID_CACHED_PAGE_SIZE));
      return 1;
    }

  /*
   * Open output file
   */
  if (output_dirname == NULL)
    output_dirname = ".";
  if (strlen (output_dirname) > PATH_MAX - 8)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_UNLOADDB, UNLOADDB_MSG_INVALID_DIR_NAME));
      return 1;
    }

#if defined(SUPPORT_THREAD_UNLOAD)
  if (thread_count > 0)
    {
      thr_param = (UNLD_THR_PARAM *) calloc (thread_count, sizeof (UNLD_THR_PARAM));
      if (thr_param == NULL)
	{
	  assert (false);
	  fprintf (stderr, "Failed to allocate memory. (%ld bytes)\n\n",
		   ((size_t) thread_count * sizeof (UNLD_THR_PARAM)));
	  return 1;
	}

      for (i = 0; i < thread_count; i++)
	{
	  alloc_text_output (&(thr_param[i].text_output), output_dirname);
	}
    }
  else
#endif
    {
      alloc_text_output (g_obj_out, output_dirname);
    }

  if (!datafile_per_class)
    {
      if (open_output_file (ctxt, output_dirname, NULL) == false)
	{
	  status = 1;
	  goto end;
	}
    }


  /*
   * The user indicates which classes are to be processed by
   * using -i with a file that contains a list of classes.
   * If the -i option is not used, it means process all classes.
   * Thus if input_filename is null, it means process all classes.
   * Three bit arrays are allocated to indicate whether a class
   * is requested, is referenced or is processed.  The index
   * into these arrays is the same as the index into class_table->mops.
   */
  if ((unload_class_table = (DB_OBJECT **) malloc (DB_SIZEOF (void *) * class_table->num)) == NULL)
    {
      status = 1;
      goto end;
    }
  for (i = 0; i < class_table->num; ++i)
    {
      unload_class_table[i] = NULL;
    }
  if ((class_requested = (char *) malloc ((class_table->num + 7) / 8)) == NULL)
    {
      status = 1;
      goto end;
    }
  if ((class_referenced = (char *) malloc ((class_table->num + 7) / 8)) == NULL)
    {
      status = 1;
      goto end;
    }
  if ((class_processed = (char *) malloc ((class_table->num + 7) / 8)) == NULL)
    {
      status = 1;
      goto end;
    }

  memset (class_requested, 0, (class_table->num + 7) / 8);
  memset (class_referenced, 0, (class_table->num + 7) / 8);
  memset (class_processed, 0, (class_table->num + 7) / 8);

  /*
   * Create the class hash table
   * Its purpose is to hash a class OID to the index into the
   * class_table->mops array.
   */
  cl_table = fh_create ("class hash", 4096, 1024, 4, NULL, FH_OID_KEY, DB_SIZEOF (int), oid_hash, oid_compare_equals);
  if (cl_table == NULL)
    {
      status = 1;
      goto end;
    }

  has_obj_ref = false;		/* init */
  num_cls_ref = 0;		/* init */

  /*
   * Total the number of objects & mark requested classes.
   */
#if defined(CUBRID_DEBUG) || defined(CUBRID_DEBUG_TEST)
  fprintf (stdout, "----- all class dump -----\n");
#endif /* CUBRID_DEBUG */
  for (i = 0; i < class_table->num; i++)
    {
      if (WS_IS_DELETED (class_table->mops[i]) || class_table->mops[i] == sm_Root_class_mop
	  || db_is_vclass (class_table->mops[i]))
	{
	  continue;
	}

      error = au_fetch_class (class_table->mops[i], NULL, AU_FETCH_READ, AU_SELECT);
      if (error != NO_ERROR)
	{
	  continue;
	}

      ws_find (class_table->mops[i], (MOBJ *) (&class_ptr));
      if (class_ptr == NULL)
	{
	  status = 1;
	  goto end;
	}

      for (cptr = prohibited_classes; *cptr; ++cptr)
	{
	  if (strcmp (*cptr, sm_ch_name ((MOBJ) class_ptr)) == 0)
	    {
	      break;
	    }
	}
      if (*cptr == NULL)
	{
#if defined(CUBRID_DEBUG) || defined(CUBRID_DEBUG_TEST)
	  fprintf (stdout, "%s%s%s\n", PRINT_IDENTIFIER (sm_ch_name ((MOBJ) class_ptr)));
#endif /* CUBRID_DEBUG */

	  SPLIT_USER_SPECIFIED_NAME (sm_ch_name ((MOBJ) class_ptr), owner_name, class_name);

	  if ((ctxt.is_dba_user == false && ctxt.is_dba_group_member == false)
	      && strcasecmp (owner_name, ctxt.login_user) != 0)
	    {
	      continue;
	    }

	  fh_put (cl_table, ws_oid (class_table->mops[i]), &i);
	  if (input_filename)
	    {
	      if (is_req_class (class_table->mops[i]))
		{
		  MARK_CLASS_REQUESTED (i);
		}
	      else if (!required_class_only)
		{
		  error = sm_is_system_class (class_table->mops[i]);
		  if (error < 0)
		    {
		      status = 1;
		      goto end;
		    }
		  if (error > 0)
		    {
		      MARK_CLASS_REQUESTED (i);
		      error = NO_ERROR;
		    }
		}
	    }
	  else
	    {
	      MARK_CLASS_REQUESTED (i);
	    }

	  if (!datafile_per_class && (!required_class_only || IS_CLASS_REQUESTED (i)))
	    {
	      PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), owner_str,
				sizeof (owner_str));

	      if (text_print
		  (g_obj_out, NULL, 0, "%cid %s%s%s%s %d\n", '%', owner_str,
		   PRINT_IDENTIFIER (class_name), i) != NO_ERROR)
		{
		  status = 1;
		  goto end;
		}
	    }

	  if (IS_CLASS_REQUESTED (i))
	    {
	      if (!datafile_per_class)
		{
		  if (!has_obj_ref)
		    {		/* not found object domain */
		      for (attribute = class_ptr->shared; attribute != NULL;
			   attribute = (SM_ATTRIBUTE *) attribute->header.next)
			{
			  /* false -> don't set */
			  if ((has_obj_ref = check_referenced_domain (attribute->domain, false, &num_cls_ref)) == true)
			    {
#if defined(CUBRID_DEBUG) || defined(CUBRID_DEBUG_TEST)
			      fprintf (stdout, "found OBJECT domain: %s%s%s->%s\n",
				       PRINT_IDENTIFIER (sm_ch_name ((MOBJ) class_ptr)), db_attribute_name (attribute));
#endif /* CUBRID_DEBUG */
			      break;
			    }
			}
		    }

		  if (!has_obj_ref)
		    {		/* not found object domain */
		      for (attribute = class_ptr->class_attributes; attribute != NULL;
			   attribute = (SM_ATTRIBUTE *) attribute->header.next)
			{
			  /* false -> don't set */
			  if ((has_obj_ref = check_referenced_domain (attribute->domain, false, &num_cls_ref)) == true)
			    {
#if defined(CUBRID_DEBUG) || defined(CUBRID_DEBUG_TEST)
			      fprintf (stdout, "found OBJECT domain: %s%s%s->%s\n",
				       PRINT_IDENTIFIER (sm_ch_name ((MOBJ) class_ptr)), db_attribute_name (attribute));
#endif /* CUBRID_DEBUG */
			      break;
			    }
			}
		    }

		  if (!has_obj_ref)
		    {		/* not found object domain */
		      for (attribute = class_ptr->ordered_attributes; attribute; attribute = attribute->order_link)
			{
			  if (attribute->header.name_space != ID_ATTRIBUTE)
			    {
			      continue;
			    }
			  has_obj_ref = check_referenced_domain (attribute->domain, false
								 /* don't set */ ,
								 &num_cls_ref);
			  if (has_obj_ref == true)
			    {
#if defined(CUBRID_DEBUG) || defined(CUBRID_DEBUG_TEST)
			      fprintf (stdout, "found OBJECT domain: %s%s%s->%s\n",
				       PRINT_IDENTIFIER (sm_ch_name ((MOBJ) class_ptr)), db_attribute_name (attribute));
#endif /* CUBRID_DEBUG */
			      break;
			    }
			}
		    }
		}
	      unload_class_table[num_unload_classes] = class_table->mops[i];
	      num_unload_classes++;
	    }

	  hfid = sm_ch_heap ((MOBJ) class_ptr);
	  if (!HFID_IS_NULL (hfid))
	    {
	      if (get_estimated_objs (hfid, &est_objects) < 0)
		{
		  status = 1;
		  goto end;
		}
	    }
	}
    }

  OR_PUT_NULL_OID (&null_oid);

#if defined(CUBRID_DEBUG) || defined(CUBRID_DEBUG_TEST)
  fprintf (stdout, "has_obj_ref = %d, num_cls_ref = %d\n", has_obj_ref, num_cls_ref);
#endif /* CUBRID_DEBUG */

  if (has_obj_ref || num_cls_ref > 0)
    {				/* found any referenced domain */
      int num_set;

      num_set = 0;		/* init */

      for (i = 0; i < class_table->num; i++)
	{
	  if (!IS_CLASS_REQUESTED (i))
	    {
	      continue;
	    }

	  /* check for emptyness, but not implemented NEED FUTURE WORk */

	  if (has_obj_ref)
	    {
	      MARK_CLASS_REFERENCED (i);
	      continue;
	    }

	  ws_find (class_table->mops[i], (MOBJ *) (&class_ptr));
	  if (class_ptr == NULL)
	    {
	      status = 1;
	      goto end;
	    }

	  if (mark_referenced_domain (class_ptr, &num_set) == false)
	    {
	      status = 1;
	      goto end;
	    }


	}			/* for (i = 0; i < class_table->num; i++) */

      if (has_obj_ref)
	{
	  ;			/* nop */
	}
      else
	{
	  if (num_cls_ref != num_set)
	    {
#if defined(CUBRID_DEBUG) || defined(CUBRID_DEBUG_TEST)
	      fprintf (stdout, "num_cls_ref = %d, num_set = %d\n", num_cls_ref, num_set);
#endif /* CUBRID_DEBUG */
	      status = 1;
	      goto end;
	    }
	}
    }

#if defined(CUBRID_DEBUG) || defined(CUBRID_DEBUG_TEST)
  {
    int total_req_cls = 0;
    int total_ref_cls = 0;

    fprintf (stdout, "----- referenced class dump -----\n");
    for (i = 0; i < class_table->num; i++)
      {
	if (!IS_CLASS_REQUESTED (i))
	  {
	    continue;
	  }
	total_req_cls++;
	if (IS_CLASS_REFERENCED (i))
	  {
	    ws_find (class_table->mops[i], (MOBJ *) (&class_ptr));
	    if (class_ptr == NULL)
	      {
		status = 1;
		goto end;
	      }
	    SPLIT_USER_SPECIFIED_NAME (sm_ch_name ((MOBJ) class_ptr), owner_name, class_name);
	    fprintf (stdout, "%s%s%s.%s%s%s\n", PRINT_IDENTIFIER (owner_name), PRINT_IDENTIFIER (class_name));
	    total_ref_cls++;
	  }
      }
    fprintf (stdout, "class_table->num = %d, total_req_cls = %d, total_ref_cls = %d\n", class_table->num,
	     total_req_cls, total_ref_cls);
  }
#endif /* CUBRID_DEBUG */

  /*
   * Lock all unloaded classes with IS_LOCK
   *   - If there is only view, num_unload_classes can be 0.
   */
  if (num_unload_classes
      && locator_fetch_set (num_unload_classes, unload_class_table, DB_FETCH_READ, DB_FETCH_READ, true) == NULL)
    {
      status = 1;
      goto end;
    }

  locator_get_append_lsa (&lsa);

  /*
   * Estimate the number of objects.
   */

  if (est_size == 0)
    {
      est_size = est_objects;
    }

  cache_size = cached_pages * page_size / (DB_SIZEOF (OID) + DB_SIZEOF (int));
  est_size = est_size > cache_size ? est_size : cache_size;

  /*
   * Create the hash table
   */
  obj_table =
    fh_create ("object hash", est_size, page_size, cached_pages, hash_filename, FH_OID_KEY, DB_SIZEOF (int),
	       oid_hash, oid_compare_equals);

  if (obj_table == NULL)
    {
      status = 1;
      goto end;
    }

  /*
   * Dump the object definitions
   */
  total_approximate_class_objects = est_objects;

  if (create_filename
      (ctxt.output_dirname, ctxt.output_prefix, "_unloaddb.log", unloadlog_filename, sizeof (unloadlog_filename)) != 0)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
      status = 1;
      goto end;
    }

  unloadlog_file = fopen (unloadlog_filename, "w+");
  if (unloadlog_file != NULL)
    {
      fprintf (unloadlog_file, HEADER_FORMAT, "Class Name", "Total Instances");
    }
  if (verbose_flag)
    {
      fprintf (stdout, HEADER_FORMAT, "Class Name", "Total Instances");
    }

  do
    {
      for (i = 0; i < class_table->num; i++)
	{
	  if (!WS_IS_DELETED (class_table->mops[i]) && class_table->mops[i] != sm_Root_class_mop)
	    {
	      int ret_val;

	      if (datafile_per_class && IS_CLASS_REQUESTED (i))
		{
		  ws_find (class_table->mops[i], (MOBJ *) (&class_ptr));
		  if (class_ptr == NULL)
		    {
		      status = 1;
		      goto end;
		    }

		  if (open_output_file (ctxt, output_dirname, sm_ch_name ((MOBJ) class_ptr)) == false)
		    {
		      status = 1;
		      goto end;
		    }
		}

	      ret_val = process_class (ctxt, i, g_obj_out);

	      if (datafile_per_class && IS_CLASS_REQUESTED (i))
		{
		  if (close_output_file () == false)
		    {
		      status = 1;
		      goto end;
		    }

#if defined(SUPPORT_THREAD_UNLOAD)
		  //  Merge and delete multiple divided files
		  if (merge_and_remove_files (ctxt, output_dirname, sm_ch_name ((MOBJ) class_ptr)) == false)
		    {
		      status = 1;
		      goto end;
		    }
#endif
		}

	      if (ret_val != NO_ERROR)
		{
		  if (!ignore_err_flag)
		    {
		      status = 1;
		      goto end;
		    }
		}
	    }
	}
    }
  while (!all_classes_processed ());

  if (failed_objects != 0)
    {
      status = 1;
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_UNLOADDB, UNLOADDB_MSG_OBJECTS_FAILED),
	       total_objects - failed_objects, total_objects);
      if (unloadlog_file != NULL)
	{
	  fprintf (unloadlog_file,
		   msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_UNLOADDB, UNLOADDB_MSG_OBJECTS_FAILED),
		   total_objects - failed_objects, total_objects);
	}
    }
  else if (verbose_flag)
    {
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_UNLOADDB, UNLOADDB_MSG_OBJECTS_DUMPED),
	       total_objects);
    }

  if (unloadlog_file != NULL)
    {
      if (failed_objects == 0)
	{
	  fprintf (unloadlog_file,
		   msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_UNLOADDB, UNLOADDB_MSG_OBJECTS_DUMPED),
		   total_objects);
	}
      fprintf (unloadlog_file, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_UNLOADDB, UNLOADDB_MSG_LOG_LSA),
	       lsa.pageid, lsa.offset);
    }

  /* flush remaining buffer */

  if (!datafile_per_class)
    {
      if (close_output_file () == false)
	{
	  status = 1;
	}
    }

/* in case of both normal and error */
end:
  if (unloadlog_file != NULL)
    {
      fclose (unloadlog_file);
    }
  /*
   * Cleanup
   */
  free_and_init (unload_class_table);
  extractobjects_cleanup ();

  /* restore previous signal handlers */
  (void) os_set_signal_handler (SIGINT, prev_intr_handler);
  (void) os_set_signal_handler (SIGTERM, prev_term_handler);
#if !defined (WINDOWS)
  (void) os_set_signal_handler (SIGQUIT, prev_quit_handler);
#endif

  return (status);
}


/*
 * gauge_alarm_handler - signal handler
 *    return: void
 *    sig(in): singal number
 */
static void
gauge_alarm_handler (int sig)
{
  if (sig == SIGALRM)
    {
      fprintf (stdout, MSG_FORMAT "\r", gauge_class_name, class_objects,
	       (class_objects > 0
		&& approximate_class_objects >=
		class_objects) ? (int) (100 * ((float) class_objects / (float) approximate_class_objects)) : 100,
	       (int) (100 * ((float) total_objects / (float) total_approximate_class_objects)));
      fflush (stdout);
    }
  else
    {
      ;
    }
#if !defined(WINDOWS)
  alarm (GAUGE_INTERVAL);
#endif
  return;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ctshim
//#define SUPPORT_THREAD_UNLOAD // #add_definitions(-DSUPPORT_THREAD_UNLOAD)

#if defined(SUPPORT_THREAD_UNLOAD)
static pthread_mutex_t g_cs_lock = PTHREAD_MUTEX_INITIALIZER;

void
unload_fetcher (UNLD_CLASS_PARAM * unld_class_param, LC_FETCH_VERSION_TYPE fetch_type)
{
  int i, error = NO_ERROR;
  int nobjects = 0;
  int nfetched = -1;
  OID last_oid;
  LC_COPYAREA *fetch_area;	/* Area where objects are received */
  LOCK lock = IS_LOCK;		/* Lock to acquire for the above purpose */
  //LC_COPYAREA_MANYOBJS *mobjs;        /* Describe multiple objects in area */
  LC_COPYAREA_ONEOBJ *obj;	/* Describe on object in area */

  OID *class_oid = unld_class_param->class_oid;
  HFID *hfid = unld_class_param->hfid;

  OID_SET_NULL (&last_oid);

  while (nobjects != nfetched)
    {
      if (locator_fetch_all (hfid, &lock, fetch_type, class_oid, &nobjects, &nfetched, &last_oid, &fetch_area
#if defined(SUPPORT_THREAD_UNLOAD_MTP)
			     , g_modular, g_accept
#endif
	  ) == NO_ERROR)
	{
	  if (fetch_area != NULL)
	    {
	      unld_class_param->cparea_lst_ref->add (fetch_area, true);
	      fetch_area = NULL;
	      //pthread_mutex_lock (&unld_class_param->mtx);  // ctshim
	      pthread_cond_signal (&unld_class_param->cond);
	      //pthread_mutex_unlock (&unld_class_param->mtx);                
	    }
	  else
	    {
	      /* No more objects */
	      break;
	    }
	}
      else
	{
	  /* some error was occurred */
	  if (!ignore_err_flag)
	    {
	      break;
	    }

	  ++failed_objects;
	}
    }
}


int
unload_writer (UNLD_THR_PARAM * unld_thr_param, LC_COPYAREA * fetch_area, TEXT_OUTPUT * obj_out)
{
  int i, error;
  RECDES recdes;		/* Record descriptor */
  LC_COPYAREA_MANYOBJS *mobjs;	/* Describe multiple objects in area */
  LC_COPYAREA_ONEOBJ *obj;	/* Describe on object in area */
  OID *class_oid = unld_thr_param->uci->class_oid;
  SM_CLASS *class_ptr = unld_thr_param->uci->class_ptr;
  DESC_OBJ *desc_obj = NULL;	/* The object described by obj */
  DB_OBJECT *class_ = unld_thr_param->uci->class_;
  int referenced_class = unld_thr_param->uci->referenced_class;
#if defined(WINDOWS)
  struct _timeb timebuffer;
  time_t start = 0;
#endif
  int ret = NO_ERROR;

#if defined(SUPPORT_THREAD_UNLOAD)
  desc_obj = make_desc_obj (class_ptr, varchar_alloc_size);
#else
  desc_obj = make_desc_obj (class_ptr);
#endif

  mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (fetch_area);
  obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (mobjs);

  for (i = 0; i < mobjs->num_objs; ++i)
    {
      /*
       * Process all objects for a requested class, but
       * only referenced objects for a referenced class.
       */
      LC_RECDES_TO_GET_ONEOBJ (fetch_area, obj, &recdes);
      error = desc_disk_to_obj (class_, class_ptr, &recdes, desc_obj);
      if (error == NO_ERROR)
	{
	  if ((error = process_object (desc_obj, &obj->oid, referenced_class, obj_out)) != NO_ERROR)
	    {
	      if (!ignore_err_flag)
		{
		  ret = ER_FAILED;
		  break;
		}
	    }
	}
      else
	{
	  if (error == ER_TF_BUFFER_UNDERFLOW)
	    {
	      ret = ER_FAILED;
	      break;
	    }
	  ++unld_thr_param->failed_objects;
	}
      obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (obj);
#if defined(WINDOWS)
      if (verbose_flag && (i % 10 == 0))
	{
	  _ftime (&timebuffer);
	  if (start == 0)
	    {
	      start = timebuffer.time;
	    }
	  else
	    {
	      if ((timebuffer.time - start) > GAUGE_INTERVAL)
		{
		  gauge_alarm_handler (SIGALRM);
		  start = timebuffer.time;
		}
	    }
	}
#endif
    }

  unld_thr_param->class_objects += i;
  unld_thr_param->total_objects += i;

  desc_free (desc_obj);
  return ret;
}

void *
unload_thread_proc (void *param)
{
  UNLD_THR_PARAM *parg = (UNLD_THR_PARAM *) param;
  int idx = parg->thread_idx;
  pthread_t tid = pthread_self ();
  LC_COPYAREA_NODE *node = NULL;
  //LC_COPYAREA *fetch_area = NULL;
  TEXT_OUTPUT *obj_out = &(thr_param[idx].text_output);

  cuberr::context * er_context_p;
// we need to register a context
  er_context_p = new cuberr::context ();
  er_context_p->register_thread_local ();

  while (parg->uci->is_terminate == false)
    {
      node = parg->uci->cparea_lst_ref->get ();
      if (node)
	{
	  unload_writer (parg, node->lc_copy_area, obj_out);
	  parg->uci->cparea_lst_ref->add_freelist (node);
	}
      else if (parg->uci->is_terminate == false)
	{
	  pthread_mutex_lock (&parg->uci->mtx);
	  pthread_cond_wait (&parg->uci->cond, &parg->uci->mtx);
	  pthread_mutex_unlock (&parg->uci->mtx);
	}
    }

  node = parg->uci->cparea_lst_ref->get ();
  while (node)
    {
      unload_writer (parg, node->lc_copy_area, obj_out);
      parg->uci->cparea_lst_ref->add_freelist (node);
      node = parg->uci->cparea_lst_ref->get ();
    }

  //
  er_context_p->deregister_thread_local ();
  delete er_context_p;
  er_context_p = NULL;

  pthread_exit (NO_ERROR);
}
#endif // #if defined(SUPPORT_THREAD_UNLOAD)
////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
 * process_class - dump one class in loader format
 *    return: NO_ERROR, if successful, error number, if not successful.
 *    cl_no(in): class object index for class_table
 */
static int
process_class (extract_context & ctxt, int cl_no, TEXT_OUTPUT * obj_out)
{
  int error = NO_ERROR;
  DB_OBJECT *class_ = class_table->mops[cl_no];
  int i = 0;
  int v = 0;
  SM_CLASS *class_ptr;
  char owner_name[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };
  char *class_name = NULL;
  SM_ATTRIBUTE *attribute;
  LC_COPYAREA *fetch_area;	/* Area where objects are received */
  HFID *hfid;
  OID *class_oid;
  OID last_oid;
  LOCK lock = IS_LOCK;		/* Lock to acquire for the above purpose */
  int nobjects, nfetched;
  LC_COPYAREA_MANYOBJS *mobjs;	/* Describe multiple objects in area */
  LC_COPYAREA_ONEOBJ *obj;	/* Describe on object in area */
  RECDES recdes;		/* Record descriptor */
  DESC_OBJ *desc_obj = NULL;	/* The object described by obj */
  int requested_class = 0;
  int referenced_class = 0;
  void (*prev_handler) (int sig) = NULL;
  unsigned int prev_alarm = 0;
#if defined(WINDOWS)
  struct _timeb timebuffer;
  time_t start = 0;
#endif
  int total;
  char output_owner[DB_MAX_USER_LENGTH + 4] = { '\0' };

  LC_FETCH_VERSION_TYPE fetch_type = latest_image_flag ? LC_FETCH_CURRENT_VERSION : LC_FETCH_MVCC_VERSION;

#if defined(SUPPORT_THREAD_UNLOAD)
#define UNLDDB_THREAD_CNT  (8)
  class copyarea_list cparea_lst_class;
  pthread_attr_t thread_attr;

  UNLD_CLASS_PARAM unld_cls_info;
#endif

  /*
   * Only process classes that were requested or classes that were
   * referenced via requested classes.
   */
  if (IS_CLASS_PROCESSED (cl_no))
    {
      goto exit_on_end;		/* do nothing successfully */
    }

  if (IS_CLASS_REQUESTED (cl_no))
    requested_class = 1;
  if (IS_CLASS_REFERENCED (cl_no))
    referenced_class = 1;

  if (!requested_class && !referenced_class)
    {
      goto exit_on_end;		/* do nothing successfully */
    }

  class_objects = 0;
  MARK_CLASS_PROCESSED (cl_no);

  /* Get the class data */
  ws_find (class_, (MOBJ *) (&class_ptr));
  if (class_ptr == NULL)
    {
      goto exit_on_error;
    }

#if defined(SUPPORT_THREAD_UNLOAD)
  if (thread_count > 0)
    {
      for (i = 0; i < class_ptr->att_count; i++)
	{
	  DB_TYPE db_type = class_ptr->attributes[i].type->get_id ();
	  switch (db_type)
	    {
	    case DB_TYPE_OID:
	    case DB_TYPE_OBJECT:
	      fprintf (stdout, "%s%s%s\n", PRINT_IDENTIFIER (sm_ch_name ((MOBJ) class_ptr)));
	      fflush (stdout);
	      goto exit_on_end;
	      break;

	    default:
	      break;
	    }
	}
/*
  attribute = class_ptr->ordered_attributes;
  for (attribute = class_ptr->ordered_attributes; attribute; attribute = attribute->order_link)
  {
    db_type = class_ptr->attributes[i].type->get_id ();
  }
*/
      for (i = 0; i < thread_count; i++)
	{
	  thr_param[i].class_objects = 0;
	  thr_param[i].failed_objects = 0;
	}
    }
#endif

  class_oid = ws_oid (class_);

  v = 0;
  for (attribute = class_ptr->shared; attribute != NULL; attribute = (SM_ATTRIBUTE *) attribute->header.next)
    {
      if (DB_VALUE_TYPE (&attribute->default_value.value) == DB_TYPE_NULL)
	{
	  continue;
	}
      if (v == 0)
	{
	  SPLIT_USER_SPECIFIED_NAME (sm_ch_name ((MOBJ) class_ptr), owner_name, class_name);

	  PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
			    sizeof (output_owner));

	  CHECK_PRINT_ERROR (text_print
			     (obj_out, NULL, 0, "%cclass %s%s%s%s shared (%s%s%s", '%',
			      output_owner, PRINT_IDENTIFIER (class_name), PRINT_IDENTIFIER (attribute->header.name)));
	}
      else
	{
	  CHECK_PRINT_ERROR (text_print (obj_out, NULL, 0, ", %s%s%s", PRINT_IDENTIFIER (attribute->header.name)));
	}

      ++v;
    }
  if (v)
    {
      CHECK_PRINT_ERROR (text_print (obj_out, ")\n", 2, NULL));
    }

  v = 0;
  for (attribute = class_ptr->shared; attribute != NULL; attribute = (SM_ATTRIBUTE *) attribute->header.next)
    {
      if (DB_VALUE_TYPE (&attribute->default_value.value) == DB_TYPE_NULL)
	{
	  continue;
	}
      if (v)
	{
	  CHECK_PRINT_ERROR (text_print (obj_out, " ", 1, NULL));
	}
      error = process_value (&attribute->default_value.value, obj_out);
      if (error != NO_ERROR)
	{
	  if (!ignore_err_flag)
	    goto exit_on_error;
	}

      ++v;
    }
  if (v)
    {
      CHECK_PRINT_ERROR (text_print (obj_out, "\n", 1, NULL));
    }

  v = 0;
  for (attribute = class_ptr->class_attributes; attribute != NULL; attribute = (SM_ATTRIBUTE *) attribute->header.next)
    {
      if (DB_VALUE_TYPE (&attribute->default_value.value) == DB_TYPE_NULL)
	{
	  continue;
	}
      if (v == 0)
	{
	  SPLIT_USER_SPECIFIED_NAME (sm_ch_name ((MOBJ) class_ptr), owner_name, class_name);

	  PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner,
			    sizeof (output_owner));

	  CHECK_PRINT_ERROR (text_print
			     (obj_out, NULL, 0, "%cclass %s%s%s%s class (%s%s%s", '%',
			      output_owner, PRINT_IDENTIFIER (class_name), PRINT_IDENTIFIER (attribute->header.name)));
	}
      else
	{
	  CHECK_PRINT_ERROR (text_print (obj_out, NULL, 0, ", %s%s%s", PRINT_IDENTIFIER (attribute->header.name)));
	}
      ++v;
    }
  if (v)
    {
      CHECK_PRINT_ERROR (text_print (obj_out, ")\n", 2, NULL));
    }

  v = 0;
  for (attribute = class_ptr->class_attributes; attribute != NULL; attribute = (SM_ATTRIBUTE *) attribute->header.next)
    {

      if (DB_VALUE_TYPE (&attribute->default_value.value) == DB_TYPE_NULL)
	{
	  continue;
	}
      if (v)
	{
	  CHECK_PRINT_ERROR (text_print (obj_out, " ", 1, NULL));
	}
      if ((error = process_value (&attribute->default_value.value, obj_out)) != NO_ERROR)
	{
	  if (!ignore_err_flag)
	    {
	      goto exit_on_error;
	    }
	}

      ++v;
    }

  SPLIT_USER_SPECIFIED_NAME (sm_ch_name ((MOBJ) class_ptr), owner_name, class_name);

  PRINT_OWNER_NAME (owner_name, (ctxt.is_dba_user || ctxt.is_dba_group_member), output_owner, sizeof (output_owner));

  CHECK_PRINT_ERROR (text_print (obj_out, NULL, 0, (v) ? "\n%cclass %s%s%s%s ("	/* new line */
				 : "%cclass %s%s%s%s (", '%', output_owner, PRINT_IDENTIFIER (class_name)));

  v = 0;
  attribute = class_ptr->ordered_attributes;
  while (attribute)
    {
      if (attribute->header.name_space == ID_ATTRIBUTE)
	{
	  CHECK_PRINT_ERROR (text_print (obj_out, NULL, 0, (v) ? " %s%s%s"	/* space */
					 : "%s%s%s", PRINT_IDENTIFIER (attribute->header.name)));
	  ++v;
	}
      attribute = (SM_ATTRIBUTE *) attribute->order_link;
    }
  CHECK_PRINT_ERROR (text_print (obj_out, ")\n", 2, NULL));

  /* Find the heap where the instances are stored */
  hfid = sm_ch_heap ((MOBJ) class_ptr);
  if (hfid->vfid.fileid == NULL_FILEID)
    {
      if (total_objects == total_approximate_class_objects)
	{
	  total = 100;
	}
      else
	{
	  total = (int) (100 * ((float) total_objects / (float) total_approximate_class_objects));
	}
      fprintf (unloadlog_file, MSG_FORMAT "\n", sm_ch_name ((MOBJ) class_ptr), (long) 0, 100, total);
      fflush (unloadlog_file);
      if (verbose_flag)
	{
	  fprintf (stdout, MSG_FORMAT "\n", sm_ch_name ((MOBJ) class_ptr), (long) 0, 100, total);
	  fflush (stdout);
	}
      goto exit_on_end;
    }

  /* Flush all the instances */

  if (locator_flush_all_instances (class_, DONT_DECACHE) != NO_ERROR)
    {
      if (total_objects == total_approximate_class_objects)
	{
	  total = 100;
	}
      else
	{
	  total = (int) (100 * ((float) total_objects / (float) total_approximate_class_objects));
	}
      fprintf (unloadlog_file, MSG_FORMAT "\n", sm_ch_name ((MOBJ) class_ptr), (long) 0, 100, total);
      fflush (unloadlog_file);
      if (verbose_flag)
	{
	  fprintf (stdout, MSG_FORMAT "\n", sm_ch_name ((MOBJ) class_ptr), (long) 0, 100, total);
	  fflush (stdout);
	}
      goto exit_on_end;
    }

  nobjects = 0;
  nfetched = -1;
  OID_SET_NULL (&last_oid);

  /* Now start fetching all the instances */

  approximate_class_objects = 0;
  if (get_estimated_objs (hfid, &approximate_class_objects) < 0)
    {
      if (!ignore_err_flag)
	goto exit_on_error;
    }

  if (verbose_flag)
    {
      gauge_class_name = (char *) sm_ch_name ((MOBJ) class_ptr);
#if !defined (WINDOWS)
      prev_handler = os_set_signal_handler (SIGALRM, gauge_alarm_handler);
      prev_alarm = alarm (GAUGE_INTERVAL);
#endif
    }

#if defined(SUPPORT_THREAD_UNLOAD)
  if (thread_count > 0)
    {
      unld_cls_info.hfid = hfid;
      unld_cls_info.class_oid = class_oid;
      unld_cls_info.class_ptr = class_ptr;
      unld_cls_info.class_ = class_;
      unld_cls_info.pctxt = &ctxt;
      unld_cls_info.referenced_class = 0;
      unld_cls_info.is_terminate = false;

      if (pthread_mutex_init (&unld_cls_info.mtx, NULL) == 0)
	{
	  error = pthread_cond_init (&unld_cls_info.cond, NULL);
	}

      cparea_lst_class.set_max_list (thread_count * 4);
      //cparea_lst_class.set_max_list (thread_count);
      unld_cls_info.cparea_lst_ref = &cparea_lst_class;

      pthread_attr_init (&thread_attr);
      pthread_attr_setdetachstate (&thread_attr, PTHREAD_CREATE_DETACHED);
      pthread_attr_setscope (&thread_attr, PTHREAD_SCOPE_SYSTEM);


      //pthread_mutex_lock (&unld_cls_info.mtx);
      for (i = 0; i < thread_count; i++)
	{
	  thr_param[i].thread_idx = i;
	  thr_param[i].uci = &unld_cls_info;
	  thr_param[i].total_objects = 0;
	  thr_param[i].class_objects = 0;
	  thr_param[i].failed_objects = 0;

	  if (pthread_create (&(thr_param[i].tid), NULL, unload_thread_proc, (void *) (thr_param + i)) != NO_ERROR)
	    {
	      abort ();
	      exit (0);
	    }
	}
      //pthread_mutex_unlock (&unld_cls_info.mtx);   

      std::this_thread::yield ();
      unload_fetcher (&unld_cls_info, fetch_type);

      unld_cls_info.is_terminate = true;
      pthread_cond_broadcast (&unld_cls_info.cond);
      std::this_thread::yield ();

      for (i = 0; i < thread_count; i++)
	{
	  void *retval;

	  pthread_join (thr_param[i].tid, &retval);
	  if (retval != NO_ERROR)
	    {
	      abort ();
	      exit (0);
	    }
	}

      pthread_cond_destroy (&unld_cls_info.cond);
      //unld_cls_info.cparea_lst_ref->clear_freelist ();

      for (i = 0; i < thread_count; i++)
	{
	  total_objects += thr_param[i].total_objects;
	  class_objects += thr_param[i].class_objects;
	  failed_objects += thr_param[i].failed_objects;
	}
    }
  else
#endif // #if defined(SUPPORT_THREAD_UNLOAD)
    {
#if defined(SUPPORT_THREAD_UNLOAD)
      desc_obj = make_desc_obj (class_ptr, varchar_alloc_size);
#else
      desc_obj = make_desc_obj (class_ptr);
#endif

      while (nobjects != nfetched)
	{
	  //fprintf(stdout, ">>>>>>>> %d, %d\n", g_modular, g_accept); fflush(stdout); // ctshim
	  if (locator_fetch_all (hfid, &lock, fetch_type, class_oid, &nobjects, &nfetched, &last_oid, &fetch_area
#if defined(SUPPORT_THREAD_UNLOAD_MTP)
				 , g_modular, g_accept
#endif
	      ) == NO_ERROR)
	    {
	      if (fetch_area != NULL)
		{
		  mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (fetch_area);
		  obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (mobjs);
#if defined(SUPPORT_THREAD_UNLOAD)
		  if (check_fetch_time == false)
#endif
		    for (i = 0; i < mobjs->num_objs; ++i)
		      {
			/*
			 * Process all objects for a requested class, but
			 * only referenced objects for a referenced class.
			 */
			++class_objects;
			++total_objects;
			LC_RECDES_TO_GET_ONEOBJ (fetch_area, obj, &recdes);
			if ((error = desc_disk_to_obj (class_, class_ptr, &recdes, desc_obj)) == NO_ERROR)
			  {
			    if ((error = process_object (desc_obj, &obj->oid, referenced_class, obj_out)) != NO_ERROR)
			      {
				if (!ignore_err_flag)
				  {
				    desc_free (desc_obj);
				    locator_free_copy_area (fetch_area);
				    goto exit_on_error;
				  }
			      }
			  }
			else
			  {
			    if (error == ER_TF_BUFFER_UNDERFLOW)
			      {
				desc_free (desc_obj);
				goto exit_on_error;
			      }
			    ++failed_objects;
			  }
			obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (obj);
#if defined(WINDOWS)
			if (verbose_flag && (i % 10 == 0))
			  {
			    _ftime (&timebuffer);
			    if (start == 0)
			      {
				start = timebuffer.time;
			      }
			    else
			      {
				if ((timebuffer.time - start) > GAUGE_INTERVAL)
				  {
				    gauge_alarm_handler (SIGALRM);
				    start = timebuffer.time;
				  }
			      }
			  }
#endif
		      }
		  locator_free_copy_area (fetch_area);
		}
	      else
		{
		  /* No more objects */
		  break;
		}
	    }
	  else
	    {
	      /* some error was occurred */
	      if (!ignore_err_flag)
		{
		  desc_free (desc_obj);
		  goto exit_on_error;
		}
	      else
		++failed_objects;
	    }
	}

      desc_free (desc_obj);
    }

  total_approximate_class_objects += (class_objects - approximate_class_objects);
  if (total_objects == total_approximate_class_objects)
    {
      total = 100;
    }
  else
    {
      total = (int) (100 * ((float) total_objects / (float) total_approximate_class_objects));
    }
  if (verbose_flag)
    {
#if !defined(WINDOWS)
      alarm (prev_alarm);
      (void) os_set_signal_handler (SIGALRM, prev_handler);
#endif

      fprintf (stdout, MSG_FORMAT "\n", sm_ch_name ((MOBJ) class_ptr), class_objects, 100, total);
      fflush (stdout);
    }
  fprintf (unloadlog_file, MSG_FORMAT "\n", sm_ch_name ((MOBJ) class_ptr), class_objects, 100, total);

exit_on_end:
  return error;

exit_on_error:

  CHECK_EXIT_ERROR (error);
  goto exit_on_end;

}

/*
 * process_object - dump one object in loader format
 *    return: NO_ERROR, if successful, error number, if not successful.
 *    desc_obj(in): object data
 *    obj_oid(in): object oid
 *    referenced_class(in): is referenced ?
 */
static int
process_object (DESC_OBJ * desc_obj, OID * obj_oid, int referenced_class, TEXT_OUTPUT * obj_out)
{
  int error = NO_ERROR;
  SM_CLASS *class_ptr;
  SM_ATTRIBUTE *attribute;
  DB_VALUE *value;
  OID *class_oid;
  int data;
  int v = 0;

  class_ptr = desc_obj->class_;
  class_oid = ws_oid (desc_obj->classop);
  if (!datafile_per_class && referenced_class)
    {				/* need to hash OID */
      update_hash (obj_oid, class_oid, &data);
      if (debug_flag)
	{
	  CHECK_PRINT_ERROR (text_print
			     (obj_out, NULL, 0, "%d/*%d.%d.%d*/: ", data, obj_oid->volid, obj_oid->pageid,
			      obj_oid->slotid));
	}
      else
	{
	  CHECK_PRINT_ERROR (text_print (obj_out, NULL, 0, "%d: ", data));
	}
    }

  attribute = class_ptr->ordered_attributes;
  for (attribute = class_ptr->ordered_attributes; attribute; attribute = attribute->order_link)
    {

      if (attribute->header.name_space != ID_ATTRIBUTE)
	continue;

      if (v)
	CHECK_PRINT_ERROR (text_print (obj_out, " ", 1, NULL));

      value = &desc_obj->values[attribute->storage_order];

      if ((error = process_value (value, obj_out)) != NO_ERROR)
	{
	  if (!ignore_err_flag)
	    goto exit_on_error;
	}

      ++v;
    }
  CHECK_PRINT_ERROR (text_print (obj_out, "\n", 1, NULL));

exit_on_end:

  return error;

exit_on_error:

  CHECK_EXIT_ERROR (error);
  goto exit_on_end;

}

/*
 * process_set - dump a set in loader format
 *    return: NO_ERROR, if successful, error number, if not successful.
 *    set(in): set
 * Note:
 *    Should only get here for class and shared attributes that have
 *    default values.
 */
static int
process_set (DB_SET * set, TEXT_OUTPUT * obj_out)
{
  int error = NO_ERROR;
  SET_ITERATOR *it = NULL;
  DB_VALUE *element_value;
  int check_nelem = 0;

  CHECK_PRINT_ERROR (text_print (obj_out, "{", 1, NULL));

  it = set_iterate (set);
  while ((element_value = set_iterator_value (it)) != NULL)
    {

      if ((error = process_value (element_value, obj_out)) != NO_ERROR)
	{
	  if (!ignore_err_flag)
	    goto exit_on_error;
	}

      check_nelem++;

      if (set_iterator_next (it))
	{
	  CHECK_PRINT_ERROR (text_print (obj_out, ", ", 2, NULL));
	  if (check_nelem >= 10)
	    {			/* set New-Line for each 10th elements */
	      CHECK_PRINT_ERROR (text_print (obj_out, "\n", 1, NULL));
	      check_nelem -= 10;
	    }
	}
    }
  CHECK_PRINT_ERROR (text_print (obj_out, "}", 1, NULL));

exit_on_end:
  if (it != NULL)
    {
      set_iterator_free (it);
    }
  return error;

exit_on_error:

  CHECK_EXIT_ERROR (error);
  goto exit_on_end;
}


/*
 * process_value - dump one value in loader format
 *    return: NO_ERROR, if successful, error number, if not successful.
 *    value(in): the value to process
 */
static int
process_value (DB_VALUE * value, TEXT_OUTPUT * obj_out)
{
  int error = NO_ERROR;

  switch (DB_VALUE_TYPE (value))
    {
    case DB_TYPE_OID:
    case DB_TYPE_OBJECT:
      {
	OID *ref_oid;
	int ref_data;
	OID ref_class_oid;
	DB_OBJECT *classop;
	SM_CLASS *class_ptr;
	int *cls_no_ptr, cls_no;

	if (DB_VALUE_TYPE (value) == DB_TYPE_OID)
	  {
	    ref_oid = db_get_oid (value);
	  }
	else
	  {
	    ref_oid = WS_OID (db_get_object (value));
	  }

	if (required_class_only || (ref_oid == (OID *) 0) || (OID_EQ (ref_oid, &null_oid)) || datafile_per_class)
	  {
	    CHECK_PRINT_ERROR (text_print (obj_out, "NULL", 4, NULL));
	    break;
	  }

	OID_SET_NULL (&ref_class_oid);

	error = locator_does_exist (ref_oid, NULL_CHN, IS_LOCK, &ref_class_oid, NULL_CHN, false, false, NULL,
				    TM_TRAN_READ_FETCH_VERSION ());
	if (error == LC_EXIST)
	  {
	    classop = is_class (ref_oid, &ref_class_oid);
	    if (classop != NULL)
	      {
		ws_find (classop, (MOBJ *) (&class_ptr));
		if (class_ptr == NULL)
		  {
		    goto exit_on_error;
		  }
		CHECK_PRINT_ERROR (text_print (obj_out, NULL, 0, "@%s", sm_ch_name ((MOBJ) class_ptr)));
		break;
	      }

	    /*
	     * Lock referenced class with S_LOCK
	     */
	    error = NO_ERROR;	/* clear */
	    classop = is_class (&ref_class_oid, WS_OID (sm_Root_class_mop));
	    if (classop != NULL)
	      {
		if (locator_fetch_class (classop, DB_FETCH_QUERY_READ) == NULL)
		  {
		    error = LC_ERROR;
		  }
	      }
	    else
	      {
		error = LC_ERROR;
	      }
	  }
	else if (error != LC_DOESNOT_EXIST)
	  {
	    error = LC_ERROR;
	  }
	else
	  {
	    CHECK_PRINT_ERROR (text_print (obj_out, "NULL", 4, NULL));
	    break;
	  }

	if (error != NO_ERROR)
	  {
	    if (!ignore_err_flag)
	      {
		goto exit_on_error;
	      }
	    else
	      {
		(void) text_print (obj_out, "NULL", 4, NULL);
		break;
	      }
	  }

	/*
	 * Output a reference indication if all classes are being processed,
	 * or if a class_list is being used and references are being included,
	 * or if a class_list is being used and the referenced class is a
	 * requested class.  Otherwise, output "NULL".
	 */

	/* figure out what it means for this to be NULL, I think this happens only for the reserved system classes like
	 * db_user that are not dumped.  This is a problem because trigger objects for one, like to point directly at
	 * the user object.  There will probably be others in time. */
	error = fh_get (cl_table, &ref_class_oid, (FH_DATA *) (&cls_no_ptr));
	if (error != NO_ERROR || cls_no_ptr == NULL)
	  {
	    CHECK_PRINT_ERROR (text_print (obj_out, "NULL", 4, NULL));
	  }
	else
	  {
	    cls_no = *cls_no_ptr;
	    if (!input_filename || include_references || (IS_CLASS_REQUESTED (cls_no)))
	      {
		update_hash (ref_oid, &ref_class_oid, &ref_data);
		if (debug_flag)
		  {
		    int *temp;
		    error = fh_get (cl_table, &ref_class_oid, (FH_DATA *) (&temp));
		    if (error != NO_ERROR || temp == NULL)
		      {
			CHECK_PRINT_ERROR (text_print (obj_out, "NULL", 4, NULL));
		      }
		    else
		      {
			CHECK_PRINT_ERROR (text_print
					   (obj_out, NULL, 0, "@%d|%d/*%d.%d.%d*/", *temp, ref_data, ref_oid->volid,
					    ref_oid->pageid, ref_oid->slotid));
		      }
		  }
		else
		  {
		    int *temp;
		    error = fh_get (cl_table, &ref_class_oid, (FH_DATA *) (&temp));
		    if (error != NO_ERROR || temp == NULL)
		      {
			CHECK_PRINT_ERROR (text_print (obj_out, "NULL", 4, NULL));
		      }
		    else
		      {
			CHECK_PRINT_ERROR (text_print (obj_out, NULL, 0, "@%d|%d", *temp, ref_data));
		      }
		  }
	      }
	    else
	      {
		CHECK_PRINT_ERROR (text_print (obj_out, "NULL", 4, NULL));
	      }
	  }
	break;
      }

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      CHECK_PRINT_ERROR (process_set (db_get_set (value), obj_out));
      break;

    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
      {
	DB_ELO *elo;
	DB_TYPE dt = db_value_type (value);
	char dts;

	if (dt == DB_TYPE_BLOB)
	  {
	    dts = 'B';
	  }
	else
	  {
	    dts = 'C';
	  }

	elo = db_get_elo (value);

	if (elo != NULL)
	  {
	    if (elo->type == ELO_FBO)
	      {
		CHECK_PRINT_ERROR (text_print
				   (obj_out, NULL, 0, "^E'%c%lld|%s|%s'", dts, elo->size, elo->locator,
				    elo->meta_data != NULL ? elo->meta_data : ""));
	      }
	    else
	      {
		/* should not happen */
		assert (0);
	      }
	  }
	else
	  {
	    CHECK_PRINT_ERROR (text_print (obj_out, "NULL", 4, NULL));
	  }
	break;
      }

    default:
      CHECK_PRINT_ERROR (desc_value_special_fprint (obj_out, value));
      break;
    }

exit_on_end:

  return error;

exit_on_error:

  CHECK_EXIT_ERROR (error);
  goto exit_on_end;

}


/*
 * update_hash - update obj_table hash
 *    return: void
 *    object_oid(in): the object oid used as hash key
 *    class_oid(in): the oid of the object's class
 *    data(out): the value to associate with the oid
 * Note:
 *    If the object oid exists, return the data.  Otherwise, get the value for
 *    the class oid, increment it and use it for the data.  Store the data
 *    with the object oid  and class oid.  The data for the class id is the
 *    next number to use for the class.  If the class oid doesn't exist,
 *    initialize the data to 1.
 */
static void
update_hash (OID * object_oid, OID * class_oid, int *data)
{
  int *d;
  int error;

  error = fh_get (obj_table, object_oid, (FH_DATA *) (&d));
  if (error != NO_ERROR || d == NULL)
    {
      error = fh_get (obj_table, class_oid, (FH_DATA *) (&d));
      if (error != NO_ERROR || d == NULL)
	{
	  *data = 1;
	}
      else
	{
	  *data = *d + 1;
	}
      if (fh_put (obj_table, class_oid, data) != NO_ERROR)
	{
	  perror ("SYSTEM ERROR related with hash-file\n==>unloaddb is NOT completed");
	  exit (1);
	}
      if (fh_put (obj_table, object_oid, data) != NO_ERROR)
	{
	  perror ("SYSTEM ERROR related with hash-file\n==>unloaddb is NOT completed");
	  exit (1);
	}
    }
  else
    {
      *data = *d;
    }
}


/*
 * is_class - determine whether the object is actually a class.
 *    return: MOP for the object
 *    obj_oid(in): the object oid
 *    class_oid(in): the class oid
 */
static DB_OBJECT *
is_class (OID * obj_oid, OID * class_oid)
{
  if (OID_EQ (class_oid, WS_OID (sm_Root_class_mop)))
    {
      return ws_mop (obj_oid, NULL);
    }
  return 0;
}


/*
 * is_req_class - determine whether the class was requested in the input file
 *    return: 1 if true, otherwise 0.
 *    class(in): the class object
 */
int
is_req_class (DB_OBJECT * class_)
{
  int n;

  for (n = 0; n < class_table->num; ++n)
    {
      if (req_class_table[n] == class_)
	return 1;
    }
  return 0;
}


/*
 * all_classes_processed - compares class_requested with class_processed.
 *    return: 1 if true, otherwise 0.
 */
static int
all_classes_processed (void)
{
  int n;

  for (n = 0; n < (class_table->num + 7) / 8; ++n)
    {
      if ((class_requested[n] | class_referenced[n]) != class_processed[n])
	return 0;
    }
  return 1;
}

/*
 * ltrim - trim a given string.
 *    return: pointer to the trimed string.
 */
static char *
ltrim (char *s)
{
  char *begin;

  assert (s != NULL);

  begin = s;
  while (*begin != '\0')
    {
      if (isspace (*begin))
	{
	  begin++;
	}
      else
	{
	  break;
	}
    }
  s = begin;

  return s;
}

/*
 * get_requested_classes - read the class names from input_filename
 *    return: 0 for success, non-zero otherwise
 *    input_filename(in): the name of the input_file
 *    class_list(out): the class table to store classes in
 */
int
get_requested_classes (const char *input_filename, DB_OBJECT * class_list[])
{
  FILE *input_file;
  char buffer[LINE_MAX];
  int i = 0;			/* index of class_list */
  int error = NO_ERROR;

  if (input_filename == NULL)
    {
      return NO_ERROR;
    }

  input_file = fopen (input_filename, "r");
  if (input_file == NULL)
    {
      perror (input_filename);
      return ER_GENERIC_ERROR;
    }

  while (fgets ((char *) buffer, LINE_MAX, input_file) != NULL)
    {
      DB_OBJECT *class_ = NULL;
      MOP *sub_partitions = NULL;
      int is_partition = 0;
      int j = 0;		/* index of sub_partitions */
      const char *dot = NULL;
      char downcase_class_name[DB_MAX_IDENTIFIER_LENGTH];
      int len = 0;

      trim (buffer);

      len = STATIC_CAST (int, strlen (buffer));
      if (len < 1)
	{
	  /* empty string */
	  continue;
	}

      dot = strchr (buffer, '.');
      if (dot)
	{
	  /* user specified name */

	  /* user name of user specified name */
	  len = STATIC_CAST (int, dot - buffer);
	  if (len >= DB_MAX_USER_LENGTH)
	    {
	      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_UNLOADDB,
						     UNLOADDB_MSG_EXCEED_MAX_USER_LEN), DB_MAX_USER_LENGTH - 1);
	      fclose (input_file);
	      return ER_GENERIC_ERROR;
	    }

	  /* class name of user specified name */
	  len = STATIC_CAST (int, strlen (dot + 1));
	}

      if (len >= DB_MAX_IDENTIFIER_LENGTH - DB_MAX_USER_LENGTH)
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_UNLOADDB,
						 UNLOADDB_MSG_EXCEED_MAX_LEN),
				 DB_MAX_IDENTIFIER_LENGTH - DB_MAX_USER_LENGTH - 1);
	  fclose (input_file);
	  return ER_GENERIC_ERROR;
	}

      sm_user_specified_name (buffer, downcase_class_name, DB_MAX_IDENTIFIER_LENGTH);

      class_ = locator_find_class (downcase_class_name);
      if (class_ != NULL)
	{
	  class_list[i] = class_;
	  error = sm_partitioned_class_type (class_, &is_partition, NULL, &sub_partitions);
	  if (error != NO_ERROR)
	    {
	      fclose (input_file);
	      return error;
	    }
	  if (is_partition == 1 && sub_partitions != NULL)
	    {
	      for (j = 0; sub_partitions[j]; j++)
		{
		  i++;
		  class_list[i] = sub_partitions[j];
		}
	    }
	  if (sub_partitions != NULL)
	    {
	      free_and_init (sub_partitions);
	    }
	  i++;
	}
      else
	{
	  PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_UNLOADDB,
						 UNLOADDB_MSG_CLASS_NOT_FOUND), downcase_class_name);
	}
    }				/* while */

  fclose (input_file);

  return error;
}

static int
create_filename (const char *output_dirname, const char *output_prefix, const char *suffix, char *output_filename_p,
		 const size_t filename_size)
{
  if (output_dirname == NULL)
    {
      output_dirname = ".";
    }

  size_t total = strlen (output_dirname) + strlen (output_prefix) + strlen (suffix) + 8;

#if defined(SUPPORT_THREAD_UNLOAD)
  char proc_name[32];

  if (g_modular > 1)
    {
      total += sprintf (proc_name, "p%d(%d)", g_accept, g_modular);
    }
#endif

  if (total > filename_size)
    {
      return -1;
    }

#if defined(SUPPORT_THREAD_UNLOAD)
  if (g_modular > 1)
    {
      snprintf (output_filename_p, filename_size - 1, "%s/%s_%s%s", output_dirname, output_prefix, proc_name, suffix);
      return 0;
    }
#endif

  snprintf (output_filename_p, filename_size - 1, "%s/%s%s", output_dirname, output_prefix, suffix);

  return 0;
}
