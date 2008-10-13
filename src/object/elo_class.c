/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * elo_class.c - ELO interface
 *
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "error_manager.h"
#include "common.h"
#include "oid.h"
#include "server.h"
#include "object_representation.h"
#include "work_space.h"
#include "locator_cl.h"
#include "object_primitive.h"
#include "elo_class.h"
#include "fbo_class.h"
#include "elo_recovery.h"
#include "glo_class.h"
#include "network_interface_sky.h"

/* This is used for the page size field in the ELO.  Need to find out what
   the appropriate value is */

#define ELO_
#define ELO_INVALID_TYPE -3

/*
 * NAME: Elo_volume                                                         
 *                                                                          
 * This is only for integration.  The function lo_create must have the      
 * volume initially specified in the LOID before it can allocate a new one. 
 * I'm not sure how we're supposed to be allocating volumes at this level.  
 * For now, assume that its volume zero.                                    
 * THIS WILL HAVE TO BE CHANGED !!!                                         
 */

static short Elo_volume = 0;
static int elo_stream_buffer_size = 4096;

static int elo_copy_to_char (char *target, char *source, int c, int length);
static int elo_read_next_buffer_from_offset (ELO_STREAM * elo_stream,
					     int offset);
static int elo_read_next_buffer (ELO_STREAM * elo_stream);
static void elo_loid_init (LOID * loid);
static void elo_loid_copy (LOID * dest, LOID * src);
static char *elo_get_pathname_internal (DB_OBJECT * glo, DB_ELO * elo,
					char *expanded_pathname, int length);
static char elo_mode_check (const char *mode);
static int elo_flush (ELO_STREAM * elo_stream);

/*
 * LOID FUNCTIONS                               
 */

/* Shouldn't have this level of intelligence here, should go in lo.c */

/*
 * elo_loid_init() - Initialize LARGE OBJECT IDENTIFIER 
 *      return: none 
 *  loid(out) : the LOID object 
 * 
 */

static void
elo_loid_init (LOID * loid)
{
  loid->vpid.volid = -1;
  loid->vpid.pageid = -1;
  loid->vfid.volid = -1;
  loid->vfid.fileid = -1;
}

/*
 * elo_loid_copy() - copy from src LOID to des LOID
 *      return: none 
 *  dest(out) : destination LOID
 *  src(in) : source LOID   
 * 
 */

static void
elo_loid_copy (LOID * dest, LOID * src)
{
  dest->vpid.volid = src->vpid.volid;
  dest->vpid.pageid = src->vpid.pageid;
  dest->vfid.volid = src->vfid.volid;
  dest->vfid.fileid = src->vfid.fileid;
}

/*
 * elo_get_pathname_internal() - Returns the current pathname of the FBO from 
 *                           the elo struct or from the shadow file recovery 
 *                           queue in mm/recover.c    
 *      return: the pathname of the FBO
 *  glo(in) : the glo object
 *  elo(in) : the ELO object
 *  expanded_pathname(in) : destination buffer
 *  length(in) : length of destination buffer
 */

static char *
elo_get_pathname_internal (DB_OBJECT * glo, DB_ELO * elo,
			   char *expanded_pathname, int length)
{
  char *pathname;

  /* Note: we should not be hiding the int passed back to us. */
  if ((esm_get_shadow_file_name (glo, &pathname)) != NO_ERROR ||
      pathname == NULL)
    {
      pathname = (char *) elo->pathname;

      esm_expand_pathname (pathname, expanded_pathname, length);
      pathname = expanded_pathname;
    }
  return (pathname);
}

/*
 * elo_read_from() - reads length bytes from the elo object
 *      return: returns actual number of bytes read or a negative value 
 *              indicating the error received from the operation               
 *  elo(in) : the elo object
 *  offset(in) : offset to start reading from.   
 *  length(in) : number of bytes to read
 *  buffer(in) : buffer to read into
 *  glo(in) : the containing Glo instance (for file mgr info)
 * 
 */

int
elo_read_from (DB_ELO * elo, const long offset, const int length,
	       char *buffer, DB_OBJECT * glo)
{
  int err = NO_ERROR;
  int return_value;
  char pathname[PATH_MAX];

  switch (elo->type)
    {
    case ELO_LO:
      {
	if ((&elo->loid)->vpid.pageid == NULL_PAGEID)
	  {
	    return_value = 0;
	  }
	else
	  {
	    return_value =
	      largeobjmgr_read (&elo->loid, offset, length, buffer);
	    if (return_value < 0)
	      {
		err = er_errid ();
	      }
	  }
	break;
      }
    case ELO_FBO:
      {
	return_value = err =
	  (int)
	  esm_read (elo_get_pathname_internal
		    (glo, elo, pathname, PATH_MAX), offset, length, buffer);
	break;
      }
    default:
      return (ELO_INVALID_TYPE);
    }
  if (err < 0)
    {
      return (err);
    }
  return (return_value);
}

/*
 * elo_write_to() - writes length bytes from buffer over the data in the elo      
 *                  starting at offset
 *      return: Returns the number of bytes written or a negative number 
 *              corresponding to the error received during the operation.          
 *  elo(in) : the elo object
 *  offset(in) : offset to start writing at        
 *  length(in) : number of bytes to write
 *  buffer(in) : buffer to write from
 *  glo(in) : the containing Glo instance (for file mgr info)   
 * 
 */

int
elo_write_to (DB_ELO * elo, long offset, int length, char *buffer,
	      DB_OBJECT * glo)
{
  int err = NO_ERROR;
  OID *oid;
  char pathname[PATH_MAX];

  switch (elo->type)
    {
    case ELO_LO:
      {
	if ((&elo->loid)->vpid.pageid == NULL_PAGEID)
	  {
	    /* !!! need to do the right thing here */
	    oid = ws_oid (glo);
	    if ((oid)->pageid < NULL_PAGEID)
	      {
		oid = locator_assign_permanent_oid (glo);
		if (oid == NULL)
		  {
		    err = er_errid ();
		    break;
		  }
	      }

	    Elo_volume = 0;
	    elo->loid.vpid.volid = Elo_volume;
	    elo->loid.vfid.volid = Elo_volume;

	    if (offset > 0)
	      {
		if (largeobjmgr_create (&elo->loid, 0, NULL, length, oid) ==
		    NULL
		    || largeobjmgr_write (&elo->loid, offset, length,
					  buffer) < 0)
		  {
		    err = er_errid ();
		  }
	      }
	    else
	      {
		if (largeobjmgr_create
		    (&elo->loid, length, buffer, length, oid) == NULL)
		  {
		    err = er_errid ();
		  }
	      }
	    /* we could mask the error here but it may be better
	       to pass up the internal error */
	  }
	else
	  {
	    if (largeobjmgr_write (&elo->loid, offset, length, buffer) < 0)
	      {
		err = er_errid ();
	      }
	  }
	break;
      }

    case ELO_FBO:
      {
	err = (int) esm_write (elo_get_pathname_internal (glo, elo, pathname,
							  PATH_MAX),
			       offset, length, buffer);
	break;
      }

    default:
      return (ELO_INVALID_TYPE);
    }

  if (err < 0)
    {
      return (err);
    }

  return (length);
}

/*
 * elo_write_to() - inserts length bytes from buffer into the elo object      
 *      return: Returns the number of bytes inserted or a negative number 
 *              corresponding to the error code encountered during     
 *              the operation.                                                    
 *  elo(in) : the elo object
 *  offset(in) : offset to start inserting at       
 *  length(in) : number of bytes to insert
 *  buffer(in) : buffer to insert data from     
 *  glo(in) : the containing Glo instance (for file mgr info)  
 * 
 */

int
elo_insert_into (DB_ELO * elo, long offset, int length, char *buffer,
		 DB_OBJECT * glo)
{
  int err = NO_ERROR;
  OID *oid;
  int return_value;
  char pathname[PATH_MAX];

  switch (elo->type)
    {
    case ELO_LO:
      {
	if (length == 0)
	  {
	    return_value = 0;
	  }
	else
	  {
	    if ((&elo->loid)->vpid.pageid == NULL_PAGEID)
	      {
		/* !!! need to do the right thing here */
		oid = ws_oid (glo);
		if ((oid)->pageid < NULL_PAGEID)
		  {
		    oid = locator_assign_permanent_oid (glo);
		    if (oid == NULL)
		      {
			err = er_errid ();
			break;
		      }
		  }
		Elo_volume = 0;
		elo->loid.vpid.volid = Elo_volume;
		elo->loid.vfid.volid = Elo_volume;

		if (offset > 0)
		  {
		    if (largeobjmgr_create (&elo->loid, 0, NULL, length, oid)
			== NULL
			|| largeobjmgr_insert (&elo->loid, offset, length,
					       buffer) < 0)
		      {
			err = er_errid ();
		      }
		  }
		else
		  {
		    if (largeobjmgr_create
			(&elo->loid, length, buffer, length, oid) == NULL)
		      {
			err = er_errid ();
		      }
		  }
		/* we could mask the error here but it may be better
		   to pass up the internal error */
	      }
	    else
	      {
		return_value = largeobjmgr_insert (&elo->loid, offset, length,
						   buffer);
		if (return_value < 0)
		  {
		    err = er_errid ();
		  }
	      }
	  }
	break;
      }
    case ELO_FBO:
      {
	err = (int) esm_insert (elo_get_pathname_internal (glo, elo, pathname,
							   PATH_MAX),
				offset, length, buffer);
	break;
      }
    default:
      return (ELO_INVALID_TYPE);
    }				/* switch */
  if (err < 0)
    {
      return (err);
    }
  return (length);
}				/* elo_insert_into */

/*
 * elo_delete_from() - deletes length bytes from elo object
 *      return: Returns the number of bytes deleted.                         
 *  elo(in) : the elo object
 *  offset(in) : offset to start deleting at  
 *  length(in) : number of bytes to delete
 *  glo(in) : the glo object
 */

long
elo_delete_from (DB_ELO * elo, long offset, int length, DB_OBJECT * glo)
{
  int lsize;
  char pathname[PATH_MAX];

  switch (elo->type)
    {
    case ELO_LO:
      {
	if ((&elo->loid)->vpid.pageid == NULL_PAGEID)
	  {
	    return (0);
	  }
	else
	  {
	    lsize = largeobjmgr_delete (&elo->loid, offset, length);
	    if (lsize < 0)
	      {
		return (long) er_errid ();
	      }
	    return (lsize);
	  }
      }
    case ELO_FBO:
      {
	return esm_delete (elo_get_pathname_internal (glo, elo, pathname,
						      PATH_MAX),
			   offset, length);
      }
    default:
      return ((long) ELO_INVALID_TYPE);
    }				/* switch */
}				/* elo_delete_from */

/*
 * elo_get_size() - get the elo objects size
 *      return: Returns the current size of the ELO object or the negative    
 *              value associated with the error code encountered during       
 *              the operation
 *  elo(in) : the elo object
 *  glo(in) : the glo object  
 * 
 */

long
elo_get_size (DB_ELO * elo, DB_OBJECT * glo)
{
  long err = 0L;
  char pathname[PATH_MAX];

  switch (elo->type)
    {
    case ELO_LO:
      {
	if ((&elo->loid)->vpid.pageid == NULL_PAGEID)
	  {
	    return (0);
	  }
	else
	  {
	    err = (long) largeobjmgr_length (&elo->loid);
	    if (err < 0)
	      {
		return (long) er_errid ();
	      }
	    return err;
	  }
      }
    case ELO_FBO:
      {
	return (esm_get_size (elo_get_pathname_internal (glo, elo, pathname,
							 PATH_MAX)));
      }
    default:
      return ((long) ELO_INVALID_TYPE);
    }				/* switch */
}				/* elo_get_size */

/*
 * elo_truncate() - Truncates the elo object at the current position
 *      return: Returns the truncation size or the negative value associated 
 *              with the error code received during the operation       
 *  elo(in) : the elo object
 *  offset(in) : offset to truncate from                                  
 *  glo(in) : the glo object 
 * 
 */

long
elo_truncate (DB_ELO * elo, long offset, DB_OBJECT * glo)
{
  char pathname[PATH_MAX];

  switch (elo->type)
    {
    case ELO_LO:
      {
	int lsize;

	if ((&elo->loid)->vpid.pageid == NULL_PAGEID)
	  {
	    return (0);
	  }
	else
	  {
	    lsize = largeobjmgr_truncate (&elo->loid, offset);
	    if (lsize < 0)
	      {
		return (long) er_errid ();
	      }
	    return ((long) lsize);
	  }
      }
    case ELO_FBO:
      {
	return esm_truncate (elo_get_pathname_internal (glo, elo,
							pathname,
							PATH_MAX), offset);
      }
    default:
      return ((long) ELO_INVALID_TYPE);
    }
}

/*
 * elo_append_to() - appends length bytes from buffer to end of elo object
 *      return: Returns the number of bytes appended or a negative value      
 *              corresponding to the error code received during the operation
 *  elo(in) : the elo object
 *  length(in) :   number of bytes in buffer to append
 *  buffer(in) : data to append to end of elo
 *  glo(in) : the containing Glo instance (for file mgr info)
 * 
 * Note : 
 *      The elo's size is adjusted and current position is set to eof.  
 */

int
elo_append_to (DB_ELO * elo, int length, char *buffer, DB_OBJECT * glo)
{
  int err = NO_ERROR;
  OID *oid;
  char pathname[PATH_MAX];

  switch (elo->type)
    {
    case ELO_LO:
      {
	if ((&elo->loid)->vpid.pageid == NULL_PAGEID)
	  {
	    /* !!! need to do the right thing here */
	    oid = ws_oid (glo);
	    if ((oid)->pageid < NULL_PAGEID)
	      {
		oid = locator_assign_permanent_oid (glo);
		if (oid == NULL)
		  {
		    err = er_errid ();
		    break;
		  }
	      }

	    Elo_volume = 0;
	    elo->loid.vpid.volid = Elo_volume;
	    elo->loid.vfid.volid = Elo_volume;

	    if (largeobjmgr_create (&elo->loid, length, buffer, length, oid)
		== NULL)
	      {
		err = er_errid ();
	      }
	  }
	else
	  {
	    if (largeobjmgr_append (&elo->loid, length, buffer) < 0)
	      {
		err = er_errid ();
	      }
	  }
	break;
      }
    case ELO_FBO:
      {
	err = (int) esm_append (elo_get_pathname_internal (glo, elo, pathname,
							   PATH_MAX),
				length, buffer);
      }
      break;

    default:
      return (ELO_INVALID_TYPE);

    }				/* switch */

  if (err != NO_ERROR)
    {
      return (err);
    }
  return (length);
}				/* elo_append_to */

/*
 * elo_compress() - compresses large objects only
 *      return: returns 0 or error code received during operation       
 *  elo(in) : elo object to compress 
 * 
 */
/* TODO : later~~ */
int
elo_compress (DB_ELO * elo)
{
  int err = NO_ERROR;

  switch (elo->type)
    {
    case ELO_LO:
      if ((&elo->loid)->vpid.pageid != NULL_PAGEID &&
	  (largeobjmgr_compress (&elo->loid) != NO_ERROR))
	err = er_errid ();
      break;
    case ELO_FBO:
      break;
    default:
      err = ELO_INVALID_TYPE;
      break;
    }
  return ((int) err);
}

/* ELO CONSTRUCTORS */

/*
 * elo_new_elo() - Allocate and initialize area for elo object
 *      return: return the object or NULL if an error occurs.              
 * 
 */


DB_ELO *
elo_new_elo (void)
{
  DB_ELO *new_object = (DB_ELO *) db_ws_alloc (sizeof (ELO));

  if (new_object == NULL)
    {
      return (NULL);
    }
  new_object->type = ELO_LO;
  new_object->pathname = NULL;
  new_object->original_pathname = NULL;
  elo_loid_init (&new_object->loid);

  return (new_object);
}

/*
 * elo_copy() - Copy an ELO
 *      return: the elo object 
 *  src(in) : the elo object
 * 
 */

DB_ELO *
elo_copy (DB_ELO * src)
{
  DB_ELO *new_;

  new_ = (DB_ELO *) db_ws_alloc (sizeof (ELO));
  if (new_ != NULL)
    {
      elo_loid_copy (&new_->loid, &src->loid);
      new_->type = src->type;
      new_->pathname = ws_copy_string (src->pathname);
      new_->original_pathname = ws_copy_string (src->pathname);
      if (src->pathname && (!new_->pathname || !new_->original_pathname))
	{
	  return elo_free (new_);
	}
    }
  return (new_);
}

/*
 * elo_create() - Creates a new elo (large object) in the database with an      
 *                initial size of 0
 *      return: Returns elo object or NULL if an error occurs during 
 *              the operation
 *  pathname(in) : path name
 * 
 */

DB_ELO *
elo_create (const char *pathname)
{
  DB_ELO *elo;

  elo = (DB_ELO *) db_ws_alloc (sizeof (ELO));

  if (elo == NULL)
    {
      return (NULL);
    }
  elo->pathname = NULL;
  elo->original_pathname = NULL;
  elo_loid_init (&elo->loid);

  if (pathname == NULL)
    {
      elo->type = ELO_LO;
    }
  else
    {
      elo->type = ELO_FBO;
      /* formerly called esm_create(pathname) here which didn't   
         do anything - what should this be now ? */
      elo->pathname = ws_copy_string (pathname);
      elo->original_pathname = ws_copy_string (pathname);
      if (!elo->pathname || !elo->original_pathname)
	{
	  return elo_free (elo);
	}
    }
  return (elo);
}

/*
 * elo_destroy() - removes any data associated with LOs, and makes the Glo 
 *                 appear empty
 *      return: NULL
 *  elo(in) : elo object to destroy
 *  glo(in) : the glo object  
 * 
 */
/* TODO : later */
DB_ELO *
elo_destroy (DB_ELO * elo, DB_OBJECT * glo)
{
  switch (elo->type)
    {
    case ELO_LO:
      {
	int err;

	if (elo != NULL)
	  {
	    if ((&elo->loid)->vpid.pageid != NULL_PAGEID &&
		(largeobjmgr_destroy (&elo->loid) != NO_ERROR))
	      {
		err = er_errid ();
	      }
	    elo_loid_init (&elo->loid);
	    elo->pathname = NULL;
	    elo->original_pathname = NULL;
	  }
	break;
      }
    case ELO_FBO:
      {
	/*  do not destroy the file associated with the FBO. */
/*
      if (elo->pathname != NULL) {
	esm_destroy(internal_get_pathname(glo, elo));
      }
*/
	break;
      }
    default:
      return (NULL);
    }
  return (NULL);
}

/*
 * elo_free() - free elo object
 *      return: NULL 
 *  elo(in) : the elo object to free
 * 
 */

DB_ELO *
elo_free (DB_ELO * elo)
{

  switch (elo->type)
    {
    case ELO_LO:
      {
	break;
      }
    case ELO_FBO:
      {
	if (elo->pathname != NULL)
	  {
	    ws_free_string (elo->pathname);
	  }
	if (elo->original_pathname != NULL)
	  {
	    ws_free_string (elo->original_pathname);
	  }
	break;
      }
    default:
      return (NULL);
    }				/* switch */
  db_ws_free ((void *) elo);
  return (NULL);
}				/* elo_free */

/*
 * elo_get_pathname() - return the pathname
 *      return: return the pathname otherwise if object is a large object 
 *              return NULL  
 *  elo(in) : the elo object 
 * 
 */

const char *
elo_get_pathname (DB_ELO * elo)
{
  return (elo->pathname);
}				/* elo_get_pathname */

/*
 * elo_set_pathname() - copies pathname to elo's pathname 
 *                      allocating/deallocating space as needed
 *      return: Returns object or NULL on error
 *  elo(in) : the elo object
 *  pathname(in) : string containing the new pathname  
 * 
 */

DB_ELO *
elo_set_pathname (DB_ELO * elo, const char *pathname)
{

  /* always start by clearing what's there if anything */
  if (elo->pathname != NULL)
    {
      ws_free_string (elo->pathname);
      elo->pathname = NULL;
    }

  if (elo->type == ELO_FBO)
    {
      if (pathname != NULL)
	{
	  elo->pathname = ws_copy_string (pathname);
	  if (!elo->pathname)
	    {
	      return elo_free (elo);
	    }
	}
      else
	{
	  elo->type = ELO_LO;
	}
    }
  else
    {
      if (pathname != NULL)
	{
	  /* turn it into an FBO */
	  /* do we destroy the LOID here ?? mark as deleted ?? */
	  elo_loid_init (&elo->loid);
	  elo->type = ELO_FBO;
	  elo->pathname = ws_copy_string (pathname);
	  elo->original_pathname = ws_copy_string (pathname);
	  if (!elo->pathname || !elo->original_pathname)
	    {
	      return elo_free (elo);
	    }
	}
    }
  return (elo);
}

/* ELO STREAMS */

/*
 * elo_mode_check() - tests the open mode string to see if a legal value was passed
 *      return: will return ELO_READ_ONLY, ELO_READ_WRITE or ELO_MODE_ERROR
 *  mode(in) : open mode string
 * 
 */

static char
elo_mode_check (const char *mode)
{
  if (strcmp (mode, ELO_OPEN_READ_ONLY) == 0)
    {
      return (ELO_READ_ONLY);
    }
  if (strcmp (mode, ELO_OPEN_WRITE_ONLY) == 0)
    {
      return (ELO_READ_WRITE);
    }
  if (strcmp (mode, ELO_OPEN_APPEND) == 0)
    {
      return (ELO_READ_WRITE);
    }
  if (strcmp (mode, ELO_OPEN_UPDATE) == 0)
    {
      return (ELO_READ_WRITE);
    }
  if (strcmp (mode, ELO_OPEN_WRITE_UPDATE) == 0)
    {
      return (ELO_READ_WRITE);
    }
  if (strcmp (mode, ELO_OPEN_APPEND_UPDATE) == 0)
    {
      return (ELO_READ_WRITE);
    }
  return (ELO_MODE_ERROR);
}

/*
 * elo_open() - opens and allocates the elo stream
 *      return: return an ELO_STREAM on the elo, or NULL on error
 *  glo(in) : elo to open ELO_STREAM on
 *  mode(in) : open mode string   
 * 
 */

ELO_STREAM *
elo_open (DB_OBJECT * glo, const char *mode)
{
  ELO_STREAM *return_value = (ELO_STREAM *) db_ws_alloc (sizeof (ELO_STREAM));
  return_value->mode = elo_mode_check (mode);
  if (return_value->mode == ELO_MODE_ERROR)
    {
      db_ws_free ((void *) return_value);
      return (NULL);
    }
  if (return_value == NULL)
    return (NULL);

  return_value->buffer = (char *) db_ws_alloc (elo_stream_buffer_size);
  if (return_value->buffer == NULL)
    {
      db_ws_free ((void *) return_value);
      return (NULL);
    }
  return_value->offset = 0L;
  return_value->buffer_pos = 0;
  return_value->buffer_size = elo_stream_buffer_size;
  return_value->bytes_in_buffer = 0;
  return_value->buffer_current = false;
  return_value->buffer_modified = false;
  return_value->elo = esm_get_glo_from_holder_for_read (glo);
  return_value->glo = glo;
  return (return_value);
}

/*
 * elo_close() - closes and deallocates the elo stream
 *      return: Returns the number of bytes written or a negative number 
 *              corresponding to the error received during the operation
 *  elo_stream(in) : elo stream to deallocate
 * 
 */

int
elo_close (ELO_STREAM * elo_stream)
{
  int return_value = 0;

  if (elo_stream->buffer_modified)
    {
      return_value =
	elo_write_to (elo_stream->elo, elo_stream->offset,
		      elo_stream_buffer_size, elo_stream->buffer,
		      elo_stream->glo);
    }
  db_ws_free ((void *) elo_stream->buffer);
  db_ws_free ((void *) elo_stream);
  return (return_value);
}

/*
 * elo_flush() - 
 *      return: Returns the number of bytes written or a negative number 
 *              corresponding to the error received during the operation
 *  elo_stream(in) : the elo stream
 * 
 */


static int
elo_flush (ELO_STREAM * elo_stream)
{
  int bytes_written = 0;

  if (elo_stream->buffer_modified &&
      elo_stream->buffer_current && (elo_stream->mode == ELO_READ_WRITE))
    {
      bytes_written =
	elo_write_to (elo_stream->elo, elo_stream->offset,
		      elo_stream->bytes_in_buffer, elo_stream->buffer,
		      elo_stream->glo);
    }
  elo_stream->buffer_modified = false;
  return (bytes_written);
}

/*
 * elo_seek() - seeks to the requested location in the stream.  
 *      return: 
 *  elo_stream(in) : the elo stream 
 *  offset(in) : offset  
 *  origin(in) : where to seek relative from                    
 * 
 */

int
elo_seek (ELO_STREAM * elo_stream, long offset, int origin)
{
  long new_offset;

  switch (origin)
    {
    case SEEK_SET:
      {
	new_offset = offset;
	break;
      }
    case SEEK_CUR:
      {
	new_offset = offset + elo_stream->offset;
	break;
      }
    case SEEK_END:
      {
	new_offset = elo_get_size (elo_stream->elo, elo_stream->glo) - offset;
	if (new_offset < 0)
	  {
	    return (-1);
	  }
	if (new_offset < elo_stream->offset + elo_stream->bytes_in_buffer)
	  {
	    new_offset = elo_stream->offset + elo_stream->bytes_in_buffer;
	  }
	break;
      }
    default:
      return (END_OF_ELO);
    }
  if (new_offset == elo_stream->offset)
    {
      elo_stream->buffer_pos = 0;
      return (0);
    }
  if ((new_offset > elo_stream->offset) &&
      (new_offset < elo_stream->offset + elo_stream->bytes_in_buffer))
    {
      elo_stream->buffer_pos = new_offset - elo_stream->offset;
      return (0);
    }
  return (elo_read_next_buffer_from_offset (elo_stream, new_offset));
}

/*
 * elo_getc() - 
 *      return: returns the next character of elo_stream as an unsigned char
 *              or END_OF_ELO if an end of file occurs
 *  elo_stream(out) : elo stream
 * 
 */

int
elo_getc (ELO_STREAM * elo_stream)
{
  unsigned char return_value;

  if (!((elo_stream->buffer_current) &&
	(elo_stream->buffer_pos < elo_stream->bytes_in_buffer)))
    {
      if (elo_read_next_buffer (elo_stream) == END_OF_ELO)
	{
	  return (END_OF_ELO);
	}
    }
  return_value =
    (unsigned char) elo_stream->buffer[(elo_stream->buffer_pos)++];

  return (return_value);
}

/*
 * elo_gets() - 
 *      return: 
 *  s(in) : string
 *  n(in) : size of buffer (including \0 terminator)  
 *  elo_stream(out) : the stream representation
 * 
 */

char *
elo_gets (char *s, int n, ELO_STREAM * elo_stream)
{
  int bytes_copied;
  int max = n - 1;
  int total_bytes_copied = 0;
  int bytes_to_end_of_buffer;

  if (!((elo_stream->buffer_current) &&
	(elo_stream->buffer_pos < elo_stream->bytes_in_buffer)))
    {
      if (elo_read_next_buffer (elo_stream) == END_OF_ELO)
	{
	  return (NULL);
	}
    }

  bytes_to_end_of_buffer =
    elo_stream->bytes_in_buffer - elo_stream->buffer_pos;

  for (;;)
    {
      if (bytes_to_end_of_buffer > max)
	{
	  bytes_to_end_of_buffer = max;
	}
      bytes_copied = elo_copy_to_char (s, elo_stream->buffer, '\n',
				       bytes_to_end_of_buffer);
      if (bytes_copied == END_OF_ELO)
	{
	  return (NULL);
	}
      elo_stream->buffer_pos += bytes_copied;
      if (bytes_copied < bytes_to_end_of_buffer)
	{
	  return (s);
	}
      total_bytes_copied += bytes_copied;
      if (total_bytes_copied >= (n - 1))
	{
	  s[total_bytes_copied] = 0;
	  return (s);
	}
      if (elo_read_next_buffer (elo_stream) == END_OF_ELO)
	{
	  return (s);
	}
      bytes_to_end_of_buffer = elo_stream->bytes_in_buffer;
      max -= bytes_copied;
    }
}

/*
 * elo_putc() - eputc writes the character c (converted to an unsigned char) 
 *           on elo_stream
 *      return: It returns the character written or END_OF_ELO for error
 *  c(in) : character to put on stream
 *  elo_stream(out) : the stream representation  
 * 
 */

int
elo_putc (int c, ELO_STREAM * elo_stream)
{
  if (elo_stream->mode == ELO_READ_WRITE)
    {
      if (!((elo_stream->buffer_current) &&
	    (elo_stream->buffer_pos < elo_stream->buffer_size)))
	{
	  elo_read_next_buffer (elo_stream);
	}
      elo_stream->buffer[(elo_stream->buffer_pos)++] =
	(unsigned char) (0xff & c);
      if (elo_stream->buffer_pos >= elo_stream->bytes_in_buffer)
	{
	  elo_stream->bytes_in_buffer = elo_stream->buffer_pos;
	}
      elo_stream->buffer_modified = true;
      return (c);
    }
  return (-1);
}

/*
 * elo_puts() - 
 *      return: 
 *  s(out) : string to write to elo stream
 *  elo_stream(out) : the stream representation  
 * 
 */

int
elo_puts (const char *s, ELO_STREAM * elo_stream)
{
  int max = strlen (s);
  int bytes_to_write;
  int rc;

  if (elo_stream->mode == ELO_READ_WRITE)
    {

      if (!((elo_stream->buffer_current) &&
	    (elo_stream->buffer_pos < elo_stream->bytes_in_buffer)))
	{
	  elo_read_next_buffer (elo_stream);
	}

      bytes_to_write = elo_stream->bytes_in_buffer - elo_stream->buffer_pos;

      for (;;)
	{
	  if (max < bytes_to_write)
	    {
	      bytes_to_write = max + 1;
	    }
	  (void) memcpy (elo_stream->buffer + elo_stream->buffer_pos,
			 s, bytes_to_write);
	  elo_stream->bytes_in_buffer += bytes_to_write;
	  elo_stream->buffer_modified = true;
	  max -= bytes_to_write;
	  if (max <= 0)
	    {
	      return (strlen (s));
	    }
	  rc = elo_flush (elo_stream);
	  if (rc != bytes_to_write)
	    {
	      return (rc);
	    }
	  elo_stream->offset += elo_stream->bytes_in_buffer;
	  elo_stream->bytes_in_buffer = 0;
	  bytes_to_write = elo_stream->buffer_size;
	}
    }
  return (-1);
}

/*
 * elo_setvbuf() - release the standard buffer, use buf instead
 *      return: Returns 0 or -1 if there is an error.                        
 *  elo_stream(out) : elo stream
 *  buf(in/out) : buffer to use instead of default        
 *  buf_size(in/out) : size of buffer
 * 
 */


int
elo_setvbuf (ELO_STREAM * elo_stream, char *buf, int buf_size)
{
  if (buf == NULL)
    {
      return (-1);
    }
  elo_flush (elo_stream);
  db_ws_free ((void *) elo_stream->buffer);

  elo_stream->buffer = buf;
  elo_stream->buffer_size = buf_size;
  elo_stream->buffer_current = false;
  elo_stream->buffer_modified = false;
  return (0);
}

/*
 * elo_copy_to_char() - copies at most length characters from source to target 
 *                      or less if the character c is encountered                   
 *      return: 
 *  target(in) : 
 *  source(in) :   
 *  c(in) : char to search for
 *  length(in) : maximum length
 */


static int
elo_copy_to_char (char *target, char *source, int c, int length)
{
  char *found;

  found = (char *) memchr (source, c, length);

  if (found != NULL)
    {
      int new_length = found - source;

      (void) memcpy (target, source, new_length);
      target[length] = 0;
      return (new_length);
    }
  else
    {
      (void) memcpy (target, source, length);
      return (length);
    }
}

/*
 * elo_read_next_buffer_from_offset() - Reads the next elo into buffer 
 *      return: returns 0 if successful or END_OF_ELO if the end of the elo 
 *              is reached
 *  elo_stream(out) : the elo stream representation
 *  offset(in) : offset   
 * 
 */

static int
elo_read_next_buffer_from_offset (ELO_STREAM * elo_stream, int offset)
{
  int bytes_read;

  if (elo_stream->buffer_modified)
    {
      bytes_read = elo_flush (elo_stream);
      if (bytes_read < 0)
	{
	  return (bytes_read);
	}
    }
  bytes_read = elo_read_from (elo_stream->elo,
			      offset,
			      elo_stream->buffer_size,
			      elo_stream->buffer, elo_stream->glo);
  elo_stream->offset = offset;
  if (bytes_read > 0)
    {
      elo_stream->bytes_in_buffer = bytes_read;
    }
  else
    {
      elo_stream->bytes_in_buffer = 0;
    }
  elo_stream->buffer_pos = 0;
  elo_stream->buffer_current = true;
  elo_stream->buffer_modified = false;
  if (bytes_read <= 0)
    {
      memset (elo_stream->buffer, '\0', elo_stream->buffer_size);
      return (END_OF_ELO);
    }
  else
    {
      return (0);
    }
}

/*
 * elo_read_next_buffer() - Reads the next elo into buffer
 *      return: returns 0 if successful or END_OF_ELO if the end of the elo 
 *              is reached
 *  elo_stream(out) : the elo stream representation  
 * 
 */

static int
elo_read_next_buffer (ELO_STREAM * elo_stream)
{
  return (elo_read_next_buffer_from_offset
	  (elo_stream, elo_stream->offset + elo_stream->bytes_in_buffer));
}
