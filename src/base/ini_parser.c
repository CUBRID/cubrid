/*
 * Copyright (c) 2000-2007 by Nicolas Devillard.
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#include "porting.h"
#include "ini_parser.h"
#include "chartype.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define INI_BUFSIZ         	(512)
#define INI_INVALID_KEY		((char*)-1)

enum ini_line_status
{
  LINE_UNPROCESSED,
  LINE_ERROR,
  LINE_EMPTY,
  LINE_COMMENT,
  LINE_SECTION,
  LINE_VALUE
};
typedef enum ini_line_status INI_LINE_STATUS;

static void *ini_dblalloc (void *ptr, int size);
static unsigned int ini_table_hash (char *key);
static INI_TABLE *ini_table_new (int size);
static void ini_table_free (INI_TABLE * vd);
static const char *ini_table_get (INI_TABLE * ini, char *key, const char *def, int *lineno);
static int ini_table_set (INI_TABLE * vd, char *key, char *val, int lineno);
#if defined (ENABLE_UNUSED_FUNCTION)
static void ini_table_unset (INI_TABLE * ini, char *key);
#endif /* ENABLE_UNUSED_FUNCTION */
static char *ini_str_lower (const char *s);
static char *ini_str_trim (char *s);
static INI_LINE_STATUS ini_parse_line (char *input_line, char *section, char *key, char *value);
static const char *ini_get_internal (INI_TABLE * ini, const char *key, const char *def, int *lineno);

/*
 * ini_dblalloc() - Doubles the allocated size associated to a pointer
 *   return: new allocated pointer
 *   p(in): pointer
 *   size(in): current allocated size
 */
static void *
ini_dblalloc (void *p, int size)
{
  void *newp;

  newp = calloc (2 * size, 1);
  if (newp == NULL)
    {
      return NULL;
    }
  memcpy (newp, p, size);
  free (p);

  return newp;
}

/*
 * ini_table_hash() - Compute the hash key for a string
 *   return: hasn value
 *   key(in): Character string to use for key
 *   size(in): current allocated size
 *
 * Note: This hash function has been taken from an Article in Dr Dobbs Journal.
 *       This is normally a collision-free function, distributing keys evenly.
 *       The key is stored anyway in the struct so that collision can be avoided
 *       by comparing the key itself in last resort.
 */
static unsigned int
ini_table_hash (char *key)
{
  size_t len;
  size_t i;
  unsigned int hash;

  len = strlen (key);
  for (hash = 0, i = 0; i < len; i++)
    {
      hash += (unsigned) key[i];
      hash += (hash << 10);
      hash ^= (hash >> 6);
    }
  hash += (hash << 3);
  hash ^= (hash >> 11);
  hash += (hash << 15);
  return hash;
}

/*
 * ini_table_new() - Create a new INI_TABLE object
 *   return: newly allocated INI_TABLE objet
 *   size(in): Optional initial size of the INI_TABLE
 *
 * Note: If you do not know in advance (roughly) the number of entries
 *       in the INI_TABLE, give size=0.
 */
static INI_TABLE *
ini_table_new (int size)
{
  INI_TABLE *ini;

  /* If no size was specified, allocate space for 128 */
  if (size < 128)
    {
      size = 128;
    }

  ini = (INI_TABLE *) calloc (1, sizeof (INI_TABLE));
  if (ini == NULL)
    {
      return NULL;
    }
  ini->size = size;
  ini->val = (char **) calloc (size, sizeof (char *));
  if (ini->val == NULL)
    {
      goto error;
    }
  ini->key = (char **) calloc (size, sizeof (char *));
  if (ini->key == NULL)
    {
      goto error;
    }
  ini->lineno = (int *) calloc (size, sizeof (int));
  if (ini->lineno == NULL)
    {
      goto error;
    }
  ini->hash = (unsigned int *) calloc (size, sizeof (unsigned int));
  if (ini->hash == NULL)
    {
      goto error;
    }

  return ini;

error:
  if (ini->hash != NULL)
    {
      free (ini->hash);
    }
  if (ini->lineno != NULL)
    {
      free (ini->lineno);
    }
  if (ini->key != NULL)
    {
      free (ini->key);
    }
  if (ini->val != NULL)
    {
      free (ini->val);
    }
  if (ini != NULL)
    {
      free (ini);
    }
  return NULL;
}

/*
 * ini_table_free() - Delete a INI_TABLE object
 *   return: void
 *   ini(in): INI_TABLE object to deallocate
 *
 * Note:
 */
static void
ini_table_free (INI_TABLE * ini)
{
  int i;

  if (ini == NULL)
    {
      return;
    }
  for (i = 0; i < ini->size; i++)
    {
      if (ini->key[i] != NULL)
	{
	  free (ini->key[i]);
	}
      if (ini->val[i] != NULL)
	{
	  free (ini->val[i]);
	}
    }
  free (ini->val);
  ini->val = NULL;
  free (ini->key);
  ini->key = NULL;
  free (ini->lineno);
  ini->lineno = NULL;
  free (ini->hash);
  ini->hash = NULL;
  free (ini);
  ini = NULL;
  return;
}

/*
 * ini_table_get() - Get a value from a INI_TABLE
 *   return: pointer to internally allocated character string
 *   ini(in): INI_TABLE object to search
 *   key(in): Key to look for in the INI_TABLE
 *   def(in): Default value to return if key not found
 *
 * Note: The returned character pointer points to data internal to the
 *       INI_TABLE object, you should not try to free it or modify it
 */
static const char *
ini_table_get (INI_TABLE * ini, char *key, const char *def, int *lineno)
{
  unsigned int hash;
  int i;

  hash = ini_table_hash (key);
  for (i = 0; i < ini->size; i++)
    {
      if (ini->key[i] == NULL)
	{
	  continue;
	}
      /* Compare hash */
      if (hash == ini->hash[i])
	{
	  /* Compare string, to avoid hash collisions */
	  if (!strcmp (key, ini->key[i]))
	    {
	      if (lineno)
		{
		  *lineno = ini->lineno[i];
		}
	      return ini->val[i];
	    }
	}
    }
  return def;
}

/*
 * ini_table_set() - Set a value in a INI_TABLE
 *   return: 0 if Ok, anything else otherwise
 *   ini(in): INI_TABLE object to modify
 *   key(in): Key to modify or add
 *   val(in): Value to add
 *   lineno(in): line number in ini file
 *
 * Note:
 */
static int
ini_table_set (INI_TABLE * ini, char *key, char *val, int lineno)
{
  int i;
  unsigned int hash;

  if (ini == NULL || key == NULL)
    {
      return -1;
    }

  /* Compute hash for this key */
  hash = ini_table_hash (key);
  /* Find if value is already in INI_TABLE */
  if (ini->n > 0)
    {
      for (i = 0; i < ini->size; i++)
	{
	  if (ini->key[i] == NULL)
	    {
	      continue;
	    }
	  if (hash == ini->hash[i])
	    {			/* Same hash value */
	      if (!strcmp (key, ini->key[i]))
		{		/* Same key */
		  /* Found a value: modify and return */
		  if (ini->val[i] != NULL)
		    {
		      free (ini->val[i]);
		    }
		  ini->val[i] = val ? strdup (val) : NULL;
		  /* Value has been modified: return */
		  return 0;
		}
	    }
	}
    }
  /* Add a new value */
  /* See if INI_TABLE needs to grow */
  if (ini->n == ini->size)
    {

      /* Reached maximum size: reallocate INI_TABLE */
      ini->val = (char **) ini_dblalloc (ini->val, ini->size * sizeof (char *));
      ini->key = (char **) ini_dblalloc (ini->key, ini->size * sizeof (char *));
      ini->lineno = (int *) ini_dblalloc (ini->lineno, ini->size * sizeof (int));
      ini->hash = (unsigned int *) ini_dblalloc (ini->hash, ini->size * sizeof (unsigned int));
      if ((ini->val == NULL) || (ini->key == NULL) || (ini->lineno == NULL) || (ini->hash == NULL))
	{
	  /* Cannot grow INI_TABLE */
	  return -1;
	}
      /* Double size */
      ini->size *= 2;
    }

  /* Insert key in the first empty slot */
  for (i = 0; i < ini->size; i++)
    {
      if (ini->key[i] == NULL)
	{
	  /* Add key here */
	  break;
	}
    }
  ini->n++;
  ini->lineno[i] = lineno;
  ini->hash[i] = hash;
  ini->key[i] = strdup (key);
  if (val == NULL)
    {
      ini->nsec++;		/* section */
      ini->val[i] = NULL;
    }
  else
    {
      ini->val[i] = strdup (val);
    }
  return 0;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * ini_table_unset() - Delete a key in a INI_TABLE
 *   return: void
 *   ini(in): INI_TABLE object to modify
 *   key(in): Key to remove
 *
 * Note:
 */
static void
ini_table_unset (INI_TABLE * ini, char *key)
{
  unsigned int i, hash;

  if (key == NULL)
    {
      return;
    }

  hash = ini_table_hash (key);
  for (i = 0; i < ini->size; i++)
    {
      if (ini->key[i] == NULL)
	{
	  continue;
	}
      /* Compare hash */
      if (hash == ini->hash[i])
	{
	  /* Compare string, to avoid hash collisions */
	  if (!strcmp (key, ini->key[i]))
	    {
	      /* Found key */
	      break;
	    }
	}
    }
  if (i >= ini->size)
    {
      /* Key not found */
      return;
    }

  free (ini->key[i]);
  ini->key[i] = NULL;
  if (ini->val[i] != NULL)
    {
      free (ini->val[i]);
      ini->val[i] = NULL;
    }
  ini->lineno[i] = EOF;
  ini->hash[i] = 0;
  ini->n--;
  return;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * ini_str_lower() - Convert a string to lowercase
 *   return: statically allocated string
 *   s(in): String to convert
 *
 * Note: not re-entrant
 */
static char *
ini_str_lower (const char *s)
{
  static char result[INI_BUFSIZ + 1];
  int i;

  if (s == NULL)
    {
      return NULL;
    }
  memset (result, 0, INI_BUFSIZ + 1);
  i = 0;
  while (s[i] && i < INI_BUFSIZ)
    {
      result[i] = (char) char_tolower ((int) s[i]);
      i++;
    }
  result[INI_BUFSIZ] = (char) 0;
  return result;
}

/*
 * ini_str_trim() - Remove blanks at the beginning and the end of a string
 *   return: statically allocated string
 *   s(in): String to strip
 *
 * Note: not re-entrant
 */
static char *
ini_str_trim (char *s)
{
  static char result[INI_BUFSIZ + 1];
  char *last;

  if (s == NULL)
    {
      return NULL;
    }

  while (char_isspace ((int) *s) && *s)
    {
      s++;
    }
  memset (result, 0, INI_BUFSIZ + 1);
  strcpy (result, s);
  last = result + strlen (result);
  while (last > result)
    {
      if (!char_isspace ((int) *(last - 1)))
	{
	  break;
	}
      last--;
    }
  *last = (char) 0;
  return result;
}

/*
 * ini_parse_line() - Load a single line from an INI file
 *   return: line status
 *   input_line(in): input line
 *   section(out): section name
 *   key(out): key name
 *   value(out): value
 *
 * Note:
 */
static INI_LINE_STATUS
ini_parse_line (char *input_line, char *section, char *key, char *value)
{
  INI_LINE_STATUS status;
  char line[INI_BUFSIZ + 1];
  int len;

  strcpy (line, ini_str_trim (input_line));
  len = (int) strlen (line);

  status = LINE_UNPROCESSED;
  if (len < 1)
    {
      /* Empty line */
      status = LINE_EMPTY;
    }
  else if (line[0] == '#')
    {
      /* Comment line */
      status = LINE_COMMENT;
    }
  else if (line[0] == '[' && line[len - 1] == ']')
    {
      /* Section name */
      char leading_char;

      sscanf (line, "[%[^]]", section);
      strcpy (section, ini_str_trim (section));
      leading_char = section[0];
      if (leading_char == '@' || leading_char == '%')
	{
	  sprintf (section, "%c%s", leading_char, ini_str_trim (section + 1));
	}

      if (leading_char != '@')
	{
	  strcpy (section, ini_str_lower (section));
	}
      status = LINE_SECTION;
    }
  else if (sscanf (line, "%[^=] = \"%[^\"]\"", key, value) == 2 || sscanf (line, "%[^=] = '%[^\']'", key, value) == 2
	   || sscanf (line, "%[^=] = %[^;#]", key, value) == 2)
    {
      /* Usual key=value, with or without comments */
      strcpy (key, ini_str_trim (key));
      strcpy (key, ini_str_lower (key));
      strcpy (value, ini_str_trim (value));
      /*
       * sscanf cannot handle '' or "" as empty values
       * this is done here
       */
      if (!strcmp (value, "\"\"") || (!strcmp (value, "''")))
	{
	  value[0] = 0;
	}
      status = LINE_VALUE;
    }
  else if (sscanf (line, "%[^=] = %[;#]", key, value) == 2 || sscanf (line, "%[^=] %[=]", key, value) == 2)
    {
      /*
       * Special cases:
       * key=
       * key=;
       * key=#
       */
      strcpy (key, ini_str_trim (key));
      strcpy (key, ini_str_lower (key));
      value[0] = 0;
      status = LINE_VALUE;
    }
  else
    {
      /* Generate syntax error */
      status = LINE_ERROR;
    }
  return status;
}

/*
 * ini_parser_load() - Parse an ini file
 *   return: Pointer to newly allocated INI_TABLE
 *   ininame(in): ini file to read
 *
 * Note: The returned INI_TABLE must be freed using ini_parser_free()
 */
INI_TABLE *
ini_parser_load (const char *ininame)
{
  FILE *in;

  char line[INI_BUFSIZ + 1];
  char section[INI_BUFSIZ + 1];
  char key[INI_BUFSIZ + 1];
  char tmp[(INI_BUFSIZ + 1) * 2];
  char val[INI_BUFSIZ + 1];

  int last = 0;
  int len;
  int lineno = 0;
  int errs = 0;

  INI_TABLE *ini;

  if ((in = fopen (ininame, "r")) == NULL)
    {
      fprintf (stderr, "ini_parser: cannot open %s\n", ininame);
      return NULL;
    }

  ini = ini_table_new (0);
  if (!ini)
    {
      fclose (in);
      return NULL;
    }

  memset (line, 0, INI_BUFSIZ + 1);
  memset (section, 0, INI_BUFSIZ + 1);
  memset (key, 0, INI_BUFSIZ + 1);
  memset (val, 0, INI_BUFSIZ + 1);
  last = 0;

  while (fgets (line + last, INI_BUFSIZ - last, in) != NULL)
    {
      lineno++;
      len = (int) strlen (line) - 1;
      /* Safety check against buffer overflows */
      if (line[len] != '\n' && len >= INI_BUFSIZ - 2)
	{
	  fprintf (stderr, "ini_parser: input line too long in %s (%d)\n", ininame, lineno);
	  ini_table_free (ini);
	  fclose (in);
	  return NULL;
	}
      /* Get rid of \n and spaces at end of line */
      while ((len > 0) && ((line[len] == '\n') || (char_isspace (line[len]))))
	{
	  line[len] = 0;
	  len--;
	}
      /* Detect multi-line */
      if (line[len] == '\\')
	{
	  /* Multi-line value */
	  last = len;
	  continue;
	}
      else
	{
	  last = 0;
	}
      switch (ini_parse_line (line, section, key, val))
	{
	case LINE_EMPTY:
	case LINE_COMMENT:
	  break;

	case LINE_SECTION:
	  errs = ini_table_set (ini, section, NULL, lineno);
	  break;

	case LINE_VALUE:
	  sprintf (tmp, "%s:%s", section, key);
	  errs = ini_table_set (ini, tmp, val, lineno);
	  break;

	case LINE_ERROR:
	  fprintf (stderr, "ini_parser: syntax error in %s (%d):\n", ininame, lineno);
	  fprintf (stderr, "-> %s\n", line);
	  errs++;
	  break;

	default:
	  break;
	}
      memset (line, 0, INI_BUFSIZ);
      last = 0;
      if (errs < 0)
	{
	  fprintf (stderr, "ini_parser: memory allocation failure\n");
	  break;
	}
    }
  if (errs)
    {
      ini_table_free (ini);
      ini = NULL;
    }
  fclose (in);
  return ini;
}

/*
 * ini_parser_free() - Free all memory associated to an ini INI_TABLE
 *   return: void
 *   ini(in): Dictionary to free
 *
 * Note:
 */
void
ini_parser_free (INI_TABLE * ini)
{
  ini_table_free (ini);
}

/*
 * ini_findsec() - Find name for section n in a INI_TABLE
 *   return: true or false
 *   ini(in): INI_TABLE to examine
 *   sec(in): section name to search
 *
 * Note:
 */
int
ini_findsec (INI_TABLE * ini, const char *sec)
{
  int i;

  if (ini == NULL || sec == NULL)
    {
      return 0;
    }

  for (i = 0; i < ini->size; i++)
    {
      if (ini->key[i] == NULL)
	{
	  continue;
	}
      if (strcasecmp (ini->key[i], sec) == 0)
	{
	  return 1;
	}
    }

  return 0;
}

/*
 * ini_getsecname() - Get name for section n in a INI_TABLE
 *   return: name of section or NULL if fail
 *   ini(in): INI_TABLE to examine
 *
 * Note:
 */
char *
ini_getsecname (INI_TABLE * ini, int n, int *lineno)
{
  int i, foundsec;

  if (ini == NULL || n < 0)
    {
      return NULL;
    }
  foundsec = 0;
  for (i = 0; i < ini->size; i++)
    {
      if (ini->key[i] == NULL)
	{
	  continue;
	}
      if (strchr (ini->key[i], ':') == NULL)
	{
	  foundsec++;
	  if (foundsec > n)
	    {
	      break;
	    }
	}
    }
  if (foundsec <= n)
    {
      return NULL;
    }
  if (lineno != NULL && ini->lineno != NULL)
    {
      *lineno = ini->lineno[i];
    }
  return ini->key[i];
}

/*
 * ini_hassec() - Check key string has section
 *   return: 1 true, or 0 false
 *   key(in): key to examine
 *
 * Note:
 */
int
ini_hassec (const char *key)
{
  return (key[0] != ':');
}

/*
 * ini_seccmp() - compare two key has same section
 *   return: 0 if not equal, or return section length
 *   key1(in): key to examine
 *   key2(in): key to examine
 *
 * Note:
 */
int
ini_seccmp (const char *key1, const char *key2, bool ignore_case)
{
  const char *s1 = strchr (key1, ':');
  const char *s2 = strchr (key2, ':');
  int key1_sec_len, key2_sec_len;

  if (s1)
    {
      key1_sec_len = CAST_STRLEN (s1 - key1);
    }
  else
    {
      key1_sec_len = (int) strlen (key1);
    }

  if (s2)
    {
      key2_sec_len = CAST_STRLEN (s2 - key2);
    }
  else
    {
      key2_sec_len = (int) strlen (key2);
    }

  if (key1_sec_len != key2_sec_len)
    {
      return 0;
    }

  if (ignore_case && strncasecmp (key1, key2, key1_sec_len) == 0)
    {
      return key1_sec_len;
    }

  if (ignore_case == false && strncmp (key1, key2, key1_sec_len) == 0)
    {
      return key1_sec_len;
    }

  return 0;
}

/*
 * ini_get_internal() - Get the string associated to a key
 *   return: pointer to statically allocated character string
 *   ini(in): INI_TABLE to search
 *   key(in): key to look for
 *   def(in): default value to return if key not found
 *   lineno(out): line number
 *
 * Note: A key as read from an ini file is given as "section:key"
 *       If the key cannot be found, the pointer passed as 'def' is returned
 *       do not free or modify returned pointer
 */
static const char *
ini_get_internal (INI_TABLE * ini, const char *key, const char *def, int *lineno)
{
  char *lc_key;
  const char *sval;

  if (ini == NULL || key == NULL)
    {
      return def;
    }

  lc_key = ini_str_lower (key);
  sval = ini_table_get (ini, lc_key, def, lineno);
  return sval;
}

/*
 * ini_getstr() - Get the string associated to a key
 *   return: pointer to statically allocated character string
 *   ini(in): INI_TABLE to search
 *   sec(in): section to look for
 *   key(in): key to look for
 *   def(in): default value to return if key not found
 *   lineno(out): line number
 *
 * Note: A key as read from an ini file is given as "key"
 *       If the key cannot be found, the pointer passed as 'def' is returned
 *       do not free or modify returned pointer
 */
const char *
ini_getstr (INI_TABLE * ini, const char *sec, const char *key, const char *def, int *lineno)
{
  char sec_key[INI_BUFSIZ + 1];

  sprintf (sec_key, "%s:%s", sec, key);
  return ini_get_internal (ini, sec_key, def, lineno);
}

/*
 * ini_getint() - Get the string associated to a key, convert to an int
 *   return: int
 *   ini(in): INI_TABLE to search
 *   sec(in): section to look for
 *   key(in): key to look for
 *   def(in): default value to return if key not found
 *   lineno(out): line number
 *
 * Note:
 */
int
ini_getint (INI_TABLE * ini, const char *sec, const char *key, int def, int *lineno)
{
  const char *str;
  int val;

  str = ini_getstr (ini, sec, key, INI_INVALID_KEY, lineno);
  if (str == INI_INVALID_KEY || str == NULL)
    {
      return def;
    }

  parse_int (&val, str, 0);
  return val;
}

/*
 * ini_getuint() - Get the string associated to a key, convert to positive int
 *   return: positive int
 *   ini(in): INI_TABLE to search
 *   sec(in): section to look for
 *   key(in): key to look for
 *   def(in): default value to return if key not found
 *   lineno(out): line number
 *
 * Note:
 */
int
ini_getuint (INI_TABLE * ini, const char *sec, const char *key, int def, int *lineno)
{
  int result;

  result = ini_getint (ini, sec, key, def, lineno);
  if (result <= 0)
    {
      return def;
    }

  return result;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * ini_getuint_min() - Get the string associated to a key, convert to
 *                     positive int with minimum limit
 *   return: positive int
 *   ini(in): INI_TABLE to search
 *   sec(in): section to look for
 *   key(in): key to look for
 *   def(in): default value to return if key not found
 *   min(in): minimum limit
 *   lineno(out): line number
 *
 * Note:
 */
int
ini_getuint_min (INI_TABLE * ini, const char *sec, const char *key, int def, int min, int *lineno)
{
  int result;

  result = ini_getuint (ini, sec, key, def, lineno);
  if (result < min)
    {
      return min;
    }

  return result;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * ini_getuint_max() - Get the string associated to a key, convert to
 *                     positive int with maximum limit
 *   return: positive int
 *   ini(in): INI_TABLE to search
 *   sec(in): section to look for
 *   key(in): key to look for
 *   def(in): default value to return if key not found
 *   max(in): maximum limit
 *   lineno(out): line number
 *
 * Note:
 */
int
ini_getuint_max (INI_TABLE * ini, const char *sec, const char *key, int def, int max, int *lineno)
{
  int result;

  result = ini_getuint (ini, sec, key, def, lineno);
  if (result > max)
    {
      return max;
    }

  return result;
}

/*
 * ini_gethex() - Get the string associated to a key, convert to an hex
 *   return: int
 *   ini(in): INI_TABLE to search
 *   sec(in): section to look for
 *   key(in): key to look for
 *   def(in): default value to return if key not found
 *   lineno(out): line number
 *
 * Note: the conversion may overflow. see strtol().
 */
int
ini_gethex (INI_TABLE * ini, const char *sec, const char *key, int def, int *lineno)
{
  const char *str;
  int val;

  str = ini_getstr (ini, sec, key, INI_INVALID_KEY, lineno);
  if (str == INI_INVALID_KEY || str == NULL)
    {
      return def;
    }

  parse_int (&val, str, 16);
  return val;
}

/*
 * ini_getfloat() - Get the string associated to a key, convert to an float
 *   return: float
 *   ini(in): INI_TABLE to search
 *   sec(in): section to look for
 *   key(in): key to look for
 *   def(in): default value to return if key not found
 *   lineno(out): line number
 *
 * Note: the conversion may overflow. see strtod().
 */
float
ini_getfloat (INI_TABLE * ini, const char *sec, const char *key, float def, int *lineno)
{
  const char *str;

  str = ini_getstr (ini, sec, key, INI_INVALID_KEY, lineno);
  if (str == INI_INVALID_KEY || str == NULL)
    {
      return def;
    }
  return (float) strtod (str, NULL);
}
