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
 * unicode_support.c : Unicode support
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "porting.h"

#include "locale_support.h"
#include "intl_support.h"
#include "language_support.h"
#include "error_manager.h"
#include "utility.h"
#include "environment_variable.h"
#include "system_parameter.h"
#include "unicode_support.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"


#define UNICODEDATA_FILE "unicodedata.txt"

/* Unicode data file constants */
#define UNICODE_FILE_LINE_SIZE 512
#define UNICODE_FILE_FIELDS 14

/* Field position : starting from 0 */
#define UNICODE_FILE_GENERAL_CAT_POS		2
#define UNICODE_FILE_CHAR_DECOMPOSITION_MAPPING	5
#define UNICODE_FILE_UPPER_CASE_MAP		12
#define UNICODE_FILE_LOWER_CASE_MAP		13

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
  int id;
  char *std_val;		/* Standard value as defined by Unicode Consortium */
} CANONICAL_COMBINING_CLASS;

/* The maximum number of codepoints to which a single codepoint can be
 * rewritten in canonically fully decomposed form.
 */
#define UNICODE_DECOMP_MAP_CP_COUNT 4

typedef struct
{
  /* general category for this character */
  GENERAL_CATEG_ID gen_cat_id;

  uint32 lower_cp[INTL_CASING_EXPANSION_MULTIPLIER];
  uint32 upper_cp[INTL_CASING_EXPANSION_MULTIPLIER];

  char unicode_mapping_cp_count;

  uint32 unicode_mapping[UNICODE_DECOMP_MAP_CP_COUNT];
  char unicode_full_decomp_cp_count;

} UNICODE_CHAR;

typedef struct
{
  uint32 cp;			/* Codepoint value */
  uint32 *map;			/* A fully decomposed canonical mapping stored as codepoints */

  int size;			/* The number of codepoints in the mapping */
  bool is_full_decomp;		/* true - if map is fully decomposed false - otherwise. */
} UNICODE_CP_MAPPING;

static UNICODE_CHAR *unicode_data = NULL;
static int unicode_data_lower_mult = 1;
static int unicode_data_upper_mult = 1;

static char last_unicode_file[PATH_MAX] = { 0 };

static int load_unicode_data (const LOCALE_DATA * ld);
static int create_alphabet (ALPHABET_DATA * a, const int max_letters, const int lower_multiplier,
			    const int upper_multiplier);
static int count_full_decomp_cp (int cp);
static int count_decomp_steps (int cp);
static int unicode_make_normalization_data (UNICODE_CP_MAPPING * decomp_maps, LOCALE_DATA * ld);
static int comp_func_unicode_cp_mapping (const void *arg1, const void *arg2);
static int comp_func_grouping_unicode_cp_mapping (const void *arg1, const void *arg2);


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
  ALPHABET_DATA *i_a = NULL;
  ALPHABET_TAILORING *a_tailoring = NULL;
  char unicode_file[PATH_MAX];
  char err_msg[ERR_MSG_SIZE];
  int er_status = NO_ERROR;
  uint32 cp;
  int lower_mult = 1;
  int upper_mult = 1;
  int i;

  assert (ld != NULL);

  a = &(ld->alphabet);
  i_a = &(ld->identif_alphabet);
  a_tailoring = &(ld->alpha_tailoring);

  /* compute lower and upper multiplier from rules */
  for (i = 0; i < a_tailoring->count_rules; i++)
    {
      TRANSFORM_RULE *tf_rule = &(a_tailoring->rules[i]);
      uint32 dummy_array;
      int dummy;
      int dest_len;

      dest_len = intl_utf8_to_cp_list ((unsigned char *) (tf_rule->dest), tf_rule->dest_size, &dummy_array, 1, &dummy);

      if (dest_len > INTL_CASING_EXPANSION_MULTIPLIER)
	{
	  snprintf (err_msg, sizeof (err_msg) - 1,
		    "Invalid alphabet rule :%d" ". Destination buffer contains more than 2 characters", i);
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
      er_status = create_alphabet (a, a_tailoring->sett_max_letters, lower_mult, upper_mult);
      if (er_status != NO_ERROR)
	{
	  goto error;
	}

      er_status = create_alphabet (i_a, a_tailoring->sett_max_letters, 1, 1);
      if (er_status != NO_ERROR)
	{
	  goto error;
	}

      for (cp = 0; (int) cp < a->l_count; cp++)
	{
	  i_a->upper_cp[cp] = a->upper_cp[cp] = cp;
	  i_a->lower_cp[cp] = a->lower_cp[cp] = cp;
	}

      for (cp = (int) 'a'; cp <= (int) 'z'; cp++)
	{
	  i_a->upper_cp[cp] = a->upper_cp[cp] = cp - ('a' - 'A');
	  i_a->lower_cp[cp - ('a' - 'A')] = a->lower_cp[cp - ('a' - 'A')] = cp;
	}

      i_a->a_type = a->a_type = ALPHABET_ASCII;
    }
  else
    {
      if (a_tailoring->alphabet_mode == 1)
	{
	  strncpy (unicode_file, a_tailoring->unicode_data_file, sizeof (unicode_file));
	  unicode_file[sizeof (unicode_file) - 1] = '\0';

	  /* a user defined unicode file is handled as a tailored alphabet */
	  a->a_type = ALPHABET_TAILORED;
	}
      else
	{
	  assert (a_tailoring->alphabet_mode == 0);
	  envvar_localedatadir_file (unicode_file, sizeof (unicode_file), UNICODEDATA_FILE);

	  a->a_type = ALPHABET_UNICODE;
	}

      if (is_verbose)
	{
	  printf ("Creating UNICODE alphabet from: %s\n", unicode_file);
	}

      er_status = load_unicode_data (ld);
      if (er_status != NO_ERROR)
	{
	  goto error;
	}

      lower_mult = MAX (lower_mult, unicode_data_lower_mult);
      upper_mult = MAX (upper_mult, unicode_data_upper_mult);

      er_status = create_alphabet (a, a_tailoring->sett_max_letters, lower_mult, upper_mult);
      if (er_status != NO_ERROR)
	{
	  goto error;
	}

      er_status =
	create_alphabet (i_a, a_tailoring->sett_max_letters, unicode_data_lower_mult, unicode_data_upper_mult);
      if (er_status != NO_ERROR)
	{
	  goto error;
	}

      for (cp = 0; (int) cp < a_tailoring->sett_max_letters; cp++)
	{
	  /* set lower and upper case of each codepoint to itself */
	  a->lower_cp[cp * lower_mult] = cp;
	  a->upper_cp[cp * upper_mult] = cp;

	  i_a->lower_cp[cp * unicode_data_lower_mult] = cp;
	  i_a->upper_cp[cp * unicode_data_upper_mult] = cp;

	  /* overwrite with UnicodeData */
	  if (unicode_data[cp].gen_cat_id == CAT_Lu)
	    {
	      memcpy (&(a->lower_cp[cp * lower_mult]), &(unicode_data[cp].lower_cp),
		      sizeof (uint32) * MIN (unicode_data_lower_mult, lower_mult));

	      memcpy (&(i_a->lower_cp[cp * unicode_data_lower_mult]), &(unicode_data[cp].lower_cp),
		      sizeof (uint32) * unicode_data_lower_mult);
	    }
	  else if (unicode_data[cp].gen_cat_id == CAT_Ll)
	    {
	      memcpy (&(a->upper_cp[cp * upper_mult]), &(unicode_data[cp].upper_cp),
		      sizeof (uint32) * MIN (unicode_data_upper_mult, upper_mult));

	      memcpy (&(i_a->upper_cp[cp * unicode_data_upper_mult]), &(unicode_data[cp].upper_cp),
		      sizeof (uint32) * unicode_data_upper_mult);
	    }
	}
    }

  if (a_tailoring->count_rules > 0)
    {
      a->a_type = ALPHABET_TAILORED;
    }

  if (is_verbose && a_tailoring->count_rules > 0)
    {
      printf ("Applying %d alphabet tailoring rules\n", a_tailoring->count_rules);
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
      src_len = intl_utf8_to_cp_list ((unsigned char *) (tf_rule->src), tf_rule->src_size, &cp_src, 1, &src_cp_count);

      if (src_len != 1 || src_len != src_cp_count)
	{
	  LOG_LOCALE_ERROR ("Invalid source buffer for alphabet rule", ER_LOC_GEN, true);
	  er_status = ER_LOC_GEN;
	  goto error;
	}

      if ((int) cp_src >= a_tailoring->sett_max_letters)
	{
	  LOG_LOCALE_ERROR ("Codepoint for casing rule exceeds maximum" " allowed value", ER_LOC_GEN, true);
	  er_status = ER_LOC_GEN;
	  goto error;
	}

      /* destination codepoints */
      dest_len =
	intl_utf8_to_cp_list ((unsigned char *) (tf_rule->dest), tf_rule->dest_size, cp_dest,
			      INTL_CASING_EXPANSION_MULTIPLIER, &dest_cp_count);

      if (dest_len < 1 || dest_len != dest_cp_count)
	{
	  LOG_LOCALE_ERROR ("Invalid destination buffer for alphabet rule", ER_LOC_GEN, true);
	  er_status = ER_LOC_GEN;
	  goto error;
	}

      if (tf_rule->type == TR_UPPER)
	{
	  assert (dest_cp_count <= upper_mult);
	  memset (&(a->upper_cp[cp_src * upper_mult]), 0, upper_mult * sizeof (uint32));
	  memcpy (&(a->upper_cp[cp_src * upper_mult]), cp_dest, sizeof (uint32) * MIN (dest_cp_count, upper_mult));
	}
      else
	{
	  assert (tf_rule->type == TR_LOWER);

	  assert (dest_cp_count <= lower_mult);
	  memset (&(a->lower_cp[cp_src * lower_mult]), 0, lower_mult * sizeof (uint32));
	  memcpy (&(a->lower_cp[cp_src * lower_mult]), cp_dest, sizeof (uint32) * MIN (dest_cp_count, lower_mult));
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
 * ld(in) : locale data
 */
static int
load_unicode_data (const LOCALE_DATA * ld)
{
  FILE *fp = NULL;
  char err_msg[ERR_MSG_SIZE];
  int status = NO_ERROR;
  char str[UNICODE_FILE_LINE_SIZE];
  int line_count = 0;

  assert (ld != NULL);

  /* Build the full filepath to the selected (or default) Unicode data file */
  if (ld->unicode_mode == 0)
    {
      /* using default Unicode file */
      envvar_localedatadir_file ((char *) (ld->unicode_data_file), sizeof (ld->unicode_data_file), UNICODEDATA_FILE);
    }
  else
    {
      assert (ld->unicode_mode == 1);
    }

  if (strcmp (ld->unicode_data_file, last_unicode_file) == 0)
    {
      assert (unicode_data != NULL);
      return status;
    }

  unicode_free_data ();

  unicode_data = (UNICODE_CHAR *) malloc (MAX_UNICODE_CHARS * sizeof (UNICODE_CHAR));
  if (unicode_data == NULL)
    {
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      status = ER_LOC_GEN;
      goto error;
    }

  memset (unicode_data, 0, MAX_UNICODE_CHARS * sizeof (UNICODE_CHAR));

  fp = fopen_ex (ld->unicode_data_file, "rt");
  if (fp == NULL)
    {
      snprintf_dots_truncate (err_msg, sizeof (err_msg) - 1, "Cannot open file %s", ld->unicode_data_file);
      LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
      status = ER_LOC_GEN;
      goto error;
    }

  while (fgets (str, sizeof (str), fp))
    {
      uint32 cp = 0;
      int result = 0;
      int i;
      char *s, *end, *end_p;
      UNICODE_CHAR *uc = NULL;

      line_count++;

      result = str_to_uint32 (&cp, &end_p, str, 16);
      /* skip Unicode values above 0xFFFF */
      if (result != 0 || cp >= MAX_UNICODE_CHARS)
	{
	  continue;
	}

      s = str;
      uc = &(unicode_data[cp]);
      uc->lower_cp[0] = cp;
      uc->upper_cp[0] = cp;

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
	  char *save;
	  int cp_count;

	  strcpy (str_p, s);

	  end = strtok_r (str_p, ";", &save);

	  /* check generic category */
	  if (i == UNICODE_FILE_GENERAL_CAT_POS)
	    {
	      int cat_idx;

	      for (cat_idx = 0; cat_idx < (int) (sizeof (list_gen_cat) / sizeof (list_gen_cat[0])); cat_idx++)
		{
		  if (strcmp (list_gen_cat[cat_idx].val, str_p) == 0)
		    {
		      uc->gen_cat_id = list_gen_cat[cat_idx].id;
		      break;
		    }
		}
	    }
	  else if (i == UNICODE_FILE_UPPER_CASE_MAP && uc->gen_cat_id == CAT_Ll)
	    {
	      /* lower case codepoints */
	      cp_count = string_to_int_array (str_p, uc->upper_cp, INTL_CASING_EXPANSION_MULTIPLIER, " ");
	      if (cp_count > INTL_CASING_EXPANSION_MULTIPLIER)
		{
		  snprintf_dots_truncate (err_msg, sizeof (err_msg) - 1,
					  "Invalid line %d" " of file %s contains more than 2 characters for "
					  "upper case definition", line_count, ld->unicode_data_file);
		  LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
		  status = ER_LOC_GEN;
		  goto error;
		}

	      unicode_data_upper_mult = (cp_count > unicode_data_upper_mult) ? cp_count : unicode_data_upper_mult;
	    }
	  else if (i == UNICODE_FILE_LOWER_CASE_MAP && uc->gen_cat_id == CAT_Lu)
	    {
	      /* lower case codepoints */
	      cp_count = string_to_int_array (str_p, uc->lower_cp, INTL_CASING_EXPANSION_MULTIPLIER, " ");

	      if (cp_count > INTL_CASING_EXPANSION_MULTIPLIER)
		{
		  snprintf_dots_truncate (err_msg, sizeof (err_msg) - 1,
					  "Invalid line %d" " of file %s contains more than 2 characters for "
					  "lower case definition", line_count, ld->unicode_data_file);
		  LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
		  status = ER_LOC_GEN;
		  goto error;
		}

	      unicode_data_lower_mult = (cp_count > unicode_data_lower_mult) ? cp_count : unicode_data_lower_mult;
	    }
	  else if (i == UNICODE_FILE_CHAR_DECOMPOSITION_MAPPING)
	    {
	      uc->unicode_mapping_cp_count = 0;	/* init */

	      do
		{
		  /* if no decomposition available, or decomposition is a compatibility one, discard the specified
		   * decomposition */
		  if (str_p[0] == ';' || str_p[0] == '<')
		    {
		      break;
		    }

		  if (str_p != NULL)
		    {
		      uc->unicode_mapping_cp_count =
			string_to_int_array (str_p, uc->unicode_mapping, UNICODE_DECOMP_MAP_CP_COUNT, " ");
		    }
		  break;
		}
	      while (0);
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

  strncpy (last_unicode_file, ld->unicode_data_file, sizeof (last_unicode_file) - 1);
  last_unicode_file[sizeof (last_unicode_file) - 1] = '\0';

  return status;

error:

  if (fp != NULL)
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
create_alphabet (ALPHABET_DATA * a, const int max_letters, const int lower_multiplier, const int upper_multiplier)
{
  int er_status = NO_ERROR;

  assert (a != NULL);
  assert (lower_multiplier > 0 && lower_multiplier <= INTL_CASING_EXPANSION_MULTIPLIER);
  assert (upper_multiplier > 0 && upper_multiplier <= INTL_CASING_EXPANSION_MULTIPLIER);

  if (lower_multiplier > 1 && upper_multiplier > 1)
    {
      LOG_LOCALE_ERROR ("CUBRID does not support collations with both lower "
			"and upper multipliers with values above 1.", ER_LOC_GEN, true);
      return ER_LOC_GEN;
    }

  memset (a, 0, sizeof (ALPHABET_DATA));

  if (max_letters <= 0 || max_letters > MAX_UNICODE_CHARS)
    {
      LOG_LOCALE_ERROR ("invalid number of letters", ER_LOC_GEN, true);
      return ER_LOC_GEN;
    }

  if (max_letters > 0)
    {
      a->lower_cp = (uint32 *) malloc (max_letters * lower_multiplier * sizeof (uint32));
      a->upper_cp = (uint32 *) malloc (max_letters * upper_multiplier * sizeof (uint32));

      if (a->lower_cp == NULL || a->upper_cp == NULL)
	{
	  LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
	  er_status = ER_LOC_GEN;
	  goto er_exit;
	}

      memset (a->lower_cp, 0, max_letters * lower_multiplier * sizeof (uint32));
      memset (a->upper_cp, 0, max_letters * upper_multiplier * sizeof (uint32));
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
 * string_to_int_array() - builds a list of codepoints from a string
 *
 * Returns: count of codepoints found
 * s(in): nul-terminated string
 * cp_list(out): array of codepoints
 * cp_list_size(in): maximum allowed size of codepoint list
 * delims(in) : possible delimiters between values
 *
 *  Note : the string containts unsigned integers in hexadecimal.
 *	   The case of returned number of codepoints is greater than
 *	    'cp_list_size' should be handled as error.
 *
 */
int
string_to_int_array (char *s, uint32 * cp_list, const int cp_list_size, const char *delims)
{
  int i = 0;
  char *str;
  char *str_end;
  char *str_cursor;

  assert (cp_list != NULL);

  str = s;
  str_end = s + strlen (s);

  while (str != NULL && str < str_end)
    {
      int result = 0;
      uint32 val;

      result = str_to_uint32 (&val, &str_cursor, str, 16);
      if (result != 0 || str_cursor <= str)
	{
	  break;
	}

      if (i < cp_list_size)
	{
	  *cp_list++ = val;
	}
      i++;

      while (str_cursor < str_end && strchr (delims, *str_cursor) != NULL)
	{
	  str_cursor++;
	}
      str = str_cursor;
    }

  return i;
}

/*
 * unicode_process_normalization() - Process character decomposition mappings
 *			  imported from the Unicode data file, and prepare
 *			  the data structures required for converting strings
 *			  to fully composed.
 *
 * Returns: error code
 * ld(in/out) :    locale data structure
 * is_verbose(in): enable or disable verbose mode
 */
int
unicode_process_normalization (LOCALE_DATA * ld, bool is_verbose)
{
  int i, orig_mapping_count, curr_mapping, mapping_cursor;
  UNICODE_CP_MAPPING *um;
  UNICODE_CP_MAPPING *new_map;
  UNICODE_CHAR *uc;
  int mapping_start, mapping_count;
  UNICODE_NORMALIZATION *norm;
  uint32 cp, old_cp, j;
  int err_status = NO_ERROR;

  int *unicode_decomp_map_count = NULL;
  /* perm_unicode_mapping[cp] = the number of possible sorted permutations of the cp decomposition mapping */
  UNICODE_CP_MAPPING *temp_list_unicode_decomp_maps = NULL;

  assert (ld != NULL);
  norm = &(ld->unicode_normalization);

  err_status = load_unicode_data (ld);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

  unicode_decomp_map_count = (int *) malloc (MAX_UNICODE_CHARS * sizeof (int));
  if (unicode_decomp_map_count == NULL)
    {
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      err_status = ER_LOC_GEN;
      goto exit;
    }
  memset (unicode_decomp_map_count, 0, MAX_UNICODE_CHARS * sizeof (int));

  /* Count the number of steps (buffers) necessary for the decomposition of each codepoint. */
  for (cp = 0; cp < MAX_UNICODE_CHARS; cp++)
    {
      uc = &(unicode_data[cp]);

      if (uc->unicode_mapping_cp_count <= 1 || uc->unicode_mapping[0] > MAX_UNICODE_CHARS)
	{
	  unicode_decomp_map_count[cp] = 0;
	}
      else
	{
	  uc->unicode_full_decomp_cp_count = count_full_decomp_cp (cp);
	  unicode_decomp_map_count[cp] = count_decomp_steps (cp);
	}
      if (is_verbose)
	{
	  printf ("CP : %04X\t\tDeco CP count: %2d\t\tDeco steps: %2d\n", cp, uc->unicode_full_decomp_cp_count,
		  unicode_decomp_map_count[cp]);
	}
      norm->unicode_mappings_count += unicode_decomp_map_count[cp];
    }

  if (is_verbose)
    {
      printf ("\nTotal number of composition maps (sum of deco steps) : %d\n", norm->unicode_mappings_count);
    }

  /* Prepare the generation of all decomposition steps for all codepoints */
  temp_list_unicode_decomp_maps =
    (UNICODE_CP_MAPPING *) malloc (norm->unicode_mappings_count * sizeof (UNICODE_CP_MAPPING));
  if (temp_list_unicode_decomp_maps == NULL)
    {
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      err_status = ER_LOC_GEN;
      goto exit;
    }
  memset (temp_list_unicode_decomp_maps, 0, norm->unicode_mappings_count * sizeof (UNICODE_CP_MAPPING));

  /* Copy mappings loaded from UnicodeData.txt */
  cp = 0;
  orig_mapping_count = 0;
  while (cp < MAX_UNICODE_CHARS)
    {
      if (unicode_decomp_map_count[cp] > 0)
	{
	  um = &(temp_list_unicode_decomp_maps[orig_mapping_count]);
	  um->cp = cp;
	  um->size = unicode_data[cp].unicode_mapping_cp_count;
	  um->map = (uint32 *) malloc (um->size * sizeof (uint32));
	  if (um->map == NULL)
	    {
	      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
	      err_status = ER_LOC_GEN;
	      goto exit;
	    }
	  memcpy (um->map, unicode_data[cp].unicode_mapping, um->size * sizeof (uint32));
	  orig_mapping_count++;
	}
      cp++;
    }

  /* Decompose each mapping, top-down, until no mapping can be further decomposed. Total number of decomposition
   * mappings(steps) was computed previously for each codepoint in unicode_decomp_map_count[cp] and their sum in
   * unicode_decomp_map_total. These constants will be used for validation (as assert args). */
  mapping_cursor = orig_mapping_count;
  curr_mapping = 0;
  while (curr_mapping < mapping_cursor)
    {
      if (mapping_cursor >= norm->unicode_mappings_count)
	{
	  break;
	}
      um = &(temp_list_unicode_decomp_maps[curr_mapping]);
      new_map = &(temp_list_unicode_decomp_maps[mapping_cursor]);

      if (um->size > 0 && um->map[0] < MAX_UNICODE_CHARS)
	{
	  if (unicode_decomp_map_count[um->map[0]] > 0)
	    {
	      new_map->size = um->size - 1 + unicode_data[um->map[0]].unicode_mapping_cp_count;
	      new_map->cp = um->cp;
	      new_map->map = (uint32 *) malloc (new_map->size * sizeof (uint32));
	      if (new_map->map == NULL)
		{
		  LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
		  err_status = ER_LOC_GEN;
		  goto exit;
		}

	      for (i = 0; i < new_map->size; i++)
		{
		  if (i < unicode_data[um->map[0]].unicode_mapping_cp_count)
		    {
		      new_map->map[i] = unicode_data[um->map[0]].unicode_mapping[i];
		    }
		  else
		    {
		      new_map->map[i] = um->map[1 + i - unicode_data[um->map[0]].unicode_mapping_cp_count];
		    }
		}
	      mapping_cursor++;
	      if (is_verbose)
		{
		  printf ("\nNew mapping step : %04X -> ", um->cp);
		  for (i = 0; i < new_map->size; i++)
		    {
		      printf ("%04X ", new_map->map[i]);
		    }
		}
	    }
	}
      curr_mapping++;
    }

  for (i = 0; i < norm->unicode_mappings_count; i++)
    {
      um = &(temp_list_unicode_decomp_maps[i]);
      if (um->size > 0 && unicode_decomp_map_count[um->map[0]] == 0)
	{
	  /* This means that for um->cp, the um->map can't be further decomposed, thus being the fully decomposed
	   * representation for um->cp. It will be marked as such. */
	  um->is_full_decomp = true;
	}
    }

  /* Sort/group the decompositions in list_unicode_decomp_maps by the value of the first codepoint in each mapping. The
   * grouping is necessary for optimizing the future search for possible decompositions when putting a string in fully
   * composed form. */
  qsort (temp_list_unicode_decomp_maps, norm->unicode_mappings_count, sizeof (UNICODE_CP_MAPPING),
	 comp_func_grouping_unicode_cp_mapping);

  /* Build starting indexes for each cp which is the first cp in a compact group of mappings */
  norm->unicode_mapping_index = (int *) malloc ((MAX_UNICODE_CHARS + 1) * sizeof (int));
  if (norm->unicode_mapping_index == NULL)
    {
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      err_status = ER_LOC_GEN;
      goto exit;
    }
  memset (norm->unicode_mapping_index, 0, (MAX_UNICODE_CHARS + 1) * sizeof (int));
  cp = temp_list_unicode_decomp_maps[0].map[0];
  mapping_start = 0;
  mapping_count = 1;
  for (i = 1; i < norm->unicode_mappings_count; i++)
    {
      if (temp_list_unicode_decomp_maps[i].map[0] == (uint32) cp)
	{
	  mapping_count++;
	}
      else
	{
	  SET_MAPPING_INDEX (norm->unicode_mapping_index[cp], true, mapping_start);
	  old_cp = cp;
	  cp = (uint32) temp_list_unicode_decomp_maps[i].map[0];
	  mapping_count = 1;
	  mapping_start = i;
	  for (j = old_cp + 1; j < cp; j++)
	    {
	      SET_MAPPING_INDEX (norm->unicode_mapping_index[j], false, mapping_start);
	    }
	}
    }
  SET_MAPPING_INDEX (norm->unicode_mapping_index[cp], true, mapping_start);
  SET_MAPPING_INDEX (norm->unicode_mapping_index[cp + 1], false, (mapping_start + mapping_count));

  /* Sort descending each range of UNICODE_MAPPINGs from list_unicode_decomp_maps, having the same codepoint value in
   * UNICODE_MAPPING.map[0], using memcmp. The sorting is necessary for optimizing the future search for possible
   * decompositions when putting a string in fully composed form. */
  for (cp = 0; cp < MAX_UNICODE_CHARS; cp++)
    {
      int mapping_start = 0;
      int mapping_count = 0;

      if (!CP_HAS_MAPPINGS (norm->unicode_mapping_index[cp]))
	{
	  continue;
	}
      mapping_start = GET_MAPPING_OFFSET (norm->unicode_mapping_index[cp]);
      mapping_count = GET_MAPPING_OFFSET (norm->unicode_mapping_index[cp + 1]) - mapping_start;

      qsort (temp_list_unicode_decomp_maps + mapping_start, mapping_count, sizeof (UNICODE_CP_MAPPING),
	     comp_func_unicode_cp_mapping);
    }

  err_status = unicode_make_normalization_data (temp_list_unicode_decomp_maps, ld);

exit:
  if (unicode_decomp_map_count != NULL)
    {
      free (unicode_decomp_map_count);
      unicode_decomp_map_count = NULL;
    }

  if (temp_list_unicode_decomp_maps != NULL)
    {
      for (i = 0; i < norm->unicode_mappings_count; i++)
	{
	  um = &(temp_list_unicode_decomp_maps[i]);
	  if (um->map != NULL)
	    {
	      free (um->map);
	      /* um->map = NULL not necessary, list_unicode_decomp_maps is freed afterwards. */
	    }
	}
      free (temp_list_unicode_decomp_maps);
      temp_list_unicode_decomp_maps = NULL;
    }

  return err_status;
}

/*
 * count_full_decomp_cp() - Counts the number of codepoints to needed to store
 *			  the full decomposition representation for a
 *			  codepoint.
 *
 * Returns: codepoint count
 * cp(in) : codepoint
 *
 *  Note : this is a recursive function.
 */
static int
count_full_decomp_cp (int cp)
{
  UNICODE_CHAR *uc;

  uc = &(unicode_data[cp]);
  if (cp >= MAX_UNICODE_CHARS)
    {
      return 1;
    }

  uc = &(unicode_data[cp]);

  if (uc->unicode_mapping_cp_count == 0)
    {
      return 1;
    }

  return uc->unicode_mapping_cp_count - 1 + count_full_decomp_cp ((int) uc->unicode_mapping[0]);
}

/*
 * count_decomp_steps() - Counts the number of steps for putting a codepoint
 *			into fully decomposed form, by replacing one
 *			decomposable codepoint at every step.
 *
 * Returns: step count
 * cp(in) : codepoint
 *
 *  Note : this is a recursive function.
 */
static int
count_decomp_steps (int cp)
{
  UNICODE_CHAR *uc;

  uc = &(unicode_data[cp]);
  if (uc->unicode_mapping_cp_count == 0)
    {
      return 0;
    }
  if ((uc->unicode_mapping_cp_count == 1 && uc->unicode_mapping[0] < MAX_UNICODE_CHARS)
      || (uc->unicode_mapping_cp_count > 1))
    {
      return 1 + count_decomp_steps (uc->unicode_mapping[0]);
    }

  return 0;
}

/*
 * unicode_make_normalization_data() - takes the data loaded from UnicodeData,
 *		which was previously sorted, and puts it into optimized form
 *		into the locale data structure, ready to be exported into
 *		a shared library.
 *
 * Returns: ER_LOC_GEN if error
 *	    NO_ERROR otherwise
 * decomp_maps(in): variable holding the loaded and partially processed
 *		    unicode data
 * ld(in/out): locale data
 *
 */
static int
unicode_make_normalization_data (UNICODE_CP_MAPPING * decomp_maps, LOCALE_DATA * ld)
{
  int err_status = NO_ERROR;
  int i, j;
  UNICODE_CP_MAPPING *um_cp;
  UNICODE_MAPPING *um;
  unsigned char str_buf[INTL_UTF8_MAX_CHAR_SIZE * UNICODE_DECOMP_MAP_CP_COUNT];
  unsigned char *cur_pos;
  char cur_size, byte_count;
  UNICODE_NORMALIZATION *norm;

  assert (ld != NULL);
  assert (decomp_maps != NULL);

  norm = &(ld->unicode_normalization);

  /* Prepare the unicode_mappings array for storing the data from decomp_maps as utf8 buffers + length + original
   * codepoint. */
  norm->unicode_mappings = (UNICODE_MAPPING *) malloc (norm->unicode_mappings_count * sizeof (UNICODE_MAPPING));
  if (norm->unicode_mappings == NULL)
    {
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      err_status = ER_LOC_GEN;
      goto exit;
    }
  memset (norm->unicode_mappings, 0, norm->unicode_mappings_count * sizeof (UNICODE_MAPPING));

  /* Prepare the index list for fully decomposed mappings */
  norm->list_full_decomp = (int *) malloc (MAX_UNICODE_CHARS * sizeof (int));
  if (norm->list_full_decomp == NULL)
    {
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      err_status = ER_LOC_GEN;
      goto exit;
    }
  for (i = 0; i < MAX_UNICODE_CHARS; i++)
    {
      norm->list_full_decomp[i] = -1;
    }

  /* Start importing data from decomp_maps into unicode_mappings. */
  for (i = 0; i < norm->unicode_mappings_count; i++)
    {
      um_cp = &(decomp_maps[i]);
      um = &(norm->unicode_mappings[i]);

      um->cp = um_cp->cp;

      /* Empty temporary utf8 buffer */
      memset (str_buf, 0, INTL_UTF8_MAX_CHAR_SIZE * UNICODE_DECOMP_MAP_CP_COUNT);

      /* Convert the list of codepoints into a utf8 buffer */
      cur_pos = str_buf;
      cur_size = 0;
      byte_count = 0;

      for (j = 0; j < um_cp->size; j++)
	{
	  byte_count = intl_cp_to_utf8 (um_cp->map[j], cur_pos);
	  cur_size += byte_count;
	  cur_pos += byte_count;
	}

      memset (um->buffer, 0, sizeof (um->buffer));

      /* Make the final utf8 buffer used for normalization */
      memcpy (um->buffer, str_buf, cur_size);
      um->size = cur_size;

      /* If um_cp is a fully decomposed representation for cp, mark it as such. */
      if (um_cp->is_full_decomp)
	{
	  norm->list_full_decomp[um_cp->cp] = i;
	}
    }

exit:

  return err_status;
}

#if !defined (SERVER_MODE)
/*
 * unicode_string_need_compose() - Checks if a string needs composition
 *				   and returns the size required by fully
 *				   composed form.
 *
 * Returns:
 * str_in(in) : string to normalize
 * size_in(in) : size in bytes of string
 * size_out(out) : size in bytes of composed string
 * need_compose(out) : true if composition is required, false otherwise
 * norm(in) : the unicode data for normalization
 *
 *  Note : this is light check, since full check requires more complex
 *	   processing - same as composing algorithm.
 *	   All input is assumed in UTF-8 character set
 */
bool
unicode_string_need_compose (const char *str_in, const int size_in, int *size_out, const UNICODE_NORMALIZATION * norm)
{
  const char *pc;
  const char *p_end;

  assert (size_out != NULL);

  *size_out = 0;

  if (!prm_get_bool_value (PRM_ID_UNICODE_INPUT_NORMALIZATION) || norm == NULL || size_in == 0 || str_in == NULL)
    {
      return false;
    }

  assert (str_in != NULL);

  /* If all chars are in the range 0-127, then the string is ASCII and no unicode operations are neccessary e.g.
   * composition */
  /* Reuse match_found as validation flag. */
  p_end = str_in + size_in;

  for (pc = str_in; pc < p_end; pc++)
    {
      if ((unsigned char) (*pc) >= 0x80)
	{
	  *size_out = size_in;
	  return true;
	}
    }

  return false;
}

/*
 * unicode_compose_string() - Put a string into fully composed form.
 *
 * Returns:
 * str_in(in) : string to normalize
 * size_in(in) : size in bytes of string
 * str_out(out) : preallocated buffer to store composed string, output string
 *		  is not null terminated
 * size_out(out) : actual size in bytes of composed string
 * is_composed (out) : true if the string required composition
 * norm(in) : the unicode data for normalization
 */
void
unicode_compose_string (const char *str_in, const int size_in, char *str_out, int *size_out, bool * is_composed,
			const UNICODE_NORMALIZATION * norm)
{
  char *composed_str;
  int composed_index, remaining_bytes;
  const char *str_next = NULL;
  unsigned int cp;
  int map_start, map_end, i, byte_count;
  bool match_found = false, composition_found;
  UNICODE_MAPPING *um;
  const char *str_cursor;
  const char *str_end;

  assert (prm_get_bool_value (PRM_ID_UNICODE_INPUT_NORMALIZATION) && norm != NULL && size_in > 0 && str_in != NULL);

  composed_index = 0;

  /* Build composed string */
  str_next = str_in;
  str_cursor = str_in;
  remaining_bytes = size_in;
  composition_found = false;
  composed_str = str_out;
  str_end = str_in + size_in;

  while (str_cursor < str_end)
    {
      int first_cp_size;

      cp = intl_utf8_to_cp ((unsigned char *) str_cursor, remaining_bytes, (unsigned char **) &str_next);

      first_cp_size = CAST_STRLEN (str_next - str_cursor);
      remaining_bytes -= first_cp_size;

      match_found = false;

      if (cp >= MAX_UNICODE_CHARS - 2 || !CP_HAS_MAPPINGS (norm->unicode_mapping_index[cp]))
	{
	  goto match_not_found;
	}

      map_start = GET_MAPPING_OFFSET (norm->unicode_mapping_index[cp]);
      map_end = GET_MAPPING_OFFSET (norm->unicode_mapping_index[cp + 1]);

      /* Search the mapping list for a possible match */
      for (i = map_start; i < map_end; i++)
	{
	  um = &(norm->unicode_mappings[i]);
	  if (um->size > remaining_bytes + first_cp_size)
	    {
	      continue;
	    }

	  if (memcmp (um->buffer, str_cursor, um->size) == 0)
	    {
	      /* If a composition matches, apply it. */
	      composed_index += intl_cp_to_utf8 (um->cp, (unsigned char *) (&(composed_str[composed_index])));
	      str_cursor += um->size;
	      match_found = true;
	      composition_found = true;
	      break;
	    }
	}

      /* If no composition can be matched to start with the decoded codepoint, just copy the bytes corresponding to the
       * codepoint from the input string to the output, adjust pointers and loop again. */
    match_not_found:
      if (!match_found)
	{
	  byte_count = CAST_STRLEN (str_next - str_cursor);
	  memcpy (&(composed_str[composed_index]), str_cursor, byte_count);
	  composed_index += byte_count;
	  str_cursor += byte_count;
	}
    }				/* while */

  /* Set output variables */
  *size_out = composed_index;
  if (composition_found)
    {
      *is_composed = true;
    }

  return;
}

/*
 * unicode_string_need_decompose() - Checks if a string needs
 *				     decomposition and returns the size
 *				     required by decomposed form.
 *
 * Returns: true if decomposition is required
 * str_in(in) : string to normalize
 * size_in(in) : size of string in bytes
 * decomp_size(out) : size required by decomposed form in bytes
 * norm(in) : the unicode context in which the normalization is performed
 *
 *  Note : Input string is assumed UTF-8 character set.
 */
bool
unicode_string_need_decompose (const char *str_in, const int size_in, int *decomp_size,
			       const UNICODE_NORMALIZATION * norm)
{
  int bytes_read, decomp_index, decomposed_size = 0;
  unsigned int cp;
  const char *src_cursor;
  const char *src_end;
  const char *next;
  bool can_decompose;

  if (!prm_get_bool_value (PRM_ID_UNICODE_OUTPUT_NORMALIZATION) || norm == NULL)
    {
      goto no_decompose_cnt;
    }

  assert (str_in != NULL);

  /* check if ASCII */
  can_decompose = false;
  src_end = str_in + size_in;
  for (src_cursor = str_in; src_cursor < src_end; src_cursor++)
    {
      if ((unsigned char) (*src_cursor) >= 0x80)
	{
	  can_decompose = true;
	  break;
	}
    }
  if (!can_decompose)
    {
      goto no_decompose_cnt;
    }

  /* Read each codepoint and add its expanded size to the overall size */
  src_cursor = str_in;
  next = str_in;
  can_decompose = false;
  src_end = str_in + size_in;
  while (src_cursor < src_end)
    {
      cp = intl_utf8_to_cp ((unsigned char *) src_cursor, CAST_STRLEN (src_end - src_cursor), (unsigned char **) &next);
      bytes_read = CAST_STRLEN (next - src_cursor);

      decomp_index = (cp < MAX_UNICODE_CHARS) ? norm->list_full_decomp[cp] : -1;
      if (decomp_index > -1)
	{
	  decomposed_size += norm->unicode_mappings[decomp_index].size;
	  can_decompose = true;
	}
      else
	{
	  decomposed_size += bytes_read;
	}

      src_cursor = next;
    }

  /* If no decomposition is needed, return the same size as the input string and exit. */
  if (!can_decompose)
    {
      goto no_decompose_cnt;
    }

  *decomp_size = decomposed_size;

  return true;

no_decompose_cnt:
  *decomp_size = size_in;

  return false;
}

/*
 * unicode_decompose_string() - Put a string into fully decomposed form.
 *
 * Returns: ER_OUT_OF_VIRTUAL_MEMORY if internal memory allocation fails
 *	    NO_ERROR if successfull
 * str_in(in) : string to normalize
 * size_in(in) : size in bytes of string
 * str_out(out): preallocated buffer for string in decomposed form
 * size_out(out): actual size of decomposed form in bytes
 * norm(in) : the unicode context in which the normalization is performed
 */
void
unicode_decompose_string (const char *str_in, const int size_in, char *str_out, int *size_out,
			  const UNICODE_NORMALIZATION * norm)
{
  int bytes_read, decomp_index;
  unsigned int cp;
  const char *src_cursor;
  const char *src_end;
  const char *next;
  char *dest_cursor;

  assert (prm_get_bool_value (PRM_ID_UNICODE_OUTPUT_NORMALIZATION) && norm != NULL);

  assert (str_in != NULL);
  assert (str_out != NULL);
  assert (size_out != NULL);

  src_cursor = str_in;
  dest_cursor = str_out;
  next = str_in;
  src_end = str_in + size_in;
  while (src_cursor < src_end)
    {
      cp = intl_utf8_to_cp ((unsigned char *) src_cursor, CAST_STRLEN (src_end - src_cursor), (unsigned char **) &next);
      bytes_read = CAST_STRLEN (next - src_cursor);
      decomp_index = (cp < MAX_UNICODE_CHARS) ? norm->list_full_decomp[cp] : -1;
      if (decomp_index > -1)
	{
	  memcpy (dest_cursor, norm->unicode_mappings[decomp_index].buffer, norm->unicode_mappings[decomp_index].size);
	  dest_cursor += norm->unicode_mappings[decomp_index].size;
	}
      else
	{
	  memcpy (dest_cursor, src_cursor, bytes_read);
	  dest_cursor += bytes_read;
	}
      src_cursor = next;
    }

  *size_out = CAST_STRLEN (dest_cursor - str_out);
}
#endif /* SERVER_MODE */
/*
 * comp_func_unicode_cp_mapping() - compare function for sorting a group of
 *				    unicode decompositions starting with the
 *				    same codepoint
 *
 * Returns: compare result
 * arg1(in) :
 * arg2(in) :
 */
static int
comp_func_unicode_cp_mapping (const void *arg1, const void *arg2)
{
  UNICODE_CP_MAPPING *um1, *um2;
  int min_size, result;

  um1 = (UNICODE_CP_MAPPING *) arg1;
  um2 = (UNICODE_CP_MAPPING *) arg2;

  min_size = (um1->size < um2->size) ? um1->size : um2->size;
  result = memcmp (um1->map, um2->map, min_size * sizeof (uint32));
  /* Result will be reverted to obtain reverse ordering */
  if (result == 0)
    {
      if (um1->size > min_size)
	{
	  return -1;
	}
      if (um2->size > min_size)
	{
	  return 1;
	}
      if (um1->cp < um2->cp)
	{
	  return -1;
	}
      return 1;
    }

  return -result;
}

/*
 * comp_func_grouping_unicode_cp_mapping() - compare function for sorting
 *				    all decompositions
 *
 * Returns: compare result
 * arg1(in) :
 * arg2(in) :
 */
static int
comp_func_grouping_unicode_cp_mapping (const void *arg1, const void *arg2)
{
  UNICODE_CP_MAPPING *um1, *um2;
  int result;

  um1 = (UNICODE_CP_MAPPING *) arg1;
  um2 = (UNICODE_CP_MAPPING *) arg2;

  if (um1->map[0] > um2->map[0])
    {
      result = 1;
    }
  else
    {
      result = -1;
    }

  return result;
}
