/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * schema_manager_2.c - Method handling for the schema
 *                      Interfaces with the dynamic loading utility
 */

#ident "$Id$"

/* NOTE !!!
 * The static method lists are applicable to the entire execution session
 * of an application NOT just a single restart/shutdown of the database.
 * The static methods are normally declared before the database is opened
 * and may exist for many databases.  For this reason, the storage allocated
 * to maintain this table must NOT be allocated within the workspace since
 * the workspace is transient and is cleared when a database is shut down.
 * The table is instead allocated using malloc and must be flushed manually
 * when the application is in a position to do so.
*/

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HPUX
#include <a.out.h>
#endif /* HPUX */

#include "porting.h"
#include "chartype.h"
#if !defined(WINDOWS)
#include "dynamic_load.h"
#endif /* !WINDOWS */
#include "error_manager.h"
#include "work_space.h"
#include "object_primitive.h"
#include "class_object.h"
#include "schema_manager_3.h"
#include "authenticate.h"
#include "message_catalog.h"
#include "memory_manager_2.h"
#include "environment_variable.h"


#define WC_PERIOD L'.'

/*
 *    Internal structure for maintaining the global list of static
 *    method linkage information.  This list is built with user supplied
 *    information by calling sm_add_static_method().
 */
typedef struct static_method STATIC_METHOD;

struct static_method
{
  struct static_method *next;

  char *name;
  void (*function) ();

};

/*
 *    Temporary structure used to hold linking state during dynamic linking.
 */
typedef struct method_link METHOD_LINK;

struct method_link
{
  struct method_link *next;

  SM_METHOD *method;
  int namelist_index;

};

/*
 * Static_method_table
 *
 * description:
 *    Global list of static method link information.
 */

static STATIC_METHOD *Static_method_table = NULL;

/*
 * Platform specific default extension for method files.
 * These are added automatically if the extension is not found in the schema
 */
#if defined(WINDOWS)
static const char *method_file_extension = ".dll";
#elif defined (HPUX)
static const char *method_file_extension = ".sl";
#elif defined (SOLARIS) || defined(LINUX)
static const char *method_file_extension = ".so";
#elif defined(sun) || defined(AIX)
static const char *method_file_extension = ".o";
#else /* WINDOWS */
#error "Unknown machine type."
#endif /* WINDOWS */

#if !defined(WINDOWS)
#include <nlist.h>
#endif /* !WINDOWS */

static STATIC_METHOD *sm_find_static_method (const char *name);
static int sm_count_tokens (const char *string, int *maxcharp);
static int sm_split_loader_commands (const char *string,
				     const char ***command_ptr);
static void sm_free_loader_commands (const char **commands);
static void sm_free_method_links (METHOD_LINK * links);
static int sm_link_static_method (SM_METHOD * method,
				  METHOD_LINK ** link_ptr);
static int sm_link_static_methods (SM_CLASS * class_,
				   METHOD_LINK ** links_ptr);
static int sm_expand_method_files (SM_METHOD_FILE * files);
#if !defined(WINDOWS)
static int sm_build_function_nlist (METHOD_LINK * links,
				    struct nlist **nlist_ptr);
static void sm_free_function_nlist (struct nlist *namelist);
#endif /* !WINDOWS */
#if defined (sun) || defined(SOLARIS) || defined(LINUX)
#if defined(SOLARIS) || defined(LINUX)
static int sm_link_dynamic_methods (METHOD_LINK * links, const char **files);
#else /* SOLARIS || LINUX */
static int sm_link_dynamic_methods (METHOD_LINK * links, const char **files,
				    const char **commands);
#endif /* SOLARIS || LINUX */
#elif defined (_AIX)
static int sm_link_dynamic_methods (METHOD_LINK * links, const char **files,
				    const char **commands);
#elif defined(HPUX)
static int sm_link_dynamic_methods (METHOD_LINK * links, const char **files,
				    const char **commands);
#elif defined(WINDOWS)
static HINSTANCE load_dll (const char *name);
static int sm_link_dynamic_methods (METHOD_LINK * links, const char **files,
				    const char **commands);
#endif /*  sun || SOLARIS || LINUX */
static int sm_file_extension (const char *path, const char *ext);
static int sm_dynamic_link_class (SM_CLASS * class_, METHOD_LINK * links);
static int sm_link_methods (SM_CLASS * class_);
/*
 * sm_add_static_method() - Adds an element to the static link table.
 *    The name argument and the name of the function pointed to
 *    are usually the same but this is not mandatory.
 *   return: none
 *   name(in): method function name
 *   function(in): method function pointer
 */
void
sm_add_static_method (const char *name, void (*function) ())
{
  STATIC_METHOD *m, *found, *new_;

  if (name == NULL)
    {
      return;
    }

  found = NULL;
  for (m = Static_method_table; m != NULL && found == NULL; m = m->next)
    {
      if (strcmp (m->name, name) == 0)
	{
	  found = m;
	}
    }
  /* if found, assume we just want to change the function */
  if (found != NULL)
    {
      found->function = function;
    }
  else
    {
      new_ = (STATIC_METHOD *) malloc (sizeof (STATIC_METHOD));
      if (new_ != NULL)
	{
	  new_->next = Static_method_table;
	  Static_method_table = new_;
	  new_->function = function;
	  new_->name = (char *) malloc (strlen (name) + 1);
	  strcpy ((char *) new_->name, name);
	}
    }
}

/*
 * sm_delete_static_method() - Removes static link information for
 * 				the named method function
 *   return: none
 *   name(in): method function name
 */
void
sm_delete_static_method (const char *name)
{
  STATIC_METHOD *m, *prev, *found;

  found = NULL;
  prev = NULL;

  for (m = Static_method_table; m != NULL && found == NULL; m = m->next)
    {
      if (strcmp (m->name, name) == 0)
	{
	  found = m;
	}
      else
	{
	  prev = m;
	}
    }
  if (found == NULL)
    {
      return;
    }

  if (prev == NULL)
    {
      Static_method_table = found->next;
    }
  else
    {
      prev->next = found->next;
    }
  free_and_init (found->name);
  free_and_init (found);
}

/*
 * sm_flush_static_methods() - Clear the static method table
 */

void
sm_flush_static_methods ()
{
  STATIC_METHOD *m, *next;

  for (m = Static_method_table, next = NULL; m != NULL; m = next)
    {
      next = m->next;
      free_and_init (m->name);
      free_and_init (m);
    }
  Static_method_table = NULL;
}

/*
 * sm_find_static_method() - Searches the global static method list for
 *                            the named function
 *  return: static method structure
 *  name(in): method function name
 */

static STATIC_METHOD *
sm_find_static_method (const char *name)
{
  STATIC_METHOD *m, *found;

  found = NULL;

  m = Static_method_table;

  while (m != NULL && found == NULL)
    {
      if (strcmp (m->name, name) == 0)
	{
	  found = m;
	}
      m = m->next;
    }

  return (found);
}

/*
 * sm_count_tokens() - Work function for split_loader_commands.
 *    A token is defined as any string of characters separated by
 *    whitespace.  Calculate the number of tokens in the string and the
 *    maximum length of all tokens.
 *
 *   return: number of tokens in the command string
 *   string(in): loader command string
 *   maxcharp(out): returned size of maximum token length
 */

static int
sm_count_tokens (const char *string, int *maxcharp)
{
  int tokens, chars, maxchars, i;

  tokens = 0;
  maxchars = 0;

  if (string == NULL)
    {
      return (tokens);
    }

  for (i = 0; i < (int) strlen (string); i++)
    {
      if (char_isspace (string[i]))
	{
	  continue;
	}
      tokens++;

      for (chars = 0;
	   i < (int) strlen (string) && !char_isspace (string[i]);
	   i++, chars++);
      if (chars > maxchars)
	{
	  maxchars = chars;
	}
    }

  if (maxcharp != NULL)
    {
      *maxcharp = maxchars;
    }
  return (tokens);
}

/*
 * sm_split_loader_commands() - Takes a string containing loader commands
 *    separated by whitespaces and creates an argv style array with
 *    NULL termination. This is required for the dynamic loader.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   string(in): loader command string
 *   command_ptr(out): argv style array with loader commands
 */

static int
sm_split_loader_commands (const char *string, const char ***command_p)
{
  int error = NO_ERROR;
  int tokens, maxchars, i, j;
  char *buf, *ptr;
  const char *new_;
  const char **commands;

  commands = NULL;
  tokens = sm_count_tokens (string, &maxchars);

  if (!tokens)
    {
      goto end;
    }

  buf = (char *) db_ws_alloc (sizeof (char) * (maxchars + 1));

  if (buf == NULL)
    {
      error = er_errid ();
      goto end;
    }
  commands = (const char **) db_ws_alloc (sizeof (char *) * tokens + 1);

  if (commands == NULL)
    {
      error = er_errid ();
      db_ws_free (buf);
      goto end;
    }
  ptr = (char *) string;
  for (i = 0; i < tokens && error == NO_ERROR; i++)
    {
      for (; *ptr != '\0' && char_isspace (*ptr); ptr++);
      for (j = 0; *ptr != '\0' && !char_isspace (*ptr); ptr++, j++)
	{
	  buf[j] = *ptr;
	}
      buf[j] = '\0';
      new_ = ws_copy_string (buf);
      if (new_ != NULL)
	{
	  commands[i] = new_;
	}
      else
	{
	  error = er_errid ();
	  db_ws_free (commands);
	  db_ws_free (buf);
	  return (error);
	}
    }

  commands[i] = NULL;
  db_ws_free (buf);

end:
  if (error == NO_ERROR)
    {
      *command_p = commands;
    }
  return (error);
}

/*
 * sm_free_loader_commands() - Frees an array of loader commands created with
 * 			    split_loader_commands()
 *   return: none
 *   commands(in): argv style loader command array
 */

static void
sm_free_loader_commands (const char **commands)
{
  int i;

  if (commands != NULL)
    {
      for (i = 0; commands[i] != NULL; i++)
	{
	  db_ws_free ((char *) commands[i]);
	}
      db_ws_free (commands);
    }
}

/* STATIC LINKING */
/*
 * sm_free_method_links() - Free a list of temporary method link structures
 *    after dynamic linking has finished
 *   return: none
 *   links(in): list of method link structures
 */

static void
sm_free_method_links (METHOD_LINK * links)
{
  METHOD_LINK *link, *next = NULL;

  for (link = links; link != NULL; link = next)
    {
      next = link->next;
      db_ws_free (link);
    }
}

/*
 * sm_link_static_method() - Attempt to link a single method using the
 *    static method table. If a static link could not be made, construct
 *    and return a method link structure that will be used later during
 *    dynamic linking.
 *    If the method could be statically linked, set up the function
 *    pointer in the method structure and return NULL.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   method(in/out): schema method structure
 *   link_ptr(out): schema method structure
 */

static int
sm_link_static_method (SM_METHOD * method, METHOD_LINK ** link_ptr)
{
  int error = NO_ERROR;
  STATIC_METHOD *m;
  METHOD_LINK *link;

  link = NULL;

  if (method->signatures == NULL)
    {
      goto end;
    }

  m = sm_find_static_method (method->signatures->function_name);

  if (m != NULL)
    {
      /* should check for reasonable type */
      method->signatures->function = (METHOD_FUNCTION) m->function;
      /* put it in the cache as well */
      method->function = (METHOD_FUNCTION) m->function;
    }
  else
    {
      /* couldn't statically link, build dynamic link state */
      link = (METHOD_LINK *) db_ws_alloc (sizeof (METHOD_LINK));
      if (link == NULL)
	{
	  error = er_errid ();
	}
      else
	{
	  link->next = NULL;
	  link->method = method;
	  link->namelist_index = -1;
	}
    }
end:
  if (error == NO_ERROR)
    {
      *link_ptr = link;
    }
  return (error);
}

/*
 * sm_link_static_methods() - Attemps to statically link all of the methods
 *    in a class. A METHOD_LINK structure is created for every method that
 *    could not be statically linked and returned in a list.  This list
 *    is used later to dynaimcally link the remaining methods
 *   return: NO_ERROR on success, non-zero for ERROR
 *   class(in): class with methods to be linked
 *   links_ptr(out): list of method link structures
 */

static int
sm_link_static_methods (SM_CLASS * class_, METHOD_LINK ** links_ptr)
{
  int error = NO_ERROR;
  METHOD_LINK *links, *link;
  SM_METHOD *method;

  links = NULL;

  for (method = class_->methods; method != NULL && error == NO_ERROR;
       method = (SM_METHOD *) method->header.next)
    {
      error = sm_link_static_method (method, &link);
      if (error == NO_ERROR)
	{
	  if (link != NULL)
	    {
	      link->next = links;
	      links = link;
	    }
	}
    }
  for (method = class_->class_methods; method != NULL && error == NO_ERROR;
       method = (SM_METHOD *) method->header.next)
    {
      error = sm_link_static_method (method, &link);
      if (error == NO_ERROR)
	{
	  if (link != NULL)
	    {
	      link->next = links;
	      links = link;
	    }
	}
    }

  if (error == NO_ERROR)
    {
      *links_ptr = links;
    }
  return (error);
}

/* DYNAMIC LINKING */
/*
 * sm_expand_method_files() - This is called prior to dynamic linking to go 
 *    through all the method files for a class and expand any environment 
 *    variables that may be included in the file pathnames.  
 *    This expansion is delayed until link time so that changing the values of 
 *    the env variables allow site specific customization of behavior.
 *    When finished, the expanded_name field in the file structures will
 *    be non-NULL if expansions were performed or will be NULL if
 *    no expansion was necessary.  If no error code is returned,
 *    assume all expansions were performed.  If the expansion_name field
 *    is already set, free it and recalculate the expansion.
 *
 *    Changed to automatically supply method file extensions if they have
 *    not been specified in the schema.  This is usefull when databases
 *    are used in a heterogeneous environment, eliminating the need to
 *    have multiple versions of the schema for each platform.  This will
 *    handle the most common cases, for really radical platforms, a more
 *    general mechanism mey be necessary
 *   return: NO_ERROR on success, non-zero for ERROR
 *   files(in/out): list of method files
 */
static int
sm_expand_method_files (SM_METHOD_FILE * files)
{
  char filebuf[PATH_MAX];
  int error = NO_ERROR;
  SM_METHOD_FILE *f;

  for (f = files; f != NULL && error == NO_ERROR; f = f->next)
    {
      if (f->expanded_name != NULL)
	{
	  ws_free_string (f->expanded_name);
	  f->expanded_name = NULL;
	}
      if (envvar_expand (f->name, filebuf, PATH_MAX) == NO_ERROR)
	{
	  /* check for automatic extensions, this is determined by checking to see
	   * if there are no '.' characters in the name, could be more complicated.
	   * Use intl_mbs_chr just in case we need to be dealing with wide strings.
	   */
	  if (intl_mbs_chr (filebuf, WC_PERIOD) == NULL)
	    {
	      strcat (filebuf, method_file_extension);
	    }

	  /* If the name we've been manipulating is different then the original name,
	   * copy it and use it later.
	   */
	  if (strcmp (filebuf, f->name) != 0)
	    {
	      f->expanded_name = ws_copy_string (filebuf);
	      if (f->expanded_name == NULL)
		{
		  error = er_errid ();	/* out of memory */
		}
	    }
	}
      else
	{
	  /* could stop at the first one but just go through them all */
	  error = ER_SM_INVALID_METHOD_ENV;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, filebuf);
	}
    }
  return (error);
}

/*
 * sm_build_function_nlist() - Builds an nlist function name array from a list of
 *    method link structures.  The links are filtered so that only unique
 *    names will be in the nlist array.  The links structures are
 *    modified as a side effect so that their namelist_index is set to the
 *    index in the nlist array where the information for that function
 *    will be found.
 *    The nlist array must be freed with free_function_nlist
 *   return: NO_ERROR on success, non-zero for ERROR
 *   links(in): list of methodlinks
 *   nlist_ptr(out): nlist array
 */

#if !defined(WINDOWS)

static int
sm_build_function_nlist (METHOD_LINK * links, struct nlist **nlist_p)
{
  int error = NO_ERROR;
  struct nlist *namelist;
  METHOD_LINK *ml;
  const char **fnames;
  const char *new_;
  int i, nlinks, index;
  char fname[SM_MAX_IDENTIFIER_LENGTH + 2];

  namelist = NULL;
  if (links == NULL)
    {
      goto end;
    }
  /* allocation & initialize an array for building the unique name list */
  nlinks = WS_LIST_LENGTH (links);
  fnames = (const char **) db_ws_alloc (sizeof (char *) * nlinks);
  if (fnames == NULL)
    {
      error = er_errid ();
    }
  else
    {
      for (i = 0; i < nlinks; i++)
	{
	  fnames[i] = NULL;
	}

      /* populate the unique name array */
      index = 0;
      for (ml = links; ml != NULL && error == NO_ERROR; ml = ml->next)
	{
	  ml->namelist_index = -1;
	  if (ml->method->signatures->function_name != NULL)
	    {
	      /* mangle the name as appropriate, sun wants prepended '_', ibm doesn't */
#if defined(sun) && !defined(SOLARIS)
	      sprintf (fname, "_%s", ml->method->signatures->function_name);
#else /* sun && !SOLARIS */
	      sprintf (fname, "%s", ml->method->signatures->function_name);
#endif /* sun && !SOLARIS */
	      /* see if it is already in the nlist array */
	      for (i = 0; i < index && ml->namelist_index == -1; i++)
		{
		  if (strcmp (fname, fnames[i]) == 0)
		    ml->namelist_index = i;
		}
	      /* add it if not already there */
	      if (ml->namelist_index == -1)
		{
		  ml->namelist_index = index;
		  new_ = ws_copy_string ((const char *) fname);
		  if (new_ != NULL)
		    {
		      fnames[index++] = new_;
		    }
		  else
		    {
		      error = er_errid ();
		    }
		}
	    }
	}

      if (error == NO_ERROR)
	{
	  /* build an actual nlist structure from the unique name array */
	  namelist =
	    (struct nlist *) db_ws_alloc (sizeof (struct nlist) *
					  (index + 1));
	  if (namelist == NULL)
	    {
	      error = er_errid ();
	    }
	  else
	    {
	      for (i = 0; i < index; i++)
		{
		  namelist[i].n_name = (char *) fnames[i];
		}
	      namelist[index].n_name = NULL;

	    }
	}
      /* don't need this anymore */
      db_ws_free (fnames);
    }
end:
  if (error == NO_ERROR)
    {
      *nlist_p = namelist;
    }
  return (error);
}

/*
 * sm_free_function_nlist() - Frees an nlist array that was allocated with
 * 			   build_function_nlist()
 *   return: none
 *   namelist(in): nlist array
 */

static void
sm_free_function_nlist (struct nlist *namelist)
{
  int i;

  if (namelist != NULL)
    {
      for (i = 0; namelist[i].n_name != NULL; i++)
	{
	  db_ws_free (namelist[i].n_name);
	}
      db_ws_free (namelist);
    }
}
#endif /* !WINDOWS */

/*
 * sm_link_dynamic_methods() - Call the dynamic linker to resolve any function
 *    references that could not be statically linked.  The static linking phase
 *    produces a list of METHOD_LINK structures for the methods that could
 *    not be linked.  We use this list here to build the control structures
 *    for the dynamic loader.
 *    The files array has the names of the method files specified in the
 *    schema.  The commands array has the loader commands.
 *    This can be used to link methods for several classes
 *   return: NO_ERROR on success, non-zero for ERROR
 *   links(in/out): list of method link structures
 *   files(in): array of method files (NULL terminated)
 *   commands(in): array of loader commands (NULL terminated)
 */

#if defined (sun) || defined(SOLARIS) || defined(LINUX)
#if defined(SOLARIS) || defined(LINUX)
static int
sm_link_dynamic_methods (METHOD_LINK * links, const char **files)
#else /* SOLARIS || LINUX */
static int
sm_link_dynamic_methods (METHOD_LINK * links,
			 const char **files, const char **commands)
#endif				/* SOLARIS || LINUX */
{
  int error = NO_ERROR;
  METHOD_LINK *ml;
  struct nlist *namelist, *nl;
  const char *msg;
  int status;

  error = sm_build_function_nlist (links, &namelist);
  if (error == NO_ERROR && namelist != NULL)
    {

      /* invoke the linker */
#if defined(SOLARIS) || defined(LINUX)
      status = dl_load_object_module (files, &msg);
#else /* SOLARIS || LINUX */
      status = dl_load_object_module (files, &msg, commands);
#endif /* SOLARIS || LINUX */
      if (status)
	{
	  error = er_errid ();
	}
      else
	{
	  /* resolve functions */
	  status = dl_resolve_object_symbol (namelist);
	  if (status == -1)
	    {
	      error = er_errid ();
	    }
	  else
	    {
	      /* what does this accomplish ? */
	      if (status)
		{
		  error = ER_SM_UNRESOLVED_METHODS;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, status);
		}

	      /* now link the methods, signal and return an error when one is
	         encountered but go ahead and try to link whatever is there */
	      for (ml = links; ml != NULL; ml = ml->next)
		{
		  nl = &namelist[ml->namelist_index];
		  if (nl->n_type == (N_TEXT | N_EXT))
		    {
		      ml->method->signatures->function =
			(METHOD_FUNCTION) nl->n_value;
		      ml->method->function = (METHOD_FUNCTION) nl->n_value;
		    }
		  else
		    {
		      error = ER_SM_UNRESOLVED_METHOD;
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1,
			      nl->n_name);
		    }
		}
	    }
	}
      sm_free_function_nlist (namelist);
    }
  return (error);
}

#elif defined (_AIX)
static int
sm_link_dynamic_methods (METHOD_LINK * links,
			 const char **files, const char **commands)
{
  int error = NO_ERROR;
  METHOD_LINK *ml;
  struct nlist *nl, *namelist;
  const char *msg;
  int status;

  error = sm_build_function_nlist (links, &namelist);
  if (error == NO_ERROR && namelist != NULL)
    {

      /* invoke the linker and resolve functions */
      status = dl_load_and_resolve (files, &msg, commands, namelist);

      if (status == -1)
	{
	  error = er_errid ();
	}
      else
	{
	  if (status)
	    {
	      error = ER_SM_UNRESOLVED_METHODS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_SM_UNRESOLVED_METHODS, 1, status);
	    }

	  /* go ahead and try to resolve the ones that exist even if there were
	     errors on some others */
	  for (ml = links; ml != NULL; ml = ml->next)
	    {
	      nl = &namelist[ml->namelist_index];
	      ml->method->signatures->function =
		(METHOD_FUNCTION) nl->n_value;
	      ml->method->function = (METHOD_FUNCTION) nl->n_value;
	      if (nl->n_value)
		{
		  ml->method->signatures->function =
		    (METHOD_FUNCTION) nl->n_value;
		  ml->method->function = (METHOD_FUNCTION) nl->n_value;
		}
	      else
		{
		  error = ER_SM_UNRESOLVED_METHOD;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1,
			  nl->n_name);
		}
	    }
	}
      db_ws_free (namelist);
    }
  return (error);
}

#elif defined(HPUX)
static int
sm_link_dynamic_methods (METHOD_LINK * links,
			 const char **files, const char **commands)
{
  int error = NO_ERROR;
  METHOD_LINK *ml;
  struct nlist *namelist, *nl;
  const char *msg;
  int status;

  error = sm_build_function_nlist (links, &namelist);
  if (error == NO_ERROR && namelist != NULL)
    {

      /* invoke the linker */
      status = dl_load_object_module (files, &msg, commands);
      if (status)
	{
	  error = er_errid ();
	}
      else
	{
	  /* resolve functions */
	  status = dl_resolve_object_symbol (namelist);
	  if (status == -1)
	    {
	      error = er_errid ();
	    }
	  else
	    {
	      /* what does this accomplish ? */
	      if (status)
		{
		  error = ER_SM_UNRESOLVED_METHODS;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, status);
		}

	      /* now link the methods, signal and return an error when one is
	         encountered but go ahead and try to link whatever is there */
	      for (ml = links; ml != NULL; ml = ml->next)
		{
		  nl = &namelist[ml->namelist_index];
		  if (nl->n_type == (ST_ENTRY))
		    {
		      ml->method->signatures->function =
			(METHOD_FUNCTION) nl->n_value;
		      ml->method->function = (METHOD_FUNCTION) nl->n_value;
		    }
		  else
		    {
		      error = ER_SM_UNRESOLVED_METHOD;
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1,
			      nl->n_name);
		    }
		}
	    }
	}
      sm_free_function_nlist (namelist);
    }
  return (error);
}
#elif defined(WINDOWS)

/* DYNAMIC LINK LIBRARY MAINTENANCE */
/* Structure that maintains a globa list of DLL's that have been opened */
typedef struct pc_dll
{
  struct pc_dll *next;
  HINSTANCE handle;
  char *name;
} PC_DLL;

/* Global list of opened DLL's */
static PC_DLL *pc_dll_list = NULL;

/*
 * load_dll() - This returns a Windows library handle for the named DLL.
 *    It will first look on our list of opened libraries, if one is not found,
 *    it asks windows to load it and adds it to the list.
 *    Only called by the PC version of link_dynamic_methods
 *   return: library handle
 *   name(in): library name
 */

static HINSTANCE
load_dll (const char *name)
{
  PC_DLL *dll;
  HINSTANCE handle;

  handle = NULL;

  /* first see if we've already loaded this */
  for (dll = pc_dll_list; dll != NULL && strcmp (name, dll->name) != 0;
       dll = dll->next);

  if (dll != NULL)
    {
      handle = dll->handle;
    }
  else
    {
      /* never been loaded, ask windows to go find it */

      handle = LoadLibrary (name);
      if (handle != NULL)
	{
	  /* successfully loaded, add to the list */

	  dll = (PC_DLL *) malloc (sizeof (PC_DLL) + strlen (name) + 2);
	  if (dll == NULL)
	    {
	      /* memory problems */
	      FreeLibrary (handle);
	      handle = NULL;
	    }
	  else
	    {
	      dll->next = pc_dll_list;
	      pc_dll_list = dll;
	      dll->handle = handle;
	      dll->name = (char *) dll + sizeof (PC_DLL);
	      strcpy (dll->name, name);
	    }
	}
    }
  return handle;
}

/*
 * sm_method_final() - Called by sm_final() to clean up state required by
 *    dynamic linking. This goes through the global DLL list and closes all
 *    the DLL's we used during this session
 */

void
sm_method_final ()
{
  PC_DLL *dll, *next;

  for (dll = pc_dll_list, next = NULL; dll != NULL; dll = next)
    {
      next = dll->next;
      FreeLibrary (dll->handle);
      free_and_init (dll);
    }

  pc_dll_list = NULL;
}

/*
 * link_dynamic_methods()
 *   return: NO_ERROR on success, non-zero for ERROR
 *   links(in):
 *   files(in):
 *   commands(in):
 */
static int
sm_link_dynamic_methods (METHOD_LINK * links,
			 const char **files, const char **commands)
{
  char filebuf[PATH_MAX];
  char fname[SM_MAX_IDENTIFIER_LENGTH + 2];
  int error = NO_ERROR;
  METHOD_LINK *ml;
  const char *file;
  HINSTANCE libhandle;
  FARPROC func;
  int i, j;

  if (links != NULL)
    {
      /* Load the DLL associated with each file in the files array and try
         to locate each method in them.  If there are errors loading a
         DLL, could continue assuming that Windows has had a chance to
         popup a message window.
       */
      for (i = 0; files[i] != NULL && error == NO_ERROR; i++)
	{
	  file = files[i];
	  /* Should have a "method name too long" error but I don't want to
	     introduce one right now.  If we have problems with a particular
	     DLL file, just ignore it and attempt to get the methods from
	     the other files.
	   */
	  if (strlen (file) + 3 < PATH_MAX)
	    {
	      /* massage the file extension so that it has .dll */
	      strcpy (filebuf, file);
	      for (j = strlen (file) - 1; j > 0 && filebuf[j] != '.'; j--);
	      if (j > 0)
		{
		  strcpy (&filebuf[j], ".dll");
		}
	      else
		{
		  /* its a file without an extension, hmm, assume that it needs .dll
		     appended to the end */
		  strcat (filebuf, ".dll");
		}

	      /* Ask Windows to open the DLL, example for GetProcAddress uses
	         SetErrorMode to turn off the "file not found" boxes,
	         we want these though.
	       */
	      libhandle = load_dll (filebuf);
	      if (libhandle != NULL)
		{
		  /* look for each unresolved method in this file */
		  for (ml = links; ml != NULL; ml = ml->next)
		    {
		      /* Formerly only did the GetProcAddress if the signature's function
		       * pointer was NULL, this prevents us from getting new addresses
		       * if the DLL changes.  Hopefully this isn't very expensive.
		       * if (ml->method->signatures->function == NULL) {
		       */
		      /* its possible that the name they've given for the function
		         name matches exactly the name in the export list of
		         the DLL, in that case, always try the given name first,
		         if that fails, assume that they've left off the initial
		         underscore necessary for DLL function references and
		         add one automatically. */
		      strcpy (fname, ml->method->signatures->function_name);
		      func = GetProcAddress (libhandle, fname);
		      if (func == NULL)
			{
			  /* before giving up, try prefixing an underscore */
			  strcpy (fname, "_");
			  strcat (fname,
				  ml->method->signatures->function_name);
			  func = GetProcAddress (libhandle, fname);
			}
		      if (func != NULL)
			{
			  /* found one */
			  ml->method->signatures->function =
			    (METHOD_FUNCTION) func;
			  ml->method->function = (METHOD_FUNCTION) func;
			}
		    }
		}
	      /* else, could abort now but lets look in the other files to see
	         if our methods all get resolved */
	    }
	}

      /* now all the files have been processed, check to see if we couldn't resolve
         any methods */

      for (ml = links; ml != NULL && error == NO_ERROR; ml = ml->next)
	{
	  if (ml->method->function == NULL)
	    {
	      error = ER_SM_UNRESOLVED_METHOD;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1,
		      ml->method->header.name);
	    }
	}

    }

  return (error);
}

#else /*  sun || SOLARIS || LINUX */
#error "Unknown machine type for link_dynamic_methods"
#endif /*  sun || SOLARIS || LINUX */

/*
 * sm_file_extension() - Hack to check file extensions, used by dynamic_link_class
 *    to sort .a files apart from the method object files
 *   return: non-zero if the path has the given file extension
 *   path(in):
 *   ext(in):
 */

static int
sm_file_extension (const char *path, const char *ext)
{
  DB_C_INT plen, elen;

  plen = strlen (path);
  elen = strlen (ext);
  return (plen > elen) && (strcmp (&(path[plen - elen]), ext) == 0);
}

/*
 * sm_dynamic_link_class() - Perform dynamic linking for a class.
 *    Tries to resolve the methods in the METHOD_LINK list which could not be
 *    resolved through static linking.
 *    Work function for sm_link_methods & sm_link_method.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   clas(in/out): class requiring linking
 *   links(in): unsresolved method links
 */

static int
sm_dynamic_link_class (SM_CLASS * class_, METHOD_LINK * links)
{
  int error = NO_ERROR;
  SM_METHOD_FILE *files, *file;
  const char **names, **sorted_names, **commands;
  int i, nfiles, psn;

  if (links == NULL)
    {
      return error;
    }
  files = class_->method_files;
  nfiles = ws_list_length ((DB_LIST *) files);
  names = (const char **) db_ws_alloc (sizeof (char *) * (nfiles + 1));

  if (names == NULL)
    {
      error = er_errid ();
      return error;
    }
  sorted_names = (const char **) db_ws_alloc (sizeof (char *) * (nfiles + 1));

  if (sorted_names == NULL)
    {
      error = er_errid ();
      db_ws_free (names);
      return error;
    }

  error = sm_expand_method_files (files);

  if (error != NO_ERROR)
    {
      db_ws_free (sorted_names);
      db_ws_free (names);
      return (error);

    }
  for (file = files, i = 0; file != NULL; file = file->next, i++)
    {
      if (file->expanded_name != NULL)
	{
	  names[i] = file->expanded_name;
	}
      else
	{
	  names[i] = file->name;
	}
    }
  names[nfiles] = NULL;

  /* Hack, if we have any Unix library (.a) files in the file list,
   * put them at the end.  Usefull if libraries are used for method
   * file support, particularly, when inherited.  Try to keep the files
   * int the same order otherwise.
   */
  psn = 0;
  for (i = 0; i < nfiles; i++)
    {
      if (!sm_file_extension (names[i], ".a"))
	{
	  sorted_names[psn++] = names[i];
	}
    }
  for (i = 0; i < nfiles; i++)
    {
      if (sm_file_extension (names[i], ".a"))
	{
	  sorted_names[psn++] = names[i];
	}
    }
  sorted_names[nfiles] = NULL;
  error = sm_split_loader_commands (class_->loader_commands, &commands);
  if (error == NO_ERROR)
    {
#if defined(SOLARIS) || defined(LINUX)
      error = sm_link_dynamic_methods (links, sorted_names);
#else /* SOLARIS || LINUX */
      error = sm_link_dynamic_methods (links, sorted_names, commands);
#endif /* SOLARIS || LINUX */
      if (commands != NULL)
	{
	  sm_free_loader_commands (commands);
	}

      /* ONLY set this after we have tried to dynamically link the class */
      if (error == NO_ERROR)
	{
	  class_->methods_loaded = 1;
	}
    }
  db_ws_free (sorted_names);
  db_ws_free (names);
  return (error);
}

/*  FUNCTIONS */
/*
 * sm_link_methods() - Links the method functions for a class.
 *    First tries to use static linking and then uses dynamic linking
 *    for the methods that could not be statically linked
 *   return: NO_ERROR on success, non-zero for ERROR
 *   class(in): class with methods to link
 */

static int
sm_link_methods (SM_CLASS * class_)
{
  int error = NO_ERROR;
  METHOD_LINK *links;

  if (class_->methods_loaded)
    {
      return NO_ERROR;
    }
  /* first link through the static table */
  error = sm_link_static_methods (class_, &links);
  if (error == NO_ERROR)
    {

      /* if there are unresolved references, use the dynamic loader */
      if (links != NULL)
	{
	  error = sm_dynamic_link_class (class_, links);
	  sm_free_method_links (links);
	}
    }
  return (error);
}

/*
 * sm_link_method() - Link a single method.
 *    This will first try to statically link the method, while we're at it,
 *    statically link all methods.
 *    If the link fails, try dynamic linking.  Note that this is different
 *    than calling sm_link_methods (to link all methods) because it
 *    will only invoke the dynamic loader if the desired method could not
 *    be statically linked.  sm_link_static_methods will dynamic link
 *    if ANY methods in the class could not be statically linked.
 *    Note that this may return an error yet still have linked the
 *    requested function
 *   return: NO_ERROR on success, non-zero for ERROR
 *   class(in): class with method
 *   method(in): method to link
 */

int
sm_link_method (SM_CLASS * class_, SM_METHOD * method)
{
  int error = NO_ERROR;
  METHOD_LINK *links;

  if (class_->methods_loaded)
    {
      return NO_ERROR;
    }
  /* first link through the static table */
  error = sm_link_static_methods (class_, &links);
  if (error == NO_ERROR)
    {
      if (links != NULL)
	{
	  /* only dynamic link if the desired method was not resolved */
	  if (method->function == NULL)
	    {
	      error = sm_dynamic_link_class (class_, links);
	    }
	  sm_free_method_links (links);
	}
    }
  return (error);
}

/*
 * sm_force_method_link() - Called to force a method reload for a class.
 *    Note that the class is passed in as an object here
 *   return: NO_ERROR on success, non-zero for ERROR
 *   obj(in): class object
 */

int
sm_force_method_link (MOP obj)
{
  int error = NO_ERROR;
  SM_CLASS *class_;

  if (obj == NULL)
    {
      return NO_ERROR;
    }

  error = au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT);

  if (error == NO_ERROR)
    {
      class_->methods_loaded = 0;
      error = sm_link_methods (class_);
    }
  return (error);
}

/*
 * sm_prelink_methods() - Used to link the methods for a set of classes
 *    at one time. Since dynamic linking can be expensive, this avoids repeated
 *    links for each class
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classes(in): list of class objects
 */

int
sm_prelink_methods (DB_OBJLIST * classes)
{
  int error = NO_ERROR;
  DB_OBJLIST *cl;
  SM_METHOD_FILE *f;
  SM_CLASS *class_;
  const char **names;
  DB_NAMELIST *filenames, *name;
  int nfiles, i;
  METHOD_LINK *total_links, *links;

  filenames = NULL;
  total_links = NULL;

  /* build link structures for all classes */
  for (cl = classes; cl != NULL && error == NO_ERROR; cl = cl->next)
    {
      /* ignore authorization errors here, what happens if the transaction
         is aborted ??? */
      if (au_fetch_class (cl->op, &class_, AU_FETCH_READ, AU_EXECUTE) !=
	  NO_ERROR)
	{
	  continue;
	}
      /* Ignore this if the class has already been fully linked */

      if (class_->methods_loaded)
	{
	  continue;
	}

      /* first link through the static table */
      error = sm_link_static_methods (class_, &links);
      if (error != NO_ERROR)
	{
	  continue;
	}
      /* if there are unresolved references, use the dynamic loader */
      if (links == NULL)
	{
	  continue;
	}

      error = sm_expand_method_files (class_->method_files);
      if (error != NO_ERROR)
	{
	  continue;
	}

      /* NEED TO BE DETECTING MEMORY ALLOCATION FAILURES IN THE nlist
         LIBRARY FUNCTIONS ! */

      /* add the files for this class */
      for (f = class_->method_files; f != NULL && !error; f = f->next)
	{
	  if (f->expanded_name != NULL)
	    {
	      error = nlist_append (&filenames, f->expanded_name, NULL, NULL);
	    }
	  else
	    {
	      error = nlist_append (&filenames, f->name, NULL, NULL);
	    }
	}

      if (!error)
	{
	  /* put the links on the combined list */
	  ws_list_append ((DB_LIST **) & total_links, (DB_LIST *) links);
	}

      /* will need to have a composite list of loader commands !! */
    }

  /* proceed only if we have references that haven't already been statically linked */
  if (error == NO_ERROR && total_links != NULL)
    {
      /* build a name array for dl_load_object_module */
      nfiles = ws_list_length ((DB_LIST *) filenames);
      names = (const char **) db_ws_alloc (sizeof (char *) * (nfiles + 1));
      if (names == NULL)
	{
	  error = er_errid ();
	}
      else
	{
	  for (i = 0, name = filenames; name != NULL; name = name->next, i++)
	    {
	      names[i] = name->name;
	    }
	  names[nfiles] = NULL;

	  /* need to have commands here ! */
#if defined(SOLARIS) || defined(LINUX)
	  error = sm_link_dynamic_methods (total_links, names);
#else /* SOLARIS || LINUX */
	  error = sm_link_dynamic_methods (total_links, names, NULL);
#endif /* SOLARIS || LINUX */
	  db_ws_free (names);
	}
    }

  /* mark the classes as loaded, don't do this if there were errors */
  if (error == NO_ERROR)
    {
      for (cl = classes; cl != NULL; cl = cl->next)
	{
	  if (au_fetch_class (cl->op, &class_, AU_FETCH_READ, AU_EXECUTE) ==
	      NO_ERROR)
	    {
	      class_->methods_loaded = 1;
	    }
	}
    }

  nlist_free (filenames);
  sm_free_method_links (total_links);

  return (error);
}

/*
 * sm_locate_method_file() - Search a class' list of method files and
 *    find which one contains a particular implementation function.
 *    Uses the Sun OS "nlist" facility.  This may not be portable
 *   return: method file name
 *   class(in): class to search
 *   function(in): method function name
 */

const char *
sm_locate_method_file (SM_CLASS * class_, const char *function)
{
  /*
     DO NOT use nlist() because of installation problems.
     - elf library linking error on some linux platform
   */
  return NULL;
#if 0
#if defined(WINDOWS)
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PC_UNIMPLEMENTED, 1,
	  "sm_locate_method_file");
  return NULL;
#else /* WINDOWS */
  struct nlist nl[2];
  SM_METHFILE *files;
  const char *found;
  int status;
  char fname[SM_MAX_IDENTIFIER_LENGTH + 2];
  const char *filename;

  found = NULL;

  /* machine dependent name mangling */
#if defined(_AIX)
  sprintf (fname, "%s", function);
#else /* _AIX */
  sprintf (fname, "_%s", function);
#endif /* _AIX */

  nl[0].n_name = fname;
  nl[1].n_name = NULL;

  /* if the class hasn't been dynamically linked, expand the method files */
  if (class->methods_loaded ||
      sm_expand_method_files (class->method_files) == NO_ERROR)
    {

      for (files = class->method_files; files != NULL && found == NULL;
	   files = files->next)
	{
	  if (files->expanded_name != NULL)
	    {
	      filename = files->expanded_name;
	    }
	  else
	    {
	      filename = files->name;
	    }

	  status = nlist (filename, &nl[0]);
	  if (nl[0].n_type != 0)
	    {
	      found = filename;
	    }
	}
    }

  return (found);
#endif /* WINDOWS */
#endif /* 0 */
}

/*
 * sm_get_method_source_file() - This is an experimental function for
 *    the initial browser.  It isn't guarenteed to work in all cases.
 *    It will attempt to locate the .c file that contains the source for
 *    a method implementation.
 *    There isn't any way that this can be determined for certain, what it
 *    does now is find the .o file that contains the implementation function
 *    and assume that a .c file exists in the same directoty that contains
 *    the source.  This will be true in almost all of the current cases
 *    but cannot be relied upon.  In the final version, there will need
 *    to be some form of checking/checkout procedure so that the method
 *    source can be stored within the database
 *   return: C string
 *   class(in): class or instance
 *   method(in): method name
 */

char *
sm_get_method_source_file (MOP obj, const char *name)
{
#if defined(WINDOWS)
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PC_UNIMPLEMENTED, 1,
	  "sm_get_method_source_file");
  return NULL;
#else /* WINDOWS */
  SM_CLASS *class_;
  SM_METHOD *method;
  const char *ofile;
  char *cfile;
  const char *const_cfile;
  int len;

  cfile = NULL;
  if (au_fetch_class (obj, &class_, AU_FETCH_READ, AU_SELECT) != NO_ERROR)
    {
      return NULL;
    }
  method = classobj_find_method (class_, name, 0);

  if (method != NULL && method->signatures != NULL)
    {
      ofile =
	sm_locate_method_file (class_, method->signatures->function_name);
      if (ofile == NULL)
	{
	  return cfile;
	}
      /* coerce a .o file into a .c file */
      len = strlen (ofile);
      if (len <= 2)
	{
	  return cfile;
	}
      if (ofile[len - 1] == 'o' && ofile[len - 2] == '.')
	{
	  /* noise to prevent const conversion warnings */
	  const_cfile = ws_copy_string (ofile);
	  cfile = (char *) const_cfile;
	  cfile[len - 1] = 'c';
	}
    }
  return (cfile);
#endif /* WINDOWS */
}
