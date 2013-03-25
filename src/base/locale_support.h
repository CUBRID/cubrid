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
 * locale_support.h : Locale support using LDML files
 *
 */

#ifndef _LOCALE_SUPPORT_H_
#define _LOCALE_SUPPORT_H_

#ident "$Id$"

#include <stddef.h>
#include "porting.h"
#include "dbtype.h"
#include "locale_lib_common.h"

/* Maximum Unicode characters
 * Do not change this above 65536 */
#define MAX_UNICODE_CHARS 65536

/* Allowed multiplier for data string casing.
 * How many times a string can grow or shrink (in characters) when performing
 * lower / upper */
#define INTL_CASING_EXPANSION_MULTIPLIER 2

/* Allowed multiplier for identifier casing. 
 * How many times a string identifier can grow or shrink (in bytes size) when
 * performing lower / upper on DB identifiers . 
 * Do not use for user string.
 * See Turkish casing for 'i' and 'I' */
#define INTL_IDENTIFIER_CASING_SIZE_MULTIPLIER 2

/* Mask for next sequence. Used to determine next string in sorting order
 * in LIKE operator.
 * If the value of 'next' has this bit set, then the next sequence is a
 * contraction, and the lower part of value indicates the contraction id
 * Otherwise, the 'next' value indicates a Unicode codepoint */
#define INTL_MASK_CONTR  0x80000000

#define INTL_IS_NEXT_CONTR(v) \
  (((v) & INTL_MASK_CONTR) == INTL_MASK_CONTR)

#define INTL_GET_NEXT_CONTR_ID(v) ((v) & (~INTL_MASK_CONTR))

/*
 * Encoding of L1-L3 UCA weights on 32 bit unsigned int:
 * 33333332 22222222 1111111 1111111 
 * L1 = 0000-ffff
 * L2 = 0000-01ff
 * L3 = 0000-007f 
 */
#define UCA_GET_L1_W(v) ((v) & 0x0000ffff)
#define UCA_GET_L2_W(v) (((v) & 0x01ff0000) >> 16)
#define UCA_GET_L3_W(v) (((v) & 0xfe000000) >> 25)

#define LOC_LOCALE_STR_SIZE 10
#define LOC_DATA_BUFF_SIZE  256

#define COLL_NAME_SIZE 32
#define LOC_LIB_SYMBOL_NAME_SIZE 64


/* constants for Gregorian calendar */
#define CAL_MONTH_COUNT  12
#define CAL_DAY_COUNT  7
#define CAL_AM_PM_COUNT  12

/* Length in character of abbreviated format text for month : "Mon" */
#define LOC_CAL_FMT_MONTH_ABBR_LEN  3
/* Length in character of wide format text for month : "Month" */
#define LOC_CAL_FMT_MONTH_WIDE_LEN  5
/* Length in character of abbreviated format text for day : "Dy" */
#define LOC_CAL_FMT_DAY_ABBR_LEN  2
/* Length in character of wide format text for day : "Day" */
#define LOC_CAL_FMT_DAY_WIDE_LEN  3
/* Length in character of wide format text for day : "AM" */
#define LOC_CAL_FMT_AM_LEN  2

/* Multiplier for number of characters that a calendar token can have;
 * It applies for each token format text.
 * Current value is set based on the 'Day' format text and longest day name
 * in km_KH which is 14 chars */
#define LOC_PARSE_FRMT_TO_TOKEN_MULT  5

#define LOC_DATA_MONTH_ABBR_SIZE (LOC_CAL_FMT_MONTH_ABBR_LEN) * \
				 (LOC_PARSE_FRMT_TO_TOKEN_MULT) * \
				 (INTL_UTF8_MAX_CHAR_SIZE)
#define LOC_DATA_MONTH_WIDE_SIZE (LOC_CAL_FMT_MONTH_WIDE_LEN) * \
				 (LOC_PARSE_FRMT_TO_TOKEN_MULT) * \
				 (INTL_UTF8_MAX_CHAR_SIZE)

#define LOC_DATA_DAY_ABBR_SIZE (LOC_CAL_FMT_DAY_ABBR_LEN) * \
			       (LOC_PARSE_FRMT_TO_TOKEN_MULT) * \
			       (INTL_UTF8_MAX_CHAR_SIZE)

#define LOC_DATA_DAY_WIDE_SIZE (LOC_CAL_FMT_DAY_WIDE_LEN) * \
			       (LOC_PARSE_FRMT_TO_TOKEN_MULT) * \
			       (INTL_UTF8_MAX_CHAR_SIZE)

#define LOC_DATA_AM_PM_SIZE (LOC_CAL_FMT_AM_LEN) * \
			    (LOC_PARSE_FRMT_TO_TOKEN_MULT) * \
			    (INTL_UTF8_MAX_CHAR_SIZE)

#define LOC_DATA_CURRENCY_ISO_CODE_LEN 3

#define LOC_DATA_COLL_TWO_CHARS 13
#define LOC_DATA_TAILOR_RULES_COUNT_GROW 128
#define LOC_DATA_COLL_CUBRID_TAILOR_COUNT_GROW 8
#define MAX_STRLEN_FOR_COLLATION_ELEMENT 136

#define DUMPLOCALE_IS_CALENDAR			1
#define DUMPLOCALE_IS_NUMBERING			(1 << 1)
#define DUMPLOCALE_IS_ALPHABET			(1 << 2)
#define DUMPLOCALE_IS_ALPHABET_LOWER		(1 << 3)
#define DUMPLOCALE_IS_ALPHABET_UPPER		(1 << 4)
#define DUMPLOCALE_IS_IDENTIFIER_ALPHABET	(1 << 5)
#define DUMPLOCALE_IS_IDENTIFIER_ALPHABET_LOWER	(1 << 6)
#define DUMPLOCALE_IS_IDENTIFIER_ALPHABET_UPPER	(1 << 7)
#define DUMPLOCALE_IS_COLLATION_CP_ORDER	(1 << 8)
#define DUMPLOCALE_IS_COLLATION_WEIGHT_ORDER	(1 << 9)
#define DUMPLOCALE_IS_NORMALIZATION		(1 << 10)
#define DUMPLOCALE_IS_TEXT_CONV			(1 << 11)

#define ERR_MSG_SIZE 512

#define LOG_LOCALE_ERROR(msg, er_status, do_print) \
  do { \
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 1, msg); \
      if (do_print) \
	{ \
	  fprintf (stderr, "Error processing locales: %s\n", msg); \
	} \
    } while (0)

#define MAPPING_INDEX_MASK  0x100000

#define SET_MAPPING_INDEX(val, is_used, offset)	  \
  do {						  \
    val = (offset);				  \
    if (is_used)				  \
      {						  \
	val |= MAPPING_INDEX_MASK;		  \
      }						  \
  } while (0);

#define CP_HAS_MAPPINGS(val)			  \
  (((val) & MAPPING_INDEX_MASK) == MAPPING_INDEX_MASK)

#define GET_MAPPING_OFFSET(val)	((val) & ~MAPPING_INDEX_MASK)

typedef unsigned short UCA_CP;
typedef unsigned short UCA_W;

typedef struct locale_file LOCALE_FILE;
struct locale_file
{
  char *locale_name;
  char *ldml_file;
  char *lib_file;
};

typedef struct ldml_context LDML_CONTEXT;
struct ldml_context
{
  char *ldml_file;
  int line_no;
};

/* Collation structures */
/* Tailoring level */
typedef enum
{
  TAILOR_UNDEFINED = 0,
  TAILOR_PRIMARY,
  TAILOR_SECONDARY,
  TAILOR_TERTIARY,
  TAILOR_QUATERNARY,
  TAILOR_IDENTITY
} T_LEVEL;

/* Tailoring anchoring direction */
typedef enum
{
  TAILOR_AFTER = 0,
  TAILOR_BEFORE
} TAILOR_DIR;

/* Type of char data. If tag is cp, ecp, buffer type ill be BUF_TYPE_CODE  
 * If tag is ch, ech, buffer type will be BUF_TYPE_CHAR. */
typedef enum
{
  BUF_TYPE_CHAR,
  BUF_TYPE_CODE
} CP_BUF_TYPE;

/* Tailoring position */
typedef enum
{
  RULE_POS_BUFFER = 0,		/* Non-logical position, use buffer */

  RULE_POS_FIRST_VAR,		/* Logical first variable */
  RULE_POS_LAST_VAR,		/* Logical last variable */

  RULE_POS_FIRST_PRI_IGN,	/* Logical first primary ignorable */
  RULE_POS_LAST_PRI_IGN,	/* Logical last primary ignorable */

  RULE_POS_FIRST_SEC_IGN,	/* Logical first secondary ignorable */
  RULE_POS_LAST_SEC_IGN,	/* Logical last secondary ignorable */

  RULE_POS_FIRST_TERT_IGN,	/* Logical first tertiary ignorable */
  RULE_POS_LAST_TERT_IGN,	/* Logical last tertiary ignorable */

  RULE_POS_FIRST_NON_IGN,	/* Logical first non-ignorable */
  RULE_POS_LAST_NON_IGN,	/* Logical last non-ignorable */

  RULE_POS_FIRST_TRAIL,		/* Logical first trailing */
  RULE_POS_LAST_TRAIL		/* Logical last trailing */
} RULE_POS_TYPE;

typedef struct tailor_rule TAILOR_RULE;
struct tailor_rule
{
  T_LEVEL level;		/* weight level : primary, .. identity */

  /* anchor (reference) buffer, for which the rule is defined
   * it may contain one or two (for expansion rule) UTF-8 chars
   * buffer is nul-terminated */
  char anchor_buf[LOC_DATA_COLL_TWO_CHARS];

  /* Reference : */
  RULE_POS_TYPE r_pos_type;	/* processing flag : 
				 * logical position or buffer value for reference */
  char *r_buf;			/* Buffer containing UTF-8 characters of reference */
  int r_buf_size;

  TAILOR_DIR direction;		/* direction for applying rule : after, before */

  /* Buffer containing UTF-8 characters to be tailored */
  /* buffer is NOT nul-terminated */
  char *t_buf;
  int t_buf_size;

  bool multiple_chars;		/* true : indicates a rule for tailoring multiple chars 
				 * false : rule for a single character */
};


/* 
 * CUBRID_TAILOR_RULE - Structure used for representing the rules for 
 *			absolute tailoring e.g. manually setting the weights 
 *			and collation elements for unicode character or
 *  			character ranges.
*/
typedef struct cubrid_tailor_rule CUBRID_TAILOR_RULE;
struct cubrid_tailor_rule
{
  /* The first and last (incl.) codepoints of the 
   * codepoint range to be tailored, in text format 
   * for later validation and parsing. */
  char start_cp_buf[LOC_DATA_BUFF_SIZE];
  char end_cp_buf[LOC_DATA_BUFF_SIZE];
  CP_BUF_TYPE start_cp_buf_type;
  CP_BUF_TYPE end_cp_buf_type;

  char start_weight[MAX_STRLEN_FOR_COLLATION_ELEMENT];
  /* Buffer containing the weight value to 
   * use in the rule. 
   * Buffer is NOT NULL-terminated.
   * Example : [100.0.0.0][0.0.0.2]...etc.
   */

  char step[MAX_STRLEN_FOR_COLLATION_ELEMENT];
  /* The step (per level) with which we increase the 
   * weight range.
   * Default value is 0 for all levels, 
   * so single-codepoint and identical tailoring 
   * can be easily implemented.
   */
};

typedef enum
{
  CONTR_IGNORE = 0x0,
  CONTR_TAILORING_USE = 0x1,
  CONTR_DUCET_USE = 0x2
} COLL_CONTR_POLICY;

/* Matching of a pattern containing a contraction starter on last position:
 * if "ch" is a contraction, then :
 * "bac" is not matched in "bachxxx", if MATCH_CONTR_BOUND_FORBID 
 * "bac" is matched in "bachxxx", if MATCH_CONTR_BOUND_ALLOW */
typedef enum
{
  MATCH_CONTR_BOUND_FORBID = 0,
  MATCH_CONTR_BOUND_ALLOW = 1
} COLL_MATCH_CONTR;

/* UCA sort options */
typedef struct uca_options UCA_OPTIONS;
struct uca_options
{
  /* collation settings */
  T_LEVEL sett_strength;	/* collation strength (primary, .. identity) */
  bool sett_backwards;		/* backwards on/off */
  bool sett_caseLevel;		/* caseLevel on/off */
  int sett_caseFirst;		/* 0=off; 1=upper ; 2=lower */
  bool sett_expansions;		/* use expansions */

  /* how to handle contractions, should be regarded as bit-field flag */
  int sett_contr_policy;

  /* set only when sorting for 'next' with expansions : not serialized */
  bool use_only_first_ce;

  /* how to handle string matching when contractions spans over the boundary */
  COLL_MATCH_CONTR sett_match_contr;
};

/* Below there are members containing the symbol name from where to load
   * certain weight arrays. By default, the symbol name is the corresponding
   * name of the exported weight array. However, if 2 collations have
   * identical weight arrays after compiling, the symbol name corresponding to
   * one of the arrays will be set to the name of the other, and the actual
   * array will not be exported into the shared library */
typedef struct coll_data_ref COLL_DATA_REF;
struct coll_data_ref
{
  char coll_weights_ref[LOC_LIB_SYMBOL_NAME_SIZE];
  char coll_next_cp_ref[LOC_LIB_SYMBOL_NAME_SIZE];
  char coll_uca_num_ref[LOC_LIB_SYMBOL_NAME_SIZE];
  char coll_uca_w_l13_ref[LOC_LIB_SYMBOL_NAME_SIZE];
  char coll_uca_w_l4_ref[LOC_LIB_SYMBOL_NAME_SIZE];
  char coll_contr_list_ref[LOC_LIB_SYMBOL_NAME_SIZE];
  char coll_cp_first_contr_array_ref[LOC_LIB_SYMBOL_NAME_SIZE];
};

typedef struct coll_data COLL_DATA;
struct coll_data
{
  int coll_id;			/* collation id */
  char coll_name[COLL_NAME_SIZE];	/* collation name */

  UCA_OPTIONS uca_opt;

  unsigned int *weights;	/* array of weight (one weight per CP) */
  unsigned int *next_cp;	/* next CP (in order defined by collation) */
  int w_count;			/* # of codepoints in this collation */

  /* Size of uca_w = 'w_count' X 'uca_exp_num' X 'sizeof (UCA_W)' */
  /* For each codepoint entry in uca_w only the corresponding uca_num weights
   * are used */
  int uca_exp_num;		/* max number of CE per codepoint */
  char *uca_num;		/* number of CE for each codepoint */
  UCA_L13_W *uca_w_l13;		/* weight array L1, L2, L3 */
  UCA_L4_W *uca_w_l4;

  COLL_CONTRACTION *contr_list;	/* contactions lists; contractions are stored 
				 * in binary ascending order of UTF-8 buffer */
  int count_contr;
  int contr_min_size;		/* size of smallest contraction buffer (in bytes) */

  /* array of first contraction index for each codepoint contains 'w_count'
   * elements : value -1 means CP is not a contraction starter 
   * other value = index of contraction in contractions list ('contr_list') */
  int *cp_first_contr_array;
  /* codepoint value from which 'cp_first_contr_array' can be used */
  unsigned int cp_first_contr_offset;
  /* # of codepoints in 'cp_first_contr_array' */
  unsigned int cp_first_contr_count;

  char checksum[32 + 1];
};

typedef struct coll_tailoring COLL_TAILORING;
struct coll_tailoring
{
  char coll_name[COLL_NAME_SIZE];	/* collation name */

  int coll_id;

  UCA_OPTIONS uca_opt;

  /* number of codepoints to take into account for collation 
   * -1 means unlimited (we support up to MAX_UNICODE_CHARS) */
  int sett_max_cp;

  /* collation tailoring rules */
  int count_rules;		/* # of tailorings */
  int max_rules;		/* # of max (allocated tailorings) */
  TAILOR_RULE *rules;		/* tailoring rules */

  CUBRID_TAILOR_RULE *cub_rules;	/* absolute tailoring rules */
  int cub_count_rules;		/* # of tailorings */
  int cub_max_rules;		/* # of max (allocated tailorings) */
  LDML_CONTEXT ldml_context;
};

/* Alphabet generation type :
 * in case several locales use the same UNICODE or ASCII modes, only one
 * reference copy is loaded */
typedef enum
{
  ALPHABET_UNICODE = 0,
  ALPHABET_ASCII,
  ALPHABET_TAILORED
} ALPHABET_TYPE;

/* alphabet structures (lower, upper) */
typedef struct alphabet_data ALPHABET_DATA;
struct alphabet_data
{
  ALPHABET_TYPE a_type;
  int codeset;			/* codeset of alphabet : not serialized */
  int l_count;			/* number of elements */

  int lower_multiplier;		/* how many codepoints contains each lower
				 * entry */
  unsigned int *lower_cp;	/* lower CP */

  int upper_multiplier;		/* how many codepoints contains each upper
				 * entry */
  unsigned int *upper_cp;	/* upper CP */

  bool do_not_save;		/* used by genlocale if shared alphabet */
};

typedef enum
{
  TR_UPPER = 0,
  TR_LOWER
} TRANSFORM_TYPE;

/* Describes how a text tranforms into another text 
 * Used for lower / upper casing rule description */
typedef struct transform_rule TRANSFORM_RULE;
struct transform_rule
{
  TRANSFORM_TYPE type;

  char *src;
  int src_size;

  char *dest;
  int dest_size;
};


typedef struct alphabet_tailoring ALPHABET_TAILORING;
struct alphabet_tailoring
{
  /* number of codepoints the optimization process will to take into account
   * for casing : -1 means unlimited (we support up to MAX_UNICODE_CHARS) */
  int sett_max_letters;

  int alphabet_mode;		/* 0 : default UnicodeData
				 * 1 : UnicodeData with specified file
				 * 2 : ASCII letter and casing */
  /* file path for Unicode data (if 'alphabet_mode' == 1) */
  char unicode_data_file[PATH_MAX];

  int count_rules;		/* # of tailorings */
  int max_rules;		/* # of max (allocated tailorings) */
  TRANSFORM_RULE *rules;
  LDML_CONTEXT ldml_context;
};

/* text conversions */
typedef enum
{
  TEXT_CONV_NO_CONVERSION = 0,
  TEXT_CONV_ISO_88591_BUILTIN,
  TEXT_CONV_ISO_88599_BUILTIN,
  TEXT_CONV_GENERIC_1BYTE,	/* user defined UTF-8 to single byte codepage */
  TEXT_CONV_GENERIC_2BYTE	/* user defined UTF-8 to double byte codepage */
} TEXT_CONV_TYPE;

#define TXT_CONV_SYSTEM_STR_SIZE	256
typedef struct text_conversion TEXT_CONVERSION;
struct text_conversion
{
  TEXT_CONV_TYPE conv_type;

  /* both identifiers are used to ensure locale binary files portability */
  char *win_codepages;		/* Windows codepage identifier */
  char *nl_lang_str;		/* Linux language string */

  unsigned char byte_flag[256];	/* used in DBCS encoding schemes :
				 * 0 : single byte character 
				 * 1 : leading byte for double byte char
				 * 2 : invalid byte */
  /* UTF-8 to text */
  unsigned int utf8_first_cp;
  unsigned int utf8_last_cp;
  CONV_CP_TO_BYTES *utf8_to_text;

  /* text to UTF-8 */
  unsigned int text_first_cp;
  unsigned int text_last_cp;
  CONV_CP_TO_BYTES *text_to_utf8;

  int (*utf8_to_text_func) (const char *, const int, char **, int *);
  int (*text_to_utf8_func) (const char *, const int, char **, int *);
  void (*init_conv_func) (void);
};

typedef struct text_conversion_prm TEXT_CONVERSION_PRM;
struct text_conversion_prm
{
  TEXT_CONV_TYPE conv_type;

  char win_codepages[TXT_CONV_SYSTEM_STR_SIZE];	/* Windows codepage identifier */
  char nl_lang_str[TXT_CONV_SYSTEM_STR_SIZE];	/* Linux language string */

  char conv_file[PATH_MAX];
};

#define UNICODE_NORMALIZATION_DECORATOR "std"

typedef struct unicode_normalization UNICODE_NORMALIZATION;
struct unicode_normalization
{
  UNICODE_MAPPING *unicode_mappings;
  int unicode_mappings_count;	/* total number of mappings, fully,
				 * partially or not decomposed. */
  int *unicode_mapping_index;
  int *list_full_decomp;

  bool do_not_save;
};

#define CAL_SIMPLE_DATE_FORMAT_SIZE  30
#define CAL_COMP_DATE_FORMAT_SIZE  48

/* user defined LOCALE DATA */
typedef struct locale_collation LOCALE_COLLATION;
struct locale_collation
{
  COLL_TAILORING tail_coll;	/* collation info gathered from LDML */
  COLL_DATA opt_coll;		/* optimized collation data */
  COLL_DATA_REF coll_ref;	/* collation array export identifiers */
  bool do_not_save;		/* set true if collation is shared and already
				 * processed */
};

typedef struct locale_data LOCALE_DATA;
struct locale_data
{
  /* name of locale : used for validation;
   * should be set by application, before
   * LDML parsing */
  char locale_name[LOC_LOCALE_STR_SIZE];

  /* calendar info : only Gregorian calendar is supported */
  char dateFormat[CAL_SIMPLE_DATE_FORMAT_SIZE];	/* date format */
  char timeFormat[CAL_SIMPLE_DATE_FORMAT_SIZE];	/* time format */

  char datetimeFormat[CAL_COMP_DATE_FORMAT_SIZE];	/* datetime format */
  char timestampFormat[CAL_COMP_DATE_FORMAT_SIZE];	/* datetime format */

  /* name of months , week days, day periods */
  char month_names_abbreviated[CAL_MONTH_COUNT][LOC_DATA_MONTH_ABBR_SIZE];
  char month_names_wide[CAL_MONTH_COUNT][LOC_DATA_MONTH_WIDE_SIZE];
  char day_names_abbreviated[CAL_DAY_COUNT][LOC_DATA_DAY_ABBR_SIZE];
  char day_names_wide[CAL_DAY_COUNT][LOC_DATA_DAY_WIDE_SIZE];
  char am_pm[CAL_AM_PM_COUNT][LOC_DATA_AM_PM_SIZE];

  char month_names_abbr_parse_order[CAL_MONTH_COUNT];
  char month_names_wide_parse_order[CAL_MONTH_COUNT];
  char day_names_abbr_parse_order[CAL_DAY_COUNT];
  char day_names_wide_parse_order[CAL_DAY_COUNT];
  char am_pm_parse_order[CAL_AM_PM_COUNT];

  /* numeric symbols : digit grouping, decimal */
  char number_decimal_sym;
  char number_group_sym;
  DB_CURRENCY default_currency_code;	/* ISO code for default locale currency. */

  LOCALE_COLLATION *collations;
  int coll_cnt;

  ALPHABET_TAILORING alpha_tailoring;
  ALPHABET_DATA alphabet;	/* data for user lower / uppper */
  ALPHABET_DATA identif_alphabet;	/* data for lower / uppper for identifiers */

  /* unicode data file used for alphabets and normalization */
  int unicode_mode;		/* 0 : default UnicodeData
				 * 1 : UnicodeData with specified file */
  /* file path for Unicode data (if 'alphabet_mode' == 1) */
  char unicode_data_file[PATH_MAX];

  /* normalization */
  UNICODE_NORMALIZATION unicode_normalization;

  /* console text conversion */
  TEXT_CONVERSION txt_conv;
  TEXT_CONVERSION_PRM txt_conv_prm;

  /* data members used during processing : */
  int curr_period;		/* processing index for calendar :
				 * 0-11 : months
				 * 0-6 : week days
				 * 0-12 : AM, PM period names */
  int name_type;		/* processing flag for calendar name :
				 * 1 - abbr
				 * 2 - wide;
				 * 0 - uninitialized */

  /* processing : last anchor : used when build a new collation rule */
  /* buffer is nul-terminated */
  char last_anchor_buf[LOC_DATA_COLL_TWO_CHARS];
  RULE_POS_TYPE last_rule_pos_type;	/* processing flag : 
					 * logical position or buffer */
  TAILOR_DIR last_rule_dir;	/* processing flag :
				 * after, before */
  T_LEVEL last_rule_level;	/* processing flag :
				 * weight level : primary, .. identity
				 * (used for validation) */

  /* processing : last tailoring reference : used when building collation rules
   * pointer to a buffer : either a tailoring buffer (not nul-terminated) in a rule 
   * or an anchor buffer (last_anchor_buf) */
  char *last_r_buf_p;
  int last_r_buf_size;

  /* processing : used for intermediary (partial) content data in LDML
   * buffer is nul-terminated */
  char data_buffer[LOC_DATA_BUFF_SIZE];
  int data_buf_count;

  char checksum[32 + 1];

  LDML_CONTEXT ldml_context;
};

#ifdef __cplusplus
extern "C"
{
#endif

  void locale_init_data (LOCALE_DATA * ld, const char *locale_name);
  void locale_destroy_data (LOCALE_DATA * ld);
  void locale_destroy_alphabet_data (const ALPHABET_DATA * a);
  void locale_destroy_normalization_data (UNICODE_NORMALIZATION * norm);
  int locale_get_cfg_locales (LOCALE_FILE ** p_locale_files,
			      int *p_num_locales, bool is_lang_init);
  int locale_check_and_set_default_files (LOCALE_FILE * lf,
					  bool is_lang_init);
  int locale_prepare_C_file (void);
  int locale_compile_locale (LOCALE_FILE * lf, LOCALE_DATA * ld,
			     bool is_verbose);
  void locale_mark_duplicate_collations (LOCALE_DATA ** ld, int start_index,
					 int end_index, bool is_verbose);
  int locale_save_all_to_C_file (LOCALE_DATA ** ld, int start_index,
				 int end_index, LOCALE_FILE * lf);
  int locale_dump (void *data, LOCALE_FILE * lf,
		   int dl_settings, int start_value, int end_value);
  int locale_dump_lib_collations (void *lib_handle, const LOCALE_FILE * lf,
				  int dl_settings, int start_value,
				  int end_value);
  void locale_free_shared_data (void);

#ifdef __cplusplus
}
#endif

#endif				/* _LOCALE_SUPPORT_H_ */
