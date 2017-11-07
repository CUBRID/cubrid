#include "object_print_util.hpp"
#include "dbdef.h"
#include "work_space.h"
#include <assert.h>

/*
 * obj_print_free_strarray() -  Most of the help functions build an array of
 *                              strings that contains the descriptions
 *                              of the object
 *      return: none
 *  strs(in) : array of strings
 *
 *  Note :
 *      This function frees the array when it is no longer necessary.
 */
void object_print::free_strarray (char **strs)
{
  int i;

  if (strs == NULL)
    {
      return;
    }
  for (i = 0; strs[i] != NULL; i++)
    {
      free_and_init (strs[i]);
    }
  free_and_init (strs);
}

/*
 * obj_print_copy_string() - Copies a string, allocating space with malloc
 *      return: new string
 *  source(in) : string to copy
 */
char *object_print::copy_string (const char *source)
{
  char *new_str = NULL;

  if (source != NULL)
    {
      new_str = (char *) malloc (strlen (source) + 1);
      if (new_str != NULL)
	{
	  strcpy (new_str, source);
	}
    }
  return new_str;
}
