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
 * unicode_support.c : Unicode support
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "porting.h"

#include "locale_support.h"
#include "intl_support.h"
#include "error_manager.h"
#include "utility.h"
#include "environment_variable.h"
#include "intl_support.h"
#include "unicode_support.h"



#define UNICODEDATA_FILE "unicodedata.txt"

/* Unicode data file constants */
#define UNICODE_FILE_LINE_SIZE 512
#define UNICODE_FILE_FIELDS 14

/* Field position : starting from 0 */
#define UNICODE_FILE_GENERAL_CAT_POS	2
#define UNICODE_FILE_UPPER_CASE_MAP	12
#define UNICODE_FILE_LOWER_CASE_MAP	13

typedef enum
{
  CAT_Cn = 0,			/* other, not assigned */
  CAT_Lu,			/* Letter, uppercase */
  CAT_Ll,			/* Letter, lowercase */

  /* add new values here */
  CAT_MAX			/* maximum category value */
} GENERAL_CATEG_ID;

typedef struct
{
  GENERAL_CATEG_ID id;
  const char *val;
} GENERAL_CATEGORY;


/* available list of general categories (id, name) */
GENERAL_CATEGORY list_gen_cat[] = {
  {CAT_Lu, "Lu"},
  {CAT_Ll, "Ll"},
};

typedef struct
{
  /* general category for this character */
  GENERAL_CATEG_ID gen_cat_id;

  uint32 lower_cp[INTL_CASING_EXPANSION_MULTIPLIER];
  uint32 upper_cp[INTL_CASING_EXPANSION_MULTIPLIER];

} UNICODE_CHAR;


static UNICODE_CHAR *unicode_data = NULL;
static int unicode_data_lower_mult = 1;
static int unicode_data_upper_mult = 1;

static char last_unicode_file[LOC_FILE_PATH_SIZE] = { 0 };

static int load_unicode_data (const char *file_path, const int max_letters);
static int create_alphabet (ALPHABET_DATA * a, const int max_letters,
			    const int lower_multiplier,
			    const int upper_multiplier);
static int clone_alphabet (const ALPHABET_DATA * a, ALPHABET_DATA * a_clone);
static int string_to_cp_list (const char *s, uint32 * cp_list,
			      const int cp_list_size);

/* 
 * unicode_process_alphabet() - Process alphabet (casing) data for given
 *				locale
 *
 * Returns: error code
 * ld(in/out) : locale data structure
 */
int
unicode_process_alphabet (LOCALE_DATA * ld, bool is_verbose)
{
  ALPHABET_DATA *a = NULL;
  ALPHABET_TAILORING *a_tailoring = NULL;
  char unicode_file[LOC_FILE_PATH_SIZE];
  char err_msg[ERR_MSG_SIZE];
  int er_status = NO_ERROR;
  uint32 cp;
  int lower_mult = 1;
  int upper_mult = 1;
  int i;

  assert (ld != NULL);

  a = &(ld->alphabet);
  a_tailoring = &(ld->alpha_tailoring);

  /* compute lower and upper multiplier from rules */
  for (i = 0; i < a_tailoring->count_rules; i++)
    {
      TRANSFORM_RULE *tf_rule = &(a_tailoring->rules[i]);
      uint32 dummy_array;
      int dummy;
      int dest_len;

      dest_len = intl_utf8_to_cp_list (tf_rule->dest, tf_rule->dest_size,
				       &dummy_array, 1, &dummy);

      if (dest_len > INTL_CASING_EXPANSION_MULTIPLIER)
	{
	  snprintf (err_msg, sizeof (err_msg) - 1, "Invalid alphabet rule :%d"
		    ". Destination buffer contains more than 2 characters",
		    i);
	  LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
	  er_status = ER_LOC_GEN;
	  goto error;
	}
      if (tf_rule->type == TR_UPPER)
	{
	  upper_mult = MAX (upper_mult, dest_len);
	}
      else
	{
	  assert (tf_rule->type == TR_LOWER);
	  lower_mult = MAX (lower_mult, dest_len);
	}
    }

  if (a_tailoring->alphabet_mode == 2)
    {
      if (is_verbose)
	{
	  printf ("Creating ASCII alphabet\n");
	}

      /* ASCII alphabet */
      er_status =
	create_alphabet (a, a_tailoring->sett_max_letters,
			 lower_mult, upper_mult);
      if (er_status != NO_ERROR)
	{
	  goto error;
	}

      for (cp = 0; (int) cp < a->l_count; cp++)
	{
	  a->upper_cp[cp] = cp;
	  a->lower_cp[cp] = cp;
	}

      for (cp = (int) 'a'; cp <= (int) 'z'; cp++)
	{
	  a->upper_cp[cp] = cp - ('a' - 'A');
	  a->lower_cp[cp - ('a' - 'A')] = cp;
	}

      a->a_type = ALPHABET_ASCII;
    }
  else
    {
      if (a_tailoring->alphabet_mode == 1)
	{
	  strncpy (unicode_file, a_tailoring->unicode_data_file,
		   sizeof (unicode_file));
	  unicode_file[sizeof (unicode_file) - 1] = '\0';

	  /* a user defined unicode file is handled as a tailored alphabet */
	  a->a_type = ALPHABET_TAILORED;
	}
      else
	{
	  assert (a_tailoring->alphabet_mode == 0);
	  envvar_confdir_file (unicode_file, sizeof (unicode_file),
			       UNICODEDATA_FILE);

	  a->a_type = ALPHABET_UNICODE;
	}

      if (is_verbose)
	{
	  printf ("Creating UNICODE alphabet from: %s\n", unicode_file);
	}

      er_status = load_unicode_data (unicode_file,
				     a_tailoring->sett_max_letters);
      if (er_status != NO_ERROR)
	{
	  goto error;
	}

      lower_mult = MAX (lower_mult, unicode_data_lower_mult);
      upper_mult = MAX (upper_mult, unicode_data_upper_mult);

      er_status =
	create_alphabet (a, a_tailoring->sett_max_letters,
			 lower_mult, upper_mult);
      if (er_status != NO_ERROR)
	{
	  goto error;
	}

      for (cp = 0; (int) cp < a_tailoring->sett_max_letters; cp++)
	{
	  /* set lower and upper case of each codepoint to itself */
	  a->lower_cp[cp * lower_mult] = cp;
	  a->upper_cp[cp * upper_mult] = cp;

	  /* overwrite with UnicodeData */
	  if (unicode_data[cp].gen_cat_id == CAT_Lu)
	    {
	      memcpy (&(a->lower_cp[cp * lower_mult]),
		      &(unicode_data[cp].lower_cp), sizeof (uint32) *
		      MIN (unicode_data_lower_mult, lower_mult));
	    }
	  else if (unicode_data[cp].gen_cat_id == CAT_Ll)
	    {
	      memcpy (&(a->upper_cp[cp * upper_mult]),
		      &(unicode_data[cp].upper_cp), sizeof (uint32) *
		      MIN (unicode_data_upper_mult, upper_mult));
	    }
	}
    }

  if (a_tailoring->count_rules > 0)
    {
      a->a_type = ALPHABET_TAILORED;
    }

  /* create identifiers alphabet from un-tailored alphabet */
  er_status = clone_alphabet (a, &(ld->identif_alphabet));
  if (er_status != NO_ERROR)
    {
      goto error;
    }

  if (is_verbose && a_tailoring->count_rules > 0)
    {
      printf ("Applying %d alphabet tailoring rules\n",
	      a_tailoring->count_rules);
    }
  /* apply tailoring rules on user-alphabet only */
  for (i = 0; i < a_tailoring->count_rules; i++)
    {
      TRANSFORM_RULE *tf_rule = &(a_tailoring->rules[i]);
      uint32 cp_src;
      uint32 cp_dest[INTL_CASING_EXPANSION_MULTIPLIER];
      int src_cp_count = 0;
      int src_len = 0;
      int dest_cp_count = 0;
      int dest_len = 0;

      /* source codepoints */
      /* TODO : allow casing compression (many CPs for source) */
      src_len = intl_utf8_to_cp_list (tf_rule->src, tf_rule->src_size,
				      &cp_src, 1, &src_cp_count);

      if (src_len != 1 || src_len != src_cp_count)
	{
	  LOG_LOCALE_ERROR ("Invalid source buffer for alphabet rule",
			    ER_LOC_GEN, true);
	  er_status = ER_LOC_GEN;
	  goto error;
	}

      if ((int) cp_src >= a_tailoring->sett_max_letters)
	{
	  LOG_LOCALE_ERROR ("Codepoint for casint rule exceeds maximum"
			    " allowed value", ER_LOC_GEN, true);
	  er_status = ER_LOC_GEN;
	  goto error;
	}

      /* destination codepoints */
      dest_len =
	intl_utf8_to_cp_list (tf_rule->dest, tf_rule->dest_size, cp_dest,
			      INTL_CASING_EXPANSION_MULTIPLIER,
			      &dest_cp_count);

      if (dest_len < 1 || dest_len != dest_cp_count)
	{
	  LOG_LOCALE_ERROR ("Invalid destination buffer for alphabet rule",
			    ER_LOC_GEN, true);
	  er_status = ER_LOC_GEN;
	  goto error;
	}

      if (tf_rule->type == TR_UPPER)
	{
	  assert (dest_cp_count <= upper_mult);
	  memset (&(a->upper_cp[cp_src * upper_mult]), 0,
		  upper_mult * sizeof (uint32));
	  memcpy (&(a->upper_cp[cp_src * upper_mult]),
		  cp_dest, sizeof (uint32) * MIN (dest_cp_count, upper_mult));
	}
      else
	{
	  assert (tf_rule->type == TR_LOWER);

	  assert (dest_cp_count <= lower_mult);
	  memset (&(a->lower_cp[cp_src * lower_mult]), 0,
		  lower_mult * sizeof (uint32));
	  memcpy (&(a->lower_cp[cp_src * lower_mult]),
		  cp_dest, sizeof (uint32) * MIN (dest_cp_count, lower_mult));
	}
    }

  return er_status;

error:

  return er_status;
}

/* 
 * load_unicode_data() - Loads the UNICODEDATA file (standardised 
 *			 and availabe at Unicode.org).
 * Returns: error code
 * file_path(in) : file path for UnicodeData file
 * max_letters(in) : maximum number of letters (codepoints) to load
 */
static int
load_unicode_data (const char *file_path, const int max_letters)
{
  FILE *fp = NULL;
  char err_msg[ERR_MSG_SIZE];
  int status = NO_ERROR;
  char str[UNICODE_FILE_LINE_SIZE];
  int line_count = 0;

  assert (max_letters > 0 && max_letters <= MAX_UNICODE_CHARS);

  if (strcmp (file_path, last_unicode_file) == 0)
    {
      assert (unicode_data != NULL);
      return status;
    }

  unicode_free_data ();

  unicode_data =
    (UNICODE_CHAR *) malloc (max_letters * sizeof (UNICODE_CHAR));
  if (unicode_data == NULL)
    {
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      status = ER_LOC_GEN;
      goto error;
    }

  memset (unicode_data, 0, max_letters * sizeof (UNICODE_CHAR));

  fp = fopen_ex (file_path, "rt");
  if (fp == NULL)
    {
      snprintf (err_msg, sizeof (err_msg) - 1, "Cannot open file %s",
		file_path);
      LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
      status = ER_LOC_GEN;
      goto error;
    }

  while (fgets (str, sizeof (str), fp))
    {
      uint32 cp = 0;
      int i;
      char *s;
      char *end;
      UNICODE_CHAR *uc = NULL;

      line_count++;

      cp = strtol (str, NULL, 16);

      /* skip Unicode values above 0xFFFF */
      if ((int) cp >= max_letters)
	{
	  continue;
	}

      s = str;
      uc = &(unicode_data[cp]);

      /* next field */
      s = strchr (s, ';');

      assert (s != NULL);
      if (s == NULL)
	{
	  continue;
	}
      s++;

      for (i = 1; i < UNICODE_FILE_FIELDS; i++)
	{
	  char str_p[UNICODE_FILE_LINE_SIZE];
	  int cp_count;

	  strcpy (str_p, s);

	  end = strtok (str_p, ";");

	  /* check generic category */
	  if (i == UNICODE_FILE_GENERAL_CAT_POS)
	    {
	      int cat_idx;

	      for (cat_idx = 0;
		   cat_idx < sizeof (list_gen_cat) / sizeof (list_gen_cat[0]);
		   cat_idx++)
		{
		  if (strcmp (list_gen_cat[cat_idx].val, str_p) == 0)
		    {
		      uc->gen_cat_id = list_gen_cat[cat_idx].id;
		      break;
		    }
		}
	    }
	  else if (i == UNICODE_FILE_UPPER_CASE_MAP &&
		   uc->gen_cat_id == CAT_Ll)
	    {
	      /* lower case codepoints */
	      cp_count = string_to_cp_list (str_p, uc->upper_cp,
					    INTL_CASING_EXPANSION_MULTIPLIER);
	      if (cp_count > INTL_CASING_EXPANSION_MULTIPLIER)
		{
		  snprintf (err_msg, sizeof (err_msg) - 1, "Invalid line %d"
			    " of file %s contains more than 2 characters for "
			    "upper case definition", line_count, file_path);
		  LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
		  status = ER_LOC_GEN;
		  goto error;
		}

	      unicode_data_upper_mult =
		(cp_count > unicode_data_upper_mult) ? cp_count :
		unicode_data_upper_mult;
	    }
	  else if (i == UNICODE_FILE_LOWER_CASE_MAP &&
		   uc->gen_cat_id == CAT_Lu)
	    {
	      /* lower case codepoints */
	      cp_count = string_to_cp_list (str_p, uc->lower_cp,
					    INTL_CASING_EXPANSION_MULTIPLIER);

	      if (cp_count > INTL_CASING_EXPANSION_MULTIPLIER)
		{
		  snprintf (err_msg, sizeof (err_msg) - 1, "Invalid line %d"
			    " of file %s contains more than 2 characters for "
			    "lower case definition", line_count, file_path);
		  LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
		  status = ER_LOC_GEN;
		  goto error;
		}

	      unicode_data_lower_mult =
		(cp_count > unicode_data_lower_mult) ? cp_count :
		unicode_data_lower_mult;
	    }

	  s = strchr (s, ';');
	  if (s == NULL)
	    {
	      break;
	    }

	  s++;
	}
    }

  assert (fp != NULL);
  fclose (fp);

  strncpy (last_unicode_file, file_path, sizeof (last_unicode_file) - 1);
  last_unicode_file[sizeof (last_unicode_file) - 1] = '\0';

  return status;

error:

  if (fp == NULL)
    {
      fclose (fp);
    }

  unicode_free_data ();

  return status;
}

/* 
 * unicode_free_data() - Frees Unicode data structures.
 * Returns:
 */
void
unicode_free_data (void)
{
  if (unicode_data != NULL)
    {
      free (unicode_data);
      unicode_data = NULL;
    }

  *last_unicode_file = '\0';
}

/* 
 * create_alphabet () - allocated arrays for alphabet
 * Returns: error code
 * a(in/out) : alphabet
 * max_letters(in) : number of letters in alphabet
 * lower_multiplier(in) : lower case multlipier
 * upper_multiplier(in) : upper case multlipier
 */
static int
create_alphabet (ALPHABET_DATA * a, const int max_letters,
		 const int lower_multiplier, const int upper_multiplier)
{
  int er_status = NO_ERROR;

  assert (a != NULL);
  assert (lower_multiplier > 0 &&
	  lower_multiplier <= INTL_CASING_EXPANSION_MULTIPLIER);
  assert (upper_multiplier > 0 &&
	  upper_multiplier <= INTL_CASING_EXPANSION_MULTIPLIER);

  memset (a, 0, sizeof (ALPHABET_DATA));

  if (max_letters <= 0 || max_letters > MAX_UNICODE_CHARS)
    {
      LOG_LOCALE_ERROR ("invalid number of letters", ER_LOC_GEN, true);
      return ER_LOC_GEN;
    }

  if (max_letters > 0)
    {
      a->lower_cp = (uint32 *) malloc (max_letters * lower_multiplier *
				       sizeof (uint32));
      a->upper_cp = (uint32 *) malloc (max_letters * upper_multiplier *
				       sizeof (uint32));

      if (a->lower_cp == NULL || a->upper_cp == NULL)
	{
	  LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
	  er_status = ER_LOC_GEN;
	  goto er_exit;
	}

      memset (a->lower_cp, 0, max_letters * lower_multiplier *
	      sizeof (uint32));
      memset (a->upper_cp, 0, max_letters * upper_multiplier *
	      sizeof (uint32));
    }

  a->l_count = max_letters;
  a->lower_multiplier = lower_multiplier;
  a->upper_multiplier = upper_multiplier;

  return er_status;

er_exit:
  if (a->lower_cp != NULL)
    {
      free (a->lower_cp);
      a->lower_cp = NULL;
    }

  if (a->upper_cp != NULL)
    {
      free (a->upper_cp);
      a->upper_cp = NULL;
    }

  return er_status;
}

/* 
 * clone_alphabet () - creates a deep copy of a locale alphabet
 * Returns: error code
 * a(in) : alphabet to copy (src)
 * a_clone(in/out): alphabet to copy to (dest)
 */
static int
clone_alphabet (const ALPHABET_DATA * a, ALPHABET_DATA * a_clone)
{
  int status = NO_ERROR;

  assert (a != NULL);
  assert (a_clone != NULL);
  assert (a->l_count > 0);

  if (a->is_shared)
    {
      assert (a->a_type == ALPHABET_UNICODE || a->a_type == ALPHABET_ASCII);
      memcpy (a_clone, a, sizeof (ALPHABET_DATA));
      return status;
    }

  /* full copy */

  memset (a_clone, 0, sizeof (ALPHABET_DATA));

  a_clone->a_type = a->a_type;
  a_clone->lower_cp = (uint32 *) malloc (a->l_count * a->lower_multiplier *
					 sizeof (uint32));
  a_clone->upper_cp = (uint32 *) malloc (a->l_count * a->upper_multiplier *
					 sizeof (uint32));

  if (a_clone->lower_cp == NULL || a_clone->upper_cp == NULL)
    {
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      status = ER_LOC_GEN;
      goto err_exit;
    }

  memcpy (a_clone->lower_cp, a->lower_cp,
	  a->l_count * a->lower_multiplier * sizeof (uint32));
  memcpy (a_clone->upper_cp, a->upper_cp,
	  a->l_count * a->upper_multiplier * sizeof (uint32));

  a_clone->l_count = a->l_count;
  a_clone->lower_multiplier = a->lower_multiplier;
  a_clone->upper_multiplier = a->upper_multiplier;

  return status;

err_exit:
  if (a_clone->lower_cp != NULL)
    {
      free (a_clone->lower_cp);
      a_clone->lower_cp = NULL;
    }

  if (a_clone->upper_cp != NULL)
    {
      free (a_clone->upper_cp);
      a_clone->upper_cp = NULL;
    }

  return status;
}

/* 
 * string_to_cp_list() - builds a list of codepoints from a string
 *
 * Returns: count of codepoints found
 * s(in): nul-terminated string
 * cp_list(out): array of codepoints
 * cp_list_size(in): maximum allowed size of codepoint list
 *
 *  Note : the string containts unsigned integers in hexadecimal separated
 *	   by space
 *	   The case of returned number of codepoints is greater than
 *	    'cp_list_size' should be handled as error.
 *	   
 */
static int
string_to_cp_list (const char *s, uint32 * cp_list, const int cp_list_size)
{
  int i = 0;
  assert (cp_list != NULL);

  do
    {
      uint32 code = 0;

      code = strtol (s, NULL, 16);

      if (i < cp_list_size)
	{
	  *cp_list++ = code;
	}

      i++;
      s = strchr (s, ' ');

      if (s == NULL)
	{
	  break;
	}
      s++;
    }
  while (*s != '\0');

  return i;
}
