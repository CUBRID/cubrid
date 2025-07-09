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
 * uca_support.c : Unicode Collation Algorithm support
 */
#include <assert.h>

#include <errno.h>
#include "utility.h"
#include "environment_variable.h"
#include "locale_support.h"
#include "error_manager.h"
#include "porting.h"

#include "intl_support.h"
#include "uca_support.h"
#include "unicode_support.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

#define DUCET_FILE "ducet.txt"

#define MAX_WEIGHT_LEVELS 4

#define MAX_UCA_WEIGHT 0xFFFF
#define MAX_UCA_CODEPOINT 0xFFFF

#define UCA_CONTR_EXP_CNT_GROW 8

#define MAX_LOGICAL_POS	14

/* Collation element */
typedef struct uca_coll_ce UCA_COLL_CE;
struct uca_coll_ce
{
  UCA_W weight[MAX_WEIGHT_LEVELS];
};

typedef struct uca_coll_ce_list UCA_COLL_CE_LIST;
struct uca_coll_ce_list
{
  unsigned char num;
  UCA_COLL_CE ce[MAX_UCA_EXP_CE];
};

#define GET_UCA_WEIGHT(ce_list,i,w) ((ce_list)->ce[(i)].weight[(w)])
#define SET_UCA_WEIGHT(ce_list,i,w,val) \
  do { \
    (ce_list)->ce[(i)].weight[(w)] = (val); \
    } while (0);

/* UCA contraction */
typedef struct uca_chr_seq UCA_CHR_SEQ;
struct uca_chr_seq
{
  UCA_CP cp_list[LOC_MAX_UCA_CHARS_SEQ];
  int cp_count;

  UCA_COLL_CE_LIST ce;
};

typedef UCA_CHR_SEQ UCA_CONTRACTION;
typedef UCA_CHR_SEQ UCA_EXPANSION;

/* Contraction, ID tuple, used for reconciliation after contractions list optimization */
typedef struct uca_coll_contr_id UCA_COLL_CONTR_ID;
struct uca_coll_contr_id
{
  COLL_CONTRACTION contr_ref;
  int pos_id;
};

/* Collation keys : single codepoint, multiple codepoints (contractions) */
typedef enum
{
  COLL_KEY_TYPE_CP = 0,
  COLL_KEY_TYPE_CONTR,
  COLL_KEY_TYPE_EXP
} UCA_COLL_KEY_TYPE;

typedef struct uca_coll_key UCA_COLL_KEY;
struct uca_coll_key
{
  UCA_COLL_KEY_TYPE type;
  union
  {
    int cp;
    int contr_id;
    int exp_id;
  } val;
};

typedef struct uca_storage UCA_STORAGE;

typedef struct uca_weight_key_list UCA_WEIGHT_KEY_LIST;
struct uca_weight_key_list
{
  UCA_COLL_KEY *key_list;
  int list_count;
};

struct uca_storage
{
  /* single code-point CE */
  UCA_COLL_CE_LIST *coll_cp;

  /* contractions CE */
  UCA_CONTRACTION *coll_contr;
  int max_contr;
  int count_contr;

  /* tailoring expansions CE */
  UCA_EXPANSION *coll_exp;
  int max_exp;
  int count_exp;

  /* settings for previous instance (used only by DUCET storage) */
  char prev_file_path[PATH_MAX];
  int prev_contr_policy;
};

static UCA_STORAGE ducet = {
  NULL,
  NULL, 0, 0,
  NULL, 0, 0,
  "", CONTR_IGNORE
};

static UCA_STORAGE curr_uca = {
  NULL,
  NULL, 0, 0,
  NULL, 0, 0,
  "", CONTR_IGNORE
};

static UCA_W *w_occurences[MAX_WEIGHT_LEVELS];

static UCA_WEIGHT_KEY_LIST *weight_key_list;

static int logical_pos_cp[MAX_LOGICAL_POS];

/* used for sorting */
static UCA_OPTIONS *uca_tailoring_options = NULL;

static int load_ducet (const char *file_path, const int sett_contr_policy);
static int init_uca_instance (LOCALE_COLLATION * lc);
static int destroy_uca_instance (void);
static int build_key_list_groups (LOCALE_COLLATION * lc);
static void sort_coll_key_lists (LOCALE_COLLATION * lc);
static void sort_one_coll_key_list (LOCALE_COLLATION * lc, int weight_index);
static int uca_comp_func_coll_key_fo (const void *arg1, const void *arg2);
static int uca_comp_func_coll_key (const void *arg1, const void *arg2);
static UCA_COLL_CE_LIST *get_ce_list_from_coll_key (const UCA_COLL_KEY * key);
static int create_opt_weights (LOCALE_COLLATION * lc);
static int optimize_coll_contractions (LOCALE_COLLATION * lc);
static int set_next_value_for_coll_key (LOCALE_COLLATION * lc, const UCA_COLL_KEY * coll_key,
					const UCA_COLL_KEY * next_key);
static int add_opt_coll_contraction (LOCALE_COLLATION * lc, const UCA_COLL_KEY * contr_key, const unsigned int wv,
				     bool use_expansions);
static int compare_ce_list (UCA_COLL_CE_LIST * ce_list1, UCA_COLL_CE_LIST * ce_list2, UCA_OPTIONS * uca_opt);
static UCA_COLL_KEY *get_key_with_ce_sublist (UCA_COLL_CE_LIST * uca_item, const int lvl);
static void make_coll_key (UCA_COLL_KEY * key, UCA_COLL_KEY_TYPE type, const int key_id);
static int find_contr_id (const unsigned int *cp_array, const int cp_count, UCA_STORAGE * st);
static int find_exp_id (const unsigned int *cp_array, const int cp_count, UCA_STORAGE * st);
static int apply_tailoring_rule (TAILOR_DIR dir, UCA_COLL_KEY * anchor_key, UCA_COLL_KEY * key, UCA_COLL_KEY * ref_key,
				 T_LEVEL lvl);
static int apply_tailoring_rule_identity (UCA_COLL_KEY * key, UCA_COLL_KEY * ref_key);
static int apply_tailoring_rule_w_dir (TAILOR_DIR dir, UCA_COLL_KEY * anchor_key, UCA_COLL_KEY * key,
				       UCA_COLL_KEY * ref_key, T_LEVEL lvl);
static int apply_tailoring_rules (LOCALE_COLLATION * lc);
static int compute_weights_per_level_stats (void);
#if 0
static int compact_weight_values (const int level, const UCA_W max_weight);
static void build_weight_remap_filter (const UCA_W * w_ocurr, const int max_weight, UCA_W * w_filter);
#endif
static int add_key_to_weight_stats_list (const UCA_COLL_KEY * key, UCA_W wv);
static int remove_key_from_weight_stats_list (const UCA_COLL_KEY * key, UCA_W wv);
static int change_key_weight_list (const UCA_COLL_KEY * key, UCA_W w_from, UCA_W w_to);
static int string_to_coll_ce_list (char *s, UCA_COLL_CE_LIST * ui);
static int apply_absolute_tailoring_rules (LOCALE_COLLATION * lc);
static UCA_CONTRACTION *new_contraction (UCA_STORAGE * storage);
static int add_uca_contr_or_exp (LOCALE_COLLATION * lc, UCA_STORAGE * storage, const unsigned int *cp_array,
				 const int cp_count, const UCA_COLL_KEY_TYPE seq_type);
static int read_cp_from_tag (unsigned char *buffer, CP_BUF_TYPE type, UCA_CP * cp);

static int comp_func_coll_contr_bin (const void *arg1, const void *arg2);

static int create_opt_ce_w_exp (LOCALE_COLLATION * lc);

static int uca_comp_func_coll_list_exp_fo (const void *arg1, const void *arg2);
static int uca_comp_func_coll_list_exp (const void *arg1, const void *arg2);

static void build_compressed_uca_w_l13 (const UCA_COLL_CE_LIST * ce_list, UCA_L13_W * uca_w_l13);
static void build_uca_w_l4 (const UCA_COLL_CE_LIST * ce_list, UCA_L4_W * uca_w_l4);
/*
 * load_ducet - Read the DUCET file (standardised and availabe at Unicode.org)
 *		into the ducet array; parse it and load all the information in
 *		the ducet storage.
 * Returns: error status
 *
 * file_path(in): file path for DUCET file
 * sett_contr_policy(in): behavior for contractions
 *
 */
static int
load_ducet (const char *file_path, const int sett_contr_policy)
{
  FILE *f = NULL;
  char str[256];
  char str_ref[256];
  char str_orig[256];
  char *weights[64];
  int lines = 1;
  int i;
  int err_status = NO_ERROR;
  char err_msg[ERR_MSG_SIZE];
  unsigned int cp;
  int w;

  UCA_COLL_CE_LIST *temp_ducet = NULL;
  UCA_COLL_CE_LIST *ducet_cp = NULL;

  assert (file_path != NULL);

  if (strcmp (file_path, ducet.prev_file_path) == 0 && sett_contr_policy == ducet.prev_contr_policy)
    {
      /* already loaded */
      return NO_ERROR;
    }

  uca_free_data ();

  temp_ducet = (UCA_COLL_CE_LIST *) malloc ((MAX_UCA_CODEPOINT + 1) * sizeof (UCA_COLL_CE_LIST));
  if (temp_ducet == NULL)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      goto exit;
    }

  ducet_cp = ducet.coll_cp = temp_ducet;

  for (i = 0; i <= MAX_UCA_CODEPOINT; i++)
    {
      ducet_cp[i].num = 0;
      for (w = 0; w < MAX_WEIGHT_LEVELS; w++)
	{
	  int ce_pos;

	  for (ce_pos = 0; ce_pos < MAX_UCA_EXP_CE; ce_pos++)
	    {
	      SET_UCA_WEIGHT (&(ducet_cp[i]), ce_pos, w, 0);
	    }
	}
    }

  f = fopen_ex (file_path, "rt");
  if (f == NULL)
    {
      err_status = ER_LOC_GEN;
      snprintf_dots_truncate (err_msg, sizeof (err_msg) - 1, "Cannot open file %s", file_path);
      LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
      goto exit;
    }

  assert (RULE_POS_LAST_TRAIL < sizeof (logical_pos_cp) / sizeof (logical_pos_cp[0]));
  for (i = 0; i < (int) (sizeof (logical_pos_cp) / sizeof (logical_pos_cp[0])); i++)
    {
      logical_pos_cp[i] = -1;
    }

  while (fgets (str, sizeof (str), f))
    {
      char *comment;
      char *weight;
      char *s, *save;
      int codenum;
      bool is_variable = false;
      char is_ignorable[4] = { -1, -1, -1, -1 };
      UCA_COLL_CE_LIST *ce_list = NULL;
      UCA_CP cp_list[LOC_MAX_UCA_CHARS_SEQ];
      uint32 cp_int_list[LOC_MAX_UCA_CHARS_SEQ];
      int cp_list_count = 0;
      bool is_allowed = true;
      int val;

      lines++;

      strcpy (str_ref, str);
      strcpy (str_orig, str);
      str_to_int32 (&val, &s, str, 16);
      cp = (unsigned int) val;

      /* Skip comment lines and unicode values above max allowed CP */
      if (str[0] == '#' || (cp > MAX_UCA_CODEPOINT))
	{
	  continue;
	}

      if ((comment = strchr (str, '#')))
	{
	  /* Remove comments found after the weight list. */
	  *comment++ = '\0';
	  for (; *comment == ' '; comment++);
	}
      else
	{
	  /* Skip empty lines and @version line. */
	  continue;
	}

      if ((weight = strchr (str, ';')))
	{
	  /* Drop chars after ';' until '[' reached */
	  *weight++ = '\0';
	  for (; *weight == ' '; weight++);
	  /* At this point, we have the unicode representation stored in str and weight items stored in the weight
	   * string var */
	}
      else
	{
	  /* If the "if" tests above pass, but the line does not contain ';' then the line is not valid, so skip it */
	  continue;
	}

      /* Count number of chars in Uncode item e.g. codenum will be greater that 1 if contraction found. */
      codenum = 1;
      cp_int_list[cp_list_count++] = cp;
      str_to_int32 (&val, &s, str, 16);
      codenum += string_to_int_array (s, &(cp_int_list[1]), LOC_MAX_UCA_CHARS_SEQ - 1, " \t");

      if (codenum > LOC_MAX_UCA_CHARS_SEQ)
	{
	  /* If the number of codepoints found in the input string is greater than LOC_MAX_UCA_CHARS_SEQ, only the
	   * first LOC_MAX_UCA_CHARS_SEQ are used, and no error is thrown. */
	  codenum = LOC_MAX_UCA_CHARS_SEQ;
	}

      cp_list_count = codenum;

      for (i = 0; i < codenum; i++)
	{
	  if (cp_int_list[i] > MAX_UCA_CODEPOINT)
	    {
	      is_allowed = false;
	      cp_list_count = i;
	      break;
	    }
	  cp_list[i] = (UCA_CP) cp_int_list[i];
	}

      if (!is_allowed)
	{
	  continue;
	}

      if (codenum > 1)
	{
	  UCA_CONTRACTION *contr = NULL;

	  if ((sett_contr_policy & CONTR_DUCET_USE) != CONTR_DUCET_USE)
	    {
	      continue;
	    }

	  assert (codenum <= LOC_MAX_UCA_CHARS_SEQ);

	  codenum = MIN (codenum, LOC_MAX_UCA_CHARS_SEQ);

	  contr = new_contraction (&ducet);
	  if (contr == NULL)
	    {
	      err_status = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto exit;
	    }

	  contr->cp_count = cp_list_count;
	  memcpy (contr->cp_list, cp_list, cp_list_count * sizeof (UCA_CP));

	  ce_list = &(contr->ce);
	}
      else
	{
	  ce_list = &(ducet_cp[cp]);
	}

      assert (ce_list != NULL);

      ce_list->num = 0;
      s = strtok_r (weight, " []", &save);
      while (s)
	{
	  /* Count the number of collation elements of the current char */
	  weights[ce_list->num] = s;
	  s = strtok_r (NULL, " []", &save);
	  ce_list->num++;
	}

      for (w = 0; w < ce_list->num; w++)
	{
	  int partnum;

	  partnum = 0;
	  s = weights[w];

	  is_variable = ((*s) == '*') ? true : false;

	  /* Now, in weights[...] we have the collation elements stored as strings We decompose the N collation
	   * elements into 4 weight values and store them into uca[code].weight[1..4][1..N] */
	  while (*s)
	    {
	      char *endptr;
	      int result = 0;
	      int val;

	      result = str_to_int32 (&val, &endptr, s + 1, 16);
	      SET_UCA_WEIGHT (ce_list, w, partnum, (int) val);

	      assert (partnum < 4);
	      if (val == 0 && is_ignorable[partnum] == -1)
		{
		  is_ignorable[partnum] = 1;
		}
	      else
		{
		  is_ignorable[partnum] = 0;
		}

	      s = endptr;
	      partnum++;
	    }
	}

      if (is_variable)
	{
	  logical_pos_cp[RULE_POS_LAST_VAR] = cp;
	  if (logical_pos_cp[RULE_POS_FIRST_VAR] == -1)
	    {
	      logical_pos_cp[RULE_POS_FIRST_VAR] = cp;
	    }
	}

      assert (is_ignorable[0] == 0 || is_ignorable[0] == 1);
      assert (is_ignorable[1] == 0 || is_ignorable[1] == 1);
      assert (is_ignorable[2] == 0 || is_ignorable[2] == 1);

      if (is_ignorable[0] == 1)
	{
	  logical_pos_cp[RULE_POS_LAST_PRI_IGN] = cp;
	  if (logical_pos_cp[RULE_POS_FIRST_PRI_IGN] == -1)
	    {
	      logical_pos_cp[RULE_POS_FIRST_PRI_IGN] = cp;
	    }
	}

      if (is_ignorable[1] == 1)
	{
	  logical_pos_cp[RULE_POS_LAST_SEC_IGN] = cp;
	  if (logical_pos_cp[RULE_POS_FIRST_SEC_IGN] == -1)
	    {
	      logical_pos_cp[RULE_POS_FIRST_SEC_IGN] = cp;
	    }
	}

      if (is_ignorable[2] == 1)
	{
	  logical_pos_cp[RULE_POS_LAST_TERT_IGN] = cp;
	  if (logical_pos_cp[RULE_POS_FIRST_TERT_IGN] == -1)
	    {
	      logical_pos_cp[RULE_POS_FIRST_TERT_IGN] = cp;
	    }
	}

      if (is_ignorable[0] == 0 && is_ignorable[1] == 0 && is_ignorable[2] == 0)
	{
	  logical_pos_cp[RULE_POS_LAST_NON_IGN] = cp;
	  if (logical_pos_cp[RULE_POS_FIRST_NON_IGN] == -1)
	    {
	      logical_pos_cp[RULE_POS_FIRST_NON_IGN] = cp;
	    }
	}
    }

  fclose (f);
  f = NULL;

  /* Set implicit weights for unicode values not found in the DUCET file */
  for (cp = 0; cp <= MAX_UCA_CODEPOINT; cp++)
    {
      unsigned int base, aaaa, bbbb;

      /* Skip if the Unicode value was found in the DUCET file */
      if (ducet_cp[cp].num)
	{
	  continue;
	}

      /*
       * 3400;<CJK Ideograph Extension A, First> 4DB5;<CJK Ideograph Extension A, Last> 4E00;<CJK Ideograph, First>
       * 9FA5;<CJK Ideograph, Last> */

      if (cp >= 0x3400 && cp <= 0x4DB5)
	{
	  base = 0xFB80;
	}
      else if (cp >= 0x4E00 && cp <= 0x9FA5)
	{
	  base = 0xFB40;
	}
      else
	{
	  base = 0xFBC0;
	}

      aaaa = base + (cp >> 15);
      bbbb = (cp & 0x7FFF) | 0x8000;
      SET_UCA_WEIGHT (&(ducet_cp[cp]), 0, 0, aaaa);
      SET_UCA_WEIGHT (&(ducet_cp[cp]), 1, 0, bbbb);

      SET_UCA_WEIGHT (&(ducet_cp[cp]), 0, 1, 0x0020);
      SET_UCA_WEIGHT (&(ducet_cp[cp]), 1, 1, 0x0000);

      SET_UCA_WEIGHT (&(ducet_cp[cp]), 0, 2, 0x0002);
      SET_UCA_WEIGHT (&(ducet_cp[cp]), 1, 2, 0x0000);

      SET_UCA_WEIGHT (&(ducet_cp[cp]), 0, 3, 0x0001);
      SET_UCA_WEIGHT (&(ducet_cp[cp]), 1, 3, 0x0000);

      ducet_cp[cp].num = 2;
    }

exit:
  strncpy (ducet.prev_file_path, file_path, sizeof (ducet.prev_file_path));
  ducet.prev_file_path[sizeof (ducet.prev_file_path) - 1] = '\0';

  ducet.prev_contr_policy = sett_contr_policy;

  if (f != NULL)
    {
      fclose (f);
    }

  return err_status;
}

/*
 * init_uca_instance - Prepares one UCA instance for processing
 * Returns :  ERR_OUT_OF_VIRTUAL_MEMORY - if an allocation fails;
 *	      NO_ERROR - success if otherwise.
 */
static int
init_uca_instance (LOCALE_COLLATION * lc)
{
  int i;
  int err_status = NO_ERROR;
  UCA_COLL_CE_LIST *uca_cp = NULL;
  char ducet_file_path[PATH_MAX];

  assert (lc != NULL);

  uca_cp = (UCA_COLL_CE_LIST *) malloc ((MAX_UCA_CODEPOINT + 1) * sizeof (UCA_COLL_CE_LIST));
  if (uca_cp == NULL)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      goto exit;
    }
  memset (uca_cp, 0, (MAX_UCA_CODEPOINT + 1) * sizeof (UCA_COLL_CE_LIST));

  curr_uca.coll_cp = uca_cp;

  envvar_localedatadir_file (ducet_file_path, sizeof (ducet_file_path), DUCET_FILE);
  err_status = load_ducet (ducet_file_path, lc->tail_coll.uca_opt.sett_contr_policy);

  if (err_status != NO_ERROR)
    {
      goto exit;
    }
  memcpy (uca_cp, ducet.coll_cp, (MAX_UCA_CODEPOINT + 1) * sizeof (UCA_COLL_CE_LIST));

  /* copy contractions */
  for (i = 0; i < ducet.count_contr; i++)
    {
      UCA_CONTRACTION *ducet_contr = &(ducet.coll_contr[i]);
      UCA_CONTRACTION *uca_contr;
      int j;

      for (j = 0; j < ducet_contr->cp_count; j++)
	{
	  if (ducet_contr->cp_list[j] >= lc->tail_coll.sett_max_cp)
	    {
	      err_status = ER_LOC_GEN;
	      LOG_LOCALE_ERROR ("Codepoint in DUCET contraction exceeds " "maximum allowed codepoint", ER_LOC_GEN,
				true);
	      goto exit;
	    }
	}

      uca_contr = new_contraction (&curr_uca);

      if (uca_contr == NULL)
	{
	  err_status = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit;
	}

      uca_contr->cp_count = ducet_contr->cp_count;
      memcpy (uca_contr->cp_list, ducet_contr->cp_list, ducet_contr->cp_count * sizeof (UCA_CP));
      memcpy (&(uca_contr->ce), &(ducet_contr->ce), sizeof (UCA_COLL_CE_LIST));
    }

  for (i = 0; i < MAX_WEIGHT_LEVELS; i++)
    {
      w_occurences[i] = (UCA_W *) malloc ((MAX_UCA_WEIGHT + 1) * sizeof (UCA_W));

      if (w_occurences[i] == NULL)
	{
	  err_status = ER_LOC_GEN;
	  LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
	  goto exit;
	}

      memset (w_occurences[i], 0, (MAX_UCA_WEIGHT + 1) * sizeof (UCA_W));
    }

  weight_key_list = (UCA_WEIGHT_KEY_LIST *) malloc ((MAX_UCA_WEIGHT + 1) * sizeof (UCA_WEIGHT_KEY_LIST));
  if (weight_key_list == NULL)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      goto exit;
    }
  memset (weight_key_list, 0, (MAX_UCA_WEIGHT + 1) * sizeof (UCA_WEIGHT_KEY_LIST));

exit:
  return err_status;
}

/*
 * destroy_uca_instance - Unload the UCa array and all auxiliary arrays.
 * Returns :  NO_ERROR.
 */
static int
destroy_uca_instance (void)
{
  int i;

  if (curr_uca.coll_cp != NULL)
    {
      free (curr_uca.coll_cp);
      curr_uca.coll_cp = NULL;
    }

  if (curr_uca.coll_contr != NULL)
    {
      assert (curr_uca.max_contr > 0);

      free (curr_uca.coll_contr);
      curr_uca.coll_contr = NULL;
      curr_uca.max_contr = 0;
      curr_uca.count_contr = 0;
    }

  if (curr_uca.coll_exp != NULL)
    {
      assert (curr_uca.max_exp > 0);

      free (curr_uca.coll_exp);
      curr_uca.coll_exp = NULL;
      curr_uca.max_exp = 0;
      curr_uca.count_exp = 0;
    }

  for (i = 0; i < MAX_WEIGHT_LEVELS; i++)
    {
      if (w_occurences[i] != NULL)
	{
	  free (w_occurences[i]);
	  w_occurences[i] = NULL;
	}
    }

  if (weight_key_list != NULL)
    {
      for (i = 0; i <= MAX_UCA_WEIGHT; i++)
	{
	  if (weight_key_list[i].key_list != NULL)
	    {
	      free (weight_key_list[i].key_list);
	      weight_key_list[i].key_list = NULL;
	    }
	}
      free (weight_key_list);
      weight_key_list = NULL;
    }

  return NO_ERROR;
}

/*
 * compare_ce_list - compares two lists of collation elements using the
 *		     settings of locale
 * Returns : -1 if ce_list1 collates before ce_list2;
 *	      0 if ce_list1 collates on the same level as ce_list2;
 *	      1 if ce_list1 collates after ce_list2.
 * ce_list1 (in) : collation element list to compare.
 * ce_list2 (in) : collation element list to compare.
 * uca_opt(in) : sorting options
 */
static int
compare_ce_list (UCA_COLL_CE_LIST * ce_list1, UCA_COLL_CE_LIST * ce_list2, UCA_OPTIONS * uca_opt)
{
  int result, i, weight_level;
  int numCodepoints = 0;

  assert (ce_list1 != NULL);
  assert (ce_list2 != NULL);
  assert (uca_opt != NULL);

  numCodepoints = MIN (ce_list1->num, ce_list2->num);

  if (numCodepoints == 0)
    {
      /* one of the chars has no codepoints attached */
      if (ce_list1->num > 0)
	{
	  return 1;
	}
      else if (ce_list2->num > 0)
	{
	  return -1;
	}

      return 0;
    }
  result = 0;

  numCodepoints = MAX (ce_list1->num, ce_list2->num);

  if (uca_opt->use_only_first_ce)
    {
      numCodepoints = MIN (numCodepoints, 1);
    }

  for (weight_level = 0; (weight_level < MAX_WEIGHT_LEVELS) && (result == 0); weight_level++)
    {
      if (weight_level + 1 > (int) (uca_opt->sett_strength)
	  && ((uca_opt->sett_caseFirst + (uca_opt->sett_caseLevel ? 1 : 0) == 0)
	      || ((uca_opt->sett_caseFirst + (uca_opt->sett_caseLevel ? 1 : 0) > 0) && weight_level != 2)))
	{
	  continue;
	}

      for (i = 0; (i < numCodepoints) && (result == 0); i++)
	{
	  if (GET_UCA_WEIGHT (ce_list1, i, weight_level) < GET_UCA_WEIGHT (ce_list2, i, weight_level))
	    {
	      result = -1;
	    }
	  else if (GET_UCA_WEIGHT (ce_list1, i, weight_level) > GET_UCA_WEIGHT (ce_list2, i, weight_level))
	    {
	      result = 1;
	    }

	  /* accents level */
	  if (weight_level == 1 && uca_opt->sett_backwards == 1)
	    {
	      /* backwards */
	      result = -result;
	    }
	  else if (weight_level == 2 && uca_opt->sett_caseFirst == 1)
	    {
	      /* caseFirst = upper : revert L3 DUCET order */
	      result = -result;
	    }
	}
    }

  return result;
}

/*
 * uca_process_collation - Calls all the functions which
 *			   actually do the processing.
 * Returns: ER_LOC_GEN if a tailoring rule occurs;
 *	    ER_OUT_OF_VIRTUAL_MEMORY if some memory allocation fails;
 *	    NO_ERROR if tailoring is successful.
 * lc(in/out) : contains the collation settings and optimization results.
 *
 */
int
uca_process_collation (LOCALE_COLLATION * lc, bool is_verbose)
{
  int err_status = NO_ERROR;

  if (is_verbose)
    {
      printf ("Initializing UCA\n");
    }

  err_status = init_uca_instance (lc);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

  if (is_verbose)
    {
      printf ("DUCET file has %d contractions\n", ducet.count_contr);
      printf ("Applying %d CUBRID tailoring rules\n", lc->tail_coll.cub_count_rules);
    }

  uca_tailoring_options = &(lc->tail_coll.uca_opt);
  err_status = apply_absolute_tailoring_rules (lc);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

  if (is_verbose)
    {
      printf ("Build weight statistics\n");
    }

  err_status = compute_weights_per_level_stats ();
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

  err_status = build_key_list_groups (lc);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

  if (is_verbose)
    {
      printf ("Applying %d UCA tailoring rules\n", lc->tail_coll.count_rules);
    }
  err_status = apply_tailoring_rules (lc);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

  if (lc->tail_coll.uca_opt.sett_expansions)
    {
      if (is_verbose)
	{
	  printf ("Building optimized weights with expansions\n");
	}

      err_status = create_opt_ce_w_exp (lc);
      if (err_status != NO_ERROR)
	{
	  goto exit;
	}
    }
  else
    {
      if (is_verbose)
	{
	  printf ("Sorting weight keys lists\n");
	}
      sort_coll_key_lists (lc);

      if (is_verbose)
	{
	  printf ("Building optimized weights\n");
	}

      err_status = create_opt_weights (lc);
      if (err_status != NO_ERROR)
	{
	  goto exit;
	}
    }

  memcpy (&(lc->opt_coll.uca_opt), &(lc->tail_coll.uca_opt), sizeof (UCA_OPTIONS));
exit:
  destroy_uca_instance ();
  uca_tailoring_options = NULL;
  if (is_verbose)
    {
      printf ("UCA finished\n");
    }
  return err_status;
}

/*
 * apply_tailoring_rules - Loop through the tailoring rules in
 *			   LOCALE_COLLATION::coll.rules, parse the composed
 *			   rules if any, and call the function which does
 *			   the tailoring e.g. execute the rule on the
 *			   already processed data.
 * Returns: error status
 * lc (in/out) : collation settings and optimization results.
 */
static int
apply_tailoring_rules (LOCALE_COLLATION * lc)
{
  int i;
  UCA_COLL_KEY anchor_key;
  UCA_COLL_KEY ref_key;
  UCA_COLL_KEY tailor_key;
  unsigned char *ptr_uchar;
  int err_status = NO_ERROR;
  TAILOR_RULE *t_rule = NULL;
  char er_msg[ERR_MSG_SIZE];

  for (i = 0; i < lc->tail_coll.count_rules; i++)
    {
      unsigned int anchor_cp_list[LOC_MAX_UCA_CHARS_SEQ];
      unsigned int ref_cp_list[LOC_MAX_UCA_CHARS_SEQ];
      int anchor_cp_count = 0;
      int ref_cp_count = 0;
      int buf_size;
      int cp_found;
      int contr_id;

      t_rule = &(lc->tail_coll.rules[i]);

      /* anchor key : */
      if (t_rule->r_pos_type != RULE_POS_BUFFER)
	{
	  int anchor_cp;

	  assert (t_rule->r_pos_type > 0);
	  assert (t_rule->r_pos_type < MAX_LOGICAL_POS);

	  anchor_cp = logical_pos_cp[t_rule->r_pos_type];

	  assert (anchor_cp >= 0 && anchor_cp <= MAX_UCA_CODEPOINT);
	  if (anchor_cp >= lc->tail_coll.sett_max_cp)
	    {
	      err_status = ER_LOC_GEN;
	      snprintf (er_msg, sizeof (er_msg) - 1, "Invalid anchor in rule :%d. Codepoint value too big", i);
	      LOG_LOCALE_ERROR (er_msg, ER_LOC_GEN, true);
	      goto exit;
	    }

	  assert (anchor_cp > 0);
	  make_coll_key (&anchor_key, COLL_KEY_TYPE_CP, anchor_cp);
	  memcpy (&ref_key, &anchor_key, sizeof (UCA_COLL_KEY));
	}
      else
	{
	  buf_size = strlen (t_rule->anchor_buf);
	  ptr_uchar = (unsigned char *) t_rule->anchor_buf;

	  cp_found =
	    intl_utf8_to_cp_list (ptr_uchar, buf_size, anchor_cp_list, LOC_MAX_UCA_CHARS_SEQ, &anchor_cp_count);

	  if (cp_found <= 0 || cp_found > anchor_cp_count)
	    {
	      err_status = ER_LOC_GEN;
	      snprintf (er_msg, sizeof (er_msg) - 1, "Invalid anchor in rule :%d", i);
	      LOG_LOCALE_ERROR (er_msg, ER_LOC_GEN, true);
	      goto exit;
	    }

	  if (anchor_cp_count == 1)
	    {
	      assert (*anchor_cp_list > 0);
	      make_coll_key (&anchor_key, COLL_KEY_TYPE_CP, *anchor_cp_list);
	    }
	  else
	    {
	      assert (anchor_cp_count > 1);

	      /* contraction or expansion */
	      if ((lc->tail_coll.uca_opt.sett_contr_policy & CONTR_TAILORING_USE) != CONTR_TAILORING_USE
		  && !lc->tail_coll.uca_opt.sett_expansions)
		{
		  continue;
		}

	      contr_id = find_contr_id (anchor_cp_list, anchor_cp_count, &curr_uca);

	      if (contr_id != -1)
		{
		  make_coll_key (&anchor_key, COLL_KEY_TYPE_CONTR, contr_id);
		}
	      else
		{
		  int exp_id;
		  assert (contr_id == -1);

		  /* this is an expansion */
		  if (!lc->tail_coll.uca_opt.sett_expansions)
		    {
		      /* ignore expansions */
		      continue;
		    }

		  exp_id = find_exp_id (anchor_cp_list, anchor_cp_count, &curr_uca);

		  if (exp_id == -1)
		    {
		      exp_id = add_uca_contr_or_exp (lc, &curr_uca, anchor_cp_list, anchor_cp_count, COLL_KEY_TYPE_EXP);
		    }

		  if (exp_id == -1)
		    {
		      err_status = ER_LOC_GEN;
		      snprintf (er_msg, sizeof (er_msg) - 1, "Invalid anchor in rule :%d." "Cannot create expansion",
				i);
		      LOG_LOCALE_ERROR (er_msg, ER_LOC_GEN, true);
		      goto exit;
		    }

		  make_coll_key (&anchor_key, COLL_KEY_TYPE_EXP, exp_id);
		}
	    }


	  /* reference key */
	  buf_size = t_rule->r_buf_size;
	  ptr_uchar = (unsigned char *) t_rule->r_buf;
	  cp_found = intl_utf8_to_cp_list (ptr_uchar, buf_size, ref_cp_list, LOC_MAX_UCA_CHARS_SEQ, &ref_cp_count);

	  if (cp_found <= 0 || cp_found > ref_cp_count)
	    {
	      err_status = ER_LOC_GEN;
	      snprintf (er_msg, sizeof (er_msg) - 1, "Invalid reference in rule :%d", i);
	      LOG_LOCALE_ERROR (er_msg, ER_LOC_GEN, true);
	      goto exit;
	    }

	  if (ref_cp_count == 1)
	    {
	      if ((int) (*ref_cp_list) >= lc->tail_coll.sett_max_cp)
		{
		  err_status = ER_LOC_GEN;
		  snprintf (er_msg, sizeof (er_msg) - 1, "Invalid reference in rule :%d." " Codepoint value too big",
			    i);
		  LOG_LOCALE_ERROR (er_msg, ER_LOC_GEN, true);
		  goto exit;
		}

	      assert (*ref_cp_list > 0);
	      make_coll_key (&ref_key, COLL_KEY_TYPE_CP, *ref_cp_list);
	    }
	  else
	    {
	      assert (ref_cp_count > 1);

	      /* contraction or expansion */
	      if ((lc->tail_coll.uca_opt.sett_contr_policy & CONTR_TAILORING_USE) != CONTR_TAILORING_USE
		  && !lc->tail_coll.uca_opt.sett_expansions)
		{
		  continue;
		}

	      contr_id = find_contr_id (ref_cp_list, ref_cp_count, &curr_uca);

	      if (contr_id != -1)
		{
		  make_coll_key (&ref_key, COLL_KEY_TYPE_CONTR, contr_id);
		}
	      else
		{
		  int exp_id;

		  /* expansion */
		  assert (contr_id == -1);

		  if (!lc->tail_coll.uca_opt.sett_expansions)
		    {
		      continue;
		    }

		  exp_id = find_exp_id (ref_cp_list, ref_cp_count, &curr_uca);
		  if (exp_id == -1)
		    {
		      exp_id = add_uca_contr_or_exp (lc, &curr_uca, ref_cp_list, ref_cp_count, COLL_KEY_TYPE_EXP);
		    }

		  if (exp_id == -1)
		    {
		      err_status = ER_LOC_GEN;
		      snprintf (er_msg, sizeof (er_msg) - 1, "Invalid reference in rule: %d" "Cannot create expansion",
				i);
		      LOG_LOCALE_ERROR (er_msg, ER_LOC_GEN, true);
		      goto exit;
		    }

		  make_coll_key (&ref_key, COLL_KEY_TYPE_EXP, exp_id);
		}
	    }
	}

      if (t_rule->multiple_chars)
	{
	  unsigned char *tailor_curr;
	  unsigned char *tailor_next;
	  unsigned char *tailor_end;

	  tailor_curr = (unsigned char *) (t_rule->t_buf);
	  tailor_next = tailor_curr;
	  tailor_end = tailor_curr + t_rule->t_buf_size;

	  while (tailor_next < tailor_end)
	    {
	      unsigned int tailor_cp =
		intl_utf8_to_cp (tailor_curr, CAST_STRLEN (tailor_end - tailor_curr), &tailor_next);

	      assert (lc->tail_coll.sett_max_cp >= 0);
	      if (tailor_cp >= (unsigned int) lc->tail_coll.sett_max_cp)
		{
		  err_status = ER_LOC_GEN;
		  snprintf (er_msg, sizeof (er_msg) - 1, "Invalid tailoring in rule :%d." "Codepoint : %4X too big", i,
			    tailor_cp);
		  LOG_LOCALE_ERROR (er_msg, ER_LOC_GEN, true);
		  goto exit;
		}

	      assert (tailor_cp > 0);
	      make_coll_key (&tailor_key, COLL_KEY_TYPE_CP, tailor_cp);

	      err_status = apply_tailoring_rule (t_rule->direction, &anchor_key, &tailor_key, &ref_key, t_rule->level);
	      if (err_status != NO_ERROR)
		{
		  err_status = ER_LOC_GEN;
		  snprintf (er_msg, sizeof (er_msg) - 1, "Cannot apply :%d", i);
		  LOG_LOCALE_ERROR (er_msg, ER_LOC_GEN, true);
		  goto exit;
		}

	      tailor_curr = tailor_next;

	      memcpy (&ref_key, &tailor_key, sizeof (UCA_COLL_KEY));
	    }
	}
      else
	{
	  unsigned int tailor_cp_list[LOC_MAX_UCA_CHARS_SEQ];
	  int tailor_cp_count;

	  buf_size = t_rule->t_buf_size;
	  ptr_uchar = (unsigned char *) t_rule->t_buf;
	  cp_found =
	    intl_utf8_to_cp_list (ptr_uchar, buf_size, tailor_cp_list, LOC_MAX_UCA_CHARS_SEQ, &tailor_cp_count);

	  if (cp_found <= 0 || cp_found > tailor_cp_count)
	    {
	      err_status = ER_LOC_GEN;
	      snprintf (er_msg, sizeof (er_msg) - 1,
			"Invalid tailoring in rule :%d." "Invalid number of codepoints: %d", i, cp_found);
	      LOG_LOCALE_ERROR (er_msg, ER_LOC_GEN, true);
	      goto exit;
	    }

	  if (tailor_cp_count > 1)
	    {
	      /* contraction */
	      if ((lc->tail_coll.uca_opt.sett_contr_policy & CONTR_TAILORING_USE) != CONTR_TAILORING_USE)
		{
		  continue;
		}

	      contr_id = find_contr_id (tailor_cp_list, tailor_cp_count, &curr_uca);

	      if (contr_id == -1)
		{
		  contr_id = add_uca_contr_or_exp (lc, &curr_uca, tailor_cp_list, tailor_cp_count, COLL_KEY_TYPE_CONTR);
		}

	      if (contr_id == -1)
		{
		  err_status = ER_LOC_GEN;
		  snprintf (er_msg, sizeof (er_msg) - 1, "Rule :%d. Cannot create contraction.", i);
		  LOG_LOCALE_ERROR (er_msg, ER_LOC_GEN, true);
		  goto exit;
		}

	      make_coll_key (&tailor_key, COLL_KEY_TYPE_CONTR, contr_id);
	    }
	  else
	    {
	      if ((int) (*tailor_cp_list) >= lc->tail_coll.sett_max_cp)
		{
		  err_status = ER_LOC_GEN;
		  snprintf (er_msg, sizeof (er_msg) - 1, "Invalid tailoring in rule :%d." " Codepoint value too big",
			    i);
		  LOG_LOCALE_ERROR (er_msg, ER_LOC_GEN, true);
		  goto exit;
		}

	      assert (*tailor_cp_list > 0);
	      make_coll_key (&tailor_key, COLL_KEY_TYPE_CP, *tailor_cp_list);
	    }

	  err_status = apply_tailoring_rule (t_rule->direction, &anchor_key, &tailor_key, &ref_key, t_rule->level);
	  if (err_status != NO_ERROR)
	    {
	      snprintf (er_msg, sizeof (er_msg) - 1, "Rule :%d", i);
	      LOG_LOCALE_ERROR (er_msg, ER_LOC_GEN, true);
	      goto exit;
	    }
	}
    }

exit:
  return err_status;
}

/*
 * compute_weights_per_level_stats - Build statistics regarding the number of
 *				     occurences of each weight on each level
 *				     in each collation element.
 */

static int
compute_weights_per_level_stats (void)
{
  int depth, weight_level;
  int cp, i;
  UCA_COLL_CE_LIST *uca_cp = curr_uca.coll_cp;
  int used_weights[MAX_WEIGHT_LEVELS];
  UCA_W max_weight_val[MAX_WEIGHT_LEVELS];
  int err_status = NO_ERROR;

  for (cp = 0; cp <= MAX_UCA_CODEPOINT; cp++)
    {
      for (depth = 0; depth < uca_cp[cp].num; depth++)
	{
	  for (weight_level = 0; weight_level < MAX_WEIGHT_LEVELS; weight_level++)
	    {
	      UCA_W w = GET_UCA_WEIGHT (&(uca_cp[cp]), depth, weight_level);
	      w_occurences[weight_level][w]++;
	    }
	}
    }

  for (i = 0; i < curr_uca.count_contr; i++)
    {
      UCA_CONTRACTION *contr = &(curr_uca.coll_contr[i]);

      for (depth = 0; depth < contr->ce.num; depth++)
	{
	  for (weight_level = 0; weight_level < MAX_WEIGHT_LEVELS; weight_level++)
	    {
	      UCA_W w = GET_UCA_WEIGHT (&(contr->ce), depth, weight_level);
	      w_occurences[weight_level][w]++;
	    }
	}
    }

  /* how many weight values are really used */
  for (weight_level = 0; weight_level < MAX_WEIGHT_LEVELS; weight_level++)
    {
      used_weights[weight_level] = 0;
      max_weight_val[weight_level] = 0;
      for (i = 0; i < MAX_UCA_WEIGHT + 1; i++)
	{
	  if (w_occurences[weight_level][i] != 0)
	    {
	      used_weights[weight_level]++;
	      max_weight_val[weight_level] = i;
	    }
	}
    }

  if (max_weight_val[1] > 0x1ff || max_weight_val[2] > 0x7f + 1)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("Cannot store weights. Max weight values exceeded", ER_LOC_GEN, true);
      goto exit;
    }

exit:
  return err_status;
}

#if 0
/*
 * compact_weight_values - rewrites the weight values for one level in UCA
 *			   storage so that the weights are compact values.
 *
 *  return : error code
 *  level(in): level to compact weights
 *  max_weight(in): max weight value at this level
 *
 * Note : this function uses the global 'w_occurences' array
 */
static int
compact_weight_values (const int level, const UCA_W max_weight)
{
  UCA_W *w_filter = NULL;
  UCA_COLL_CE_LIST *uca_cp = curr_uca.coll_cp;
  int err_status = NO_ERROR;
  int cp, i, depth;

  assert (level >= 0 && level <= 3);

  w_filter = (UCA_W *) malloc ((max_weight + 1) * sizeof (UCA_W));
  if (w_filter == NULL)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      goto exit;
    }

  memset (w_filter, 0xffff, (max_weight + 1) * sizeof (UCA_W));

  build_weight_remap_filter (w_occurences[level], max_weight, w_filter);

  /* reassign new weights and reset weight statistics for this level */
  memset (w_occurences[level], 0, (MAX_UCA_WEIGHT + 1) * sizeof (UCA_W));

  for (cp = 0; cp <= MAX_UCA_CODEPOINT; cp++)
    {
      for (depth = 0; depth < uca_cp[cp].num; depth++)
	{
	  UCA_W w = GET_UCA_WEIGHT (&(uca_cp[cp]), depth, level);

	  w = w_filter[w];
	  assert (w != 0xffff);
	  SET_UCA_WEIGHT (&(uca_cp[cp]), depth, level, w);

	  w_occurences[level][w]++;
	}
    }

  for (i = 0; i < curr_uca.count_contr; i++)
    {
      UCA_CONTRACTION *contr = &(curr_uca.coll_contr[i]);

      for (depth = 0; depth < contr->ce.num; depth++)
	{
	  UCA_W w = GET_UCA_WEIGHT (&(contr->ce), depth, level);

	  w = w_filter[w];
	  assert (w != 0xffff);
	  SET_UCA_WEIGHT (&(contr->ce), depth, level, w);

	  w_occurences[level][w]++;
	}
    }

exit:
  if (w_filter != NULL)
    {
      free (w_filter);
      w_filter = NULL;
    }

  return err_status;
}

/*
 * build_weight_remap_filter - builds a filter for remapping weights to a
 *			       compact value range.
 *
 *  return :
 *  w_ocurr(in): occurences array (number of ocurrences for each weight value)
 *  max_weight(in): maximum weight before compacting
 *  w_filter(in/out): weight filter
 *
 */
static void
build_weight_remap_filter (const UCA_W * w_ocurr, const int max_weight, UCA_W * w_filter)
{
  int w;
  int last_used_w;

  assert (max_weight > 0 && max_weight <= 0xffff);
  assert (w_ocurr != NULL);

  last_used_w = 0;

  for (w = 0; w <= max_weight; w++)
    {
      if (w_ocurr[w] > 0)
	{
	  w_filter[w] = last_used_w++;
	}
    }

  assert (last_used_w < max_weight);
}
#endif

/*
 * build_key_list_groups - builds the collation key lists for each L1 weight
 *			   value
 * Returns: error status
 * lc (in): the collation settings.
 */
static int
build_key_list_groups (LOCALE_COLLATION * lc)
{
  int cp, wv, i;
  int err_status = NO_ERROR;

  for (wv = 0; wv <= MAX_UCA_WEIGHT; wv++)
    {
      weight_key_list[wv].list_count = 0;
      if (w_occurences[0][wv] == 0)
	{
	  weight_key_list[wv].key_list = NULL;
	}
      else
	{
	  weight_key_list[wv].key_list = (UCA_COLL_KEY *) malloc (w_occurences[0][wv] * sizeof (UCA_COLL_KEY));

	  if (weight_key_list[wv].key_list == NULL)
	    {
	      err_status = ER_LOC_GEN;
	      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
	      goto exit;
	    }
	}
    }

  for (cp = 0; cp < lc->tail_coll.sett_max_cp; cp++)
    {
      wv = GET_UCA_WEIGHT (&(curr_uca.coll_cp[cp]), 0, 0);
      weight_key_list[wv].key_list[weight_key_list[wv].list_count].val.cp = cp;
      weight_key_list[wv].key_list[weight_key_list[wv].list_count].type = COLL_KEY_TYPE_CP;
      weight_key_list[wv].list_count++;
    }

  for (i = 0; i < curr_uca.count_contr; i++)
    {
      wv = GET_UCA_WEIGHT (&(curr_uca.coll_contr[i].ce), 0, 0);
      weight_key_list[wv].key_list[weight_key_list[wv].list_count].val.contr_id = i;
      weight_key_list[wv].key_list[weight_key_list[wv].list_count].type = COLL_KEY_TYPE_CONTR;
      weight_key_list[wv].list_count++;
    }

exit:
  return err_status;
}

/*
 * sort_coll_key_lists - Sorts all the collation keys lists grouped
 *			 by weight values of level 1 weight
 * lc(in) : the collation settings.
 */
static void
sort_coll_key_lists (LOCALE_COLLATION * lc)
{
  int wv;

  for (wv = 0; wv <= MAX_UCA_WEIGHT; wv++)
    {
      sort_one_coll_key_list (lc, wv);
    }
}

/*
 * sort_one_coll_key_list - Sorts the collation keys grouped in a list
 *			    having the same weight value (= weight index)
 *			    at level 1 in UCA
 * lc(in) : the collation settings.
 * weight_index(in) : the index of the collation keys list to be sorted.
 *
 * Note : The collation keys list located at weight_key_list[weight_index] was
 *	  built from collation keys (codepoints or contractions) which have on
 *	  level 1 weight the same value
 */
static void
sort_one_coll_key_list (LOCALE_COLLATION * lc, int weight_index)
{
  assert (weight_index <= MAX_UCA_WEIGHT);

  if (weight_key_list[weight_index].list_count <= 0)
    {
      return;
    }

  qsort (weight_key_list[weight_index].key_list, weight_key_list[weight_index].list_count, sizeof (UCA_COLL_KEY),
	 uca_comp_func_coll_key_fo);
}

/*
 * uca_comp_func_coll_key_fo - compare function for sorting collatable
 *			       elements according to UCA algorithm, with
 *			       full order
 *
 *  Note: This function is used in the first step of computing 'next' sequence
 *	  If result of 'uca_comp_func_coll_key' is zero, the comparison
 *	  is performed on codepooints values. The purpose is to provide a
 *	  'deterministic comparison' in order to eliminate unpredictable
 *	  results of sort algorithm (qsort) when computing 'next' fields.
 *
 */
static int
uca_comp_func_coll_key_fo (const void *arg1, const void *arg2)
{
  UCA_COLL_KEY *pos1_key;
  UCA_COLL_KEY *pos2_key;
  int cmp;

  pos1_key = (UCA_COLL_KEY *) arg1;
  pos2_key = (UCA_COLL_KEY *) arg2;

  cmp = uca_comp_func_coll_key (arg1, arg2);

  if (cmp == 0)
    {
      if (pos1_key->type == pos2_key->type)
	{
	  return pos1_key->val.cp - pos2_key->val.cp;
	}
      else if (pos1_key->type == COLL_KEY_TYPE_CONTR)
	{
	  return 1;
	}
      else
	{
	  return -1;
	}
    }

  return cmp;
}

/*
 * uca_comp_func_coll_key - compare function for sorting collatable elements
 *			    according to UCA algorithm
 *
 *  Note: this function is used to sort collatable elements according to
 *	  CEs tables and UCA settins (sorting options)
 *	  The elements in array are of UCA_COLL_KEY type, keys which
 *	  may be Unicode points or contractions
 *
 */
static int
uca_comp_func_coll_key (const void *arg1, const void *arg2)
{
  UCA_COLL_KEY *pos1_key;
  UCA_COLL_KEY *pos2_key;
  UCA_COLL_CE_LIST *ce_list1;
  UCA_COLL_CE_LIST *ce_list2;
  UCA_OPTIONS *uca_opt = uca_tailoring_options;

  assert (uca_opt != NULL);

  pos1_key = (UCA_COLL_KEY *) arg1;
  pos2_key = (UCA_COLL_KEY *) arg2;

  ce_list1 = get_ce_list_from_coll_key (pos1_key);
  ce_list2 = get_ce_list_from_coll_key (pos2_key);

  return compare_ce_list (ce_list1, ce_list2, uca_opt);
}

/*
 * get_ce_list_from_coll_key - get collation element list associated to a
 *			       collation key using current UCA storage
 *
 * Returns: collation element list
 * key(in) : UCA colllation key
 */
static UCA_COLL_CE_LIST *
get_ce_list_from_coll_key (const UCA_COLL_KEY * key)
{
  assert (key != NULL);

  if (key->type == COLL_KEY_TYPE_CP)
    {
      if (key->val.cp <= MAX_UCA_CODEPOINT)
	{
	  return &(curr_uca.coll_cp[key->val.cp]);
	}
    }
  else if (key->type == COLL_KEY_TYPE_CONTR)
    {
      if (key->val.contr_id < curr_uca.count_contr)
	{
	  return &(curr_uca.coll_contr[key->val.contr_id].ce);
	}
    }
  else
    {
      assert (key->type == COLL_KEY_TYPE_EXP);

      if (key->val.exp_id < curr_uca.count_exp)
	{
	  return &(curr_uca.coll_exp[key->val.exp_id].ce);
	}
    }

  return NULL;
}

/*
 * create_opt_weights - Analyze the weight_key_list, compare collation
 *			elements fo weight keys based on locale settings,
 *			set the new weights for the keys, create the
 *			next_key array containing the relationship
 *			key -> next key in collation.
 * Returns: error status
 * lc(in/out) : contains the collation settings and optimization results.
 */
static int
create_opt_weights (LOCALE_COLLATION * lc)
{
  UCA_COLL_KEY *equal_key_list = NULL;
  int weight_index;
  int equal_key_count, i;
  unsigned int current_weight;
  int err_status = NO_ERROR;
  UCA_COLL_KEY *prev_key = NULL;
  UCA_COLL_KEY max_cp_key;
  UCA_COLL_CE_LIST *prev_ce_list = NULL;

  weight_index = 0;

  assert (lc->tail_coll.sett_max_cp <= MAX_UCA_CODEPOINT + 1 && lc->tail_coll.sett_max_cp > 0);

  equal_key_list = (UCA_COLL_KEY *) malloc ((MAX_UCA_CODEPOINT + 1) * sizeof (UCA_COLL_KEY));
  if (equal_key_list == NULL)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      goto exit;
    }

  lc->opt_coll.weights = (unsigned int *) malloc ((MAX_UCA_CODEPOINT + 1) * sizeof (unsigned int));
  if (lc->opt_coll.weights == NULL)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      goto exit;
    }

  lc->opt_coll.next_cp = (unsigned int *) malloc ((MAX_UCA_CODEPOINT + 1) * sizeof (unsigned int));
  if (lc->opt_coll.next_cp == NULL)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      goto exit;
    }

  memset (lc->opt_coll.weights, 0xff, (MAX_UCA_CODEPOINT + 1) * sizeof (unsigned int));

  memset (lc->opt_coll.next_cp, 0xff, (MAX_UCA_CODEPOINT + 1) * sizeof (unsigned int));

  /* weights */
  current_weight = 0;
  for (weight_index = 0; weight_index <= MAX_UCA_WEIGHT; weight_index++)
    {
      UCA_W key_cursor;

      for (key_cursor = 0; key_cursor < weight_key_list[weight_index].list_count; key_cursor++)
	{
	  UCA_COLL_CE_LIST *curr_ce_list = NULL;
	  UCA_COLL_KEY *curr_key = &(weight_key_list[weight_index].key_list[key_cursor]);

	  curr_ce_list = get_ce_list_from_coll_key (curr_key);
	  if (curr_ce_list == NULL)
	    {
	      err_status = ER_LOC_GEN;
	      LOG_LOCALE_ERROR ("Invalid collation element for key", ER_LOC_GEN, true);
	      goto exit;
	    }

	  if ((prev_key != NULL) && (compare_ce_list (prev_ce_list, curr_ce_list, &(lc->tail_coll.uca_opt)) != 0))
	    {
	      /* keys compare differently */
	      current_weight++;
	    }

	  if (curr_key->type == COLL_KEY_TYPE_CP)
	    {
	      assert (curr_key->val.cp >= 0 && curr_key->val.cp < lc->tail_coll.sett_max_cp);
	      lc->opt_coll.weights[curr_key->val.cp] = current_weight;
	    }
	  else
	    {
	      assert (curr_key->type == COLL_KEY_TYPE_CONTR);

	      err_status = add_opt_coll_contraction (lc, curr_key, current_weight, false);
	      if (err_status != NO_ERROR)
		{
		  goto exit;
		}
	    }

	  prev_key = curr_key;
	  prev_ce_list = curr_ce_list;
	}
    }

  /* set 'next' values */
  equal_key_count = 0;
  for (weight_index = 0; weight_index <= MAX_UCA_WEIGHT; weight_index++)
    {
      UCA_W key_cursor;

      for (key_cursor = 0; key_cursor < weight_key_list[weight_index].list_count; key_cursor++)
	{
	  UCA_COLL_CE_LIST *curr_ce_list = NULL;
	  UCA_COLL_KEY *curr_key = &(weight_key_list[weight_index].key_list[key_cursor]);

	  curr_ce_list = get_ce_list_from_coll_key (curr_key);
	  if (curr_ce_list == NULL)
	    {
	      err_status = ER_LOC_GEN;
	      LOG_LOCALE_ERROR ("Invalid collation element for key", ER_LOC_GEN, true);
	      goto exit;
	    }

	  if ((prev_key != NULL) && (compare_ce_list (prev_ce_list, curr_ce_list, &(lc->tail_coll.uca_opt)) != 0))
	    {
	      /* keys compare differently */
	      /* set next key for all previous equal keys */
	      for (i = 0; i < equal_key_count; i++)
		{
		  set_next_value_for_coll_key (lc, &(equal_key_list[i]), curr_key);
		}

	      equal_key_count = 0;
	    }

	  memcpy (&(equal_key_list[equal_key_count++]), curr_key, sizeof (UCA_COLL_KEY));

	  prev_key = curr_key;
	  prev_ce_list = curr_ce_list;
	}
    }

  /* set 'next' for remaining collation key to max codepoint */
  make_coll_key (&max_cp_key, COLL_KEY_TYPE_CP, lc->tail_coll.sett_max_cp - 1);

  for (i = 0; i < equal_key_count; i++)
    {
      set_next_value_for_coll_key (lc, &(equal_key_list[i]), &max_cp_key);
    }

  lc->opt_coll.w_count = lc->tail_coll.sett_max_cp;

  for (i = 0; i < lc->opt_coll.w_count; i++)
    {
      if (lc->opt_coll.weights[i] == 0xffffffff || lc->opt_coll.next_cp[i] == 0xffffffff)
	{
	  err_status = ER_LOC_GEN;
	  LOG_LOCALE_ERROR ("Internal error. Generated " "weight value or next CP value is invalid", ER_LOC_GEN, true);
	  goto exit;
	}
    }

  /* optimize contractions */
  if (lc->opt_coll.count_contr > 0)
    {
      err_status = optimize_coll_contractions (lc);
    }

exit:
  if (equal_key_list != NULL)
    {
      free (equal_key_list);
      equal_key_list = NULL;
    }

  return err_status;
}

/*
 * optimize_coll_contractions - optimizes collation contractions list so that
 *			        contractions are stored in binary ascending
 *				order
 * Returns: error status
 *
 * lc(in/out) : contains the collation settings and optimization results
 */
static int
optimize_coll_contractions (LOCALE_COLLATION * lc)
{
  UCA_COLL_CONTR_ID *initial_coll_tag = NULL;
  int i;
  int err_status = NO_ERROR;
  int cp;

  assert (lc != NULL);
  assert (lc->opt_coll.count_contr > 0);

  initial_coll_tag = (UCA_COLL_CONTR_ID *) malloc (lc->opt_coll.count_contr * sizeof (UCA_COLL_CONTR_ID));
  if (initial_coll_tag == NULL)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      goto exit;
    }

  for (i = 0; i < lc->opt_coll.count_contr; i++)
    {
      memcpy (&(initial_coll_tag[i].contr_ref), &(lc->opt_coll.contr_list[i]), sizeof (COLL_CONTRACTION));
      initial_coll_tag[i].pos_id = i;
    }

  /* sort contractions (binary order) */
  qsort (initial_coll_tag, lc->opt_coll.count_contr, sizeof (UCA_COLL_CONTR_ID), comp_func_coll_contr_bin);

  /* adjust 'next' contractions values for all codepoints */
  for (cp = 0; cp < lc->opt_coll.w_count; cp++)
    {
      unsigned int next_seq = lc->opt_coll.next_cp[cp];
      int curr_idx = -1;
      int opt_idx;
      bool found = false;

      if (!INTL_IS_NEXT_CONTR (next_seq))
	{
	  continue;
	}

      curr_idx = INTL_GET_NEXT_CONTR_ID (next_seq);

      assert (curr_idx < lc->opt_coll.count_contr);

      /* find index in sorted contractions */
      for (opt_idx = 0; opt_idx < lc->opt_coll.count_contr; opt_idx++)
	{
	  if (initial_coll_tag[opt_idx].pos_id == curr_idx)
	    {
	      found = true;
	      break;
	    }
	}

      if (!found)
	{
	  err_status = ER_LOC_GEN;
	  LOG_LOCALE_ERROR ("Internal error. Cannot adjust " "contraction id after optimization", ER_LOC_GEN, true);
	  goto exit;
	}

      assert (found == true);

      lc->opt_coll.next_cp[cp] = opt_idx | INTL_MASK_CONTR;
    }

  /* adjust 'next' contractions values for all contractions */
  for (i = 0; i < lc->opt_coll.count_contr; i++)
    {
      unsigned int next_seq = initial_coll_tag[i].contr_ref.next;
      int curr_idx = -1;
      int opt_idx;
      bool found = false;

      if (!INTL_IS_NEXT_CONTR (next_seq))
	{
	  continue;
	}

      curr_idx = INTL_GET_NEXT_CONTR_ID (next_seq);

      assert (curr_idx < lc->opt_coll.count_contr);

      /* find index in sorted contractions */
      for (opt_idx = 0; opt_idx < lc->opt_coll.count_contr; opt_idx++)
	{
	  if (initial_coll_tag[opt_idx].pos_id == curr_idx)
	    {
	      found = true;
	      break;
	    }
	}

      if (!found)
	{
	  err_status = ER_LOC_GEN;
	  LOG_LOCALE_ERROR ("Internal error. Cannot adjust " "contraction id after optimization", ER_LOC_GEN, true);
	  goto exit;
	}

      assert (found == true);

      initial_coll_tag[i].contr_ref.next = opt_idx | INTL_MASK_CONTR;
    }

  /* overwrite contractions in sorted order */
  for (i = 0; i < lc->opt_coll.count_contr; i++)
    {
      memcpy (&(lc->opt_coll.contr_list[i]), &(initial_coll_tag[i].contr_ref), sizeof (COLL_CONTRACTION));
    }

  /* first contraction index array */
  lc->opt_coll.cp_first_contr_array = (int *) malloc (lc->opt_coll.w_count * sizeof (int));
  if (lc->opt_coll.cp_first_contr_array == NULL)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      goto exit;
    }

  for (cp = 0; cp < lc->opt_coll.w_count; cp++)
    {
      lc->opt_coll.cp_first_contr_array[cp] = -1;
    }

  lc->opt_coll.contr_min_size = (lc->opt_coll.count_contr > 0) ? LOC_MAX_UCA_CHARS_SEQ * INTL_UTF8_MAX_CHAR_SIZE : 0;

  for (i = 0; i < lc->opt_coll.count_contr; i++)
    {
      unsigned char *c_buf = (unsigned char *) lc->opt_coll.contr_list[i].c_buf;
      unsigned char *dummy;
      int c_buf_size = strlen ((char *) c_buf);
      unsigned int cp;

      assert (c_buf_size < LOC_MAX_UCA_CHARS_SEQ * INTL_UTF8_MAX_CHAR_SIZE);
      lc->opt_coll.contr_list[i].size = (unsigned char) c_buf_size;

      lc->opt_coll.contr_min_size = MIN (lc->opt_coll.contr_min_size, c_buf_size);

      /* get first code-point */
      cp = intl_utf8_to_cp (c_buf, c_buf_size, &dummy);

      if (cp < (unsigned int) lc->opt_coll.w_count && lc->opt_coll.cp_first_contr_array[cp] == -1)
	{
	  lc->opt_coll.cp_first_contr_array[cp] = i;
	}
    }

  /* compute interval of codepoints with contractions */
  lc->opt_coll.cp_first_contr_offset = -1;
  for (cp = 0; cp < lc->opt_coll.w_count; cp++)
    {
      if (lc->opt_coll.cp_first_contr_array[cp] != -1)
	{
	  lc->opt_coll.cp_first_contr_offset = cp;
	  break;
	}
    }

  assert (lc->opt_coll.cp_first_contr_offset < MAX_UNICODE_CHARS);

  for (cp = lc->opt_coll.w_count - 1; cp >= 0; cp--)
    {
      if (lc->opt_coll.cp_first_contr_array[cp] != -1)
	{
	  lc->opt_coll.cp_first_contr_count = cp - lc->opt_coll.cp_first_contr_offset + 1;
	  break;
	}
    }

  assert ((int) lc->opt_coll.cp_first_contr_count < lc->opt_coll.w_count);

  if (lc->opt_coll.cp_first_contr_offset > 0)
    {
      for (i = 0; i < (int) lc->opt_coll.cp_first_contr_count; i++)
	{
	  lc->opt_coll.cp_first_contr_array[i] =
	    lc->opt_coll.cp_first_contr_array[i + lc->opt_coll.cp_first_contr_offset];
	}
    }

exit:
  if (initial_coll_tag != NULL)
    {
      free (initial_coll_tag);
      initial_coll_tag = NULL;
    }

  return err_status;
}

/*
 * set_next_value_for_coll_key - sets the next collation key
 * Returns: error status
 *
 * lc(in/out) : contains the collation settings and optimization results
 * coll_key(in) : collation key
 * next_key(in) : next key value to set
 */
static int
set_next_value_for_coll_key (LOCALE_COLLATION * lc, const UCA_COLL_KEY * coll_key, const UCA_COLL_KEY * next_key)
{
  /* set next key for all previous equal keys */
  if (next_key->type == COLL_KEY_TYPE_CP)
    {
      if (coll_key->type == COLL_KEY_TYPE_CP)
	{
	  lc->opt_coll.next_cp[coll_key->val.cp] = next_key->val.cp;
	}
      else
	{
	  assert (coll_key->type == COLL_KEY_TYPE_CONTR);
	  assert (coll_key->val.contr_id < lc->opt_coll.count_contr);
	  lc->opt_coll.contr_list[coll_key->val.contr_id].next = next_key->val.cp;
	}
    }
  else
    {
      assert (next_key->type == COLL_KEY_TYPE_CONTR);
      assert (next_key->val.contr_id < lc->opt_coll.count_contr);

      if (coll_key->type == COLL_KEY_TYPE_CP)
	{
	  lc->opt_coll.next_cp[coll_key->val.cp] = next_key->val.contr_id | INTL_MASK_CONTR;
	}
      else
	{
	  assert (coll_key->type == COLL_KEY_TYPE_CONTR);
	  assert (coll_key->val.contr_id < lc->opt_coll.count_contr);

	  lc->opt_coll.contr_list[coll_key->val.contr_id].next = next_key->val.contr_id | INTL_MASK_CONTR;
	}
    }

  return NO_ERROR;
}

/*
 * add_opt_coll_contraction - adds an contraction item to optimized collation
 * Returns: error status
 *
 * lc(in/out) : contains the collation settings and optimization results
 * contr_key(in) : collation key
 * wv(in) : weight value to assign for optimized contraction
 * use_expansions(in) :
 */
static int
add_opt_coll_contraction (LOCALE_COLLATION * lc, const UCA_COLL_KEY * contr_key, const unsigned int wv,
			  bool use_expansions)
{
  COLL_CONTRACTION *opt_contr = NULL;
  UCA_CONTRACTION *uca_contr;
  char *p_buf = NULL;
  int err_status = NO_ERROR;
  int i;

  assert (contr_key != NULL);
  assert (contr_key->type == COLL_KEY_TYPE_CONTR);

  assert (contr_key->val.contr_id < curr_uca.count_contr);
  uca_contr = &(curr_uca.coll_contr[contr_key->val.contr_id]);

  lc->opt_coll.contr_list =
    (COLL_CONTRACTION *) realloc (lc->opt_coll.contr_list, (lc->opt_coll.count_contr + 1) * sizeof (COLL_CONTRACTION));

  if (lc->opt_coll.contr_list == NULL)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      goto exit;
    }

  opt_contr = &(lc->opt_coll.contr_list[lc->opt_coll.count_contr++]);
  memset (opt_contr, 0, sizeof (COLL_CONTRACTION));

  p_buf = opt_contr->c_buf;

  assert (uca_contr->cp_count > 1);

  for (i = 0; i < uca_contr->cp_count; i++)
    {
      int utf8_size = intl_cp_to_utf8 (uca_contr->cp_list[i],
				       (unsigned char *) p_buf);
      p_buf += utf8_size;
    }
  opt_contr->cp_count = uca_contr->cp_count;
  *p_buf = '\0';

  opt_contr->wv = wv;
  assert (p_buf - opt_contr->c_buf < (int) sizeof (opt_contr->c_buf));
  opt_contr->size = (unsigned char) (p_buf - opt_contr->c_buf);

  if (use_expansions)
    {
      UCA_COLL_CE_LIST *ce_list;

      memset (opt_contr->uca_w_l13, 0, sizeof (opt_contr->uca_w_l13));
      if (lc->tail_coll.uca_opt.sett_strength >= TAILOR_QUATERNARY)
	{
	  memset (opt_contr->uca_w_l4, 0, sizeof (opt_contr->uca_w_l4));
	}

      ce_list = get_ce_list_from_coll_key (contr_key);

      assert (ce_list != NULL);
      opt_contr->uca_num = ce_list->num;

      assert (opt_contr->uca_num > 0);

      build_compressed_uca_w_l13 (ce_list, opt_contr->uca_w_l13);
      if (lc->tail_coll.uca_opt.sett_strength >= TAILOR_QUATERNARY)
	{
	  for (i = 0; i < MAX_UCA_EXP_CE; i++)
	    {
	      opt_contr->uca_w_l4[i] = GET_UCA_WEIGHT (ce_list, i, 3);
	    }
	}
    }

exit:
  return err_status;
}

/*
 * uca_free_data - Unloads data used by UCA
 * Returns:
 */
void
uca_free_data (void)
{
  if (ducet.coll_cp != NULL)
    {
      free (ducet.coll_cp);
      ducet.coll_cp = NULL;
    }

  if (ducet.coll_contr)
    {
      free (ducet.coll_contr);
      ducet.coll_contr = NULL;
    }

  assert (ducet.coll_exp == NULL);

  ducet.max_contr = 0;
  ducet.count_contr = 0;

  *(ducet.prev_file_path) = '0';
}

/*
 * apply_tailoring_rule_cp - Calls the functions which apply the rules
 *			     based on their type.
 * Returns: ER_LOC_GEN if cannot apply rule, or the rule has an invald type;
 *	    ER_OUT_OF_VIRTUAL_MEMORY if moving the tailored codepoint inside
 *	    the weight stats array fails;
 *	    NO_ERROR if tailoring is successful.
 * dir(in) : 0 = after, 1 = before.
 * anchor_key(in) : the anchor for which the rule is applied.
 * key(in) : key to be tailored.
 * ref_key(in) : the key previously tailored (or anchor if this is the
 *		first rule having "anchor" as the anchor key).
 * lvl(in) : weight level used for tailoring (see T_LEVEL for values).
 *
 * Note : Alter the collation elements of the collated element in order for
 *	  the keys to comply with the specified rule after collation and
 *	  optimizations.
 *	  If the rule is a "before" rule, checks if it can be applied.
 */
static int
apply_tailoring_rule (TAILOR_DIR dir, UCA_COLL_KEY * anchor_key, UCA_COLL_KEY * key, UCA_COLL_KEY * ref_key,
		      T_LEVEL lvl)
{
  if (lvl == TAILOR_IDENTITY)
    {
      return apply_tailoring_rule_identity (key, ref_key);
    }

  return apply_tailoring_rule_w_dir (dir, anchor_key, key, ref_key, lvl);
}

/*
 * apply_tailoring_rule_identity - Apply an identity tailoring rule.
 *
 * Returns: error status
 * key(in)     : collation key to be tailored.
 * ref_key(in) : collation key to which we tailor key as identical.
 */
static int
apply_tailoring_rule_identity (UCA_COLL_KEY * key, UCA_COLL_KEY * ref_key)
{
  UCA_W current_weight;
  int err_status = NO_ERROR;
  UCA_COLL_CE_LIST *ce_list_key = NULL;
  UCA_COLL_CE_LIST *ce_list_ref_key = NULL;

  assert (key != NULL);
  assert (ref_key != NULL);

  ce_list_key = get_ce_list_from_coll_key (key);
  ce_list_ref_key = get_ce_list_from_coll_key (ref_key);

  if (ce_list_key == NULL || ce_list_ref_key == NULL)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("Cannot apply identity rule. Collation key not found", ER_LOC_GEN, true);
      goto exit;
    }

  /* Make sure the reference is represented in DUCET. */
  assert (ce_list_ref_key->num > 0);

  current_weight = GET_UCA_WEIGHT (ce_list_key, 0, 0);
  if (current_weight != GET_UCA_WEIGHT (ce_list_ref_key, 0, 0))
    {
      err_status = change_key_weight_list (key, current_weight, GET_UCA_WEIGHT (ce_list_ref_key, 0, 0));
      if (err_status != NO_ERROR)
	{
	  goto exit;
	}
    }

  assert (ce_list_key != ce_list_ref_key);
  memcpy (ce_list_key, ce_list_ref_key, sizeof (UCA_COLL_CE_LIST));

exit:
  return err_status;
}

/*
 * apply_tailoring_rule_w_dir - Applies an After/Before -rule
 * Returns: error status
 *
 * dir(in)	 : direction : 0 after, 1 before
 * anchor_key(in): the anchor for which the rule is applied.
 * key(in)	 : key to be tailored.
 * ref_key(in)	 : the key previously tailored (or anchor if this is the
 *		   first rule having "anchor" as the anchor key).
 * lvl(in) : weight level used for tailoring (see T_LEVEL for values).
 *
 * Note : Alter the collation elements of the collated element in order for
 *	  the symbols to comply with the specified rule after collation and
 *	  optimizations.
 *	  Collisions are avoided.
 */
static int
apply_tailoring_rule_w_dir (TAILOR_DIR dir, UCA_COLL_KEY * anchor_key, UCA_COLL_KEY * key, UCA_COLL_KEY * ref_key,
			    T_LEVEL lvl)
{
  int i, j;
  bool collation_finished, overflow = false;
  UCA_W current_weight;
  UCA_COLL_CE_LIST new_ce;
  int err_status = NO_ERROR;
  UCA_COLL_CE_LIST *ce_list_key = NULL;
  UCA_COLL_CE_LIST *ce_list_anchor_key = NULL;
  UCA_COLL_CE_LIST *ce_list_ref_key = NULL;

  assert (key != NULL);
  assert (ref_key != NULL);

  ce_list_key = get_ce_list_from_coll_key (key);
  ce_list_ref_key = get_ce_list_from_coll_key (ref_key);
  ce_list_anchor_key = get_ce_list_from_coll_key (anchor_key);

  if (ce_list_key == NULL || ce_list_ref_key == NULL || ce_list_anchor_key == NULL)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("Cannot apply identity rule. Collation key not found", ER_LOC_GEN, true);
      goto exit;
    }

  /* Make sure the anchor and ref codepoint are represented in DUCET. */
  assert (ce_list_anchor_key->num > 0);
  assert (ce_list_ref_key->num > 0);

  /* Clone anchor weights (uca element full clone) */
  memcpy (&new_ce, ce_list_anchor_key, sizeof (UCA_COLL_CE_LIST));

  new_ce.num = MAX (new_ce.num, ce_list_ref_key->num);

  /* overwrite with reference weights up to level */
  for (j = 0; j < (int) lvl; j++)
    {
      for (i = 0; i < (int) (new_ce.num); i++)
	{
	  SET_UCA_WEIGHT (&(new_ce), i, j, GET_UCA_WEIGHT (ce_list_ref_key, i, j));
	}
    }

  collation_finished = false;

  while (!collation_finished)
    {
      if (new_ce.num > MAX_UCA_EXP_CE)
	{
	  err_status = ER_LOC_GEN;
	  LOG_LOCALE_ERROR ("Error when applying after-rule. " "Collation element list overflow.", ER_LOC_GEN, true);
	  goto exit;
	}

      assert (new_ce.num >= 1);
      assert (lvl >= 1);

      if (dir == TAILOR_AFTER)
	{
	  UCA_W w_val = GET_UCA_WEIGHT (&new_ce, new_ce.num - 1, lvl - 1);
	  /* AFTER */
	  overflow = false;

	  /* Try increasing the weight value on the last collation element. */
	  if (w_val >= MAX_UCA_WEIGHT)
	    {
	      new_ce.num++;
	      overflow = true;
	    }

	  w_val = GET_UCA_WEIGHT (&new_ce, new_ce.num - 1, lvl - 1);
	  SET_UCA_WEIGHT (&new_ce, new_ce.num - 1, lvl - 1, w_val + 1);
	}
      else
	{
	  UCA_W w_val = GET_UCA_WEIGHT (&new_ce, new_ce.num - 1, lvl - 1);
	  /* BEFORE */
	  assert (dir == TAILOR_BEFORE);

	  /* Try decreasing the weight value on the last collation element. */
	  if (w_val > 0)
	    {
	      SET_UCA_WEIGHT (&new_ce, new_ce.num - 1, lvl - 1, w_val - 1);
	    }
	  else
	    {
	      int ce_index = (int) (new_ce.num);

	      while (ce_index > 0 && GET_UCA_WEIGHT (&new_ce, ce_index - 1, lvl - 1) == 0)
		{
		  ce_index--;
		}

	      if (ce_index <= 0)
		{
		  err_status = ER_LOC_GEN;
		  LOG_LOCALE_ERROR ("Applying before-rule. Collation element" " list underflow.", ER_LOC_GEN, true);
		  goto exit;
		}
	      SET_UCA_WEIGHT (&new_ce, ce_index - 1, lvl - 1, MAX_UCA_WEIGHT);
	    }
	}

      if (get_key_with_ce_sublist (&new_ce, lvl) != NULL)
	{
	  /* If a collision occurs, go further into the collation element list */

	  if (dir == TAILOR_AFTER)
	    {
	      /* AFTER */
	      if (!overflow)
		{
		  UCA_W w_val = GET_UCA_WEIGHT (&new_ce, new_ce.num - 1, lvl - 1);
		  /* Revert weight increase */
		  SET_UCA_WEIGHT (&new_ce, new_ce.num - 1, lvl - 1, w_val - 1);
		}
	      /* Add a new collation element. */
	      new_ce.num++;
	    }
	  else
	    {
	      /* BEFORE */
	      assert (dir == TAILOR_BEFORE);

	      /* Add a new collation element. */
	      new_ce.num++;
	      /* Maximize the weight on the new collation element. */
	      SET_UCA_WEIGHT (&new_ce, new_ce.num - 1, lvl - 1, MAX_UCA_WEIGHT);
	    }
	}
      else
	{
	  collation_finished = true;
	}
    }

  current_weight = GET_UCA_WEIGHT (ce_list_key, 0, 0);
  if (GET_UCA_WEIGHT (&new_ce, 0, 0) != current_weight)
    {
      err_status = change_key_weight_list (key, current_weight, GET_UCA_WEIGHT (&new_ce, 0, 0));
      if (err_status != NO_ERROR)
	{
	  goto exit;
	}
    }

  memcpy (ce_list_key, &new_ce, sizeof (UCA_COLL_CE_LIST));

exit:
  return err_status;
}

/*
 * get_key_with_ce_sublist - Tests if a collation key's collation element list
 *			     matches uca_item up to lvl(level) and
 *			     uca_item.num.
 * Returns  : collation key if found, or
 *	      NULL if no partial match was found.
 * uca_item(in) : an object of type UCA_COLL_CE_LIST, containing the list of
 *		  collation elements to search for.
 * lvl(in) : the level up to which the matching should be done.
 */
static UCA_COLL_KEY *
get_key_with_ce_sublist (UCA_COLL_CE_LIST * uca_item, const int lvl)
{
  UCA_W weight_index;
  int i, ce_index, level_index;
  bool found;

  assert (uca_item != NULL);

  weight_index = GET_UCA_WEIGHT (uca_item, 0, 0);

  if (weight_key_list[weight_index].list_count == 0)
    {
      return NULL;
    }

  for (i = 0; i < weight_key_list[weight_index].list_count; i++)
    {
      UCA_COLL_KEY *key = &(weight_key_list[weight_index].key_list[i]);
      UCA_COLL_CE_LIST *ce_list = get_ce_list_from_coll_key (key);

      if (ce_list == NULL)
	{
	  continue;
	}

      found = true;
      for (level_index = 0; level_index < lvl && found; level_index++)
	{
	  for (ce_index = 0; ce_index < uca_item->num; ce_index++)
	    {
	      if (GET_UCA_WEIGHT (ce_list, ce_index, level_index) != GET_UCA_WEIGHT (uca_item, ce_index, level_index))
		{
		  found = false;
		  break;
		}
	    }
	}

      if (found)
	{
	  return key;
	}
    }

  return NULL;
}

/*
 * make_coll_key - creates a collation key
 * Returns :
 * key(in/out) : key to create
 * type(in) : key type
 * key_id(in) : collation key identifier (codepoint or cotraction key)
 */
static void
make_coll_key (UCA_COLL_KEY * key, UCA_COLL_KEY_TYPE type, const int key_id)
{
  assert (key != NULL);

  assert (key_id >= 0);

  memset (key, 0, sizeof (UCA_COLL_KEY));

  if (type == COLL_KEY_TYPE_CP)
    {
      key->type = COLL_KEY_TYPE_CP;
      key->val.cp = key_id;
    }
  else if (type == COLL_KEY_TYPE_CONTR)
    {
      key->type = COLL_KEY_TYPE_CONTR;
      key->val.contr_id = key_id;
    }
  else
    {
      assert (type == COLL_KEY_TYPE_EXP);

      key->type = COLL_KEY_TYPE_EXP;
      key->val.exp_id = key_id;
    }
}

/*
 * find_contr_id - searches for a contraction
 * Returns : id of contraction or -1 if not found
 * cp_array(in) : codepoints array to create key
 * cp_count(in) : # of codepoints in array
 * st(in) : storage to check
 */
static int
find_contr_id (const unsigned int *cp_array, const int cp_count, UCA_STORAGE * st)
{
  bool found;
  int i;

  assert (cp_array != NULL);
  assert (cp_count > 0);
  assert (st != NULL);

  if (cp_count > LOC_MAX_UCA_CHARS_SEQ)
    {
      return -1;
    }

  found = false;
  for (i = 0; i < st->count_contr; i++)
    {
      int j;
      UCA_CP *st_cp_list;

      if (cp_count != st->coll_contr[i].cp_count)
	{
	  continue;
	}

      st_cp_list = st->coll_contr[i].cp_list;

      found = true;
      for (j = 0; j < cp_count; j++)
	{
	  assert (cp_array[j] < MAX_UNICODE_CHARS);

	  if (cp_array[j] != (unsigned int) (st_cp_list[j]))
	    {
	      found = false;
	      break;
	    }
	}

      if (found)
	{
	  return i;
	}
    }

  return -1;
}

/*
 * find_exp_id - searches for an expansion
 * Returns : id of expansion or -1 if not found
 * cp_array(in) : codepoints array to create key
 * cp_count(in) : # of codepoints in array
 * st(in) : storage to check
 */
static int
find_exp_id (const unsigned int *cp_array, const int cp_count, UCA_STORAGE * st)
{
  bool found;
  int i;

  assert (cp_array != NULL);
  assert (cp_count > 0);
  assert (st != NULL);

  if (cp_count > LOC_MAX_UCA_CHARS_SEQ)
    {
      return -1;
    }

  found = false;
  for (i = 0; i < st->count_exp; i++)
    {
      int j;
      UCA_CP *st_cp_list;

      if (cp_count != st->coll_exp[i].cp_count)
	{
	  continue;
	}

      st_cp_list = st->coll_exp[i].cp_list;

      found = true;
      for (j = 0; j < cp_count; j++)
	{
	  assert (cp_array[j] < MAX_UNICODE_CHARS);

	  if (cp_array[j] != (unsigned int) (st_cp_list[j]))
	    {
	      found = false;
	      break;
	    }
	}

      if (found)
	{
	  return i;
	}
    }

  return -1;
}

/*
 * string_to_coll_ce_list - Parse a string to a collation element list
 * Retuns : NO_ERROR(0) if parsing is successful;
 *	    ER_LOC_GEN if parsing fails.
 * s (in) : NULL terminated string to parse.
 * ui (in/out) : parsed collation element list.
 */
static int
string_to_coll_ce_list (char *s, UCA_COLL_CE_LIST * ui)
{
  UCA_COLL_CE_LIST uca_item;
  int weight_index, ce_index, state;
  UCA_W weight;
  bool error_found;
  char *str;
  char *end_ptr;
  char c;
  int err_status = NO_ERROR;

  memset (&uca_item, 0, sizeof (UCA_COLL_CE_LIST));

  str = s;
  weight_index = 0;
  ce_index = 0;
  state = 0;
  error_found = false;
  while (*str && !error_found)
    {
      int result = 0;
      int val;

      switch (state)
	{
	case 0:		/* read a '[' (first char from the standard string representation * of a collation
				 * element) */
	  c = str[0];
	  if (c != '[')
	    {
	      error_found = true;
	      break;
	    }
	  str++;
	  state = 1;
	  break;
	case 1:		/* read a weight value in string format, in hex */
	  if (weight_index == MAX_WEIGHT_LEVELS)
	    {
	      state = 3;
	      break;
	    }
	  /* validate weight, to be below 0xFFFF */
	  result = str_to_int32 (&val, &end_ptr, str, 16);
	  if (result != 0 || val > MAX_UCA_WEIGHT)
	    {
	      error_found = true;
	      break;
	    }
	  weight = (UCA_W) val;
	  SET_UCA_WEIGHT (&uca_item, ce_index, weight_index, weight);
	  str = end_ptr;
	  if (weight_index != 3)
	    {
	      state = 2;
	    }
	  else
	    {
	      state = 3;
	    }
	  weight_index++;
	  break;
	case 2:		/* Read a dot '.' = the weight separator inside the * string representation of a
				 * collation element */
	  c = str[0];
	  if (c != '.')
	    {
	      error_found = true;
	      break;
	    }
	  str++;
	  state = 1;
	  break;
	case 3:		/* Read a ']' (last char from the standard string representation * of a collation
				 * element) */
	  c = str[0];
	  if (c != ']')
	    {
	      error_found = true;
	      break;
	    }
	  str++;
	  state = 0;
	  weight_index = 0;
	  ce_index++;
	  break;
	}
    }

  if (!error_found)
    {
      uca_item.num = ce_index;
      memcpy (ui, &uca_item, sizeof (UCA_COLL_CE_LIST));
    }

  if (error_found)
    {
      switch (state)
	{
	case 0:
	  LOG_LOCALE_ERROR ("Invalid collation element list. '[' expected.", ER_LOC_GEN, true);
	  break;
	case 1:
	  LOG_LOCALE_ERROR ("Invalid collation element list." "Weight out of 0x0000 - 0xFFFF range.", ER_LOC_GEN, true);
	  break;
	case 2:
	  LOG_LOCALE_ERROR ("Invalid collation element list. '.' expected.", ER_LOC_GEN, true);
	  break;
	case 3:
	  LOG_LOCALE_ERROR ("Invalid collation element list. ']' expected.", ER_LOC_GEN, true);
	  break;
	}
      err_status = ER_LOC_GEN;
    }

  return err_status;
}

/*
 * apply_absolute_tailoring_rules - Function for applying the tailor
 *				    rules strored in lc->tail_coll.cub_rules.
 * Retuns : ER_LOC_GEN if an invalid rule is found;
 *	    NO_ERROR(0) if parsing is successful.
 * lc (in/out) : locale settings and optimization results.
 */
static int
apply_absolute_tailoring_rules (LOCALE_COLLATION * lc)
{
  int rule_index, weight_index;
  int ce_index;
  UCA_COLL_CE_LIST weight;
  UCA_COLL_CE_LIST step;
  UCA_COLL_CE_LIST weight_offset;
  UCA_CP cp_index;
  bool is_overflow;
  bool is_ce_empty;
  UCA_CP start_cp;
  UCA_CP end_cp;
  int err_status = NO_ERROR;
  CUBRID_TAILOR_RULE *ct_rule;
  UCA_COLL_CE_LIST *uca_cp = curr_uca.coll_cp;

  for (rule_index = 0; rule_index < lc->tail_coll.cub_count_rules; rule_index++)
    {
      ct_rule = &(lc->tail_coll.cub_rules[rule_index]);
      if (strlen (ct_rule->step) == 0)
	{
	  strcpy (ct_rule->step, "[0001.0000.0000.0000]\0");
	}
      if (string_to_coll_ce_list (ct_rule->step, &step) != 0)
	{
	  err_status = ER_LOC_GEN;
	  LOG_LOCALE_ERROR ("Invalid collation element list for range step.", ER_LOC_GEN, true);
	  goto exit;
	}
      if (string_to_coll_ce_list (ct_rule->start_weight, &weight) != 0)
	{
	  err_status = ER_LOC_GEN;
	  LOG_LOCALE_ERROR ("Invalid collation element list for starting weight.", ER_LOC_GEN, true);
	  goto exit;
	}

      /* Parse the char buffers for start and end codepoint */
      if (read_cp_from_tag ((unsigned char *) (ct_rule->start_cp_buf), ct_rule->start_cp_buf_type, &start_cp) !=
	  NO_ERROR)
	{
	  goto exit;
	}
      if (read_cp_from_tag ((unsigned char *) (ct_rule->end_cp_buf), ct_rule->end_cp_buf_type, &end_cp) != NO_ERROR)
	{
	  goto exit;
	}

      /* Validate starting weight, step and the number of codepoints in the range to be tailored, so that there are no
       * overflows above MAX_WEIGHT (0xFFFF). */
      is_overflow = false;
      for (weight_index = 0; weight_index < MAX_WEIGHT_LEVELS; weight_index++)
	{
	  for (ce_index = 0; ce_index < step.num; ce_index++)
	    {
	      if (GET_UCA_WEIGHT (&step, ce_index, weight_index) * (end_cp - start_cp) > MAX_UCA_WEIGHT)
		{
		  is_overflow = true;
		  break;
		}
	    }
	  if (is_overflow)
	    {
	      break;
	    }
	}

      if (is_overflow)
	{
	  err_status = ER_LOC_GEN;
	  LOG_LOCALE_ERROR ("Weight range overflow" "Weight or step too big.", ER_LOC_GEN, true);
	  goto exit;
	}

      memcpy (&weight_offset, &weight, sizeof (UCA_COLL_CE_LIST));
      weight_offset.num = (weight.num > step.num) ? weight.num : step.num;

      for (cp_index = start_cp; cp_index <= end_cp; cp_index++)
	{
	  memcpy (&uca_cp[cp_index], &weight_offset, sizeof (UCA_COLL_CE_LIST));
	  for (weight_index = 0; weight_index < MAX_WEIGHT_LEVELS; weight_index++)
	    {
	      for (ce_index = 0; ce_index < weight_offset.num; ce_index++)
		{
		  SET_UCA_WEIGHT (&weight_offset, ce_index, weight_index,
				  GET_UCA_WEIGHT (&step, ce_index, weight_index));
		}
	    }
	  /* Remove any collation elements with all weight values zero. */
	  is_ce_empty = true;
	  while (is_ce_empty)
	    {
	      for (weight_index = 0; weight_index < MAX_WEIGHT_LEVELS; weight_index++)
		{
		  if (GET_UCA_WEIGHT (&(uca_cp[cp_index]), uca_cp[cp_index].num - 1, weight_index) != 0)
		    {
		      is_ce_empty = false;
		    }
		}
	      if (is_ce_empty && uca_cp[cp_index].num > 1)
		{
		  uca_cp[cp_index].num--;
		}
	      if (uca_cp[cp_index].num == 1)
		{
		  break;
		}
	    }
	}
    }

exit:
  return err_status;
}

/*
 * add_key_to_weight_stats_list - Adds a collation key to the list of
 *		keys having the selected weight inside the first
 *		collation element on level 1
 * Returns : error code
 * key(in): the key to add in a list.
 * wv(in) : the weight value corresponding to the list where the key will be
 *	    added.
 */
static int
add_key_to_weight_stats_list (const UCA_COLL_KEY * key, UCA_W wv)
{
  int err_status = NO_ERROR;

  assert (key != NULL);

  if ((weight_key_list[wv].key_list =
       (UCA_COLL_KEY *) realloc (weight_key_list[wv].key_list,
				 (weight_key_list[wv].list_count + 1) * sizeof (UCA_COLL_KEY))) == NULL)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      goto exit;
    }
  memcpy (&(weight_key_list[wv].key_list[weight_key_list[wv].list_count]), key, sizeof (UCA_COLL_KEY));
  weight_key_list[wv].list_count++;

exit:
  return err_status;
}

/*
 * remove_key_from_weight_stats_list - Remove a collation key from the list of
 *		keys having a selected weight inside the first
 *		collation element on level 1
 * Returns : error status
 * cp(in)	    : the codepoint to remove from the list.
 * weight_index(in) : the identifying weight value for the list where
 *		      the codepoint will be removed from.
 */
static int
remove_key_from_weight_stats_list (const UCA_COLL_KEY * key, UCA_W wv)
{
  int found_at, i;
  int err_status = NO_ERROR;

  if (weight_key_list[wv].list_count == 0)
    {
      goto exit;
    }
  else if (weight_key_list[wv].list_count == 1)
    {
      if (memcmp (&(weight_key_list[wv].key_list[0]), key, sizeof (UCA_COLL_KEY)) == 0)
	{
	  weight_key_list[wv].list_count = 0;
	  memset (&(weight_key_list[wv].key_list[0]), 0, sizeof (UCA_COLL_KEY));
	  goto exit;
	}
    }

  assert (weight_key_list[wv].list_count > 1);

  found_at = -1;
  for (i = 0; i < weight_key_list[wv].list_count; i++)
    {
      if (memcmp (&(weight_key_list[wv].key_list[i]), key, sizeof (UCA_COLL_KEY)) == 0)
	{
	  found_at = i;
	  break;
	}
    }

  assert (found_at != -1);

  for (i = found_at; i < weight_key_list[wv].list_count - 1; i++)
    {
      memcpy (&(weight_key_list[wv].key_list[i]), &(weight_key_list[wv].key_list[i + 1]), sizeof (UCA_COLL_KEY));
    }

  weight_key_list[wv].list_count--;

  /* zero last element */
  i = weight_key_list[wv].list_count;
  memset (&(weight_key_list[wv].key_list[i]), 0, sizeof (UCA_COLL_KEY));

exit:
  return err_status;
}

/*
 * change_key_weight_list - Removes a collation key from the list of
 *		keys having a selected weight inside the first
 *		collation element on level 1, and moving it to another
 *		simmilar list.
 * Returns : error status
 * key(in) : the key to move
 * w_from(in) : the identifying weight value for the list from where
 *		the key will be removed.
 * w_to(in) :  the identifying weight value for the list where the key will be
 *	       added.
 */
static int
change_key_weight_list (const UCA_COLL_KEY * key, UCA_W w_from, UCA_W w_to)
{
  int err_status = NO_ERROR;

  assert (key != NULL);

  err_status = remove_key_from_weight_stats_list (key, w_from);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }
  err_status = add_key_to_weight_stats_list (key, w_to);

exit:
  return err_status;
}

/*
 * new_contraction - creates new UCA contraction element
 * Returns : pointer to newly created contraction, or NULL if memory cannot be
 *	     allocated
 *
 * storage(in/out)   : storage for UCA contraction
 *
 */
static UCA_CONTRACTION *
new_contraction (UCA_STORAGE * storage)
{
  UCA_CONTRACTION *contr = NULL;

  assert (storage != NULL);

  if (storage->count_contr >= storage->max_contr)
    {
      storage->coll_contr =
	(UCA_CONTRACTION *) realloc (storage->coll_contr,
				     sizeof (UCA_CONTRACTION) * (storage->max_contr + UCA_CONTR_EXP_CNT_GROW));

      if (storage->coll_contr == NULL)
	{
	  LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
	  return NULL;
	}

      storage->max_contr += UCA_CONTR_EXP_CNT_GROW;
    }

  assert (storage->coll_contr != NULL);

  contr = &(storage->coll_contr[storage->count_contr++]);
  memset (contr, 0, sizeof (UCA_CONTRACTION));

  return contr;
}

/*
 * new_expansion - creates new UCA expansion element
 * Returns : pointer to newly created expansion, or NULL if memory cannot be
 *	     allocated
 *
 * storage(in/out)   : storage for UCA expansion
 *
 */
static UCA_EXPANSION *
new_expansion (UCA_STORAGE * storage)
{
  UCA_EXPANSION *exp = NULL;

  assert (storage != NULL);

  if (storage->count_exp >= storage->max_exp)
    {
      storage->coll_exp =
	(UCA_EXPANSION *) realloc (storage->coll_exp,
				   sizeof (UCA_EXPANSION) * (storage->max_exp + UCA_CONTR_EXP_CNT_GROW));

      if (storage->coll_exp == NULL)
	{
	  LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
	  return NULL;
	}

      storage->max_exp += UCA_CONTR_EXP_CNT_GROW;
    }

  assert (storage->coll_exp != NULL);

  exp = &(storage->coll_exp[storage->count_exp++]);
  memset (exp, 0, sizeof (UCA_EXPANSION));

  return exp;
}

/*
 * add_uca_contr_or_exp - creates an UCA contraction or expansion from
 *			  codepoints
 * Returns : id of uca contraction/expansion or -1 if not be created
 *
 * lc(in): locale data
 * storage(in/out): storage to create in
 * cp_array(in): list of codepoints
 * cp_count(in): number of codepoints
 *
 */
static int
add_uca_contr_or_exp (LOCALE_COLLATION * lc, UCA_STORAGE * storage, const unsigned int *cp_array, const int cp_count,
		      const UCA_COLL_KEY_TYPE seq_type)
{
  UCA_CHR_SEQ *uca_seq = NULL;
  UCA_COLL_KEY contr_key;
  int ce_count;
  int i;

  assert (storage != NULL);
  assert (cp_array != NULL);
  assert (cp_count > 1);
  assert (cp_count <= LOC_MAX_UCA_CHARS_SEQ);

  if (seq_type == COLL_KEY_TYPE_CONTR)
    {
      uca_seq = new_contraction (storage);
    }
  else
    {
      assert (seq_type == COLL_KEY_TYPE_EXP);
      uca_seq = new_expansion (storage);
    }

  if (uca_seq == NULL)
    {
      return -1;
    }

  uca_seq->cp_count = cp_count;
  for (i = 0; i < cp_count; i++)
    {
      if ((int) cp_array[i] >= lc->tail_coll.sett_max_cp)
	{
	  LOG_LOCALE_ERROR ("Codepoint value in sequence exceeds " "maximum allowed codepoint", ER_LOC_GEN, true);
	  return -1;
	}

      uca_seq->cp_list[i] = (UCA_CP) cp_array[i];
    }

  /* build collation element list from CE of codepoints */
  ce_count = 0;
  for (i = 0; i < cp_count; i++)
    {
      UCA_COLL_KEY key_cp;
      UCA_COLL_CE_LIST *cp_ce_list;
      int lev;
      int ce_index;

      make_coll_key (&key_cp, COLL_KEY_TYPE_CP, cp_array[i]);

      cp_ce_list = get_ce_list_from_coll_key (&key_cp);
      if (cp_ce_list == NULL)
	{
	  LOG_LOCALE_ERROR ("Cannot find CE list for key", ER_LOC_GEN, true);
	  return -1;
	}

      assert (cp_ce_list->num < MAX_UCA_EXP_CE);

      if (ce_count + cp_ce_list->num >= MAX_UCA_EXP_CE)
	{
	  LOG_LOCALE_ERROR ("Cannot create contraction." "Too many collation elements", ER_LOC_GEN, true);
	  return -1;
	}

      /* copy all CE of codepoint */
      for (ce_index = 0; ce_index < cp_ce_list->num; ce_index++)
	{
	  for (lev = 0; lev < MAX_WEIGHT_LEVELS; lev++)
	    {
	      SET_UCA_WEIGHT (&(uca_seq->ce), ce_index + ce_count, lev, GET_UCA_WEIGHT (cp_ce_list, ce_index, lev));
	    }
	}

      ce_count += cp_ce_list->num;
    }

  uca_seq->ce.num = ce_count;

  if (seq_type == COLL_KEY_TYPE_EXP)
    {
      assert (storage->count_exp > 0);
      return storage->count_exp - 1;
    }

  assert (seq_type == COLL_KEY_TYPE_CONTR);

  assert (storage->count_contr > 0);

  /* contraction key to statistics */
  make_coll_key (&contr_key, COLL_KEY_TYPE_CONTR, storage->count_contr - 1);
  if (add_key_to_weight_stats_list (&contr_key, GET_UCA_WEIGHT (&(uca_seq->ce), 0, 0)) != NO_ERROR)
    {
      return -1;
    }

  w_occurences[0][GET_UCA_WEIGHT (&(uca_seq->ce), 0, 0)]++;

  return storage->count_contr - 1;
}

/*
 * read_cp_from_tag - reads a codepoint value from a string tag
 * Returns : error status
 *
 * buffer(in): string buffer (nul terminated)
 * type(in): type of stored codepoint
 * cp(out): codepoint value
 *
 */
static int
read_cp_from_tag (unsigned char *buffer, CP_BUF_TYPE type, UCA_CP * cp)
{
  int temp_cp = 0;
  int result = 0;
  int err_status = NO_ERROR;
  char *chr_ptr;
  unsigned char *dummy;
  char err_msg[ERR_MSG_SIZE];

  assert (buffer != NULL);
  assert (cp != NULL);

  if (*buffer == '\0')
    {
      err_status = ER_LOC_GEN;
      snprintf (err_msg, sizeof (err_msg) - 1, "Tag has no content");
      LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
      goto exit;
    }
  else if (type == BUF_TYPE_CHAR)
    {
      dummy = buffer;
      if (intl_count_utf8_chars (dummy, strlen ((char *) dummy)) > 1)
	{
	  err_status = ER_LOC_GEN;
	  snprintf (err_msg, sizeof (err_msg) - 1, "Multiple chars found in codepoint tag." "Tag content: %s", buffer);
	  LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
	  goto exit;
	}

      dummy = buffer;
      temp_cp = intl_utf8_to_cp (buffer, strlen ((char *) buffer), &dummy);

      if (temp_cp > 0xFFFF || temp_cp < 0)
	{
	  err_status = ER_LOC_GEN;
	  snprintf (err_msg, sizeof (err_msg) - 1, "Codepoint found in tag was out of range." "Tag content: %s",
		    buffer);
	  LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
	  goto exit;
	}
    }
  else if (type == BUF_TYPE_CODE)
    {
      chr_ptr = NULL;

      result = str_to_int32 (&temp_cp, &chr_ptr, (const char *) buffer, 16);
      if (result != 0 || temp_cp > 0xFFFF || temp_cp < 0)
	{
	  err_status = ER_LOC_GEN;
	  snprintf (err_msg, sizeof (err_msg) - 1, "Codepoint found in tag was out of range." "Tag content: %s",
		    buffer);
	  LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
	  goto exit;
	}
      else if (temp_cp == 0 && (chr_ptr == (char *) buffer))
	{
	  /* If tag content does not start with a hex number. */
	  err_status = ER_LOC_GEN;
	  snprintf (err_msg, sizeof (err_msg) - 1, "No valid codepoint could be found in tag." "Tag content: %s",
		    buffer);
	  LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
	  goto exit;
	}
      else if (temp_cp > 0 && strlen (chr_ptr) != 0)
	{
	  /* If tag content looks like "1234ZZZZ". */
	  err_status = ER_LOC_GEN;
	  snprintf (err_msg, sizeof (err_msg) - 1, "Encountered codepoint tag with invalid content." "Tag content: %s",
		    buffer);
	  LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
	  goto exit;
	}
    }
  *cp = (UCA_CP) temp_cp;
exit:
  return err_status;
}


/*
 * comp_func_coll_contr_bin - compare function for sorting contractions list
 *
 *  Note : Contractions are sorted in binary order
 *	   Elements in array to be sorted are of COLL_CONTRACTION type
 */
static int
comp_func_coll_contr_bin (const void *arg1, const void *arg2)
{
  UCA_COLL_CONTR_ID *c1 = (UCA_COLL_CONTR_ID *) arg1;
  UCA_COLL_CONTR_ID *c2 = (UCA_COLL_CONTR_ID *) arg2;

  return strcmp (c1->contr_ref.c_buf, c2->contr_ref.c_buf);
}

/*
 * UCA with expansion support
 */

/*
 * create_opt_ce_w_exp - creates weight tables and next seq array for UCA
 *			 sorting with expansions
 *  lc(in) : locale struct
 */
static int
create_opt_ce_w_exp (LOCALE_COLLATION * lc)
{
  UCA_COLL_KEY key;
  UCA_COLL_CE_LIST *ce_list;
  UCA_L13_W *uca_w_l13 = NULL;
  UCA_L4_W *uca_w_l4 = NULL;
  char *uca_exp_num = NULL;
  int uca_w_array_size_l13;
  int uca_w_array_size_l4;
  int err_status = NO_ERROR;
  int i;
  int cp;
  int max_num = 0;
  unsigned int *coll_key_list = NULL;
  int coll_key_list_cnt = 0;
  bool use_level_4;
  UCA_OPTIONS uca_exp_next_opt = { TAILOR_PRIMARY, false, false, 0, true, CONTR_IGNORE, true,
    MATCH_CONTR_BOUND_FORBID
  };
  UCA_OPTIONS *saved_uca_opt = NULL;

  coll_key_list = (unsigned int *) malloc ((curr_uca.count_contr + lc->tail_coll.sett_max_cp) * sizeof (unsigned int));
  if (coll_key_list == NULL)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      goto exit;
    }

  for (cp = 0; cp < lc->tail_coll.sett_max_cp; cp++)
    {
      make_coll_key (&key, COLL_KEY_TYPE_CP, cp);
      ce_list = get_ce_list_from_coll_key (&key);

      max_num = MAX (max_num, ce_list->num);
    }

  use_level_4 = (lc->tail_coll.uca_opt.sett_strength >= TAILOR_QUATERNARY) ? true : false;

  uca_w_array_size_l13 = lc->tail_coll.sett_max_cp * max_num * sizeof (UCA_L13_W);
  uca_w_array_size_l4 = lc->tail_coll.sett_max_cp * max_num * sizeof (UCA_L4_W);
  uca_w_l13 = (UCA_L13_W *) malloc (uca_w_array_size_l13);
  uca_exp_num = (char *) malloc (lc->tail_coll.sett_max_cp);

  if (uca_w_l13 == NULL || uca_exp_num == NULL)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      goto exit;
    }

  memset (uca_w_l13, 0, uca_w_array_size_l13);
  memset (uca_exp_num, 0, lc->tail_coll.sett_max_cp);

  /* do not generate L4 if tailoring level doesn't require it */
  if (use_level_4)
    {
      uca_w_l4 = (UCA_L4_W *) malloc (uca_w_array_size_l4);
      if (uca_w_l4 == NULL)
	{
	  err_status = ER_LOC_GEN;
	  LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
	  goto exit;
	}

      memset (uca_w_l4, 0, uca_w_array_size_l4);
    }

  lc->opt_coll.next_cp = (unsigned int *) malloc (lc->tail_coll.sett_max_cp * sizeof (unsigned int));
  if (lc->opt_coll.next_cp == NULL)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      goto exit;
    }

  memset (lc->opt_coll.next_cp, 0xff, lc->tail_coll.sett_max_cp * sizeof (unsigned int));

  lc->opt_coll.uca_w_l13 = uca_w_l13;
  lc->opt_coll.uca_w_l4 = uca_w_l4;
  lc->opt_coll.uca_num = uca_exp_num;
  lc->opt_coll.uca_exp_num = max_num;
  lc->opt_coll.w_count = lc->tail_coll.sett_max_cp;

  for (cp = 0; cp < lc->tail_coll.sett_max_cp; cp++)
    {
      make_coll_key (&key, COLL_KEY_TYPE_CP, cp);
      ce_list = get_ce_list_from_coll_key (&key);

      uca_exp_num[cp] = ce_list->num;

      assert (uca_exp_num[cp] > 0);

      build_compressed_uca_w_l13 (ce_list, &(uca_w_l13[cp * max_num]));
      if (use_level_4)
	{
	  build_uca_w_l4 (ce_list, &(uca_w_l4[cp * max_num]));
	}

      coll_key_list[coll_key_list_cnt++] = cp;
    }

  assert (lc->opt_coll.count_contr == 0);

  for (i = 0; i < curr_uca.count_contr; i++)
    {
      make_coll_key (&key, COLL_KEY_TYPE_CONTR, i);
      ce_list = get_ce_list_from_coll_key (&key);

      coll_key_list[coll_key_list_cnt++] = i | INTL_MASK_CONTR;

      err_status = add_opt_coll_contraction (lc, &key, 0, true);
      if (err_status != NO_ERROR)
	{
	  goto exit;
	}
    }

  qsort (coll_key_list, coll_key_list_cnt, sizeof (unsigned int), uca_comp_func_coll_list_exp_fo);

  saved_uca_opt = uca_tailoring_options;
  uca_tailoring_options = &uca_exp_next_opt;

  /* TODO : optimize this to speed up 'next' computing */
  for (i = 0; i < coll_key_list_cnt - 1; i++)
    {
      UCA_COLL_KEY curr_key;
      UCA_COLL_KEY next_key;
      unsigned int curr_pos = coll_key_list[i];
      unsigned int next_pos = 0;
      int j;

      if (INTL_IS_NEXT_CONTR (curr_pos))
	{
	  make_coll_key (&curr_key, COLL_KEY_TYPE_CONTR, INTL_GET_NEXT_CONTR_ID (curr_pos));
	}
      else
	{
	  make_coll_key (&curr_key, COLL_KEY_TYPE_CP, curr_pos);
	}

      /* 'next' item is the first element which has weight level 1 greater then current item */
      for (j = i + 1; j < coll_key_list_cnt; j++)
	{
	  next_pos = coll_key_list[j];

	  if (uca_comp_func_coll_list_exp (&curr_pos, &next_pos) < 0)
	    {
	      break;
	    }
	}

      if (INTL_IS_NEXT_CONTR (next_pos))
	{
	  make_coll_key (&next_key, COLL_KEY_TYPE_CONTR, INTL_GET_NEXT_CONTR_ID (next_pos));
	}
      else
	{
	  make_coll_key (&next_key, COLL_KEY_TYPE_CP, next_pos);
	}

      set_next_value_for_coll_key (lc, &curr_key, &next_key);
    }

  uca_tailoring_options = saved_uca_opt;

  /* set next for last key to itself */
  {
    UCA_COLL_KEY curr_key;
    unsigned int curr_pos = coll_key_list[coll_key_list_cnt - 1];
    if (INTL_IS_NEXT_CONTR (curr_pos))
      {
	make_coll_key (&curr_key, COLL_KEY_TYPE_CONTR, INTL_GET_NEXT_CONTR_ID (curr_pos));
      }
    else
      {
	make_coll_key (&curr_key, COLL_KEY_TYPE_CP, curr_pos);
      }
    set_next_value_for_coll_key (lc, &curr_key, &curr_key);
  }

  if (lc->opt_coll.count_contr > 0)
    {
      err_status = optimize_coll_contractions (lc);
    }

exit:
  if (coll_key_list != NULL)
    {
      free (coll_key_list);
    }

  return err_status;
}

/*
 * uca_comp_func_coll_list_exp_fo - compare function for sorting collatable
 *				    elements according to UCA algorithm,
 *				    with full order
 *
 *  Note: This function is used in the first step of computing 'next' sequence
 *	  If result of 'uca_comp_func_coll_list_exp' is zero, the comparison
 *	  is performed on codepooints values. The purpose is to provide a
 *	  'deterministic comparison' in order to eliminate unpredictable
 *	  results of sort algorithm (qsort) when computing 'next' fields.
 */
static int
uca_comp_func_coll_list_exp_fo (const void *arg1, const void *arg2)
{
  unsigned int pos1;
  unsigned int pos2;
  int cmp;

  pos1 = *((unsigned int *) arg1);
  pos2 = *((unsigned int *) arg2);

  cmp = uca_comp_func_coll_list_exp (arg1, arg2);

  return (cmp == 0) ? (int) (pos1 - pos2) : cmp;
}

/*
 * uca_comp_func_coll_list_exp - compare function for sorting collatable
 *				 elements according to UCA algorithm
 *
 *  Note: this function is used to sort collatable elements according to
 *	  CEs tables and UCA settins (sorting options)
 *	  The elements in array are 32 bit unsigned integers, keys which
 *	  may be Unicode points or contractions (when highest bit is set)
 *
 */
static int
uca_comp_func_coll_list_exp (const void *arg1, const void *arg2)
{
  UCA_COLL_KEY pos1_key;
  UCA_COLL_KEY pos2_key;
  unsigned int pos1;
  unsigned int pos2;

  pos1 = *((unsigned int *) arg1);
  pos2 = *((unsigned int *) arg2);

  if (INTL_IS_NEXT_CONTR (pos1))
    {
      make_coll_key (&pos1_key, COLL_KEY_TYPE_CONTR, INTL_GET_NEXT_CONTR_ID (pos1));
    }
  else
    {
      make_coll_key (&pos1_key, COLL_KEY_TYPE_CP, pos1);
    }

  if (INTL_IS_NEXT_CONTR (pos2))
    {
      make_coll_key (&pos2_key, COLL_KEY_TYPE_CONTR, INTL_GET_NEXT_CONTR_ID (pos2));
    }
  else
    {
      make_coll_key (&pos2_key, COLL_KEY_TYPE_CP, pos2);
    }

  return uca_comp_func_coll_key (&pos1_key, &pos2_key);
}

/*
 * build_compressed_uca_w_l13 - builds the equivalent UCA weights for levels
 *			        1 to 3 of a collation element list
 *
 *  Note :
 *  The encoding of levels 1 to 3 is :
 *  33333332 22222222 1111111 1111111
 *  Ranges
 *  L1 = 0000-ffff
 *  L2 = 0000-01ff
 *  L3 = 0000-007f
 *
 */
static void
build_compressed_uca_w_l13 (const UCA_COLL_CE_LIST * ce_list, UCA_L13_W * uca_w_l13)
{
  int i;

  assert (ce_list != NULL);
  assert (uca_w_l13 != NULL);

  for (i = 0; i < ce_list->num; i++)
    {
      UCA_W w1 = GET_UCA_WEIGHT (ce_list, i, 0);
      UCA_W w2 = GET_UCA_WEIGHT (ce_list, i, 1);
      UCA_W w3 = GET_UCA_WEIGHT (ce_list, i, 2);
      UCA_L13_W w_l123 = w3 & 0x0000007f;

      w_l123 <<= 9;
      w_l123 |= w2 & 0x000001ff;
      w_l123 <<= 16;

      w_l123 |= w1 & 0x0000ffff;

      uca_w_l13[i] = w_l123;
    }
}

/*
 * build_uca_w_l4 - builds the equivalent UCA weights for level 4 from a
 *		    collation element list
 */
static void
build_uca_w_l4 (const UCA_COLL_CE_LIST * ce_list, UCA_L4_W * uca_w_l4)
{
  int i;

  assert (ce_list != NULL);
  assert (uca_w_l4 != NULL);

  for (i = 0; i < ce_list->num; i++)
    {
      uca_w_l4[i] = GET_UCA_WEIGHT (ce_list, i, 3);
    }
}
