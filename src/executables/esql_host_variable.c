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
 * esql_host_variable.c - Routines for preparing statements
 *                        with the esql parser.
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>

#include "util_func.h"
#include "misc_string.h"

#include "esql_misc.h"
#define   ZZ_PREFIX em_
#include "zzpref.h"
#define  INSIDE_SCAN_DOT_C
#include "esql_grammar_tokens.h"
#include "memory_alloc.h"

#define BAD_C_TYPE		((C_TYPE) -1)
#define UNINIT_C_TYPE		((C_TYPE) -2)
#define C_TYPE_STRUCT		((C_TYPE) -3)
#define UNINITIALIZED(p)	((p) == UNINIT_C_TYPE)

enum
{
  MSG_MUST_BE_SHORT = 1,
  MSG_INDICATOR_NOT_ALLOWED = 2,
  MSG_INCOMPLETE_DEF = 3,
  MSG_NOT_VALID = 4,
  MSG_TYPE_NOT_ACCEPTABLE = 5,
  MSG_UNKNOWN_HV_TYPE = 6,
  MSG_BAD_ADDRESS = 7,
  MSG_NOT_POINTER = 8,
  MSG_NOT_POINTER_TO_STRUCT = 9,
  MSG_NOT_STRUCT = 10,
  MSG_NO_FIELD = 11,
  MSG_DEREF_NOT_ALLOWED = 12
};

typedef struct builtin_type_s BUILTIN_TYPE;
struct builtin_type_s
{
  const char *name;
  SYMBOL *sym;
  C_TYPE c_type;
  /* True if the an object of this type must be reached through a pointer */
  bool mode;
};

static BUILTIN_TYPE builtin_types[] = {
  /* These are esql-specific types that the preprocessor needs to
     know about to do its job. */
  {
   "CUBRIDDA", NULL, C_TYPE_SQLDA, true},
  /* These are SQL/X types that programmers need to use, and so the
     preprocessor also needs to know about them. */
  {
   "DB_VALUE", NULL, C_TYPE_DB_VALUE, false},
  {
   "DB_OBJECT", NULL, C_TYPE_OBJECTID, true},
  {
   "DB_DATE", NULL, C_TYPE_DATE, false},
  {
   "DB_TIME", NULL, C_TYPE_TIME, false},
  {
   "DB_UTIME", NULL, C_TYPE_TIMESTAMP, false},
  {
   "DB_TIMESTAMP", NULL, C_TYPE_TIMESTAMP, false},
  {
   "DB_MONETARY", NULL, C_TYPE_MONETARY, false},
  {
   "DB_SET", NULL, C_TYPE_BASICSET, true},
  {
   "DB_MULTISET", NULL, C_TYPE_MULTISET, true},
  {
   "DB_SEQ", NULL, C_TYPE_SEQUENCE, true},
  {
   "DB_COLLECTION", NULL, C_TYPE_COLLECTION, true}
};

/*
 * This keeps track of whether we're gathering input or output host vars
 * by pointing at either input_refs or output_refs.  This is convenient
 * when it's necessary to update pp_host_refs, since it allows us to
 * easily update whichever of input_refs or output_refs is supposed to be
 * in sync with pp_host_refs.
 */
static HOST_LOD **pp_gathering;

static HOST_LOD *input_refs;
static HOST_LOD *output_refs;
static HOST_LOD *pp_host_refs;
static SYMBOL *string_dummy;
static HOST_LOD *host_lod_chain;
static HOST_LOD *host_lod_free_list;

static HOST_REF *pp_add_struct_field_refs (HOST_VAR * var, int *n_refs);
static C_TYPE pp_check (HOST_VAR * var, bool structs_allowed);
static C_TYPE pp_check_builtin_type (LINK * type, bool mode);
static char *pp_expr (HOST_VAR * var);
static char *pp_addr_expr (HOST_VAR * var);
static SYMBOL *pp_find_field (STRUCTDEF * sdef, const char *field);
static void pp_add_dummy_structdef (SYMBOL ** symp, const char *name);

/*
 * pp_gather_input_refs() -
 * return:
 */
void
pp_gather_input_refs (void)
{
  /* Don't do anything if we're already gathering input refs. */
  if (pp_gathering == &output_refs)
    {
      output_refs = pp_host_refs;
      pp_host_refs = input_refs;
      pp_gathering = &input_refs;
    }
}

/*
 * pp_gather_output_refs() -
 * return:
 */
void
pp_gather_output_refs (void)
{
  /* Don't do anything if we're already gathering output refs. */
  if (pp_gathering == &input_refs)
    {
      input_refs = pp_host_refs;
      pp_host_refs = output_refs;
      pp_gathering = &output_refs;
    }
}

/*
 * pp_input_refs() -
 * return:
 */
HOST_LOD *
pp_input_refs (void)
{
  return input_refs;
}

/*
 * pp_output_refs() -
 * return:
 */
HOST_LOD *
pp_output_refs (void)
{
  return output_refs;
}


/*
 * pp_clear_host_refs() - Clears all HOST_LOD structures that have been
 *    allocated since the last call to pp_clear_host_refs(), placing them
 *    on the host_lod_free_list.
 * return : void
 */
void
pp_clear_host_refs (void)
{
  if (host_lod_chain != NULL)
    {
      HOST_LOD *chain, *next;

      /*
       * Run down the chain of allocated HOST_LOD structures, clearing
       * them as we go but leaving the 'next' fields intact.  When we
       * reach the end, just tack the chain on the the free list, point
       * the free list to the head of the chain, and be off.
       */
      next = host_lod_chain;
      do
	{
	  chain = next;
	  pp_clear_host_lod (chain);
	  next = chain->next;
	}
      while (next);

      chain->next = host_lod_free_list;
      host_lod_free_list = host_lod_chain;
      host_lod_chain = NULL;
    }

  input_refs = NULL;
  output_refs = NULL;
  pp_host_refs = NULL;
  pp_gathering = &input_refs;
}

/*
 * pp_new_host_var() - Return a new host variable structure initialized
 *    with the given type. Allocate a new HOST_VAR if var is NULL.
 * return : HOST_VAR *
 * var(in): A pointer to a host_var to be initialized, or NULL.
 * sym(in): The root symbol of the host variable reference.
 */
HOST_VAR *
pp_new_host_var (HOST_VAR * var, SYMBOL * sym)
{
  if (var == NULL)
    {
      var = malloc (sizeof (HOST_VAR));
      if (var == NULL)
	{
	  return NULL;
	}
      var->heap_allocated = 1;
    }
  else
    {
      var->heap_allocated = 0;
    }

  var->type = pp_clone_type (sym->type, &var->etype);
  vs_new (&var->expr);
  vs_strcat (&var->expr, sym->name);
  vs_new (&var->addr_expr);

  return var;
}

/*
 * pp_free_host_var() - Free the host var (if heap allocated) and all of its
 *    internal variables.
 * return : void
 * var(in): The host variable reference to be freed.
 */
void
pp_free_host_var (HOST_VAR * var)
{
  if (var == NULL)
    {
      return;
    }

  if (var->type)
    {
      pp_discard_link_chain (var->type);
    }
  vs_free (&var->expr);
  vs_free (&var->addr_expr);

  if (var->heap_allocated)
    {
      free_and_init (var);
    }

}

/*
 * pp_add_host_ref() - Add the var/indicator pair to the interface table for
 *    the translator.
 * return : a pointer to the HOST_REF if everything is ok, NULL otherwise.
 * var(in): The host variable proper.
 * indicator(in): An optional indicator variable.
 * structs_allowed(in): true if structures are permitted as host vars.
 * n_refs(out): A pointer to an integer to be updated with the number
 *		       of references actually added.
 * note : If structures are allowed, they are expanded. that is, adding
 *    a ref to a structure is equivalent to adding refs to each of its
 *    components in the order in which they were declared.
 */
HOST_REF *
pp_add_host_ref (HOST_VAR * var, HOST_VAR * indicator,
		 bool structs_allowed, int *n_refs)
{
  HOST_REF *ref;
  C_TYPE uci_type;

  if (n_refs != NULL)
    {
      *n_refs = 0;
    }

  if (var == NULL)
    {
      goto bad_news;
    }

  if (indicator != NULL
      && (!(IS_INT (indicator->type) && !IS_LONG (indicator->type))))
    {
      yyverror (pp_get_msg (EX_HOSTVAR_SET, MSG_MUST_BE_SHORT),
		vs_str (&indicator->expr));
      goto bad_news;
    }

  uci_type = pp_check (var, structs_allowed);
  if (uci_type == BAD_C_TYPE)
    {
      goto bad_news;
    }

  if (uci_type == C_TYPE_STRUCT)
    {
      if (indicator)
	{
	  yyverror (pp_get_msg (EX_HOSTVAR_SET, MSG_INDICATOR_NOT_ALLOWED));
	  goto bad_news;
	}

      return pp_add_struct_field_refs (var, n_refs);
    }

  if (pp_host_refs == NULL)
    {
      pp_host_refs = pp_new_host_lod ();
      *pp_gathering = pp_host_refs;
    }

  if (pp_host_refs->n_refs >= pp_host_refs->max_refs)
    {
      HOST_REF *new_refs =
	pp_malloc ((pp_host_refs->max_refs + 4) * sizeof (HOST_REF));
      memset (new_refs, 0, (pp_host_refs->max_refs + 4) * sizeof (HOST_REF));
      if (pp_host_refs->real_refs != NULL)
	{
	  memcpy ((char *) new_refs,
		  (char *) pp_host_refs->real_refs,
		  (size_t) sizeof (HOST_REF) *
		  (size_t) pp_host_refs->max_refs);
	  free_and_init (pp_host_refs->real_refs);
	}
      pp_host_refs->max_refs += 4;
      pp_host_refs->real_refs = new_refs;
    }

  pp_host_refs->refs = pp_host_refs->real_refs;

  ref = &pp_host_refs->refs[pp_host_refs->n_refs++];
  pp_host_refs->n_real_refs++;

  ref->var = var;
  ref->ind = indicator;
  ref->uci_type = uci_type;
  ref->precision_buf = NULL;
  ref->input_size_buf = NULL;
  ref->output_size_buf = NULL;
  ref->expr_buf = NULL;
  ref->addr_expr_buf = NULL;
  ref->ind_expr_buf = NULL;
  ref->ind_addr_expr_buf = NULL;

  if (n_refs != NULL)
    {
      (*n_refs)++;
    }

  return ref;

bad_news:
  if (var != NULL)
    {
      pp_free_host_var (var);
    }
  if (indicator != NULL)
    {
      pp_free_host_var (indicator);
    }
  return NULL;
}

/*
 * pp_add_struct_field_refs() - Add a host ref for each of the fields in
 *    the structure definition, using prefix as the root.
 *    Set *n_refs to 0 or the number of fields added.
 * return : NULL if any of the fields causes a problem, a pointer to the last
 *          field otherwise
 * var: A pointer to a HOST_VAR that is known to represent a struct.
 * n_refs(out) : A pointer to an integer to be updated with the number of
 *               references actually added.
 */
static HOST_REF *
pp_add_struct_field_refs (HOST_VAR * var, int *n_refs)
{
  const char *prefix = vs_str (&var->expr);
  SYMBOL *field = var->type->decl.s.val.v_struct->fields;
  SYMBOL *next = field;
  int errors = 0;
  HOST_REF *result = NULL;

  if (field == NULL)
    {
      yyverror (pp_get_msg (EX_HOSTVAR_SET, MSG_INCOMPLETE_DEF), prefix);
      pp_free_host_var (var);
      return NULL;
    }

  for (; field; field = next)
    {
      HOST_VAR *var;

      /* Make a new host ref from this field, and then add it as a host
         variable. */
      var = pp_new_host_var (NULL, field);
      vs_prepend (&var->expr, ").");
      vs_prepend (&var->expr, prefix);
      vs_prepend (&var->expr, "(");
      result = pp_add_host_ref (var, NULL, false, NULL);
      if (result == NULL)
	{
	  errors++;
	}
      if (n_refs != NULL)
	{
	  (*n_refs)++;
	}

      next = field->next;
    }

  if (errors > 0)
    {
      if (n_refs)
	{
	  *n_refs = 0;
	}
      result = NULL;
    }

  /*
   * We want to free the incoming host var regardless of whether there
   * were errors or not.  If there were errors, it is obviously
   * unneeded.  If there were no errors, it is still unneeded because
   * only (host vars derived from) its components have been used.
   */
  pp_free_host_var (var);

  /*
   * This choice of return value is kind of arbitrary: the important
   * thing is to return some non-NULL pointer so that upper levels
   * won't be deceived into believing that an error occurred.
   */
  return result;
}

/*
 * pp_free_host_ref() - This function doesn't actually free the storage
 *    occupied by *ref, since it assumes that the ref is actually in
 *    the pp_host_refs array.
 * return : void
 * ref(in): The host reference to be freed.
 */
void
pp_free_host_ref (HOST_REF * ref)
{
  pp_free_host_var (ref->var);
  pp_free_host_var (ref->ind);

  vs_free (ref->precision_buf);
  vs_free (ref->input_size_buf);
  vs_free (ref->output_size_buf);
  vs_free (ref->expr_buf);
  vs_free (ref->addr_expr_buf);
  vs_free (ref->ind_expr_buf);
  vs_free (ref->ind_addr_expr_buf);
}

/*
 * pp_copy_host_refs() - Return the current state of pp_host_refs to the
 *    caller. The recipient of the the returned HOST_LOD no longer needs
 *    to worry about freeing it. That will happen automatically the next
 *    time pp_clear_host_refs() is called.
 * return : HOST_LOD *. Also clears pp_host_refs.
 */
HOST_LOD *
pp_copy_host_refs (void)
{
  HOST_LOD *lod = pp_host_refs;

  *pp_gathering = pp_host_refs = NULL;

  return lod;
}

/*
 * pp_detach_host_refs() - Copy the current host refs, and detach it from
 *    the chain of HOST_LODs that will be cleared at the end of compiling
 *    the current statement.  This means that the recipient of the pointer
 *    assumes responsibility for freeing the structure.
 * return : HOST_LOD *
 *
 * note : the recipient of the pointer assumes responsibility for freeing
 *        the structure.
 */
HOST_LOD *
pp_detach_host_refs (void)
{
  HOST_LOD *refs = pp_copy_host_refs ();
  HOST_LOD **link = &host_lod_chain;

  while (*link)
    {
      if (*link == refs)
	{
	  *link = refs->next;
	  refs->next = NULL;
	  return refs;
	}
      link = &((*link)->next);
    }

  return NULL;
}

/*
 * pp_add_host_str() - Allocate a host variable, assign the string as its name,
 *    and give it the special C_TYPE of C_TYPE_STRING_CONST.
 * return : pointer to the HOST_REF if everything is ok, NULL otherwise.
 * str(in) : An SQL/X string to be treated as a constant.
 */
HOST_REF *
pp_add_host_str (char *str)
{
  HOST_VAR *hv;
  HOST_REF *hr;
  varstring *vstr;

  hv = pp_new_host_var (NULL, string_dummy);
  vstr = &hv->expr;
  vs_clear (vstr);

  pp_translate_string (vstr, str, false);

  hr = pp_add_host_ref (hv, NULL, false, NULL);
  if (hr != NULL)
    {
      hr->uci_type = C_TYPE_STRING_CONST;
    }

  return hr;
}

/*
 * pp_check_type() - Check ref to make sure that its type is one of those
 *    encoded in typeset.
 * return : If the ref's type is one of those encoded in typeset,
 *          return the ref. NULL otherwise.
 * ref(in): A pointer to a host reference variable.
 * typeset(in): A set of acceptable C_TYPE codes.
 * msg(in): A message fragment to be used if the type is not acceptable.
 *
 * nore : This assumes that all types can be encoded in the number of bits
 *        provided by a BITSET.
 */
HOST_REF *
pp_check_type (HOST_REF * ref, BITSET typeset, const char *msg)
{
  C_TYPE type;

  if (ref == NULL)
    {
      return NULL;
    }

  type = pp_get_type (ref);
  if (MEMBER (typeset, type))
    {
      return ref;
    }
  else
    {
      /*
       * Copy the silly message; it's probably also coming straight out
       * of the message catalog stuff, and it will get clobbered by our
       * intervening call to pp_get_msg().
       */
      char *msg_copy;
      msg_copy = pp_strdup (msg);
      yyverror (pp_get_msg (EX_HOSTVAR_SET, MSG_NOT_VALID),
		pp_expr (ref->var), msg_copy);
      free_and_init (msg_copy);
      return NULL;
    }
}

/*
 * pp_check_host_var_list() - Check that all members of pp_host_refs have types
 *    acceptable as db parameters.
 * return : void
 */
void
pp_check_host_var_list (void)
{
  int i;

  if (pp_host_refs == NULL)
    {
      return;
    }

  for (i = 0; i < pp_host_refs->n_refs; ++i)
    {
      if (pp_get_type (&pp_host_refs->refs[i]) >= NUM_C_VARIABLE_TYPES)
	{
	  yyverror (pp_get_msg (EX_HOSTVAR_SET, MSG_TYPE_NOT_ACCEPTABLE),
		    pp_type_str (pp_host_refs->refs[i].var->type),
		    pp_get_expr (&pp_host_refs->refs[i]));
	}
    }
}

/*
 * pp_get_type() - Return the CUBRID C_TYPE described by ref's type LINK.
 * return  : C_TYPE
 * ref(in) : A pointer to a host variable reference.
 */
C_TYPE
pp_get_type (HOST_REF * ref)
{
  assert (ref != NULL);

  if (UNINITIALIZED (ref->uci_type))
    {
      ref->uci_type = pp_check (ref->var, 1 /* structs allowed */ );
    }

  return ref->uci_type;

}

/*
 * pp_get_precision() - Return a C expression that will (when evaluated) yield
 *    the nominal precision (in type appropriate units) of the variable
 *    described by the reference.
 * return : size_t
 * ref(in): A pointer to a host variable reference.
 */
char *
pp_get_precision (HOST_REF * ref)
{
  assert (ref != NULL);
  assert (ref->var != NULL);

  if (ref->precision_buf == NULL)
    {
      ref->precision_buf = vs_new (NULL);

      switch (ref->uci_type)
	{
	case C_TYPE_SHORT:
	case C_TYPE_INTEGER:
	case C_TYPE_LONG:
	case C_TYPE_FLOAT:
	case C_TYPE_DOUBLE:
	case C_TYPE_TIME:
	case C_TYPE_TIMESTAMP:
	case C_TYPE_DATE:
	case C_TYPE_MONETARY:
	case C_TYPE_DB_VALUE:
	case C_TYPE_BASICSET:
	case C_TYPE_MULTISET:
	case C_TYPE_SEQUENCE:
	case C_TYPE_COLLECTION:
	case C_TYPE_OBJECTID:
	  /* Precision is meaningless */
	  vs_strcpy (ref->precision_buf, "0");
	  break;

	case C_TYPE_CHAR_POINTER:
	  /*
	   * In this case, this means that we will determine the precision
	   * at runtime by calling strlen().  We could probably emit the
	   * strlen() code here, but it's less expensive in terms of code
	   * space to interpret it at runtime.
	   */
	  vs_strcpy (ref->precision_buf, "0");
	  break;

	case C_TYPE_CHAR_ARRAY:
	  vs_sprintf (ref->precision_buf, "sizeof(%s)-1", pp_get_expr (ref));
	  break;

	case C_TYPE_VARCHAR:
	  vs_sprintf (ref->precision_buf, "sizeof((%s).%s)-1",
		      pp_get_expr (ref), VARCHAR_ARRAY_NAME);
	  break;

	case C_TYPE_VARBIT:
	case C_TYPE_BIT:
	  vs_sprintf (ref->precision_buf, "(%s).%s",
		      pp_get_expr (ref), VARCHAR_LENGTH_NAME);
	  break;

	case C_TYPE_STRING_CONST:
	  vs_sprintf (ref->precision_buf, "sizeof(%s)-1", pp_get_expr (ref));
	  break;

	default:
	  {
	    yyverror (pp_get_msg (EX_HOSTVAR_SET, MSG_UNKNOWN_HV_TYPE),
		      pp_type_str (ref->var->type));
	    vs_strcpy (ref->precision_buf, "0");
	  }
	  break;
	}
    }

  return vs_str (ref->precision_buf);
}

/*
 * pp_get_input_size() - Return a C expression that will (when evaluated)
 *    yield the nominal size (in size_t units) of the variable described by
 *    the reference (when that variable is used as an input variable).
 * return : input size of variable.
 * ref(in): A pointer to a host variable reference.
 */
char *
pp_get_input_size (HOST_REF * ref)
{
  const char *size_str = NULL;

  assert (ref != NULL);
  assert (ref->var != NULL);

  if (ref->input_size_buf == NULL)
    {
      ref->input_size_buf = vs_new (NULL);

      switch (ref->uci_type)
	{
	case C_TYPE_SHORT:
	  size_str = "sizeof(short)";
	  break;
	case C_TYPE_INTEGER:
	  size_str = "sizeof(int)";
	  break;
	case C_TYPE_LONG:
	  size_str = "sizeof(long)";
	  break;
	case C_TYPE_FLOAT:
	  size_str = "sizeof(float)";
	  break;
	case C_TYPE_DOUBLE:
	  size_str = "sizeof(double)";
	  break;
	case C_TYPE_TIME:
	  size_str = "sizeof(DB_TIME)";
	  break;
	case C_TYPE_TIMESTAMP:
	  size_str = "sizeof(DB_TIMESTAMP)";
	  break;
	case C_TYPE_DATE:
	  size_str = "sizeof(DB_DATE)";
	  break;
	case C_TYPE_MONETARY:
	  size_str = "sizeof(DB_MONETARY)";
	  break;
	case C_TYPE_DB_VALUE:
	  size_str = "sizeof(DB_VALUE)";
	  break;
	case C_TYPE_BASICSET:
	  size_str = "sizeof(DB_SET *)";
	  break;
	case C_TYPE_MULTISET:
	  size_str = "sizeof(DB_MULTISET *)";
	  break;
	case C_TYPE_SEQUENCE:
	  size_str = "sizeof(DB_SEQ *)";
	  break;
	case C_TYPE_COLLECTION:
	  size_str = "sizeof(DB_COLLECTION *)";
	  break;
	case C_TYPE_OBJECTID:
	  size_str = "sizeof(DB_OBJECT *)";
	  break;

	case C_TYPE_CHAR_ARRAY:
	case C_TYPE_STRING_CONST:
	  vs_sprintf (ref->input_size_buf, "sizeof(%s)", pp_get_expr (ref));
	  break;

	case C_TYPE_VARCHAR:
	case C_TYPE_VARBIT:
	case C_TYPE_BIT:
	  vs_sprintf (ref->input_size_buf, "(%s).%s", pp_get_expr (ref),
		      VARCHAR_LENGTH_NAME);
	  break;

	case C_TYPE_CHAR_POINTER:
	  vs_sprintf (ref->input_size_buf, "strlen(%s)", pp_get_expr (ref));
	  break;

	default:
	  {
	    yyverror (pp_get_msg (EX_HOSTVAR_SET, MSG_UNKNOWN_HV_TYPE),
		      pp_type_str (ref->var->type));
	    size_str = "0";
	  }
	  break;
	}

      if (size_str != NULL)
	{
	  vs_strcpy (ref->input_size_buf, size_str);
	}
    }

  return vs_str (ref->input_size_buf);
}

/*
 * pp_get_output_size() - Return a C expression that will (when evaluated)
 *    yield the nominal size (in size_t units) of the variable described by
 *    the reference (when that variable is used as an output variable).
 * return : output size of variable
 * ref(in): A pointer to a host variable reference.
 */
char *
pp_get_output_size (HOST_REF * ref)
{
  char *size_str = NULL;

  assert (ref != NULL);
  assert (ref->var != NULL);

  if (ref->uci_type == C_TYPE_CHAR_POINTER)
    {
      if (ref->output_size_buf == NULL)
	{
	  ref->output_size_buf = vs_new (NULL);
	  vs_sprintf (ref->output_size_buf, "strlen(%s)+1",
		      pp_get_expr (ref));
	}
      size_str = vs_str (ref->output_size_buf);
    }
  else if (ref->uci_type == C_TYPE_VARCHAR
	   || ref->uci_type == C_TYPE_VARBIT || ref->uci_type == C_TYPE_BIT)
    {
      if (ref->output_size_buf == NULL)
	{
	  ref->output_size_buf = vs_new (NULL);
	  vs_sprintf (ref->output_size_buf, "sizeof(%s.%s)",
		      pp_get_expr (ref), VARCHAR_ARRAY_NAME);
	}
      size_str = vs_str (ref->output_size_buf);
    }
  else
    {
      size_str = pp_get_input_size (ref);
    }

  return size_str;
}


/*
 * pp_get_expr() - Return a string of C text that can be used in an rvalue
 *    context in emitted codes.
 * return : rvalue string of variable.
 * ref: The given host reference.
 */
char *
pp_get_expr (HOST_REF * ref)
{
  if (ref == NULL)
    {
      return NULL;
    }

  if (ref->expr_buf == NULL)
    {
      ref->expr_buf = vs_new (NULL);
      vs_strcpy (ref->expr_buf, pp_expr (ref->var));
    }

  return vs_str (ref->expr_buf);
}

/*
 * pp_get_addr_expr() - Return an rvalue expression for the location identified
 *    by the given host variable.  For most variables this is simply
 *    &var, i.e., the address of the storage of the variable.  For string
 *    variables, however, this is the address of the string bytes themselves,
 *    i.e., the *value* of the variable.
 * return : rvalue expression.
 * ref(in): The given host reference.
 */
char *
pp_get_addr_expr (HOST_REF * ref)
{
  if (ref == NULL)
    {
      return NULL;
    }

  if (ref->addr_expr_buf == NULL)
    {
      ref->addr_expr_buf = vs_new (NULL);
      vs_strcpy (ref->addr_expr_buf, pp_addr_expr (ref->var));
    }

  return vs_str (ref->addr_expr_buf);
}

/*
 * pp_get_ind_expr() - Return a C expression for the indicator variable
 *    associated with the given host variable, or NULL if there is none.
 * returns/side-effects: C expression.
 * ref(in): The given host variable reference.
 */
char *
pp_get_ind_expr (HOST_REF * ref)
{
  if (ref == NULL || ref->ind == NULL)
    {
      return NULL;
    }

  if (ref->ind_expr_buf == NULL)
    {
      ref->ind_expr_buf = vs_new (NULL);
      vs_strcpy (ref->ind_expr_buf, pp_expr (ref->ind));
    }

  return vs_str (ref->ind_expr_buf);
}

/*
 * pp_get_ind_addr_expr() - Return a C expression giving the address of the
 *    indicator variable associated with the give host variable, or NULL if
 *    there is none.
 * return : C expression
 * ref(in): The given host variable reference.
 */
char *
pp_get_ind_addr_expr (HOST_REF * ref)
{
  if (ref == NULL)
    {
      return NULL;
    }

  if (ref->ind == NULL)
    {
      if (pp_internal_ind)
	{
	  return (char *) "&uci_null_ind";
	}
      else
	{
	  return (char *) "NULL";
	}
    }

  if (ref->ind_addr_expr_buf == NULL)
    {
      ref->ind_addr_expr_buf = vs_new (NULL);
      vs_strcpy (ref->ind_addr_expr_buf, pp_addr_expr (ref->ind));
    }

  return vs_str (ref->ind_addr_expr_buf);

}

/*
 * pp_print_host_ref() -
 * return : void
 * ref(in): The host reference to be printed.
 * fp(in): The stream to print it on.
 */
void
pp_print_host_ref (HOST_REF * ref, FILE * fp)
{
  char *p;

  if (ref->var)
    {
      fputs (pp_get_expr (ref), fp);
      p = pp_get_ind_expr (ref);
      if (p != NULL)
	{
	  fputs (":", fp);
	  fputs (p, fp);
	}
      fprintf (fp, "  (%s)", pp_get_input_size (ref));
    }
}

/*
 * pp_check() - Make sure that 'var' has an acceptable type for a host
 *    variable.
 * return : The esql type code for the type, or -1 if the type is not
 *          permitted.
 * var(in): A variable whose type should be checked.
 * structs_allowed(in): true if structures are permitted.
 */
static C_TYPE
pp_check (HOST_VAR * var, bool structs_allowed)
{
  LINK *type;
  C_TYPE c_type;

  if (var == NULL)
    {
      return BAD_C_TYPE;
    }

  type = var->type;

  if (IS_SPECIFIER (type))
    {
      switch (type->decl.s.noun)
	{
	case N_CHR:
	  break;

	case N_INT:
	  if (type->decl.s.is_unsigned)
	    {
	      break;
	    }
	  else
	    {
	      return type->decl.s.is_long ? C_TYPE_LONG :
		type->decl.s.is_short ? C_TYPE_SHORT : C_TYPE_INTEGER;
	    }

	case N_FLOAT:
	  return type->decl.s.is_long ? C_TYPE_DOUBLE : C_TYPE_FLOAT;

	case N_STRUCTURE:
	  {
	    if (!var->type->decl.s.val.v_struct->type)	/* It's really a union */
	      {
		break;
	      }
	    c_type = pp_check_builtin_type (var->type, false);
	    if (c_type != BAD_C_TYPE)
	      {
		return c_type;
	      }
	    else if (structs_allowed)
	      {
		return C_TYPE_STRUCT;
	      }
	  }
	  break;

	case N_VARCHAR:
	  return C_TYPE_VARCHAR;
	  break;

	case N_BIT:
	  return C_TYPE_BIT;

	case N_VARBIT:
	  return C_TYPE_VARBIT;

	default:
	  break;
	}
    }
  else
    {
      /* IS_DECLARATOR(type) */
      if (IS_POINTER (type))
	{
	  if (IS_CHAR (type->next))
	    {
	      return C_TYPE_CHAR_POINTER;
	    }
	  c_type = pp_check_builtin_type (type->next, true);
	  if (c_type != BAD_C_TYPE)
	    {
	      return c_type;
	    }
	}
      else if (IS_ARRAY (type))
	{
	  if (IS_CHAR (type->next))
	    {
	      return C_TYPE_CHAR_ARRAY;
	    }
	}
    }

  yyverror (pp_get_msg (EX_HOSTVAR_SET, MSG_TYPE_NOT_ACCEPTABLE),
	    pp_type_str (type), vs_str (&var->expr));
  return BAD_C_TYPE;
}

/*
 * pp_check_builtin_type() - Check the type to see whether it corresponds
 *    to some builtin type.
 * return : The C_TYPE corresponding to type or BAD_C_TYPE.
 * type(in): A pointer to a host var known to be a struct of some sort.
 * mode(in): True if the structure has been reached through a pointer.
 */
static C_TYPE
pp_check_builtin_type (LINK * type, bool mode)
{
  unsigned int i;

  for (i = 0; i < DIM (builtin_types); ++i)
    {
      if (pp_the_same_type (type, builtin_types[i].sym->type, 1)
	  && mode == builtin_types[i].mode)
	{
	  return builtin_types[i].c_type;
	}
    }

  return BAD_C_TYPE;
}

/*
 * pp_expr() - Return the text expression that denotes the host variable.
 * return : the text expression
 * var(in): A host variable.
 */
static char *
pp_expr (HOST_VAR * var)
{
  return vs_str (&var->expr);
}

/*
 * pp_addr_expr() - Return a C expression for the location identified by the
 *   given host variable.  For most variables this is simply &var, i.e.,
 *   the address of the storage of the variable. For string variables,
 *   however, this is the address of the string bytes themselves.
 * return : C expression.
 * var(in): A host variable.
 */
static char *
pp_addr_expr (HOST_VAR * var)
{
  char *result;

  if (var == NULL)
    {
      yyverror (pp_get_msg (EX_HOSTVAR_SET, MSG_BAD_ADDRESS));
      result = (char *) "NULL";
    }
  else if (IS_PTR_TYPE (var->type) && IS_CHAR (var->type->next))
    {
      result = pp_expr (var);
    }
  else
    {
      vs_clear (&var->addr_expr);
      if (IS_PSEUDO_TYPE (var->type))
	{
	  vs_sprintf (&var->addr_expr, "(%s).%s", pp_expr (var),
		      VARCHAR_ARRAY_NAME);
	}
      else
	{
	  vs_sprintf (&var->addr_expr, "&(%s)", pp_expr (var));
	}
      result = vs_str (&var->addr_expr);
    }

  return result;
}

/*
 * pp_ptr_deref() - Change the value of var->type to the new type obtained by
 *    dereferencing the existing type.
 * return : The incoming host_var.
 * var(in): A host variable to be deref'ed.
 * style(in): 0 if through '*', 1 if through '[expr]'.
 */
HOST_VAR *
pp_ptr_deref (HOST_VAR * var, int style)
{
  LINK *result_type;

  if (var == NULL)
    {
      return NULL;
    }

  if (!IS_PTR_TYPE (var->type))
    {
      yyverror (pp_get_msg (EX_HOSTVAR_SET, MSG_DEREF_NOT_ALLOWED),
		vs_str (&var->expr), style ? "[]" : "*");
      return NULL;
    }

  result_type = var->type->next;
  pp_discard_link (var->type);
  var->type = result_type;

  return var;
}

/*
 * pp_struct_deref() - Perform a struct field dereference
 *    on the incoming host var.
 * return : The incoming HOST_VAR.
 * var(in): A host variable to be deref'ed.
 * field(in): The name of the field to be extracted.
 * indirect(in): 0 if through '.', 1 if through '->'.
 */
HOST_VAR *
pp_struct_deref (HOST_VAR * var, char *field, int indirect)
{
  SYMBOL *field_def;

  if (var == NULL)
    {
      return NULL;
    }

  if (indirect)
    {
      LINK *base_type;
      if (!IS_PTR_TYPE (var->type))
	{
	  yyverror (pp_get_msg (EX_HOSTVAR_SET, MSG_NOT_POINTER),
		    vs_str (&var->expr));
	  return NULL;
	}
      base_type = var->type->next;
      pp_discard_link (var->type);
      var->type = base_type;
    }

  if (!IS_STRUCT (var->type) && !IS_PSEUDO_TYPE (var->type))
    {
      yyverror (pp_get_msg (EX_HOSTVAR_SET,
			    indirect ? MSG_NOT_POINTER_TO_STRUCT :
			    MSG_NOT_STRUCT), vs_str (&var->expr));
      return NULL;
    }

  field_def = pp_find_field (var->type->decl.s.val.v_struct, field);
  if (field_def == NULL)
    {
      yyverror (pp_get_msg (EX_HOSTVAR_SET, MSG_NO_FIELD),
		var->type->decl.s.val.v_struct->type_string, field);
      return NULL;
    }

  pp_discard_link_chain (var->type);
  var->type = pp_clone_type (field_def->type, &var->etype);

  return var;
}

/*
 * pp_addr_of() - Alter the type of var to be a pointer to the incoming type.
 * return : HOST_VAR *
 * var(in): The host variable whose address is to be taken.
 */
HOST_VAR *
pp_addr_of (HOST_VAR * var)
{
  LINK *new_link;

  if (var == NULL)
    {
      return NULL;
    }

  new_link = pp_new_link ();
  new_link->decl.d.dcl_type = D_POINTER;
  new_link->next = var->type;
  var->type = new_link;

  return var;
}

/*
 * pp_find_field() - Return the type of the named field in the given struct
 *    definition, or NULL if not present.
 * return : the type of named field
 * sdef(in): The struct to searched.
 * field(in): The field to be searched for.
 */
static SYMBOL *
pp_find_field (STRUCTDEF * sdef, const char *field)
{
  SYMBOL *sym;
  for (sym = sdef->fields; sym; sym = sym->next)
    {
      if (STREQ (field, sym->name))
	{
	  return sym;
	}
    }

  return NULL;
}

/*
 * pp_add_dummy_structdef() - Add entries to the struct and symbol tables
 *    that would arise if we had encountered
 *           typedef struct <name> <name>;
 *    This adds an empty structdef for name, which is all we really
 *    care about, and allows esql programmers to declare DB_VALUE
 *    and CUBRIDDA types without any special effort.
 * return : void
 * symp(out): Address of a SYMBOL pointer to be initialized with the dummy
 *	     symbol that we create.
 * name(in): The name to be entered in the struct and symbol tables.
 */
static void
pp_add_dummy_structdef (SYMBOL ** symp, const char *name)
{
  STRUCTDEF *dummy_struct;
  SYMBOL *dummy_symbol;

  dummy_symbol = pp_new_symbol (name, 0);
  dummy_struct = pp_new_structdef (name);
  pp_reset_current_type_spec ();
  pp_add_storage_class (TYPEDEF_);
  pp_add_struct_spec (dummy_struct);
  pp_add_spec_to_decl (pp_current_type_spec (), dummy_symbol);
  pp_discard_link (pp_current_type_spec ());
  pp_Struct_table->add_symbol (pp_Struct_table, dummy_struct);
  pp_Symbol_table->add_symbol (pp_Symbol_table, dummy_symbol);

  *symp = dummy_symbol;
}

/*
 * pp_hv_init() - Initialize various static module data.
 *
 * arguments:
 *
 * returns/side-effects: nothing
 *
 * note : Make sure that all symbol table stuff is initialized before calling
 *    this, because it is going to try to deposit a typedef in the outermost
 *    level of the symbol table.
 */
void
pp_hv_init (void)
{
  unsigned int i;
  SYMBOL *db_indicator;
  SYMBOL *db_int32;

  host_lod_chain = NULL;
  host_lod_free_list = NULL;
  input_refs = NULL;
  output_refs = NULL;
  pp_host_refs = NULL;
  pp_gathering = &input_refs;
  pp_clear_host_refs ();

  string_dummy = pp_new_symbol (NULL, 0);
  pp_reset_current_type_spec ();
  pp_add_type_noun (CHAR_);
  pp_add_declarator (string_dummy, D_ARRAY);
  pp_add_spec_to_decl (pp_current_type_spec (), string_dummy);
  pp_discard_link (pp_current_type_spec ());

  /* This assumes that DB_INDICATOR is tyepdef'ed to short.  Change
     this code when that assumption becomes invalid. */
  db_indicator = pp_new_symbol ("DB_INDICATOR", 0);
  pp_reset_current_type_spec ();
  pp_add_storage_class (TYPEDEF_);
  pp_add_type_noun (INT_);
  pp_add_type_adj (SHORT_);
  pp_add_spec_to_decl (pp_current_type_spec (), db_indicator);
  pp_discard_link (pp_current_type_spec ());
  pp_add_symbols_to_table (db_indicator);

  /* Make sure that this one corresponds to the typedef in dbport.h. */
  db_int32 = pp_new_symbol ("int", 0);
  pp_reset_current_type_spec ();
  pp_add_storage_class (TYPEDEF_);
  pp_add_type_noun (INT_);
  if (sizeof (int) < 4)
    {
      pp_add_type_adj (LONG_);
    }
  pp_add_spec_to_decl (pp_current_type_spec (), db_int32);
  pp_discard_link (pp_current_type_spec ());
  pp_add_symbols_to_table (db_int32);

  for (i = 0; i < DIM (builtin_types); ++i)
    {
      pp_add_dummy_structdef (&builtin_types[i].sym, builtin_types[i].name);
    }
}

/*
 * pp_hv_finish() - Tear down various static module data.
 * return : void
 *
 * note : The builtin struct definitions will be removed automatically when
 *   the symbol and struct tables are torn down.
 */
void
pp_hv_finish (void)
{
  while (host_lod_chain)
    {
      HOST_LOD *next = host_lod_chain->next;
      pp_free_host_lod (host_lod_chain);
      host_lod_chain = next;
    }
  while (host_lod_free_list)
    {
      HOST_LOD *next = host_lod_free_list->next;
      pp_free_host_lod (host_lod_free_list);
      host_lod_free_list = next;
    }

  pp_discard_symbol (string_dummy);
}

/*
 * pp_new_host_lod() - Allocate and initialize a new HOST_LOD. Keeps all
 *    allocated lods in a chain for easier cleanup later.
 * return : HOST_LOD *
 */
HOST_LOD *
pp_new_host_lod (void)
{
  HOST_LOD *lod = host_lod_free_list;

  if (lod == NULL)
    {
      lod = malloc (sizeof (HOST_LOD));
      if (lod == NULL)
	{
	  return NULL;
	}
      lod->real_refs = NULL;
    }
  else
    {
      host_lod_free_list = lod->next;
    }

  lod->desc = NULL;
  lod->n_refs = 0;
  lod->refs = NULL;
  lod->max_refs = 0;
  lod->n_real_refs = 0;
  lod->next = host_lod_chain;

  host_lod_chain = lod;

  return lod;
}

/*
 * pp_free_host_lod() - Free the indicated HOST_LOD and all HOST_VARs
 *    it references.
 * return : nothing
 * lod : A pointer to a HOST_LOD to be deallocated.
 */
void
pp_free_host_lod (HOST_LOD * lod)
{
  if (lod == NULL)
    {
      return;
    }

  pp_clear_host_lod (lod);
  if (lod->real_refs != NULL)
    {
      free_and_init (lod->real_refs);
    }

  free_and_init (lod);
}

/*
 * pp_clear_host_lod() - Clear pp_buf and pp_host_refs in preparation for
 *    a new statement.
 * return : void
 */
void
pp_clear_host_lod (HOST_LOD * lod)
{
  int i;

  if (lod == NULL)
    {
      return;
    }

  /*
   * *DON'T* free the character string pointed to by desc.  It is
   * assumed to point to the same string that some host_ref in
   * real_refs points to.
   */
  for (i = 0; i < lod->n_real_refs; ++i)
    {
      pp_free_host_ref (&lod->real_refs[i]);
    }

  lod->desc = NULL;
  lod->n_refs = 0;
  lod->refs = NULL;
  lod->n_real_refs = 0;

}

/*
 * pp_switch_to_descriptor() - Switch pp_host_ref's mode into descriptor mode.
 * return : NULL if there is a problem, the name of the descriptor otherwise.
 */
char *
pp_switch_to_descriptor (void)
{
  assert (pp_host_refs != NULL);
  assert (pp_host_refs->n_refs == 1);
  assert (pp_host_refs->refs != NULL);

  if (pp_host_refs == NULL
      || pp_host_refs->n_refs != 1 || pp_host_refs->refs == NULL)
    {
      return NULL;
    }

  pp_host_refs->desc = pp_get_expr (&pp_host_refs->refs[0]);
  pp_host_refs->n_refs = 0;
  pp_host_refs->refs = NULL;

  return pp_host_refs->desc;
}

/*
 * pp_translate_string() - Translate the string into another that will,
 *    when processed by a C compiler, yield the original string.  This
 *    means being careful about things like single and double quotes and
 *    backslashes. If the string is being embedded within another C string
 *   (e.g., the string is a literal within some SQL/X command that is being
 *   prepared into a C string) we need to do some things a little differently
 *   (e.g., make sure we escape the enclosing double quotes of
 *   a C-style string).
 * return : void
 * vstr(out) : A varstring to receive the translated string.
 * str(in) : An input string, following either C or SQL/X punctuation rules.
 * in_string(in) : true iff the translated string is being embedded within a C
 *	       string. false otherwise.
 */
void
pp_translate_string (varstring * vstr, const char *str, int in_string)
{
  unsigned const char *p = (unsigned const char *) str;

  if (vstr == NULL || p == NULL)
    {
      return;
    }

  if (*p == '"')
    {
      vs_strcat (vstr, in_string ? "\\\"" : "\"");

      for (++p; *p != '"'; ++p)
	{
	  switch (*p)
	    {
	    case '\\':
	      switch (*++p)
		{
		case '"':
		  vs_strcat (vstr, "\\\\\\\"");
		  break;
		case '\n':
		  break;
		default:
		  vs_strcat (vstr, "\\\\");
		  vs_putc (vstr, *p);
		  break;
		}
	      break;

	    default:
	      vs_putc (vstr, *p);
	      break;
	    }
	}

      vs_strcat (vstr, in_string ? "\\\"" : "\"");
    }
  else
    {
      vs_putc (vstr, in_string ? '\'' : '"');

      for (++p; *p; ++p)
	{
	  switch (*p)
	    {
	    case '\\':
	      if (*(p + 1) == '\n')
		{
		  ++p;
		}
	      else
		{
		  vs_strcat (vstr, "\\\\");
		}
	      break;

	    case '\'':
	      if (*(p + 1) == '\'')
		{
		  ++p;
		  if (in_string)
		    {
		      vs_putc (vstr, '\'');
		    }
		  vs_putc (vstr, '\'');
		}
	      break;

	    case '"':
	      vs_strcat (vstr, "\\\"");
	      break;

	    default:
	      vs_putc (vstr, *p);
	      break;
	    }
	}

      vs_putc (vstr, in_string ? '\'' : '"');
    }
}
