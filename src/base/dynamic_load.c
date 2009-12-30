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
 * dynamic_load.c - Dynamic loader for run-time inclusion of object code
 */

#ident "$Id$"

#include "config.h"

#if !defined(SOLARIS) && !defined(LINUX)
#include <a.out.h>
#if defined(sun) || defined(sparc)
#include <sys/mman.h>		/* for mprotect() */
#include <malloc.h>		/* for valloc() */
#endif /* defined(sun) || defined(sparc) */
#endif /* !(SOLARIS) && !(LINUX) */
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <ar.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#ifdef HAVE_VFORK
#include <vfork.h>
#endif /* HAVE_VFORK */
#include <assert.h>

#include "porting.h"

#include "intl_support.h"
#include "dynamic_load.h"
#include "error_manager.h"
#include "memory_alloc.h"
#include "environment_variable.h"
#include "system_parameter.h"
#include "util_func.h"

#if defined(_AIX)
#define N_BADMAG(x) \
        ((x).magic != 0x107 && (x).magic != 0x108 && (x).magic != 0x10b)
#elif defined(HPUX)
#define N_BADMAG(x) \
        ((x).a_magic != RELOC_MAGIC &&  /* relocatable only */ \
         (x).a_magic != EXEC_MAGIC  &&	/* normal executable */ \
         (x).a_magic != SHARE_MAGIC &&	/* shared executable */ \
         (x).a_magic != DL_MAGIC)	/* dynamic load library */
#endif /* _AIX */

#define FNAME_TBL_SIZE 127

#define DEBUG
#ifdef DEBUG
#define TEMPNAM(d,p)  dl_get_temporary_name((d), (p), __LINE__)
#define OPEN(fn, m)   dl_open_object_file((fn), (m), __LINE__)
#define CLOSE(fd)     dl_close_object_file((fd), __LINE__)
#define PIPE(fd)      dl_open_pipe((fd), __LINE__)
#endif /* DEBUG */

#ifndef TEMPNAM
#define TEMPNAM tempnam
#endif /* TEMPNAM */
#ifndef OPEN
#define OPEN    open
#endif /* OPEN */
#ifndef CLOSE
#define CLOSE   close
#endif /* CLOSE */
#ifndef PIPE
#define PIPE    pipe
#endif /* PIPE */

#ifndef FORK
#ifdef HAVE_VFORK
#define FORK vfork
#else /* HAVE_VFORK */
#define FORK fork
#endif /* HAVE_VFORK */
#endif /* FORK */

#define DL_SET_ERROR_WITH_CODE(code) \
    er_set(ER_ERROR_SEVERITY, \
           ARG_FILE_LINE, \
           dl_Errno = (code), \
           0)

#define DL_SET_ERROR_WITH_CODE_ONE_ARG(code, arg) \
    er_set(ER_ERROR_SEVERITY, \
           ARG_FILE_LINE, \
           dl_Errno = (code), \
           1, \
           (arg))

#define DL_SET_ERROR_SYSTEM_MSG() \
    er_set(ER_ERROR_SEVERITY, \
           ARG_FILE_LINE, \
           dl_Errno = ER_DL_ESYS, \
           1, \
           dl_get_system_error_message(errno))

#define DL_SET_ERROR_SYSTEM_FILENAME(fn) \
    er_set(ER_ERROR_SEVERITY, \
           ARG_FILE_LINE, \
           dl_Errno = ER_DL_EFILE, \
           2, \
           (fn), \
           dl_get_system_error_message(errno))


typedef void (*FUNCTION_POINTER_TYPE) (void);
typedef struct tbl_link TBL_LINK;
typedef struct file_entry FILE_ENTRY;
typedef struct dynamic_loader DYNAMIC_LOADER;

struct tbl_link
{
  char *filename;
  const TBL_LINK *link;
};

struct file_entry
{
  char *filename;
  bool valid;
  bool archive;
  bool duplicate;
  size_t size;
};

struct dynamic_loader
{
  bool virgin;			/* true if the loader has ever been used */
  struct
  {
    FILE_ENTRY *entries;	/* descriptors for each file to be loaded */
    int num;			/* size of candidates */
  } candidates;
  int num_need_load;		/* files that haven't already been loaded */
  TBL_LINK *loaded[FNAME_TBL_SIZE];	/* names of loaded object files. */
    pid_t (*fork_function_p) ();	/* forking function to use (fork or vfork) */
  struct
  {
    int daemon_fd;		/* The file descriptor of the pipe used to
				   communicate with the daemon.
				   -1 for unspawned daemon,
				   -2 for broken daemon,
				   greater than 0 for operating daemon. */

    char daemon_name[PATH_MAX];	/* The name of the process to be exec'ed
				   as the daemon to scavenge tmpfiles. */
  } daemon;

#if defined (SOLARIS) || defined(HPUX) || defined(LINUX)
  struct
  {
    void **handles;		/* An array of handles to the shared objects */
    int top;			/* the next handle to be allocated */
    int num;			/* the total number of handles in the array */
  } handler;
#endif				/* SOLARIS || HPUX || LINUX */

  struct
  {
    const char *cmd;		/* The pathname of the ld command to be
				   used to link the object files. */

    caddr_t *ptr;		/* pointers to chunks VALLOC'ed to hold the text
				   and data loaded int num; */
    int num;
  } loader;

  const char *image_file;	/* The name of the file that holds the up-to-date symbol table info */

#if defined(_AIX)
  char *orig_image_file;
#endif				/* AIX */
};

int dl_Errno = NO_ERROR;

/* There should only be one loader extant at any one time; */
static DYNAMIC_LOADER *dl_Loader = NULL;

/* set DL_DEBUG for debugging actions. */
static int dl_Debug = 0;

#if (defined(sun) || defined(sparc))&& !defined(SOLARIS)
static const char *dl_Default_libs[] = {
  "-lm",
  "-lc",
  NULL
};
#endif /* ((sun) || (sparc))&& !(SOLARIS) */

static const int MAX_UNLINK_RETRY = 15;

static const int DAEMON_NOT_SPAWNED = -1;
static const int DAEMON_NOT_AVAILABLE = -2;

static const char DAEMON_NAME[] = "dl_daemon";

/* number of handles to allocate per extent */
static const int HANDLES_PER_EXTENT = 10;

#if defined(sun) || defined(sparc)
static const char DEFAULT_LD_NAME[] = "/bin/ld";
#elif defined(_AIX)
static const char DEFAULT_LD_NAME[] = "/bin/cc";
#elif defined(HPUX)
/* this space intentionally blank */
#elif defined(SOLARIS)
/* this space intentionally blank */
#elif defined(LINUX)
/* this space intentionally blank */
#endif /* (sun) || (sparc) */

#ifdef DEBUG
static int dl_open_object_file (const char *filename, int mode, int lineno);
static int dl_close_object_file (int fd, int lineno);
#endif /* DEBUG */

static void dl_destroy_candidates (DYNAMIC_LOADER *);
static int dl_validate_file_entry (FILE_ENTRY *, const char *);
static int dl_initiate_dynamic_loader (DYNAMIC_LOADER *, const char *);
static void dl_destroy_dynamic_loader (DYNAMIC_LOADER *);
static int dl_validate_candidates (DYNAMIC_LOADER *, const char **);
static void dl_find_daemon (DYNAMIC_LOADER *);
static void dl_record_files (DYNAMIC_LOADER *);
static int dl_is_valid_image_file (DYNAMIC_LOADER *);
static int dl_resolve_symbol (DYNAMIC_LOADER *, struct nlist *);

#if !defined (SOLARIS) && !defined(HPUX) && !defined(LINUX)
static void dl_notify_daemon (DYNAMIC_LOADER *);
static void dl_spawn_daemon (DYNAMIC_LOADER *);
static void dl_set_pipe_handler (void);
static void dl_set_new_image_file (DYNAMIC_LOADER *, const char *);
static int dl_set_new_load_points (DYNAMIC_LOADER * this_, const caddr_t);
static int dl_load_object_image (caddr_t, const char *);
static const char **dl_parse_extra_options (int *num_options,
					    char *option_string);
static void dl_decipher_waitval (int waitval);
#ifdef DEBUG
static char *dl_get_temporary_name (const char *dir, const char *prefix,
				    int lineno);
static int dl_open_pipe (int *fd, int lineno);
#endif /* DEBUG */
#endif /* !(SOLARIS) && !(HPUX) && !(LINUX) */

#if (defined(sun) || defined(sparc)) && !defined(SOLARIS)
static size_t dl_get_image_file_size (const char *);
static int dl_link_file (DYNAMIC_LOADER *, const char *, caddr_t,
			 const char **);
static int dl_load_objects (DYNAMIC_LOADER *, size_t *, const char **,
			    const char **, const size_t,
			    enum dl_estimate_mode);
#elif defined(_AIX)
static int dl_link_file (DYNAMIC_LOADER *, const char *, struct nlist *,
			 const char **);
static int dl_load_and_resolve_objects (DYNAMIC_LOADER * const char **,
					const char **, struct nlist *);
#elif defined(HPUX) || defined(SOLARIS) || defined(LINUX)
static int dl_load_objects (DYNAMIC_LOADER *, const char **);
#else /* ((sun) || (sparc)) && !(SOLARIS) */
#error "Unknown machine type."
#endif /* ((sun) || (sparc)) && !(SOLARIS) */

static const char *
dl_get_system_error_message (int n)
{
  return (strerror (n));
}

#ifdef DEBUG
#if ((defined(sun) || defined(sparc))&& !defined(SOLARIS)) || defined(_AIX)
static char *
dl_get_temporary_name (const char *dir, const char *prefix, int lineno)
{
  char *result;

  result = tempnam (dir, prefix);
  if (dl_Debug)
    {
      fprintf (stderr, "%s[%d]: tempnam(", __FILE__, lineno);
      if (dir)
	{
	  fprintf (stderr, "\"%s\", ", dir);
	}
      else
	{
	  fputs ("NULL, ", stderr);
	}
      if (prefix)
	{
	  fprintf (stderr, "\"%s\") => ", prefix);
	}
      else
	{
	  fputs ("NULL) => ", stderr);
	}
      fprintf (stderr, "\"%s\" (0x%p)\n", result, result);
      fflush (stderr);
    }
  return result;
}
#endif /* (((sun) || (sparc))&& !(SOLARIS)) || (_AIX) */

static int
dl_open_object_file (const char *filename, int mode, int lineno)
{
  int fd;

  if (dl_Debug)
    {
      fprintf (stderr, "%s[%d]: attempting to open %s...",
	       __FILE__, lineno, filename);
    }

  fd = open (filename, mode, 0111);
  if (dl_Debug)
    {
      if (fd == -1)
	{
	  fputs ("failed\n", stderr);
	}
      else
	{
	  fprintf (stderr, "succeeded on fd %d\n", fd);
	}
    }
  return fd;
}


static int
dl_close_object_file (int fd, int lineno)
{
  if (dl_Debug)
    {
      fprintf (stderr, "%s[%d]: closing fd %d\n", __FILE__, lineno, fd);
    }
  return close (fd);
}
#endif /* DEBUG */


#if !defined (SOLARIS) && !defined(HPUX) && !defined(LINUX)
/*
 * dl_decipher_waitval() - decipher exit status
 *   return: none
 *   waitval(in): exit status
 */
static void
dl_decipher_waitval (int waitval)
{
#ifdef __GNUG__
  /* Because of some idiocy in the GNU header files, we have to
     recreate some stupid overriding #define before WIFEXITED and
     friends will work.  Who thinks of this stuff?  */
#define wait WaitStatus
#endif /* __GNUG__ */

  if (WIFEXITED (waitval))
    {
      if (WEXITSTATUS (waitval))
	{
	  DL_SET_ERROR_WITH_CODE_ONE_ARG (ER_DL_LDEXIT,
					  WEXITSTATUS (waitval));
	}
      else
	{
	  dl_Errno = NO_ERROR;
	}
    }
  else if (WIFSIGNALED (waitval))
    {
      /*
       * Child terminated by signal.
       */
      DL_SET_ERROR_WITH_CODE_ONE_ARG (ER_DL_LDTERM, WTERMSIG (waitval));
    }
  else
    {
      if (dl_Debug)
	{
	  fprintf (stderr, "dynamic loader: unknown wait status = 0x%x\n",
		   waitval);
	}
      DL_SET_ERROR_WITH_CODE_ONE_ARG (ER_DL_LDWAIT, waitval);
    }

#ifdef __GNUG__
#undef wait
#endif /* __GNUG__ */
}
#endif /* !SOLARIS && !HPUX && !LINUX */

/*
 * dl_destroy_candidates()
 *   return: none
 *   this_(in): DYNAMIC_LOADER structure pointer
 *
 * Note: If it has been put in the FILE_ENTRY table, we leave the responsibility
 *       of freeing the string to the table.
 */
static void
dl_destroy_candidates (DYNAMIC_LOADER * this_)
{
  int i;

  assert (this_ != NULL);

  for (i = 0; i < this_->candidates.num; ++i)
    {
      FILE_ENTRY *fe = &this_->candidates.entries[i];

      if (fe->filename)
	{
	  if (!fe->valid || fe->duplicate || fe->archive)
	    {
	      free_and_init (fe->filename);
	    }
	}

    }

  if (this_->candidates.entries)
    {
      free_and_init (this_->candidates.entries);
    }

  this_->candidates.entries = NULL;
  this_->candidates.num = 0;
  this_->num_need_load = 0;
}

/*
 * dl_validate_file_entry() - Make sure that the file is usable
 *   return: Zero on valid, non-zero on invalid
 *   this_(out): save validation result
 *   filename(in): file name to validate
 */
static int
dl_validate_file_entry (FILE_ENTRY * this_, const char *filename)
{
  static char pathbuf[PATH_MAX];
#if (defined(sun) || defined(sparc)) && !defined(SOLARIS)
#define AR_MAGIC_STR		ARMAG
#define AR_MAGIC_STR_SIZ	SARMAG
  char ar_magic[AR_MAGIC_STRING_SIZ];
  struct exec hdr;
#elif defined(_AIX)
#define AR_MAGIC_STR		AIAMAG
#define AR_MAGIC_STR_SIZ	SAIAMAG
  char ar_magic[AR_MAGIC_STRING_SIZ];
  struct aouthdr hdr;
#endif /* ((sun) || (sparc)) && !(SOLARIS) */
  int fd;

  assert (this_ != NULL);

  dl_Errno = NO_ERROR;

  fd = OPEN (filename, O_RDONLY);
  if (fd == -1)
    {
      DL_SET_ERROR_SYSTEM_FILENAME (filename);
    }
  else
    {
      if (realpath ((char *) filename, pathbuf) == NULL)
	{
	  DL_SET_ERROR_WITH_CODE_ONE_ARG (ER_DL_PATH, filename);
	}
      else if ((this_->filename = strdup ((char *) filename)) == NULL)
	{
	  DL_SET_ERROR_SYSTEM_MSG ();
	}
      else
	{
#if defined (SOLARIS) || defined (HPUX) || defined(LINUX)
	  this_->archive = false;
	  this_->size = 0;	/* we rely on dlopen to catch any errors. */
#else /* SOLARIS || HPUX || LINUX */

#if defined(sun) || defined(sparc) || defined(_AIX)
#if defined(_AIX)
	  if (lseek (fd, sizeof (struct filehdr), SEEK_SET) < 0)
	    {			/* See <xcoff.h> for the layout of the file. */
	      DL_SET_ERROR_SYSTEM_MSG ();
	    }
	  else
#endif /* _AIX */
	  if (read (fd, (char *) &hdr, sizeof (hdr)) == sizeof (hdr)
		&& !N_BADMAG (hdr))
	    {
	      this_->archive = false;
#if defined(sun) || defined(sparc)
	      this_->size = hdr.a_text + hdr.a_data + hdr.a_bss;
#endif /* sun || sparc */
	    }
	  else
	    {
	      (void) lseek (fd, 0, 0);
	      if (read (fd, ar_magic, AR_MAGIC_STR_SIZ) == AR_MAGIC_STR_SIZ)
		{
		  if (strncmp (ar_magic, AR_MAGIC_STR, AR_MAGIC_STR_SIZ) == 0)
		    {
		      this_->archive = true;
		      this_->size = 0;
		    }
		}
	      else
		{
#if defined(_AIX) && defined(gcc)
		  this_->archive = false;
#else
		  DL_SET_ERROR_WITH_CODE_ONE_ARG (ER_DL_BADHDR,
						  this_->filename);
#endif /* _AIX && gcc */
		}
	    }
#endif /* sun || sparc || _AIX */

#endif /* SOLARIS || HPUX || LINUX || _AIX */
	}

      CLOSE (fd);
    }

  this_->valid = (dl_Errno == NO_ERROR);

  if (this_->valid)
    return NO_ERROR;

  return ER_FAILED;
}


/*
 * dl_initiate_dynamic_loader() - builds up dynamic loader
 *   return: Zero on success, non-zero on failure
 *   this_(out): information structure of dynamic loader
 *   original(in): original image file
 */
static int
dl_initiate_dynamic_loader (DYNAMIC_LOADER * this_, const char *original)
{
  int i;

  assert (this_ != NULL);

  if (dl_Debug)
    {
      fprintf (stderr, "%s[%d]: dynamic loader being built\n",
	       __FILE__, __LINE__);
    }

  this_->virgin = true;
  this_->candidates.entries = NULL;
  this_->candidates.num = 0;
  this_->num_need_load = 0;
  for (i = 0; i < FNAME_TBL_SIZE; i++)
    {
      this_->loaded[i] = NULL;
    }

  if (PRM_DL_FORK)
    {
      if (strcmp (PRM_DL_FORK, "fork") == 0)
	{
	  this_->fork_function_p = &fork;
	}
#if defined(HAVE_VFORK)
      else if (strcmp (PRM_DL_FORK, "vfork") == 0)
	{
	  this_->fork_function_p = &vfork;
	}
#endif /* HAVE_VFORK */
      else
	{
	  this_->fork_function_p = (pid_t (*)())NULL;
	}
    }
  else
    {
      this_->fork_function_p = &FORK;
    }

  this_->daemon.daemon_fd = DAEMON_NOT_SPAWNED;

#if defined (SOLARIS) || defined(HPUX) || defined(LINUX)
  this_->handler.handles =
    (void **) malloc (HANDLES_PER_EXTENT * sizeof (void *));
  if (this_->handler.handles == NULL)
    {
      DL_SET_ERROR_SYSTEM_MSG ();
      return ER_FAILED;
    }
  this_->handler.top = 0;
  this_->handler.num = HANDLES_PER_EXTENT;
  for (i = 0; i < this_->handler.num; i++)
    {
      this_->handler.handles[i] = 0;
    }
#endif /* SOLARIS || HPUX || LINUX */

  this_->loader.cmd = NULL;
  this_->loader.ptr = NULL;
  this_->loader.num = 0;

  this_->image_file = original;

#if defined(_AIX)
  this_->orig_image_file = strdup ((char *) original);
  if (this_->orig_image_file == NULL)
    {
      DL_SET_ERROR_SYSTEM_MSG ();
      return ER_FAILED;
    }

#endif /* _AIX */
#if !defined (SOLARIS) && !defined(HPUX) && !defined(LINUX)
  this_->loader.cmd = DEFAULT_LD_NAME;	/*use the default name. */
#endif /* !(SOLARIS) && !(HPUX) && !(LINUX) */

  if (dl_Errno == NO_ERROR)
    {
      dl_find_daemon (this_);
    }

  return dl_Errno;
}


static void
dl_destroy_dynamic_loader (DYNAMIC_LOADER * this_)
{
  int i, tbl_size;

  assert (this_ != NULL);

#if defined (SOLARIS) || defined(HPUX) || defined(LINUX)
  for (i = 0; i < this_->handler.top; i++)
    {
#if defined(HPUX)
      shl_unload (this_->handler.handles[i]);
#else /* HPUX */
      (void) dlclose (this_->handler.handles[i]);
#endif /* HPUX */
    }
  free_and_init (this_->handler.handles);
  this_->handler.top = this_->handler.num = 0;
#endif /* SOLARIS || HPUX || LINUX */

  for (i = 0, tbl_size = FNAME_TBL_SIZE; i < tbl_size; i++)
    {
      TBL_LINK *tp, *next_tp;
      for (tp = this_->loaded[i]; tp; tp = next_tp)
	{
	  next_tp = (TBL_LINK *) tp->link;
	  free_and_init (tp->filename);
	  free_and_init (tp);
	}
    }

  if (this_->loader.ptr)
    {
      for (i = 0, tbl_size = this_->loader.num; i < tbl_size; ++i)
	{
#if (defined(sun) || defined(sparc) ) && !defined(SOLARIS)
	  free (this_->loader.ptr[i]);	/* Use free because the storage was
					   allocated by VALLOC */
#elif defined(_AIX)
	  unload ((FUNCTION_POINTER_TYPE) this->loader.ptr[i]);
#endif /* ((sun) || (sparc)) && !(SOLARIS) */
	}
      free_and_init (this_->loader.ptr);
    }

#if !defined (SOLARIS) && !defined(HPUX) && !defined(LINUX)
  if (!this_->virgin)
    {
      (void) unlink (this_->image_file);
    }

  free ((char *) this_->image_file);	/* Don't use free_and_init(),
					   image_file came from exec_path */

#if defined(AIX)
  free_and_init (this->orig_image_file);
#endif /* AIX */
#endif /* !SOLARIS && !HPUX && !LINUX */

  if (this_->daemon.daemon_fd >= 0)
    {
      CLOSE (this_->daemon.daemon_fd);
    }

  if (dl_Debug)
    {
      fprintf (stderr, "%s[%d]: dynamic loader torn down\n",
	       __FILE__, __LINE__);
    }
}


/*
 * dl_validate_candidates() - initiate and validate file entry
 *   return: Zero on success, non-zero on failure
 *   filenames(in): file names of candidates
 *   this_(in): DYNAMIC_LOADER structure pointer
 */
static int
dl_validate_candidates (DYNAMIC_LOADER * this_, const char **filenames)
{
  int i;
  const char **fname;

  assert (filenames != NULL);

  this_->num_need_load = 0;

  for (fname = filenames, this_->candidates.num = 0; *fname; ++fname)
    {
      /* how many candidates we're considering. */
      ++this_->candidates.num;
    }

  if (dl_Debug)
    {
      fprintf (stderr, "Number of candidates = %d\n", this_->candidates.num);
    }

  if (this_->candidates.num == 0)
    {
      return NO_ERROR;
    }

  this_->candidates.entries =
    (FILE_ENTRY *) malloc (this_->candidates.num * sizeof (FILE_ENTRY));
  if (this_->candidates.entries == NULL)
    {
      DL_SET_ERROR_SYSTEM_MSG ();
      goto cleanup;
    }

  for (i = 0; i < this_->candidates.num; i++)
    {
      memset (&this_->candidates.entries[i], 0, sizeof (FILE_ENTRY));
    }

  for (i = 0; i < this_->candidates.num; ++i)
    {
      if (dl_validate_file_entry
	  (&this_->candidates.entries[i], filenames[i]))
	{
	  /* Perform individual validation on each.  Give up if any fail. */
	  if (dl_Debug)
	    {
	      fprintf (stderr, "dl_validate_file_entry failed on %s\n",
		       filenames[i]);
	    }

	  goto cleanup;
	}
    }

  for (i = 0; i < this_->candidates.num; ++i)
    {
      if (!this_->candidates.entries[i].archive)
	{
	  const char *path;
	  const TBL_LINK *tbl_p;
	  int j;

	  path = this_->candidates.entries[i].filename;
	  tbl_p = this_->loaded[hashpjw (path) % FNAME_TBL_SIZE];
	  for (; tbl_p; tbl_p = tbl_p->link)
	    {
	      /* look in the table of already-loaded files */
	      if (strcmp (tbl_p->filename, path) == 0)
		{
		  this_->candidates.entries[i].duplicate = true;
		  if (dl_Debug)
		    {
		      fprintf (stderr, "Duplicate: %s\n", path);
		    }
		  continue;
		}
	    }

	  for (j = 0; j < i; ++j)
	    {
	      /* make sure that it hasn't already
	         appeared in the current list of candidates */
	      if (strcmp (this_->candidates.entries[j].filename, path) == 0)
		{
		  this_->candidates.entries[i].duplicate = true;
		  if (dl_Debug)
		    {
		      fprintf (stderr, "Duplicate: %s\n", path);
		    }
		  continue;
		}
	    }

	  /* we record the fact that we need to load it. */
	  ++this_->num_need_load;
	}
    }

cleanup:

  if (dl_Debug)
    {
      fprintf (stderr, "Number needed to load (nneed) = %d\n",
	       this_->num_need_load);
    }

  if (dl_Errno == NO_ERROR)
    return NO_ERROR;

  return ER_FAILED;
}


#if (defined(sun) || defined(sparc) ) && !defined(SOLARIS)
/*
 * dl_get_image_file_size() - get static size requirements
 *   return: Returns -1 if anything goes wrong.
 *   filename(in): image file name
 */
static size_t
dl_get_image_file_size (const char *filename)
{
  struct exec hdr;
  int fd;
  size_t size = (size_t) - 1;

  assert (filename != NULL);

  fd = OPEN (filename, O_RDONLY);
  if (fd == -1)
    {
      DL_SET_ERROR_SYSTEM_FILENAME (filename);
    }
  else
    {
      if (read (fd, (char *) &hdr, sizeof (hdr)) != sizeof (hdr)
	  || N_BADMAG (hdr))
	{
	  DL_SET_ERROR_WITH_CODE_ONE_ARG (ER_DL_BADHDR, filename);
	}
      else
	{
	  size = hdr.a_text + hdr.a_data + hdr.a_bss;
	}

      if (dl_Debug)
	{
	  fprintf (stderr, "%s[%d]: closing fd %d\n", __FILE__, __LINE__, fd);
	}

      CLOSE (fd);
    }

  return size;
}
#endif /* ((sun) || (sparc)) && !(SOLARIS) */


/*
 * dl_find_daemon() - Discover the pathname for the daemon process.
 *   return: none
 *
 * Note: DL_DAEMON env-variable is used.
 * If we can't find the daemon, we simply set daemon_fd to DAEMON_NOT_AVAILABLE.
 */
static void
dl_find_daemon (DYNAMIC_LOADER * this_)
{
  char *path;

  assert (this_ != NULL);

  path = this_->daemon.daemon_name;
  envvar_bindir_file (path, PATH_MAX, DAEMON_NAME);

  if (path && path[0] != '\0')
    {
      if (access (path, X_OK) == 0)
	{
	  return;
	}
    }

  this_->daemon.daemon_name[0] = '\0';
  this_->daemon.daemon_fd = DAEMON_NOT_AVAILABLE;

  if (dl_Debug)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DL_DAEMON_MISSING, 0);
    }
}

#if !defined (SOLARIS) && !defined(HPUX) && !defined(LINUX)
/*
 * dl_parse_extra_options() - Break up a string of options into a vector
 *   return: options vector
 *      returns a NULL pointer and sets *noptions to -1 if a problem arises
 *   num_options(out): number of options
 *   option_string(in): string of options
 */
static const char **
dl_parse_extra_options (int *num_options, char *option_string)
{
  const char **option_vec = NULL;
  const char *option = NULL;
  int i = 0, opt_cnt = 0;

  for (option = (const char *) strtok (option_string, " \t");
       option; option = (const char *) strtok ((char *) NULL, " \t"))
    {
      const char **new_vec = NULL;

      opt_cnt += sizeof (const char *);
      if (option_vec)
	{
	  new_vec = realloc (option_vec, opt_cnt);
	}
      else
	{
	  new_vec = malloc (opt_cnt);
	}

      if (new_vec == NULL)
	{
	  DL_SET_ERROR_SYSTEM_MSG ();
	  if (option_vec)
	    {
	      free_and_init (option_vec);
	    }
	  *num_options = -1;
	  return NULL;
	}

      option_vec = new_vec;
      option_vec[i++] = option;
    }

  *num_options = i;
  return option_vec;
}
#endif /* !SOLARIS && !HPUX && !LINUX */

#if !defined (SOLARIS) && !defined(HPUX) && !defined(LINUX)
/*
 * dl_link_file() - Builds up a "command line" which is then forked via execv()
 *   return: Zero on success, non-zero on failure
 *   this_(in): DYNAMIC_LOADER structure pointer
 *   tmp_file(in): temporary file name to be passed to ld
 *   load_point(in): load point to be passed to ld
 *   syms(in): symbols to be used as Export and Import lists(AIX)
 *   libs(in): array of arguments to be passed to ld
 * Note : You can pass extra parameters to ld with DL_LD_OPTIONS env-variable
 */
static int
#if defined(sun) || defined(sparc)
  static int
dl_link_file (DYNAMIC_LOADER * this_, const char *tmp_file,
	      caddr_t load_point, const char **libs)
#elif defined(_AIX)
  static int
dl_link_file (DYNAMIC_LOADER * this_, const char *tmp_file,
	      struct nlist *syms, const char **libs)
#endif				/* (sun) || (sparc) */
{
  int num_libs = 0;
  const char **p;
  char *extra_options;
  const char **option_vec = (const char **) NULL;
  int num_options = 0;
  int argc = 0;
  const char **argv, **argvp;
  char load_point_buf[16];
  int i;
  pid_t pid = 0;

#if defined(_AIX)
  int num_loaded;
  const char *export_list_filename = (const char *) NULL;
  FILE *export_list_file = (FILE *) NULL;
  const char *load_main_tmp = (const char *) NULL;
  char *load_main_filename = (char *) NULL;
  FILE *load_main_file = (FILE *) NULL;
  char *import_list_filename = (char *) NULL;
  FILE *import_list_file = (FILE *) NULL;
  char *template_filename = (char *) NULL;
  FILE *template_file = (FILE *) NULL;
  char *buf = (char *) NULL;
  int rc;

  buf = (char *) malloc (PATH_MAX);
  if (buf == NULL)
    {
      DL_SET_ERROR_SYSTEM_MSG ();
      goto end_link_file;
    }

  export_list_filename = TEMPNAM (NULL, "exprt");
  if (export_list_filename == NULL)
    {
      DL_SET_ERROR_SYSTEM_MSG ();
      goto end_link_file;
    }

  export_list_file = fopen (export_list_filename, "w");
  if (export_list_file == (FILE *) NULL)
    {
      DL_SET_ERROR_SYSTEM_MSG ();
      goto end_link_file;
    }
  (void) sprintf (buf, "#!\n");	/* Make the export list file. The format is
				   a first line with #!<filename> and subsequent
				   lines with a function name per line. */
  if (fwrite (buf, 1, strlen (buf), export_list_file) == 0)
    {
      DL_SET_ERROR_SYSTEM_MSG ();
      goto end_link_file;
    }

  for (i = 0; syms[i].n_name; ++i)
    {
      (void) sprintf (buf, "%s\n", syms[i].n_name);
      if (fwrite (buf, 1, strlen (buf), export_list_file) == 0)
	{
	  DL_SET_ERROR_SYSTEM_MSG ();
	  goto end_link_file;
	}
    }

  if (export_list_file)
    {
      fclose (export_list_file);
    }

  load_main_tmp = TEMPNAM (NULL, "lmain");
  if (load_main_tmp == NULL)
    {
      DL_SET_ERROR_SYSTEM_MSG ();
      goto end_link_file;
    }
  load_main_filename = (char *) malloc (PATH_MAX);
  if (load_main_filename == NULL)
    {
      DL_SET_ERROR_SYSTEM_MSG ();
      goto end_link_file;
    }
  (void) strcpy (load_main_filename, load_main_tmp);
  free ((void *) load_main_tmp);	/* Don't use free_and_init(); from tempnam(). */
  (void) strcat (load_main_filename, ".c");
  load_main_file = fopen (load_main_filename, "w");
  if (load_main_file == NULL)
    {
      DL_SET_ERROR_SYSTEM_MSG ();
      goto end_link_file;
    }
  if (fwrite ("main(){}", 1, strlen ("main(){}"), load_main_file) == 0)
    {
      /* source file for the dummy main function. */
      DL_SET_ERROR_SYSTEM_MSG ();
      goto end_link_file;
    }
  if (load_main_file)
    fclose (load_main_file);

  template_filename = (char *) malloc (PATH_MAX);
  if (template_filename == NULL)
    {
      DL_SET_ERROR_SYSTEM_MSG ();
      goto end_link_file;
    }
  sprintf (template_filename, "%s/admin/symbols.exp", envvar_root ());
  template_file = fopen (template_filename, "r");
  if (template_file == (FILE *) NULL)
    {
      DL_SET_ERROR_SYSTEM_MSG ();
      goto end_link_file;
    }
  import_list_filename = TEMPNAM (NULL, "imprt");	/* import list file. */
  if (import_list_filename == NULL)
    {
      DL_SET_ERROR_SYSTEM_MSG ();
      goto end_link_file;
    }
  import_list_file = fopen (import_list_filename, "w");
  if (import_list_file == (FILE *) NULL)
    {
      DL_SET_ERROR_SYSTEM_MSG ();
      goto end_link_file;
    }
  if (fscanf (template_file, "%s\n", buf) <= 0)
    {
      DL_SET_ERROR_SYSTEM_MSG ();
      goto end_link_file;
    }

  /* Copy the template file admin/symbols.exp and modify the first
     line to have the name of this executing program. */
  if ((this_->orig_image_file[0] == '/')
      || (this_->orig_image_file[0] == '.'))
    {
      rc = fprintf (import_list_file, "#!%s\n", this_->orig_image_file);
    }
  else
    {
      rc = fprintf (import_list_file, "#!%s/bin/%s\n", envvar_root (),
		    this_->orig_image_file);
    }

  if (rc <= 0)
    {
      DL_SET_ERROR_SYSTEM_MSG ();
      goto end_link_file;
    }
  while ((rc = fscanf (template_file, "%s\n", buf)) != EOF)
    {
      if (rc <= 0)
	{
	  DL_SET_ERROR_SYSTEM_MSG ();
	  goto end_link_file;
	}
      if (fprintf (import_list_file, "%s\n", buf) <= 0)
	{
	  DL_SET_ERROR_SYSTEM_MSG ();
	  goto end_link_file;
	}
    }
  if (import_list_file)
    {
      fclose (import_list_file);
    }
  if (template_file)
    {
      fclose (template_file);
    }
#endif /* _AIX */

  extra_options = (char *) getenv ("DL_LD_OPTIONS");	/* to be passed to ld */
  if (extra_options)
    {
      extra_options = strdup (extra_options);
      if (extra_options == NULL)
	{
	  DL_SET_ERROR_SYSTEM_MSG ();
	}
      option_vec = dl_parse_extra_options (&num_options, extra_options);
    }

  for (p = libs; *p; p++)
    {
      num_libs++;		/* count libray parameters */
    }

#if (defined(sun) || defined(sparc)) && !defined(SOLARIS)
  /* ld -N -A <image_file> -T <load_point> -o <tmp_file>
     <...obj_files...> <...libs...> */
  argc = 8 + this_->candidates.num + num_libs + num_options;
#elif defined(_AIX)
  /* ld -o <tmp_file> -e main <main source> -bI:<import list file>
     -bE:<export list file> <already loaded files>
     <candidates> <options> <libs> */
  num_loaded = 0;
  for (i = 0; i < FNAME_TBL_SIZE; ++i)
    {
      if (this_->loaded[i])
	{
	  ++num_loaded;
	}
    }
  argc = 8 + this_->candidates.num + num_loaded + num_libs + num_options;
#endif /* ((sun) || (sparc)) && !(SOLARIS) */

  argv = malloc (sizeof (const char *) * (argc + 1));
  if (argv == NULL)
    {
      DL_SET_ERROR_SYSTEM_MSG ();
      goto end_link_file;
    }
  argvp = argv;

  *argvp++ = this_->loader.cmd;
#if (defined(sun) || defined(sparc)) && !defined(SOLARIS)
  *argvp++ = "-N";
  *argvp++ = "-A";
  *argvp++ = this_->image_file;
  *argvp++ = "-T";
  /* TODO : consider LP64 */
  (void) sprintf (load_point_buf, "%p", load_point);
  *argvp++ = load_point_buf;
  *argvp++ = "-o";
  *argvp++ = tmp_file;
#elif defined(_AIX)
  *argvp++ = "-o";
  *argvp++ = tmp_file;
  *argvp++ = "-G";
  *argvp++ = "-bnoentry";
  *argvp++ = "-bexpall";
  *argvp++ = "-lc";
  for (i = 0; i < FNAME_TBL_SIZE; ++i)
    {
      if (this_->loaded[i])	/* need to build the new loadable executable. */
	*argvp++ = this_->loaded[i]->filename;
    }
#endif /* ((sun) || (sparc)) && !(SOLARIS) */
  for (i = 0; i < this_->candidates.num; ++i)
    {
      if (this_->candidates.entries[i].valid
	  && !this_->candidates.entries[i].duplicate)
	{
	  *argvp++ = this_->candidates.entries[i].filename;
	}
    }
  for (i = 0; i < num_options; ++i)
    {
      *argvp++ = option_vec[i];
    }
  for (p = libs; *p;)
    {
      *argvp++ = *p++;
    }
  *argvp++ = NULL;

  if (dl_Debug)
    {
      const char **p;
      fprintf (stderr, "%s[%d]: ", __FILE__, __LINE__);
      for (p = argv; *p; ++p)
	{
	  fputs (*p, stderr);
	  fputc (' ', stderr);
	}
      fputc ('\n', stderr);
      fflush (stderr);
    }

  pid = (pid_t) (*this_->fork_function_p) ();
  if (pid)
    {
      /* This is the parent process, pid is the child */
      int waitval;
      if (pid == -1 || waitpid (pid, &waitval, 0) == -1)
	{
	  DL_SET_ERROR_SYSTEM_MSG ();
	}
      else
	{
	  dl_decipher_waitval (waitval);
	}
    }
  else
    {
      /* This is the child process - do the exec */
      execv ((char *) argv[0], (char **) &argv[0]);
      DL_SET_ERROR_SYSTEM_MSG ();
      _exit (1);
    }

end_link_file:
  if (extra_options)
    free_and_init (extra_options);
  if (option_vec)
    free_and_init (option_vec);
  free_and_init (argv);
#if defined(_AIX)
  free_and_init (buf);
  if (export_list_filename)
    unlink (export_list_filename);
  if (load_main_filename)
    unlink (load_main_filename);
  if (import_list_filename)
    unlink (import_list_filename);
  /* Don't use free_and_init; from tempnam(). */
  free ((void *) *(&export_list_filename));
  free ((void *) import_list_filename);
  if (export_list_file)
    fclose (export_list_file);
  if (load_main_file)
    fclose (load_main_file);
  if (import_list_file)
    fclose (import_list_file);
  free_and_init (load_main_filename);
  free_and_init (template_filename);
  if (template_file)
    fclose (template_file);
#endif /* _AIX */

  return dl_Errno;
}
#endif /* !SOLARIS && !HPUX && && !LINUX */


#if (defined(sun) || defined(sparc)) && !defined(SOLARIS)
/*
 * dl_load_object_image()
 *   return: Zero on success, non-zero on failure
 *   load_point(out): buffer to receive object image
 *   image_file(in): image file name
 */
static int
dl_load_object_image (caddr_t load_point, const char *image_file)
{
  struct exec hdr;
  size_t size = 0;

  int fd = OPEN (image_file, O_RDONLY);

  dl_Errno = NO_ERROR;

  if (fd == -1)
    {
      DL_SET_ERROR_SYSTEM_FILENAME (image_file);
      goto cleanup;
    }

  if (read (fd, (char *) &hdr, sizeof (hdr)) != sizeof (hdr))
    {
      DL_SET_ERROR_SYSTEM_FILENAME (image_file);
      goto cleanup;
    }

  if (N_BADMAG (hdr))
    {
      DL_SET_ERROR_WITH_CODE_ONE_ARG (ER_DL_BADHDR, image_file);
      goto cleanup;
    }

  if (lseek (fd, N_TXTOFF (hdr), 0) == -1)
    {
      DL_SET_ERROR_SYSTEM_FILENAME (image_file);
      goto cleanup;
    }

  size = hdr.a_text + hdr.a_data;
  if (read (fd, load_point, size) != size)
    {
      DL_SET_ERROR_SYSTEM_FILENAME (image_file);
      goto cleanup;
    }

cleanup:

  if (fd != -1)
    {
      CLOSE (fd);
    }

  if (dl_Errno == NO_ERROR)
    return NO_ERROR;

  return ER_FAILED;
}
#endif /* ((sun) || (sparc)) && !(SOLARIS) */


#if ((defined(sun) || defined(sparc)) && !defined(SOLARIS)) || defined(_AIX)
#if defined(DEBUG)
static int
dl_open_pipe (int *fd, int lineno)
{
  int result = pipe (fd);
  if (dl_Debug)
    {
      if (result == -1)
	{
	  fprintf (stderr, "%s[%d]: pipe creation failed\n",
		   __FILE__, lineno);
	}
      else
	{
	  fprintf (stderr, "%s[%d]: pipe created on fds %d and %d\n",
		   __FILE__, lineno, fd[0], fd[1]);
	}
    }
  return result;
}
#endif /* DEBUG */

static void
dl_set_pipe_handler (void)
{
  void (*old_handler) () = signal (SIGPIPE, SIG_IGN);

  if (old_handler != SIG_DFL && old_handler != SIG_IGN)
    {
      signal (SIGPIPE, old_handler);	/* restore old handler */
    }
}

/*
 * dl_spawn_daemon() - Forks a separate process that waits around to clean up
 *           any tmpfiles that we fail to close(because we abort, for example)
 */
static void
dl_spawn_daemon (DYNAMIC_LOADER * this_)
{
  int fd[2];
  int daemon_pid;

  assert (this_ != NULL);

  if (this_->daemon.daemon_fd != DAEMON_NOT_SPAWNED)
    {
      return;
    }

  if (PIPE (fd) == -1)
    {				/* pipe creation failed */
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DL_DAEMON_MISSING, 0);
      this_->daemon.daemon_fd = DAEMON_NOT_AVAILABLE;
      return;
    }

  if (dl_Debug)
    {
      fprintf (stderr, "%s[%d]: spawning %s on fds %d and %d\n",
	       __FILE__, __LINE__, this_->daemon.daemon_name, fd[0], fd[1]);
      fflush (stderr);
    }

  daemon_pid = (*this_->fork_function_p) ();
  if (daemon_pid)
    {
      /* This is the parent process. */
      if (daemon_pid == -1)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DL_DAEMON_MISSING,
		  0);
	  CLOSE (fd[0]);
	  CLOSE (fd[1]);
	  this_->daemon.daemon_fd = DAEMON_NOT_AVAILABLE;
	  return;
	}
      dl_set_pipe_handler ();
      CLOSE (fd[0]);
      this_->daemon.daemon_fd = fd[1];	/* write side of the pipe */
    }
  else
    {
      /* This is the child process. */
      int i;
      if (dup2 (fd[0], 0) == -1)	/* read side of the pipe */
	{
	  _exit (1);
	}
      for (i = 1; i < NOFILE; ++i)
	{
	  close (i);		/* Close all file descriptors except stdin */
	}
      execl (this_->daemon.daemon_name, (char *) 0);
      _exit (1);
    }
}

/*
 * dl_notify_daemon() - Let the daemon know that we have let loose of
 *                      the current tmpfile and are now using a new one.
 */
static void
dl_notify_daemon (DYNAMIC_LOADER * this_)
{
  int num_chars;

  assert (this_ != NULL);

  num_chars = strlen (this_->image_file) + 1;	/* include null */

  if (this_->daemon.daemon_fd == DAEMON_NOT_SPAWNED)
    {
      /* Start a daemon if we haven't tried already. */
      dl_spawn_daemon ();
    }

  if (this_->daemon.daemon_fd >= 0)
    {
      if (write (this_->daemon.daemon_fd, this_->image_file, num_chars)
	  != num_chars)
	{
	  if (dl_Debug)
	    {
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		      ER_DL_DAEMON_DISAPPEARED, 0);
	    }
	  this_->daemon.daemon_fd = DAEMON_NOT_AVAILABLE;
	}
    }
}

/*
 * dl_set_new_image_file()
 *   return: none
 *   this_(in): DYNAMIC_LOADER * structure pointer
 *   new_image(in): Short description of the param3
 */
static void
dl_set_new_image_file (DYNAMIC_LOADER * this_, const char *new_image)
{
  if (!this_->virgin)
    {
      (void) unlink (this_->image_file);	/* temporary old one */
    }

  /* don't use free_and_init(), it came from exec_path() */
  free ((char *) this_->image_file);

  this_->image_file = new_image;
  this_->virgin = false;

  dl_notify_daemon ();
}

/*
 * dl_set_new_load_points() - Add new lod point
 *   return: Zero on success, non-zero on failure
 *   this_(in): DYNAMIC_LOADER structure pointer
 *   load_point(in): load point to be added
 *
 * Note: If MALLOC is failed, it is ignored
 */
static int
dl_set_new_load_points (DYNAMIC_LOADER * this_, const caddr_t load_point)
{
  caddr_t *new_load_points;

  if (this_->loader.ptr)
    {
      new_load_points = (caddr_t *) realloc (this_->loader.ptr,
					     (this_->loader.num +
					      1) * sizeof (caddr_t));
    }
  else
    {
      new_load_points = (caddr_t *) malloc (sizeof (caddr_t));
    }

  if (new_load_points)
    {
      this_->loader.ptr = new_load_points;
      this_->loader.ptr[this_->loader.num++] = load_point;
    }
  else
    {
      DL_SET_ERROR_SYSTEM_MSG ();
      return ER_FAILED;
    }
}
#endif /* ((sun || sparc) && !SOLARIS) || _AIX */

/*
 * dl_record_files() - Record the names of the files that we have loaded.
 *   this_(in): DYNAMIC_LOADER * structure pointer
 */
static void
dl_record_files (DYNAMIC_LOADER * this_)
{
  int i;

  for (i = 0; i < this_->candidates.num; ++i)
    {
      FILE_ENTRY *fe = &this_->candidates.entries[i];
      if (fe->valid && !fe->archive && !fe->duplicate)
	{
	  TBL_LINK *tl;
	  int index = hashpjw (fe->filename) % FNAME_TBL_SIZE;

	  tl = (TBL_LINK *) malloc (sizeof (TBL_LINK));
	  if (tl)
	    {
	      tl->filename = fe->filename;
	      tl->link = this_->loaded[index];
	      this_->loaded[index] = tl;
	    }
	  else
	    {
	      fe->valid = false;
	    }
	}
    }
}

static int
dl_is_valid_image_file (DYNAMIC_LOADER * this_)
{
#if defined (SOLARIS) || defined(HPUX) || defined(LINUX)
  return 1;
#else /* SOLARIS || HPUX || LINUX */
  assert (this_ != NULL);
  return this_->image_file != NULL;
#endif /* SOLARIS || HPUX || LINUX */
}


#if (defined(sun) || defined(sparc)) && !defined(SOLARIS)
/*
 * dl_load_objects() - Validate and load object files
 *   return: Zero on success, non-zero on failure
 *   this_(in): DYNAMIC_LOADER structure pointer
 *   actual(out): pointer to receive information
 *   obj_files(in): array of object file names
 *   libs(in): array of library
 *   estimate(in): fudge factor for modifying estimate of size required
 *                 to load files
 *   mode(in) : if mode == DL_RELATIVE, estimated_size is an extra value to be
 *      added to the loader's estimate;
 *      if mode == DL_ABSOLUTE, estimated_size is used as the estimate.
 */
static int
dl_load_objects (DYNAMIC_LOADER * this_, size_t * actual,
		 const char **obj_files, const char **libs,
		 const size_t estimate, enum dl_estimate_mode mode)
{
  size_t estimated_size = 0;
  size_t actual_size = 0;
  caddr_t load_point = NULL, old_load_point = NULL;
  const char *tmp_file = NULL;
  int first_time = true;
  int i;

  dl_Errno = NO_ERROR;

  if (dl_validate_candidates (this_, obj_files)
      || (this_->num_need_load == 0))
    goto cleanup;

  tmp_file = TEMPNAM (NULL, "dynld");
  if (tmp_file == NULL)
    {
      DL_SET_ERROR_SYSTEM_MSG ();
      goto cleanup;
    }

  if (mode == DL_RELATIVE || (mode == DL_ABSOLUTE && estimate == 0))
    {
      for (i = 0; i < this_->candidates.num; ++i)
	{
	  if (this_->candidates.entries[i].valid
	      && !this_->candidates.entries[i].duplicate)
	    {
	      estimated_size += this_->candidates.entries[i].size;
	    }
	}
    }
  else
    {
      estimated_size = estimate;
    }

  if (mode == DL_RELATIVE)
    {
      estimated_size += estimate;
    }

  for (first_time = true;; first_time = false)
    {
      /* allocate > link > check estimated size >
         free the space and try again. */
      load_point = VALLOC (estimated_size);
      if (load_point == NULL)
	{
	  DL_SET_ERROR_SYSTEM_MSG ();
	  goto cleanup;
	}

      if (load_point != old_load_point)
	{
	  if (dl_link_file (tmp_file,
			    load_point,
			    libs == NULL ? dl_Default_libs : libs))
	    goto cleanup;
	  actual_size = dl_get_image_file_size (tmp_file);
	  if (actual_size == -1)
	    goto cleanup;
	  if (first_time && actual)
	    {
	      if (mode == DL_RELATIVE)
		{
		  *actual = actual_size - estimated_size;
		}
	      else
		{
		  *actual = actual_size;
		}
	    }
	}

      if (estimated_size >= actual_size)
	{
	  break;
	}

      estimated_size = actual_size;	/* update the estimate and try again. */
      old_load_point = load_point;

      free (load_point);	/* don't use free_and_init, they come from VALLOC() */
    }

  if (dl_load_object_image (load_point, tmp_file))
    goto cleanup;

  if (mprotect (load_point, actual_size, PROT_READ | PROT_WRITE | PROT_EXEC) == -1)	/* Add all permissions to this new chunk of memory */
    {
      DL_SET_ERROR_SYSTEM_MSG ();
      goto cleanup;
    }

  dl_set_new_image_file (this_, tmp_file);
  tmp_file = NULL;

  if (dl_set_new_load_points (this_, load_point))
    goto cleanup;

  dl_record_files (this_);

cleanup:

  if (dl_Errno)
    {
      for (i = 0; i < this_->candidates.num; ++i)
	{
	  this_->candidates.entries[i].valid = false;
	}

      if (load_point)
	free (load_point);	/* don't use free_and_init, they come from VALLOC() */

      if (tmp_file)
	{
	  int rc;
	  int max_retry = 0;

	  while (((rc = unlink (tmp_file)) != 0)
		 && (max_retry < MAX_UNLINK_RETRY))
	    {
	      if (rc < 0)
		{
		  if (dl_Debug)
		    {
		      fprintf (stderr,
			       "%s[%d]: retrying unlink: errno = %d\n",
			       __FILE__, __LINE__, errno);
		    }
		  max_retry++;
		  (void) sleep (3);
		  continue;
		}
	    }

	  if (dl_Debug)
	    {
	      if (max_retry < MAX_UNLINK_RETRY)
		{
		  fprintf (stderr, "%s[%d]: successful unlink: %s\n",
			   __FILE__, __LINE__, tmp_file);
		}
	      else
		{
		  fprintf (stderr, "%s[%d]: hit retry max for unlink: %s\n",
			   __FILE__, __LINE__, tmp_file);
		}
	    }

	  /*
	   * Don't use free_and_init on tmp_file; it came to us via tempnam().
	   */
	  free ((char *) tmp_file);
	}
    }

  dl_destroy_candidates (this_);

  if (dl_Errno == NO_ERROR)
    return NO_ERROR;

  return ER_FAILED;
}
#elif defined(_AIX)
/*
 * dl_load_and_resolve_objects() - Validate and load object files
 *   return: Zero on success, non-zero on failure
 *   this_(in): DYNAMIC_LOADER structure pointer
 *   obj_files(in): array of object file names
 *   libs(in): array of library
 *   syms(in): symbols to be used as Export and Import lists
 */
static int
dl_load_and_resolve_objects (DYNAMIC_LOADER * this_, const char **obj_files,
			     const char **libs, struct nlist *syms)
{
  caddr_t load_point = NULL;
  const char *tmp_file = NULL;
  int i;
  struct nlist name_list[2];
  const char **lib_p;

  dl_Errno = NO_ERROR;

  if (dl_validate_candidates (this_, obj_files))
    goto cleanup;

  tmp_file = TEMPNAM (NULL, "dynld");
  if (tmp_file == NULL)
    {
      DL_SET_ERROR_SYSTEM_MSG ();
      goto cleanup;
    }

  if (libs)
    {
      lib_p = libs;
    }
  else
    {
      lib_p = dl_Default_libs;
    }

  if (dl_link_file (tmp_file, syms, lib_p))
    goto cleanup;

  /* This causes loading the module to place it in the process private area */
  chmod (tmp_file, S_IRUSR | S_IWUSR | S_IXUSR);

  if (!this_->virgin)		/* removes the currently loaded executable */
    {
      unload (this_->image_file);
    }

  load_point =
    (char *) dl_load_objects (this_, (char *) tmp_file, L_NOAUTODEFER, "");
  if (load_point == (char *) 0)
    {

      char *buffer[1024];
      buffer[0] = "execerror";
      /* name of program that failed to load */
      buffer[1] = (char *) tmp_file;
      loadquery (L_GETMESSAGES, &buffer[2], sizeof buffer - 8);
      execvp ("/usr/sbin/execerror", buffer);

      DL_SET_ERROR_SYSTEM_MSG ();
      goto cleanup;
    }

  dl_set_new_image_file (this_, tmp_file);
  tmp_file = NULL;

  if (dl_set_new_load_points (this_, load_point))
    goto cleanup;

  dl_record_files (this_);

cleanup:

  name_list[0].n_name = "main";
  name_list[1].n_name = NULL;
  nlist ((char *) this_->image_file, name_list);
  nlist ((char *) this_->image_file, syms);
  for (i = 0; syms[i].n_name; ++i)
    {
      /* On the IBM, the nlist value of a name is a number */
      if (syms[i].n_value)
	{
	  syms[i].n_value +=
	    (int) this_->loader.ptr[this_->loader.num - 1] -
	    name_list[0].n_value;
	}
    }

  if (dl_Errno)
    {
      for (i = 0; i < this_->candidates.num; ++i)
	{
	  this_->candidates.entries[i].valid = false;
	}
      if (load_point)
	{
	  unload ((FUNCTION_POINTER_TYPE) load_point);
	}
      if (tmp_file)
	{
	  int rc;
	  int max_retry = 0;

	  while (((rc = unlink (tmp_file)) != 0)
		 && (max_retry < MAX_UNLINK_RETRY))
	    {
	      if (rc < 0)
		{
		  max_retry++;
		  (void) sleep (3);
		  continue;
		}
	    }

	  free ((char *) tmp_file);	/* Don't use free_and_init(); came from tempnam() */
	}
    }

  dl_destroy_candidates (this_);

  if (dl_Errno == NO_ERROR)
    return NO_ERROR;

  return ER_FAILED;
}
#elif defined(SOLARIS) || defined(HPUX) || defined(LINUX)
/*
 * dl_load_objects() - Validate and load object files
 *   return: Zero on success, non-zero on failure
 *   this_(in): DYNAMIC_LOADER * structure pointer
 *   obj_files(in): array of object file names
 */
static int
dl_load_objects (DYNAMIC_LOADER * this_, const char **obj_files)
{
  int i;

  dl_Errno = NO_ERROR;

  if (dl_validate_candidates (this_, obj_files)
      || (this_->num_need_load == 0))
    {
      /* validation failed or All of the candidates were already loaded */
      goto cleanup;
    }

  for (i = 0; i < this_->candidates.num; i++)
    {
      if (!this_->candidates.entries[i].duplicate)
	{
	  if (dl_Debug)
	    {
	      fprintf (stderr, "Opening file: %s as handle: %d\n",
		       obj_files[i], this_->handler.top);
	    }

	  /* first see if we need to extend the handle array */
	  if (this_->handler.top == this_->handler.num)
	    {
	      int new_handles_num;
	      void **new_handles;

	      new_handles_num = this_->handler.num + HANDLES_PER_EXTENT;
	      if (dl_Debug)
		{
		  fprintf (stderr, "Extending handle arrary to %d handles\n",
			   new_handles_num);
		}
	      new_handles =
		(void **) malloc (new_handles_num * sizeof (void *));
	      if (new_handles == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DL_LOAD_ERR,
			  1, "Memory allocation failed for handle extension");
		  dl_Errno = ER_FAILED;
		  goto cleanup;
		}
	      memcpy (new_handles, this_->handler.handles,
		      (this_->handler.num * sizeof (void *)));
	      free_and_init (this_->handler.handles);
	      this_->handler.handles = new_handles;
	      this_->handler.num = new_handles_num;
	    }

#if defined(HPUX)
	  this_->handler.handles[this_->handler.top] =
	    (void *) shl_load (obj_files[i], BIND_IMMEDIATE | BIND_NONFATAL,
			       0);
#else /* HPUX */
	  this_->handler.handles[this_->handler.top] =
	    dlopen (obj_files[i], RTLD_LAZY);
#endif /* HPUX */
	  if (this_->handler.handles[this_->handler.top] == NULL)
	    {
#if defined(HPUX)
	      char dl_msg[1024];
	      sprintf (dl_msg,
		       "Error shl_load'ing file: %s, with error code: %d",
		       obj_files[i], errno);
	      if (dl_Debug)
		{
		  fprintf (stderr, "%s\n", dl_msg);
		}
#else /* HPUX */
	      char *dl_msg;
	      dl_msg = dlerror ();
	      if (dl_Debug)
		{
		  fprintf (stderr,
			   "Error opening file: %s, with error \"%s\"\n",
			   obj_files[i], dl_msg);
		}
#endif /* HPUX */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DL_LOAD_ERR,
		      1, dl_msg);
	      dl_Errno = ER_FAILED;
	      goto cleanup;
	    }
	  else
	    {
	      if (dl_Debug)
		{
		  fprintf (stderr,
			   "dl_open for handle: %d succeeded, value = %p\n",
			   this_->handler.top,
			   this_->handler.handles[this_->handler.top]);
		}
	      this_->handler.top++;
	    }
	}
    }

  dl_record_files (this_);	/* Record the names of the files that we have loaded,
				   so that we don't ever load them again. */

cleanup:

  if (dl_Errno)
    {
      for (i = 0; i < this_->candidates.num; ++i)
	{
	  this_->candidates.entries[i].valid = false;	/* to free filename */
	}
    }

  dl_destroy_candidates (this_);

  if (dl_Errno == NO_ERROR)
    return NO_ERROR;

  return ER_FAILED;
}
#endif /* (SOLARIS) || (HPUX) || (LINUX) */

/*
 * dl_resolve_symbol() - obtain the address of a symbol from a object
 *   return: Zero on success, non-zero on failure
 *   this_(in): DYNAMIC_LOADER * structure pointer
 *   syms(in): symbols to resolve
 */
static int
dl_resolve_symbol (DYNAMIC_LOADER * this_, struct nlist *syms)
{
#if defined(HPUX) || defined(SOLARIS) || defined(LINUX)

#if defined(HPUX)
#define SYMS_NTYPE_NULL		ST_NULL
#define SYMS_NTYPE_ENTRY	ST_ENTRY
#else	/* HPUX */	   /* LINUX code is same with SOLARIS */
#define SYMS_NTYPE_NULL		N_UNDF
#define SYMS_NTYPE_ENTRY	(N_TEXT | N_EXT)
#endif

  int i, j, num_resolutions;
  void *resolution;

  for (i = 0; syms[i].n_name != NULL; i++)
    {
      syms[i].n_type = SYMS_NTYPE_NULL;

      if (dl_Debug)
	{
	  fprintf (stderr, "Resolving symbol: %s\n", syms[i].n_name);
	}

      for (j = 0, num_resolutions = 0; j < this_->handler.top; j++)
	{
	  if (dl_Debug)
	    {
	      fprintf (stderr,
		       "resolving symbols with handle: %d, value = %p\n",
		       j, this_->handler.handles[j]);
	    }
#if defined(HPUX)
	  if (shl_findsym
	      ((shl_t *) & this_->handler.handles[j], syms[i].n_name,
	       TYPE_PROCEDURE, &resolution) == NO_ERROR)
#else /* HPUX */
	  if ((resolution =
	       dlsym (this_->handler.handles[j], syms[i].n_name)))
#endif
	    {
	      num_resolutions++;
	      syms[i].n_value = (long) resolution;	/* TODO: consider: LP64 */
	      syms[i].n_type = SYMS_NTYPE_ENTRY;
	    }
	  else if (dl_Debug)
	    {
	      fprintf (stderr,
		       "Error resolving: %s, from handle: %d: Error code %d\n",
		       syms[i].n_name, j, errno);
	    }

	}

      if (dl_Debug)
	{
	  fprintf (stderr,
		   "Number of resolutions found for symbol: %s is: %d\n",
		   syms[i].n_name, num_resolutions);
	}

      if (num_resolutions > 1)
	{
	  /* set an error and return */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DL_MULTIPLY_DEFINED,
		  1, syms[i].n_name);
	  syms[i].n_type = SYMS_NTYPE_NULL;
	  return -1;
	}
    }

  return NO_ERROR;
#else /* (HPUX) || (SOLARIS) || (LINUX) */
  return nlist ((char *) this_->image_file, syms);
#endif /* (HPUX) || (SOLARIS) || (LINUX) */
}

/*
 * dl_initiate_module() - initialize the dynamic loader
 *   return: Zero on success, non-zero on failure.
 *   module_name(in): name of the file whose symbol table should be used
 *                      as the starting point for dynamic loading
 */
#if !defined (SOLARIS) && !defined(LINUX)
int
dl_initiate_module (const char *module_name)
#else /* !(SOLARIS) && !(LINUX) */
int
dl_initiate_module (void)
#endif				/* !(SOLARIS) && !(LINUX) */
{
  const char *image_name = NULL;

  if (dl_Loader)
    {
      DL_SET_ERROR_WITH_CODE (ER_DL_EXISTS);
      return ER_FAILED;
    }

#if !defined (SOLARIS) && !defined(LINUX)
  image_name = exec_path (module_name);
  if (image_name == NULL)
    {
      DL_SET_ERROR_WITH_CODE_ONE_ARG (ER_DL_IMAGE, module_name);
      return ER_FAILED;
    }
#endif /* !SOLARIS && !LINUX */

  dl_Debug = getenv ("DL_DEBUG") != (int) NULL;

  dl_Loader = (DYNAMIC_LOADER *) malloc (sizeof (DYNAMIC_LOADER));
  if (dl_Loader == NULL)
    {
      DL_SET_ERROR_SYSTEM_MSG ();
      return ER_FAILED;
    }

  if (dl_initiate_dynamic_loader (dl_Loader, image_name))
    {
      return ER_FAILED;
    }

  if (!dl_is_valid_image_file (dl_Loader))
    {
      DL_SET_ERROR_WITH_CODE (ER_DL_INVALID);
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * dl_destroy_module() - ear down the dynamic loader
 *   return: Zero on success, non-zero on failure.
 */
int
dl_destroy_module (void)
{
  dl_Errno = NO_ERROR;

  if (dl_Loader == NULL)
    {
      DL_SET_ERROR_WITH_CODE (ER_DL_ABSENT);
      return ER_FAILED;
    }

  if (!dl_is_valid_image_file (dl_Loader))
    {
      DL_SET_ERROR_WITH_CODE (ER_DL_INVALID);
      return ER_FAILED;
    }

  dl_destroy_dynamic_loader (dl_Loader);

  free_and_init (dl_Loader);
  dl_Debug = false;

  return dl_Errno;
}

#if defined(sun) || defined(sparc) || defined(HPUX) || defined(SOLARIS) || defined(LINUX)
/*
 * dl_load_object_module() - loads the named files
 *   return: Zero on success, non-zero on failure.
 *   obj_files(in): array of filenames to be loaded
 *   msgp(in): not used
 *   libs(in): array of arguments to be passed to ld
 *
 * Note: If you feel the need
 */
#if defined(HPUX) || defined(SOLARIS) || defined(LINUX)
int
dl_load_object_module (const char **obj_files, const char **msgp)
#else /* (HPUX) || (SOLARIS) || (LINUX) */
int
dl_load_object_module (const char **obj_files, const char **msgp,
		       const char **libs)
#endif				/* (HPUX) || (SOLARIS) || (LINUX) */
{
  dl_Errno = NO_ERROR;

  if (msgp)
    {
      *msgp = "obsolete interface; use standard error interface instead";
    }

  if (dl_Loader == NULL)
    {
      DL_SET_ERROR_WITH_CODE (ER_DL_ABSENT);
      return ER_FAILED;
    }

  if (!dl_is_valid_image_file (dl_Loader))
    {
      DL_SET_ERROR_WITH_CODE (ER_DL_INVALID);
      return ER_FAILED;
    }
#if defined(HPUX) || defined(SOLARIS) || defined(LINUX)
  return dl_load_objects (dl_Loader, obj_files);
#else /* HPUX */
  return dl_load_objects (dl_Loader, obj_files, libs, 0, NULL, DL_RELATIVE);
#endif /* HPUX */
}

/*
 * dl_resolve_object_symbol() - obtain the address of a symbol from a object
 *   return: Zero on success, non-zero on failure
 *   syms(in): symbols to resolve
 *
 * Note: For each entry in syms, if the named symbol is present in the
 *      process's current symbol table, its value and type are placed
 *      in the n_value and n_type fields.
 */
int
dl_resolve_object_symbol (struct nlist *syms)
{
  if (dl_Loader == NULL)
    {
      DL_SET_ERROR_WITH_CODE (ER_DL_ABSENT);
      return ER_FAILED;
    }

  if (!dl_is_valid_image_file (dl_Loader))
    {
      DL_SET_ERROR_WITH_CODE (ER_DL_INVALID);
      return ER_FAILED;
    }

  return dl_resolve_symbol (dl_Loader, syms);
}

/*
 * dl_load_object_with_estimate() - run dl_load_object_module and accepts extra
 *            information to try to refine size estimates for loading the files
 *   return: Zero on success, non-zero on failure
 *   actual_size(out): pointer to receive information
 *   obj_files(in): array of object file names
 *   libs(in): array of arguments to be passed to ld
 *   msgp(in): not used
 *   estimated_size(in): fudge factor for modifying estimate of size required
 *                      to load files
 *   mode(in) : if mode == DL_RELATIVE, estimated_size is an extra value to be
 *      added to the loader's estimate;
 *      if mode == DL_ABSOLUTE, estimated_size is used as the estimate.
 */
#if defined(HPUX) || defined(SOLARIS) || defined(LINUX)
int
dl_load_object_with_estimate (const char **obj_files, const char **msgp)
#elif (defined(sun) || defined(sparc)) && !defined(SOLARIS)
int
dl_load_object_with_estimate (size_t * actual_size,
			      const char **obj_files,
			      const char **msgp,
			      const char **libs,
			      const size_t estimated_size,
			      enum dl_estimate_mode mode)
#endif				/* (HPUX) || (SOLARIS) || (LINUX) */
{
  dl_Errno = NO_ERROR;

  if (msgp)
    {
      *msgp = "obsolete interface; use standard error interface instead";
    }

  if (dl_Loader == NULL)
    {
      DL_SET_ERROR_WITH_CODE (ER_DL_ABSENT);
      return ER_FAILED;
    }

  if (!dl_is_valid_image_file (dl_Loader))
    {
      DL_SET_ERROR_WITH_CODE (ER_DL_INVALID);
      return ER_FAILED;
    }
#if defined(HPUX) || defined(SOLARIS) || defined(LINUX)
  return dl_load_objects (dl_Loader, obj_files);
#else /* HPUX || SOLARIS || LINUX */
  return dl_load_objects (dl_Loader, obj_files, libs, estimated_size,
			  actual_size, mode);
#endif /* HPUX || SOLARIS || LINUX */
}
#elif defined(_AIX)
/*
 * dl_load_and_resolve() - Validate and load object files
 *   return: Zero on success, non-zero on failure
 *   obj_files(in): array of object file names to be loaded
 *   msgp(in) : not used
 *   libs(in): array of library to be passed to ld
 *   syms(in): nlist structure.
 */
int
dl_load_and_resolve (const char **obj_files,
		     const char **msgp, const char **libs, struct nlist *syms)
{
  dl_Errno = NO_ERROR;

  if (msgp)
    {
      *msgp = "obsolete interface; use standard error interface instead";
    }

  if (dl_Loader == NULL)
    {
      DL_SET_ERROR_WITH_CODE (ER_DL_ABSENT);
      return ER_FAILED;
    }

  if (!dl_is_valid_image_file (dl_Loader))
    {
      DL_SET_ERROR_WITH_CODE (ER_DL_INVALID);
      return ER_FAILED;
    }

  return dl_load_and_resolve_objects (dl_Loader, obj_files, libs, syms);
}
#endif /* sun || sparc || HPUX || SOLARIS || LINUX */
