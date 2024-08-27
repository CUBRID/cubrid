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
 * locale_support.c : Locale support using LDML (XML) files
 */

#ident "$Id$"


#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "porting.h"
#include "environment_variable.h"
#include "utility.h"
#include "xml_parser.h"
#include "chartype.h"
#include "error_manager.h"
#include "release_string.h"
#include "uca_support.h"
#include "unicode_support.h"
#include "message_catalog.h"
#include "language_support.h"
#include "system_parameter.h"
#include "crypt_opfunc.h"
#if !defined(WINDOWS)
#include <netinet/in.h>
#endif /* !WINDOWS */
#include "locale_support.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

#define TXT_CONV_LINE_SIZE 512
#define TXT_CONV_ITEM_GROW_COUNT 128

#define LOC_CURRENT_COLL_TAIL(ld) (&(ld->collations[ld->coll_cnt].tail_coll))
const char *ldml_ref_day_names[] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };

/* this must map to 'Am_Pm_name' from string_opfunc.c */
const char *ldml_ref_am_pm_names[] = { "am", "pm", "Am", "Pm", "AM", "PM",
  "a.m.", "p.m.", "A.m.", "P.m.", "A.M.", "P.M."
};

typedef struct
{
  unsigned int text_cp;
  unsigned int unicode_cp;
} TXT_CONV_ITEM;

/* shared data : this is used during genlocale process
 * data shared by several locales are centralized here; and saved only once
 * in the generated libray */
typedef enum
{
  LOC_SHARED_COLLATION = 0,
  LOC_SHARED_ALPHABET = 1,
  LOC_SHARED_NORMALIZATION = 2
} LOC_SHARED_DATA_TYPE;

typedef struct
{
  LOC_SHARED_DATA_TYPE lsd_type;
  char lsd_key[COLL_NAME_SIZE];

/* print size of an integer */
#define COLL_SHARED_DATA_SIZE (10 + 2)
  void *data;
  LDML_CONTEXT ldml_context;
} LOC_SHARED_DATA;

static LOC_SHARED_DATA *shared_data = NULL;
static int count_shared_data = 0;
static int alloced_shared_data = 0;


static int start_element_ok (void *data, const char **attr);
static int end_element_ok (void *data, const char *el_name);
static int start_calendar (void *data, const char **attr);
static int end_dateFormatCUBRID (void *data, const char *el_name);
static int end_timeFormatCUBRID (void *data, const char *el_name);
static int end_datetimeFormatCUBRID (void *data, const char *el_name);
static int end_timestampFormatCUBRID (void *data, const char *el_name);
static int end_timetzFormatCUBRID (void *data, const char *el_name);
static int end_datetimetzFormatCUBRID (void *data, const char *el_name);
static int end_timestamptzFormatCUBRID (void *data, const char *el_name);
static int start_calendar_name_context (void *data, const char **attr);
static int start_month_day_Width (void *data, const char **attr);
static int end_month_day_Width (void *data, const char *el_name);
static int start_month (void *data, const char **attr);
static int end_month (void *data, const char *el_name);
static int start_day (void *data, const char **attr);
static int end_day (void *data, const char *el_name);
static int start_dayPeriodWidth (void *data, const char **attr);
static int start_dayPeriod (void *data, const char **attr);
static int end_dayPeriod (void *data, const char *el_name);
static int start_numbers_symbols (void *data, const char **attr);
static int start_currency (void *data, const char **attr);
static int end_number_symbol (void *data, const char *el_name);
static int start_collations (void *data, const char **attr);
static int start_one_collation (void *data, const char **attr);
static int end_one_collation (void *data, const char *el_name);
static int start_collation_settings (void *data, const char **attr);
static int start_collation_reset (void *data, const char **attr);
static int end_collation_reset (void *data, const char *el_name);
static int start_collation_rule (void *data, const char **attr);
static int start_collation_cubrid_rule (void *data, const char **attr);
static int start_collation_cubrid_rule_set_wr (void *data, const char **attr);
static int end_collation_cubrid_rule_set (void *data, const char *el_name);
static int end_collation_cubrid_rule_set_cp_ch (void *data, const char *el_name);
static int end_collation_cubrid_rule_set_ech_ecp (void *data, const char *el_name);
static int end_collation_cubrid_rule_set_w_wr (void *data, const char *el_name);
static int handle_data_collation_rule (void *data, const char *s, int len);
static int end_collation_rule (void *data, const char *el_name);
static int start_collation_x (void *data, const char **attr);
static int end_collation_x (void *data, const char *el_name);
static int end_collation_x_rule (void *data, const char *el_name);
static int end_collation_x_extend (void *data, const char *el_name);
static int end_collation_x_context (void *data, const char *el_name);
static int start_collation_logical_pos (void *data, const char **attr);
static int end_collation_logical_pos (void *data, const char *el_name);
static int start_one_alphabet (void *data, const char **attr);
static int start_upper_case_rule (void *data, const char **attr);
static int end_case_rule (void *data, const char *el_name);
static int start_lower_case_rule (void *data, const char **attr);
static int end_transform_buffer (void *data, const char *el_name);
static int start_include_collation (void *data, const char **attr);

static int start_unicode_file (void *data, const char **attr);

static int start_consoleconversion (void *data, const char **attr);

static int handle_data (void *data, const char *s, int len);


static void clear_data_buffer (XML_PARSER_DATA * pd);
static LOCALE_COLLATION *new_locale_collation (LOCALE_DATA * ld);
static TAILOR_RULE *new_collation_rule (LOCALE_DATA * ld);
static TRANSFORM_RULE *new_transform_rule (LOCALE_DATA * ld);
static CUBRID_TAILOR_RULE *new_collation_cubrid_rule (LOCALE_DATA * ld);
static void locale_alloc_collation_id (COLL_TAILORING * coll_tail);
static int locale_check_collation_id (const COLL_TAILORING * coll_tail);

static void print_debug_start_el (void *data, const char **attrs, const char *msg, const int status);
static void print_debug_end_el (void *data, const char *msg, const int status);
static void print_debug_data_content (void *data, const char *msg, const int status);
static int load_console_conv_data (LOCALE_DATA * ld, bool is_verbose);

static int locale_save_to_C_file (LOCALE_FILE lf, LOCALE_DATA * ld);
static int locale_save_calendar_to_C_file (FILE * fp, LOCALE_DATA * ld);
static int locale_save_alphabets_to_C_file (FILE * fp, LOCALE_DATA * ld);
static int locale_save_one_alphabet_to_C_file (FILE * fp, ALPHABET_DATA * a, bool save_w_identier_name,
					       const char *alpha_suffix);
static int locale_save_collation_data_to_C_file (FILE * fp, LOCALE_COLLATION * lc);
static int locale_save_console_conv_to_C_file (FILE * fp, LOCALE_DATA * ld);
static int locale_save_normalization_to_C_file (FILE * fp, LOCALE_DATA * ld);

static void locale_destroy_collation_tailorings (const COLL_TAILORING * ct);
static void locale_destroy_collation_data (const COLL_DATA * cd);
static void locale_destroy_alphabet_tailoring (const ALPHABET_TAILORING * cd);
static void locale_destroy_console_conversion (const TEXT_CONVERSION * tc);
static int str_pop_token (char *str_p, char **token_p, char **next_p);
static int dump_locale_alphabet (ALPHABET_DATA * ad, int dl_settings, int lower_bound, int upper_bound);

static void dump_collation_key (COLL_DATA * coll, const unsigned int key, bool print_weight, bool print_key);
static void dump_collation_contr (COLL_DATA * coll, const COLL_CONTRACTION * contr, bool print_weight,
				  bool print_contr);
static void dump_collation_codepoint (COLL_DATA * coll, const unsigned int cp, bool print_weight, bool print_cp);
static void dump_locale_normalization (UNICODE_NORMALIZATION * norm);
static void dump_unicode_mapping (UNICODE_MAPPING * um, const int mode);
static int dump_console_conversion (TEXT_CONVERSION * tc);

static int comp_func_coll_uca_exp_fo (const void *arg1, const void *arg2);
static int comp_func_coll_uca_exp (const void *arg1, const void *arg2);

static int comp_func_coll_uca_simple_weights_fo (const void *arg1, const void *arg2);
static int comp_func_coll_uca_simple_weights (const void *arg1, const void *arg2);
static int comp_func_parse_order_index (const void *arg1, const void *arg2);
static void locale_make_calendar_parse_order (LOCALE_DATA * ld);
static int locale_check_and_set_shared_data (const LOC_SHARED_DATA_TYPE lsd_type, const char *lsd_key, const void *data,
					     LDML_CONTEXT * ldml_context, LOC_SHARED_DATA ** found_entry);
static int locale_compute_coll_checksum (COLL_DATA * cd);
static int locale_alphabet_data_size (ALPHABET_DATA * a);
static int locale_alphabet_data_to_buf (ALPHABET_DATA * a, char *buf);
static int locale_compute_locale_checksum (LOCALE_DATA * ld);
static int common_collation_end_rule (void *data, LOCALE_DATA * ld, const int rule_id, TAILOR_RULE * t_rule);
static int common_collation_start_rule (void *data, const char **attr, LOCALE_DATA * ld, TAILOR_RULE * t_rule);

#define PRINT_DEBUG_START(d, a, m, s)				      \
   do {								      \
      print_debug_start_el (d, a, m, s);			      \
   } while (0);

#define PRINT_DEBUG_END(d, m, s)				      \
   do {								      \
      print_debug_end_el (d, m, s);				      \
   } while (0);

#define PRINT_DEBUG_DATA(d, m, s)				      \
   do {								      \
      print_debug_data_content (d, m, s);			      \
   } while (0);

#define PRINT_TO_C_FILE_MAX_INT_LINE 10

#if defined(_WIN32) || defined(WINDOWS) || defined(WIN64)
#define DLL_EXPORT_PREFIX "__declspec(dllexport) "
#define LOCLIB_FILE_EXT "dll"
#else
#define DLL_EXPORT_PREFIX   ""
#define LOCLIB_FILE_EXT "so"
#endif

#define PRINT_STRING_TO_C_FILE(fp, val, len)                          \
  do {                                                                \
    int istr;                                                         \
    fprintf (fp, "\"");                                               \
    for (istr = 0; istr < len; istr++)                                \
      {                                                               \
	fprintf (fp, "\\x%02X", (unsigned char) val[istr]);           \
      }                                                               \
    fprintf (fp, "\"");						      \
  } while (0);

#define PRINT_VAR_TO_C_FILE(fp, type, valname, val, format, d)	      \
  do {								      \
    fprintf (fp, "\n" DLL_EXPORT_PREFIX "const "			      \
	     type " " valname "_%s = " format ";\n", d, val);	      \
  } while (0);

#define PRINT_STRING_VAR_TO_C_FILE(fp, valname, val, d)			  \
  do {                                                                    \
    fprintf (fp, "\n" DLL_EXPORT_PREFIX "const char " valname "_%s[] = ", d); \
    PRINT_STRING_TO_C_FILE (fp, val, strlen (val));			  \
    fprintf (fp, ";\n");                                                  \
  } while (0);

#define PRINT_STRING_ARRAY_TO_C_FILE(fp, valname, arrcount, val, d)	    \
  do {									    \
    int istrarr;							    \
    fprintf(fp, "\n" DLL_EXPORT_PREFIX"const char* " valname "_%s[] = {\n", d);\
    for (istrarr = 0; istrarr < arrcount; istrarr++)			    \
      {									    \
	fprintf(fp, "\t");						    \
	PRINT_STRING_TO_C_FILE (fp, val[istrarr], strlen(val[istrarr]));    \
	if (istrarr < arrcount - 1)					    \
	  {								    \
	    fprintf(fp, ",\n");						    \
	  }								    \
	else								    \
	  {								    \
	    fprintf(fp, "\n");						    \
	  }								    \
      }									    \
    fprintf(fp, "};\n");						    \
  } while (0);


#define PRINT_NUM_ARRAY_TO_C_FILE(fp, vname, vtype, intf, arrcount, val, d) \
  do {									    \
    int i_arr, j_arr;							    \
    fprintf(fp,								    \
	    "\n" DLL_EXPORT_PREFIX "const " vtype " " vname "_%s[] = {\n", d);    \
    j_arr = 1;								    \
    for (i_arr = 0; i_arr < arrcount; i_arr++)				    \
      {									    \
	fprintf(fp, "\t");						    \
	fprintf(fp, intf, val[i_arr]);					    \
	if (i_arr < arrcount - 1)					    \
	  {								    \
	    fprintf(fp, ",");						    \
	  }								    \
	j_arr++;							    \
	if (j_arr > PRINT_TO_C_FILE_MAX_INT_LINE)			    \
	  {								    \
	    j_arr = 1;							    \
	    fprintf(fp, "\n");						    \
	  }								    \
      }									    \
    fprintf(fp, "};\n");						    \
  } while (0);


#define PRINT_UNNAMED_NUM_ARRAY_TO_C_FILE(fp, intf, tab, arrcount, val)	    \
  do {									    \
    int i_uarr, j_uarr;							    \
    fprintf(fp, tab"{\n");						    \
    j_uarr = 1;								    \
    for (i_uarr = 0; i_uarr < arrcount; i_uarr++)			    \
      {									    \
	fprintf(fp, intf, val[i_uarr]);					    \
	if (i_uarr < arrcount - 1)					    \
	  {								    \
	    fprintf(fp, ",");						    \
	  }								    \
	j_uarr++;							    \
	if (j_uarr > PRINT_TO_C_FILE_MAX_INT_LINE)			    \
	  {								    \
	    j_uarr = 1;							    \
	    fprintf(fp, "\n" tab);					    \
	  }								    \
      }									    \
    fprintf(fp, "\n" tab "}");						    \
  } while (0);


#define XML_COMMENT_START "<!-- "
#define XML_COMMENT_END " -->"

static COLL_DATA *dump_coll_data = NULL;

static char *cmp_token_name_array = NULL;
static char cmp_token_name_size = 0;

/*
 * LDML element definition (XML schema)
 * full name is the "path" - the names of all parent elements down to
 *			     the element
 */
XML_ELEMENT_DEF ldml_elem_ldml = { "ldml", 1, (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_element_ok), NULL
};

XML_ELEMENT_DEF ldml_elem_dates = { "ldml dates", 2, (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_element_ok), NULL
};

XML_ELEMENT_DEF ldml_elem_calendars = { "ldml dates calendars", 3, (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_element_ok), NULL
};

XML_ELEMENT_DEF ldml_elem_calendar = { "ldml dates calendars calendar", 4, (ELEM_START_FUNC) (&start_calendar),
  (ELEM_END_FUNC) (&end_element_ok), NULL
};

XML_ELEMENT_DEF ldml_elem_dateFormatCUBRID = { "ldml dates calendars calendar dateFormatCUBRID", 5,
  (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_dateFormatCUBRID), (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_timeFormatCUBRID = { "ldml dates calendars calendar timeFormatCUBRID", 5,
  (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_timeFormatCUBRID), (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_datetimeFormatCUBRID = { "ldml dates calendars calendar datetimeFormatCUBRID", 5,
  (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_datetimeFormatCUBRID), (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_timestampFormatCUBRID = { "ldml dates calendars calendar timestampFormatCUBRID", 5,
  (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_timestampFormatCUBRID),
  (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_timetzFormatCUBRID = { "ldml dates calendars calendar timetzFormatCUBRID", 5,
  (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_timetzFormatCUBRID),
  (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_datetimetzFormatCUBRID = { "ldml dates calendars calendar datetimetzFormatCUBRID", 5,
  (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_datetimetzFormatCUBRID),
  (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_timestamptzFormatCUBRID = { "ldml dates calendars calendar timestamptzFormatCUBRID", 5,
  (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_timestamptzFormatCUBRID),
  (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_months = { "ldml dates calendars calendar months", 5,
  (ELEM_START_FUNC) (&start_element_ok), (ELEM_END_FUNC) (&end_element_ok),
  NULL
};

XML_ELEMENT_DEF ldml_elem_monthContext = { "ldml dates calendars calendar months monthContext", 6,
  (ELEM_START_FUNC) (&start_calendar_name_context),
  (ELEM_END_FUNC) (&end_element_ok), NULL
};

XML_ELEMENT_DEF ldml_elem_monthWidth = { "ldml dates calendars calendar months monthContext monthWidth", 7,
  (ELEM_START_FUNC) (&start_month_day_Width),
  (ELEM_END_FUNC) (&end_month_day_Width), NULL
};

XML_ELEMENT_DEF ldml_elem_month = { "ldml dates calendars calendar months monthContext monthWidth month", 8,
  (ELEM_START_FUNC) (&start_month), (ELEM_END_FUNC) (&end_month),
  (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_days = { "ldml dates calendars calendar days", 5,
  (ELEM_START_FUNC) (&start_element_ok), (ELEM_END_FUNC) (&end_element_ok),
  NULL
};

XML_ELEMENT_DEF ldml_elem_dayContext = { "ldml dates calendars calendar days dayContext", 6,
  (ELEM_START_FUNC) (&start_calendar_name_context),
  (ELEM_END_FUNC) (&end_element_ok), NULL
};

XML_ELEMENT_DEF ldml_elem_dayWidth = { "ldml dates calendars calendar days dayContext dayWidth", 7,
  (ELEM_START_FUNC) (&start_month_day_Width),
  (ELEM_END_FUNC) (&end_month_day_Width), NULL
};

XML_ELEMENT_DEF ldml_elem_day = { "ldml dates calendars calendar days dayContext dayWidth day", 8,
  (ELEM_START_FUNC) (&start_day), (ELEM_END_FUNC) (&end_day),
  (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_dayPeriods = { "ldml dates calendars calendar dayPeriods", 5,
  (ELEM_START_FUNC) (&start_element_ok), (ELEM_END_FUNC) (&end_element_ok),
  NULL
};

XML_ELEMENT_DEF ldml_elem_dayPeriodContext = { "ldml dates calendars calendar dayPeriods dayPeriodContext", 6,
  (ELEM_START_FUNC) (&start_calendar_name_context),
  (ELEM_END_FUNC) (&end_element_ok), NULL
};

XML_ELEMENT_DEF ldml_elem_dayPeriodWidth = {
  "ldml dates calendars calendar dayPeriods dayPeriodContext dayPeriodWidth",
  7,
  (ELEM_START_FUNC) (&start_dayPeriodWidth),
  (ELEM_END_FUNC) (&end_element_ok),
  NULL
};

XML_ELEMENT_DEF ldml_elem_dayPeriod = {
  "ldml dates calendars calendar dayPeriods dayPeriodContext dayPeriodWidth dayPeriod",
  8,
  (ELEM_START_FUNC) (&start_dayPeriod), (ELEM_END_FUNC) (&end_dayPeriod),
  (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_numbers = { "ldml numbers", 2, (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_element_ok), NULL
};

XML_ELEMENT_DEF ldml_elem_numbers_symbols = { "ldml numbers symbols", 3, (ELEM_START_FUNC) (&start_numbers_symbols),
  (ELEM_END_FUNC) (&end_element_ok), NULL
};

XML_ELEMENT_DEF ldml_elem_symbol_decimal = { "ldml numbers symbols decimal", 4, (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_number_symbol), (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_symbol_group = { "ldml numbers symbols group", 4, (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_number_symbol), (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_currencies = { "ldml numbers currencies", 3, (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_element_ok), NULL
};

XML_ELEMENT_DEF ldml_elem_currency = { "ldml numbers currencies currency", 4,
  (ELEM_START_FUNC) (&start_currency),
  (ELEM_END_FUNC) (&end_element_ok), NULL
};

XML_ELEMENT_DEF ldml_elem_collations = { "ldml collations", 2, (ELEM_START_FUNC) (&start_collations),
  (ELEM_END_FUNC) (&end_element_ok), NULL
};

XML_ELEMENT_DEF ldml_elem_collation = { "ldml collations collation", 3, (ELEM_START_FUNC) (&start_one_collation),
  (ELEM_END_FUNC) (&end_one_collation), NULL
};

XML_ELEMENT_DEF ldml_elem_collation_rules = { "ldml collations collation rules", 4,
  (ELEM_START_FUNC) (&start_element_ok), (ELEM_END_FUNC) (&end_element_ok),
  NULL
};

XML_ELEMENT_DEF ldml_elem_collation_settings = { "ldml collations collation settings", 4,
  (ELEM_START_FUNC) (&start_collation_settings),
  (ELEM_END_FUNC) (&end_element_ok), NULL
};

XML_ELEMENT_DEF ldml_elem_collation_reset = { "ldml collations collation rules reset", 5,
  (ELEM_START_FUNC) (&start_collation_reset),
  (ELEM_END_FUNC) (&end_collation_reset), (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_collation_p = { "ldml collations collation rules p", 5,
  (ELEM_START_FUNC) (&start_collation_rule),
  (ELEM_END_FUNC) (&end_collation_rule),
  (ELEM_DATA_FUNC) (&handle_data_collation_rule)
};

XML_ELEMENT_DEF ldml_elem_collation_s = { "ldml collations collation rules s", 5,
  (ELEM_START_FUNC) (&start_collation_rule),
  (ELEM_END_FUNC) (&end_collation_rule),
  (ELEM_DATA_FUNC) (&handle_data_collation_rule)
};

XML_ELEMENT_DEF ldml_elem_collation_t = { "ldml collations collation rules t", 5,
  (ELEM_START_FUNC) (&start_collation_rule),
  (ELEM_END_FUNC) (&end_collation_rule),
  (ELEM_DATA_FUNC) (&handle_data_collation_rule)
};

XML_ELEMENT_DEF ldml_elem_collation_i = { "ldml collations collation rules i", 5,
  (ELEM_START_FUNC) (&start_collation_rule),
  (ELEM_END_FUNC) (&end_collation_rule),
  (ELEM_DATA_FUNC) (&handle_data_collation_rule)
};

XML_ELEMENT_DEF ldml_elem_collation_pc = { "ldml collations collation rules pc", 5,
  (ELEM_START_FUNC) (&start_collation_rule),
  (ELEM_END_FUNC) (&end_collation_rule),
  (ELEM_DATA_FUNC) (&handle_data_collation_rule)
};

XML_ELEMENT_DEF ldml_elem_collation_sc = { "ldml collations collation rules sc", 5,
  (ELEM_START_FUNC) (&start_collation_rule),
  (ELEM_END_FUNC) (&end_collation_rule),
  (ELEM_DATA_FUNC) (&handle_data_collation_rule)
};

XML_ELEMENT_DEF ldml_elem_collation_tc = { "ldml collations collation rules tc", 5,
  (ELEM_START_FUNC) (&start_collation_rule),
  (ELEM_END_FUNC) (&end_collation_rule),
  (ELEM_DATA_FUNC) (&handle_data_collation_rule)
};

XML_ELEMENT_DEF ldml_elem_collation_ic = { "ldml collations collation rules ic", 5,
  (ELEM_START_FUNC) (&start_collation_rule),
  (ELEM_END_FUNC) (&end_collation_rule),
  (ELEM_DATA_FUNC) (&handle_data_collation_rule)
};

XML_ELEMENT_DEF ldml_elem_collation_x = { "ldml collations collation rules x", 5,
  (ELEM_START_FUNC) (&start_collation_x), (ELEM_END_FUNC) (&end_collation_x),
  NULL
};

XML_ELEMENT_DEF ldml_elem_collation_x_p = { "ldml collations collation rules x p", 6,
  (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_collation_x_rule),
  (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_collation_x_s = { "ldml collations collation rules x s", 6,
  (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_collation_x_rule),
  (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_collation_x_t = { "ldml collations collation rules x t", 6,
  (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_collation_x_rule),
  (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_collation_x_i = { "ldml collations collation rules x i", 6,
  (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_collation_x_rule),
  (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_collation_x_extend = { "ldml collations collation rules x extend", 6,
  (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_collation_x_extend), (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_collation_x_context = { "ldml collations collation rules x context", 6,
  (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_collation_x_context), (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_collation_reset_first_variable = { "ldml collations collation rules reset first_variable", 6,
  (ELEM_START_FUNC) (&start_collation_logical_pos),
  (ELEM_END_FUNC) (&end_collation_logical_pos), NULL
};

XML_ELEMENT_DEF ldml_elem_collation_reset_last_variable = { "ldml collations collation rules reset last_variable", 6,
  (ELEM_START_FUNC) (&start_collation_logical_pos),
  (ELEM_END_FUNC) (&end_collation_logical_pos), NULL
};

XML_ELEMENT_DEF ldml_elem_collation_reset_first_primary_ignorable =
  { "ldml collations collation rules reset first_primary_ignorable", 6,
  (ELEM_START_FUNC) (&start_collation_logical_pos),
  (ELEM_END_FUNC) (&end_collation_logical_pos), NULL
};

XML_ELEMENT_DEF ldml_elem_collation_reset_last_primary_ignorable =
  { "ldml collations collation rules reset last_primary_ignorable", 6,
  (ELEM_START_FUNC) (&start_collation_logical_pos),
  (ELEM_END_FUNC) (&end_collation_logical_pos), NULL
};

XML_ELEMENT_DEF ldml_elem_collation_reset_first_secondary_ignorable =
  { "ldml collations collation rules reset first_secondary_ignorable", 6,
  (ELEM_START_FUNC) (&start_collation_logical_pos),
  (ELEM_END_FUNC) (&end_collation_logical_pos), NULL
};

XML_ELEMENT_DEF ldml_elem_collation_reset_last_secondary_ignorable =
  { "ldml collations collation rules reset last_secondary_ignorable", 6,
  (ELEM_START_FUNC) (&start_collation_logical_pos),
  (ELEM_END_FUNC) (&end_collation_logical_pos), NULL
};

XML_ELEMENT_DEF ldml_elem_collation_reset_first_tertiary_ignorable =
  { "ldml collations collation rules reset first_tertiary_ignorable", 6,
  (ELEM_START_FUNC) (&start_collation_logical_pos),
  (ELEM_END_FUNC) (&end_collation_logical_pos), NULL
};

XML_ELEMENT_DEF ldml_elem_collation_reset_last_tertiary_ignorable =
  { "ldml collations collation rules reset last_tertiary_ignorable", 6,
  (ELEM_START_FUNC) (&start_collation_logical_pos),
  (ELEM_END_FUNC) (&end_collation_logical_pos), NULL
};

XML_ELEMENT_DEF ldml_elem_collation_reset_first_non_ignorable =
  { "ldml collations collation rules reset first_non_ignorable", 6,
  (ELEM_START_FUNC) (&start_collation_logical_pos),
  (ELEM_END_FUNC) (&end_collation_logical_pos), NULL
};

XML_ELEMENT_DEF ldml_elem_collation_reset_last_non_ignorable =
  { "ldml collations collation rules reset last_non_ignorable", 6,
  (ELEM_START_FUNC) (&start_collation_logical_pos),
  (ELEM_END_FUNC) (&end_collation_logical_pos), NULL
};

XML_ELEMENT_DEF ldml_elem_collation_reset_first_trailing = { "ldml collations collation rules reset first_trailing", 6,
  (ELEM_START_FUNC) (&start_collation_logical_pos),
  (ELEM_END_FUNC) (&end_collation_logical_pos), NULL
};

XML_ELEMENT_DEF ldml_elem_collation_reset_last_trailing = { "ldml collations collation rules reset last_trailing", 6,
  (ELEM_START_FUNC) (&start_collation_logical_pos),
  (ELEM_END_FUNC) (&end_collation_logical_pos), NULL
};

XML_ELEMENT_DEF ldml_elem_collation_cubrid_rules = {
  "ldml collations collation cubridrules", 4,
  (ELEM_START_FUNC) (&start_element_ok), (ELEM_END_FUNC) (&end_element_ok),
  NULL
};

XML_ELEMENT_DEF ldml_elem_collation_cubrid_rules_set = { "ldml collations collation cubridrules set", 5,
  (ELEM_START_FUNC) (&start_collation_cubrid_rule),
  (ELEM_END_FUNC) (&end_collation_cubrid_rule_set), NULL
};

XML_ELEMENT_DEF ldml_elem_collation_cubrid_rules_set_ch = { "ldml collations collation cubridrules set ch", 6,
  (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_collation_cubrid_rule_set_cp_ch),
  (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_collation_cubrid_rules_set_sch = { "ldml collations collation cubridrules set sch", 6,
  (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_collation_cubrid_rule_set_cp_ch),
  (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_collation_cubrid_rules_set_ech = { "ldml collations collation cubridrules set ech", 6,
  (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_collation_cubrid_rule_set_ech_ecp),
  (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_collation_cubrid_rules_set_cp = { "ldml collations collation cubridrules set cp", 6,
  (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_collation_cubrid_rule_set_cp_ch),
  (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_collation_cubrid_rules_set_scp = { "ldml collations collation cubridrules set scp", 6,
  (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_collation_cubrid_rule_set_cp_ch),
  (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_collation_cubrid_rules_set_ecp = { "ldml collations collation cubridrules set ecp", 6,
  (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_collation_cubrid_rule_set_ech_ecp),
  (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_collation_cubrid_rules_set_w = { "ldml collations collation cubridrules set w", 6,
  (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_collation_cubrid_rule_set_w_wr),
  (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_collation_cubrid_rules_set_wr = { "ldml collations collation cubridrules set wr", 6,
  (ELEM_START_FUNC) (&start_collation_cubrid_rule_set_wr),
  (ELEM_END_FUNC) (&end_collation_cubrid_rule_set_w_wr),
  (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_alphabets = { "ldml alphabets", 2, (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_element_ok), NULL
};

XML_ELEMENT_DEF ldml_elem_include_collation = { "ldml collations include", 3,
  (ELEM_START_FUNC) (&start_include_collation),
  (ELEM_END_FUNC) (&end_element_ok),
  NULL
};

XML_ELEMENT_DEF ldml_elem_alphabet = { "ldml alphabets alphabet", 3, (ELEM_START_FUNC) (&start_one_alphabet),
  (ELEM_END_FUNC) (&end_element_ok), NULL
};

XML_ELEMENT_DEF ldml_elem_alphabet_upper = { "ldml alphabets alphabet u", 4,
  (ELEM_START_FUNC) (&start_upper_case_rule),
  (ELEM_END_FUNC) (&end_case_rule), NULL
};

XML_ELEMENT_DEF ldml_elem_alphabet_upper_src =
  { "ldml alphabets alphabet u s", 5, (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_transform_buffer), (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_alphabet_upper_dest =
  { "ldml alphabets alphabet u d", 5, (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_transform_buffer), (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_alphabet_lower = { "ldml alphabets alphabet l", 4,
  (ELEM_START_FUNC) (&start_lower_case_rule),
  (ELEM_END_FUNC) (&end_case_rule), NULL
};

XML_ELEMENT_DEF ldml_elem_alphabet_lower_src =
  { "ldml alphabets alphabet l s", 5, (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_transform_buffer), (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_alphabet_lower_dest =
  { "ldml alphabets alphabet l d", 5, (ELEM_START_FUNC) (&start_element_ok),
  (ELEM_END_FUNC) (&end_transform_buffer), (ELEM_DATA_FUNC) (&handle_data)
};

XML_ELEMENT_DEF ldml_elem_unicodefile = { "ldml unicodefile", 2, (ELEM_START_FUNC) (&start_unicode_file),
  (ELEM_END_FUNC) (&end_element_ok), NULL
};

XML_ELEMENT_DEF ldml_elem_consoleconversion =
  { "ldml consoleconversion", 2, (ELEM_START_FUNC) (&start_consoleconversion),
  (ELEM_END_FUNC) (&end_element_ok), NULL
};

/*
 * LDML elements list - KEEP the order in this list !
 * it is mandatory to put in schema all parent elements, before adding a new
 * element */
XML_ELEMENT_DEF *ldml_elements[] = {
  &ldml_elem_ldml,
  &ldml_elem_dates,
  &ldml_elem_calendars,
  &ldml_elem_calendar,

  &ldml_elem_dateFormatCUBRID,
  &ldml_timeFormatCUBRID,
  &ldml_datetimeFormatCUBRID,
  &ldml_timestampFormatCUBRID,

  &ldml_elem_months,
  &ldml_elem_monthContext,
  &ldml_elem_monthWidth,
  &ldml_elem_month,

  &ldml_elem_days,
  &ldml_elem_dayContext,
  &ldml_elem_dayWidth,
  &ldml_elem_day,

  &ldml_elem_dayPeriods,
  &ldml_elem_dayPeriodContext,
  &ldml_elem_dayPeriodWidth,
  &ldml_elem_dayPeriod,

  &ldml_elem_numbers,
  &ldml_elem_numbers_symbols,
  &ldml_elem_symbol_decimal,
  &ldml_elem_symbol_group,
  &ldml_elem_currencies,
  &ldml_elem_currency,

  &ldml_elem_collations,
  &ldml_elem_include_collation,
  &ldml_elem_collation,
  &ldml_elem_collation_rules,
  &ldml_elem_collation_settings,
  &ldml_elem_collation_reset,
  &ldml_elem_collation_p,
  &ldml_elem_collation_s,
  &ldml_elem_collation_t,
  &ldml_elem_collation_i,
  &ldml_elem_collation_pc,
  &ldml_elem_collation_sc,
  &ldml_elem_collation_tc,
  &ldml_elem_collation_ic,

  &ldml_elem_collation_x,
  &ldml_elem_collation_x_p,
  &ldml_elem_collation_x_s,
  &ldml_elem_collation_x_t,
  &ldml_elem_collation_x_i,
  &ldml_elem_collation_x_extend,
  &ldml_elem_collation_x_context,

  &ldml_elem_collation_reset_first_variable,
  &ldml_elem_collation_reset_last_variable,
  &ldml_elem_collation_reset_first_primary_ignorable,
  &ldml_elem_collation_reset_last_primary_ignorable,
  &ldml_elem_collation_reset_first_secondary_ignorable,
  &ldml_elem_collation_reset_last_secondary_ignorable,
  &ldml_elem_collation_reset_first_tertiary_ignorable,
  &ldml_elem_collation_reset_last_tertiary_ignorable,
  &ldml_elem_collation_reset_first_non_ignorable,
  &ldml_elem_collation_reset_last_non_ignorable,
  &ldml_elem_collation_reset_first_trailing,
  &ldml_elem_collation_reset_last_trailing,

  &ldml_elem_collation_cubrid_rules,
  &ldml_elem_collation_cubrid_rules_set,
  &ldml_elem_collation_cubrid_rules_set_ch,
  &ldml_elem_collation_cubrid_rules_set_sch,
  &ldml_elem_collation_cubrid_rules_set_ech,
  &ldml_elem_collation_cubrid_rules_set_cp,
  &ldml_elem_collation_cubrid_rules_set_scp,
  &ldml_elem_collation_cubrid_rules_set_ecp,
  &ldml_elem_collation_cubrid_rules_set_w,
  &ldml_elem_collation_cubrid_rules_set_wr,

  &ldml_elem_alphabets,
  &ldml_elem_alphabet,
  &ldml_elem_alphabet_upper,
  &ldml_elem_alphabet_upper_src,
  &ldml_elem_alphabet_upper_dest,
  &ldml_elem_alphabet_lower,
  &ldml_elem_alphabet_lower_src,
  &ldml_elem_alphabet_lower_dest,

  &ldml_elem_unicodefile,

  &ldml_elem_consoleconversion,

  &ldml_timetzFormatCUBRID,
  &ldml_datetimetzFormatCUBRID,
  &ldml_timestamptzFormatCUBRID,
};

/*
 * start_element_ok() - Dummy XML element start function
 *			Used just for verbose purpose.
 *
 * return: always 0 (validation OK)
 * data: user data
 * attr: attribute/value pair array
 *
 *  Note : this function is registered by XML elements that normally do not
 *	   require any validation for element start, but is used only for
 *	   debug purpose, in order to properly display each parsed XML element
 */
static int
start_element_ok (void *data, const char **attr)
{
  PRINT_DEBUG_START (data, attr, "", 0);
  return 0;
}

/*
 * end_element_ok() - Dummy XML element end function
 *		      Used just for verbose purpose.
 *
 *
 * return: always 0 (OK)
 * data: user data
 * el_name: element name
 *
 *  Note : this function is registered by XML elements that normally do not
 *	   require any action for element end, but is used only for
 *	   debug purpose, in order to properly display each parsed XML element
 */
static int
end_element_ok (void *data, const char *el_name)
{
  PRINT_DEBUG_END (data, "", 0);
  return 0;
}

/*
 * start_calendar() - XML element start function
 * "ldml dates calendars calendar"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_calendar (void *data, const char **attr)
{
  if (xml_check_att_value (attr, "type", "gregorian") != 0)
    {
      PRINT_DEBUG_START (data, attr, "", 1);
      return 1;
    }

  PRINT_DEBUG_START (data, attr, "", 0);
  return 0;
}

/*
 * end_dateFormatCUBRID() - XML element end function
 * "ldml dates calendars calendar dateFormatCUBRID"
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_dateFormatCUBRID (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  /* copy data buffer to locale */
  assert (ld->data_buf_count < (int) sizeof (ld->dateFormat));

  if (ld->data_buf_count < (int) sizeof (ld->dateFormat))
    {
      strcpy (ld->dateFormat, ld->data_buffer);
    }
  else
    {
      PRINT_DEBUG_END (data, "Too much data", -1);
      return -1;
    }

  clear_data_buffer ((XML_PARSER_DATA *) data);

  PRINT_DEBUG_END (data, "", 0);

  return 0;
}

/*
 * end_timeFormatCUBRID() - XML element end function
 * "ldml dates calendars calendar timeFormatCUBRID"
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_timeFormatCUBRID (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  /* copy data buffer to locale */
  assert (ld->data_buf_count < (int) sizeof (ld->timeFormat));

  if (ld->data_buf_count < (int) sizeof (ld->timeFormat))
    {
      strcpy (ld->timeFormat, ld->data_buffer);
    }
  else
    {
      PRINT_DEBUG_END (data, "Too much data", -1);
      return -1;
    }

  clear_data_buffer ((XML_PARSER_DATA *) data);

  PRINT_DEBUG_END (data, "", 0);

  return 0;
}

/*
 * end_datetimeFormatCUBRID() - XML element end function
 * "ldml dates calendars calendar datetimeFormatCUBRID"
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_datetimeFormatCUBRID (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  /* copy data buffer to locale */
  assert (ld->data_buf_count < (int) sizeof (ld->datetimeFormat));

  if (ld->data_buf_count < (int) sizeof (ld->datetimeFormat))
    {
      strcpy (ld->datetimeFormat, ld->data_buffer);
    }
  else
    {
      PRINT_DEBUG_END (data, "Too much data", -1);
      return -1;
    }

  clear_data_buffer ((XML_PARSER_DATA *) data);

  PRINT_DEBUG_END (data, "", 0);

  return 0;
}

/*
 * end_timestampFormatCUBRID() - XML element end function
 * "ldml dates calendars calendar timestampFormatCUBRID"
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_timestampFormatCUBRID (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  /* copy data buffer to locale */
  assert (ld->data_buf_count < (int) sizeof (ld->timestampFormat));

  if (ld->data_buf_count < (int) sizeof (ld->timestampFormat))
    {
      strcpy (ld->timestampFormat, ld->data_buffer);
    }
  else
    {
      PRINT_DEBUG_END (data, "Too much data", -1);
      return -1;
    }

  clear_data_buffer ((XML_PARSER_DATA *) data);

  PRINT_DEBUG_END (data, "", 0);

  return 0;
}

/*
 * end_timetzFormatCUBRID() - XML element end function
 * "ldml dates calendars calendar timetzFormatCUBRID"
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_timetzFormatCUBRID (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  /* copy data buffer to locale */
  assert (ld->data_buf_count < (int) sizeof (ld->timetzFormat));

  if (ld->data_buf_count < (int) sizeof (ld->timetzFormat))
    {
      strcpy (ld->timetzFormat, ld->data_buffer);
    }
  else
    {
      PRINT_DEBUG_END (data, "Too much data", -1);
      return -1;
    }

  clear_data_buffer ((XML_PARSER_DATA *) data);

  PRINT_DEBUG_END (data, "", 0);

  return 0;
}

/*
 * end_datetimetzFormatCUBRID() - XML element end function
 * "ldml dates calendars calendar datetimetzFormatCUBRID"
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_datetimetzFormatCUBRID (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  /* copy data buffer to locale */
  assert (ld->data_buf_count < (int) sizeof (ld->datetimetzFormat));

  if (ld->data_buf_count < (int) sizeof (ld->datetimetzFormat))
    {
      strcpy (ld->datetimetzFormat, ld->data_buffer);
    }
  else
    {
      PRINT_DEBUG_END (data, "Too much data", -1);
      return -1;
    }

  clear_data_buffer ((XML_PARSER_DATA *) data);

  PRINT_DEBUG_END (data, "", 0);

  return 0;
}

/*
 * end_timestamptzFormatCUBRID() - XML element end function
 * "ldml dates calendars calendar timestamptzFormatCUBRID"
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_timestamptzFormatCUBRID (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  /* copy data buffer to locale */
  assert (ld->data_buf_count < (int) sizeof (ld->timestamptzFormat));

  if (ld->data_buf_count < (int) sizeof (ld->timestamptzFormat))
    {
      strcpy (ld->timestamptzFormat, ld->data_buffer);
    }
  else
    {
      PRINT_DEBUG_END (data, "Too much data", -1);
      return -1;
    }

  clear_data_buffer ((XML_PARSER_DATA *) data);

  PRINT_DEBUG_END (data, "", 0);

  return 0;
}

/*
 * start_calendar_name_context() - XML element start function
 * "ldml dates calendars calendar months monthContext"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_calendar_name_context (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  if (xml_check_att_value (attr, "type", "format") == 0)
    {
      PRINT_DEBUG_START (data, attr, "", 0);
      return 0;
    }

  PRINT_DEBUG_START (data, attr, "", 1);
  return 1;
}

/*
 * start_month_day_Width() - XML element start function
 * "ldml dates calendars calendar months monthContext monthWidth"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_month_day_Width (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  assert (ld->name_type == 0);

  if (xml_check_att_value (attr, "type", "abbreviated") == 0)
    {
      ld->name_type = 1;
      PRINT_DEBUG_START (data, attr, "", 0);
      return 0;
    }
  else if (xml_check_att_value (attr, "type", "wide") == 0)
    {
      ld->name_type = 2;
      PRINT_DEBUG_START (data, attr, "", 0);
      return 0;
    }

  PRINT_DEBUG_START (data, attr, "", 1);
  return 1;
}

/*
 * end_month_day_Width() - XML element end function
 * "ldml dates calendars calendar months monthContext monthWidth"
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_month_day_Width (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  assert (ld->name_type != 0);

  ld->name_type = 0;

  PRINT_DEBUG_END (data, "", 0);
  return 0;
}

/*
 * start_month() - XML element start function
 * "ldml dates calendars calendar months monthContext monthWidth month"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_month (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  char *month_count = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  if (xml_get_att_value (attr, "type", &month_count) == 0)
    {
      assert (month_count != NULL);
      ld->curr_period = atoi (month_count);

      assert (ld->curr_period > 0 && ld->curr_period <= CAL_MONTH_COUNT);

      if (ld->curr_period > 0 && ld->curr_period <= CAL_MONTH_COUNT)
	{
	  ld->curr_period = ld->curr_period - 1;
	}
      else
	{
	  PRINT_DEBUG_START (data, attr, "Invalid month", -1);
	  return -1;
	}

      PRINT_DEBUG_START (data, attr, "", 0);
      return 0;
    }

  PRINT_DEBUG_START (data, attr, "", 1);
  return 1;
}

/*
 * end_month() - XML element end function
 * "ldml dates calendars calendar months monthContext monthWidth month"
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_month (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  /* copy data buffer to locale */
  assert (ld->curr_period >= 0 && ld->curr_period < CAL_MONTH_COUNT);

  assert (ld->name_type == 2 || ld->name_type == 1);

  if (ld->name_type == 1)
    {
      assert (ld->data_buf_count < LOC_DATA_MONTH_ABBR_SIZE);

      if (ld->data_buf_count < LOC_DATA_MONTH_ABBR_SIZE)
	{
	  strcpy (ld->month_names_abbreviated[ld->curr_period], ld->data_buffer);
	}
      else
	{
	  PRINT_DEBUG_END (data, "Too much data", -1);
	  return -1;
	}
    }
  else if (ld->name_type == 2)
    {
      assert (ld->data_buf_count < LOC_DATA_MONTH_WIDE_SIZE);
      if (ld->data_buf_count < LOC_DATA_MONTH_WIDE_SIZE)
	{
	  strcpy (ld->month_names_wide[ld->curr_period], ld->data_buffer);
	}
      else
	{
	  PRINT_DEBUG_END (data, "Too much data", -1);
	  return -1;
	}
    }

  ld->curr_period = -1;

  clear_data_buffer ((XML_PARSER_DATA *) data);

  PRINT_DEBUG_END (data, "", 0);
  return 0;
}

/*
 * start_day() - XML element start function
 * "ldml dates calendars calendar days dayContext dayWidth day"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_day (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  char *day_ref_name = NULL;
  int i;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  if (xml_get_att_value (attr, "type", &day_ref_name) != 0)
    {
      PRINT_DEBUG_START (data, attr, "", 1);
      return 1;
    }

  assert (day_ref_name != NULL);

  for (i = 0; i < 7; i++)
    {
      if (strcmp (ldml_ref_day_names[i], day_ref_name) == 0)
	{
	  break;
	}
    }

  assert (i >= 0 && i < CAL_DAY_COUNT);

  if (i >= 0 && i < CAL_DAY_COUNT)
    {
      ld->curr_period = i;
    }
  else
    {
      PRINT_DEBUG_START (data, attr, "Invalid day", -1);
      return -1;
    }

  PRINT_DEBUG_START (data, attr, "", 0);
  return 0;
}

/*
 * end_day() - XML element end function
 * "ldml dates calendars calendar days dayContext dayWidth day"
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_day (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  /* copy data buffer to locale */
  assert (ld->curr_period >= 0 && ld->curr_period < CAL_DAY_COUNT);

  assert (ld->name_type == 2 || ld->name_type == 1);

  if (ld->name_type == 1)
    {
      assert (ld->data_buf_count < LOC_DATA_DAY_ABBR_SIZE);
      if (ld->data_buf_count < LOC_DATA_DAY_ABBR_SIZE)
	{
	  strcpy (ld->day_names_abbreviated[ld->curr_period], ld->data_buffer);
	}
      else
	{
	  PRINT_DEBUG_END (data, "Too much data", -1);
	  return -1;
	}
    }
  else if (ld->name_type == 2)
    {
      assert (ld->data_buf_count < LOC_DATA_DAY_WIDE_SIZE);
      if (ld->data_buf_count < LOC_DATA_DAY_WIDE_SIZE)
	{
	  strcpy (ld->day_names_wide[ld->curr_period], ld->data_buffer);
	}
      else
	{
	  PRINT_DEBUG_END (data, "Too much data", -1);
	  return -1;
	}
    }

  ld->curr_period = -1;

  clear_data_buffer ((XML_PARSER_DATA *) data);

  PRINT_DEBUG_END (data, "", 0);
  return 0;
}

/*
 * start_dayPeriodWidth() - XML element start function
 * "ldml dates calendars calendar dayPeriods dayPeriodContext dayPeriodWidth"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_dayPeriodWidth (void *data, const char **attr)
{
  if (xml_check_att_value (attr, "type", "wide") == 0)
    {
      PRINT_DEBUG_START (data, attr, "", 0);
      return 0;
    }

  PRINT_DEBUG_START (data, attr, "", 1);
  return 1;
}

/*
 * start_dayPeriod() - XML element start function
 * "ldml dates calendars calendar dayPeriods dayPeriodContext dayPeriodWidth dayPeriod"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_dayPeriod (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  char *am_pm_ref_name = NULL;
  int i;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  if (xml_get_att_value (attr, "type", &am_pm_ref_name) != 0)
    {
      PRINT_DEBUG_START (data, attr, "", 1);
      return 1;
    }

  assert (am_pm_ref_name != NULL);

  for (i = 0; i < CAL_AM_PM_COUNT; i++)
    {
      if (strcmp (ldml_ref_am_pm_names[i], am_pm_ref_name) == 0)
	{
	  break;
	}
    }

  assert (i >= 0 && i < CAL_AM_PM_COUNT);

  if (i >= 0 && i < CAL_AM_PM_COUNT)
    {
      ld->curr_period = i;
    }
  else
    {
      PRINT_DEBUG_START (data, attr, "Invalid dayPeriod", -1);
      return -1;
    }

  PRINT_DEBUG_START (data, attr, "", 0);
  return 0;
}

/*
 * end_dayPeriod() - XML element end function
 * "ldml dates calendars calendar dayPeriods dayPeriodContext dayPeriodWidth dayPeriod"
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_dayPeriod (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  /* copy data buffer to locale */
  assert (ld->curr_period >= 0 && ld->curr_period < CAL_AM_PM_COUNT);

  assert (ld->data_buf_count < LOC_DATA_AM_PM_SIZE);
  if (ld->data_buf_count < LOC_DATA_AM_PM_SIZE)
    {
      strcpy (ld->am_pm[ld->curr_period], ld->data_buffer);
    }
  else
    {
      PRINT_DEBUG_END (data, "Too much data", -1);
      return -1;
    }

  ld->curr_period = -1;

  clear_data_buffer ((XML_PARSER_DATA *) data);

  PRINT_DEBUG_END (data, "", 0);
  return 0;
}

/*
 * start_numbers_symbols() - XML element start function
 * "ldml numbers symbols"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_numbers_symbols (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  if (xml_check_att_value (attr, "numberSystem", "latn") != 0)
    {
      PRINT_DEBUG_START (data, attr, "", 1);
      return 1;
    }

  PRINT_DEBUG_START (data, attr, "", 0);
  return 0;
}

/*
 * end_number_symbol() - XML element end function
 * "ldml numbers symbols decimal"
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_number_symbol (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  /* number symbol is exactly one character (ASCII compatible) */
  if (ld->data_buf_count == 1 && (*(ld->data_buffer) >= ' ' && (unsigned char) *(ld->data_buffer) < 0x80))
    {
      if (strcmp (el_name, "decimal") == 0)
	{
	  ld->number_decimal_sym = *(ld->data_buffer);
	}
      else
	{
	  ld->number_group_sym = *(ld->data_buffer);
	}
    }
  else
    {
      PRINT_DEBUG_END (data, "Invalid numeric symbol", -1);
      return -1;
    }

  clear_data_buffer ((XML_PARSER_DATA *) data);

  PRINT_DEBUG_END (data, "", 0);
  return 0;
}

/*
 * start_currency() - XML element start function
 * "ldml numbers currencies currency"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_currency (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  char *att_val = NULL;
  int currency_size = 0;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  /* all setting are optional */
  if (xml_get_att_value (attr, "type", &att_val) == 0)
    {
      /* Type attribute found, store it. */
      if (strlen (att_val) != LOC_DATA_CURRENCY_ISO_CODE_LEN)
	{
	  PRINT_DEBUG_START (data, attr, "Currency ISO code invalid, " "it must be 3 chars long.", -1);
	  return -1;
	}
      if (ld->default_currency_code != DB_CURRENCY_NULL)
	{
	  PRINT_DEBUG_START (data, attr, "Currency ISO code already set.", -1);
	  return -1;
	}
      if (!intl_is_currency_symbol (att_val, &(ld->default_currency_code), &currency_size, CURRENCY_CHECK_MODE_ISO))
	{
	  PRINT_DEBUG_START (data, attr, "Currency ISO code not supported", -1);
	  return -1;
	}
    }
  else
    {
      /* If <currency> does not have a type attribute, parsing fails. */
      PRINT_DEBUG_START (data, attr, "Currency tag does not have an ISO code type " "attribute.", -1);
      return -1;
    }

  PRINT_DEBUG_START (data, attr, "", 0);
  return 0;
}


/* LDML collation */
/*
 * start_collations() - XML element start function
 * "ldml collations"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_collations (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  char *valid_locales = NULL;
  int check_loc_len;
  char *t_start;
  char *t_end;
  char *att_val_end;
  bool locale_found = false;
  bool search_end = false;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  if (xml_get_att_value (attr, "validSubLocales", &valid_locales) != 0)
    {
      PRINT_DEBUG_START (data, attr, "", 1);
      return 1;
    }

  assert (ld->locale_name != NULL);

  check_loc_len = strlen (ld->locale_name);
  att_val_end = valid_locales + strlen (valid_locales);

  /* separate the locales_name into tokens */
  t_start = t_end = valid_locales;

  do
    {
      if (*t_end == '\0' || char_isspace ((int) *t_end))
	{
	  /* token ended, check locale name */
	  if ((t_end - t_start == 3 && memcmp (t_start, "all", 3) == 0) || (t_end - t_start == 1 && *t_start == '*')
	      || (t_end - t_start == check_loc_len && memcmp (ld->locale_name, t_start, t_end - t_start) == 0))
	    {
	      locale_found = true;
	      break;
	    }

	  if (*t_end == '\0')
	    {
	      search_end = true;
	    }

	  t_end++;
	  t_start = t_end;
	}
      else
	{
	  t_end++;
	}
    }
  while (!search_end);

  if (!locale_found)
    {
      PRINT_DEBUG_START (data, attr, "", 1);
      return 1;
    }

  PRINT_DEBUG_START (data, attr, "", 0);
  return 0;
}

/*
 * start_one_collation() - XML element start function
 * "ldml collations collation"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_one_collation (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  LOCALE_COLLATION *locale_coll = NULL;
  char *att_val = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  locale_coll = new_locale_collation (ld);
  if (locale_coll == NULL)
    {
      pd->xml_error = XML_CUB_OUT_OF_MEMORY;
      PRINT_DEBUG_START (data, attr, "memory allocation failed", -1);
      return -1;
    }

  if (xml_get_att_value (attr, "type", &att_val) != 0)
    {
      PRINT_DEBUG_START (data, attr, "", 1);
      return 1;
    }

  assert (att_val != NULL);

  strncpy (locale_coll->tail_coll.coll_name, att_val, COLL_NAME_SIZE - 1);
  locale_coll->tail_coll.coll_name[COLL_NAME_SIZE - 1] = '\0';

  /* initialize default value for settings */
  locale_coll->tail_coll.uca_opt.sett_strength = TAILOR_QUATERNARY;
  locale_coll->tail_coll.uca_opt.sett_caseFirst = 1;

  locale_coll->tail_coll.ldml_context.ldml_file = strdup (ld->ldml_context.ldml_file);
  locale_coll->tail_coll.ldml_context.line_no = XML_GetCurrentLineNumber (pd->xml_parser);

  PRINT_DEBUG_START (data, attr, "", 0);
  return 0;
}

/*
 * end_one_collation() - XML element end function
 * "ldml collations collation"
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_one_collation (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  ld->coll_cnt++;

  PRINT_DEBUG_END (data, "", 0);
  return 0;
}

/*
 * start_collation_settings() - XML element start function
 * "ldml collations collation settings"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_collation_settings (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  char *att_val = NULL;
  char err_msg[ERR_MSG_SIZE + PATH_MAX];
  COLL_TAILORING *curr_coll_tail = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);
  curr_coll_tail = LOC_CURRENT_COLL_TAIL (ld);

  att_val = NULL;
  if (xml_get_att_value (attr, "id", &att_val) == 0)
    {
      assert (att_val != NULL);
      curr_coll_tail->coll_id = atoi (att_val);

      if (curr_coll_tail->coll_id < LANG_MAX_BUILTIN_COLLATIONS || curr_coll_tail->coll_id >= LANG_MAX_COLLATIONS)
	{
	  snprintf (err_msg, sizeof (err_msg) - 1,
		    "File %s, line: %lu : " "Invalid collation numeric identifier : %d"
		    " for collation '%s'. Valid range is from %d to %d", ld->ldml_context.ldml_file,
		    XML_GetCurrentLineNumber (pd->xml_parser), curr_coll_tail->coll_id, curr_coll_tail->coll_name,
		    LANG_MAX_BUILTIN_COLLATIONS, LANG_MAX_COLLATIONS - 1);
	  LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
	  return -1;
	}
    }

  att_val = NULL;
  /* all setting are optional */
  if (xml_get_att_value (attr, "strength", &att_val) == 0)
    {
      T_LEVEL coll_strength = TAILOR_QUATERNARY;
      if (strcasecmp (att_val, "primary") == 0 || strcmp (att_val, "1") == 0)
	{
	  coll_strength = TAILOR_PRIMARY;
	}
      else if ((strcasecmp (att_val, "secondary") == 0) || (strcmp (att_val, "2") == 0))
	{
	  coll_strength = TAILOR_SECONDARY;
	}
      else if ((strcasecmp (att_val, "tertiary") == 0) || (strcmp (att_val, "3") == 0))
	{
	  coll_strength = TAILOR_TERTIARY;
	}
      else if ((strcasecmp (att_val, "quaternary") == 0) || (strcmp (att_val, "4") == 0))
	{
	  coll_strength = TAILOR_QUATERNARY;
	}
      else if ((strcasecmp (att_val, "identical") == 0) || (strcmp (att_val, "5") == 0))
	{
	  coll_strength = TAILOR_IDENTITY;
	}
      else
	{
	  snprintf (err_msg, sizeof (err_msg) - 1,
		    "File %s, line: %lu : " "Invalid collation strength : '%s'" " for collation '%s'",
		    ld->ldml_context.ldml_file, XML_GetCurrentLineNumber (pd->xml_parser), att_val,
		    curr_coll_tail->coll_name);
	  LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
	  return -1;
	}

      curr_coll_tail->uca_opt.sett_strength = coll_strength;
    }

  att_val = NULL;

  if (xml_get_att_value (attr, "backwards", &att_val) == 0)
    {
      if (strcasecmp (att_val, "on") == 0)
	{
	  curr_coll_tail->uca_opt.sett_backwards = true;
	}
    }

  att_val = NULL;

  if (xml_get_att_value (attr, "caseLevel", &att_val) == 0)
    {
      if (strcasecmp (att_val, "on") == 0)
	{
	  curr_coll_tail->uca_opt.sett_caseLevel = true;
	}
    }

  att_val = NULL;

  if (xml_get_att_value (attr, "caseFirst", &att_val) == 0)
    {
      if (strcasecmp (att_val, "off") == 0)
	{
	  curr_coll_tail->uca_opt.sett_caseFirst = 0;
	}
      else if (strcasecmp (att_val, "upper") == 0)
	{
	  curr_coll_tail->uca_opt.sett_caseFirst = 1;
	}
      else if (strcasecmp (att_val, "lower") == 0)
	{
	  curr_coll_tail->uca_opt.sett_caseFirst = 2;
	}
    }

  /* CUBRID specific collation settings */
  att_val = NULL;
  if (xml_get_att_value (attr, "CUBRIDMaxWeights", &att_val) == 0)
    {
      assert (att_val != NULL);
      curr_coll_tail->sett_max_cp = atoi (att_val);
    }

  att_val = NULL;
  if (xml_get_att_value (attr, "DUCETContractions", &att_val) == 0)
    {
      assert (att_val != NULL);
      if (strcasecmp (att_val, "use") == 0)
	{
	  curr_coll_tail->uca_opt.sett_contr_policy |= CONTR_DUCET_USE;
	}
    }

  att_val = NULL;
  if (xml_get_att_value (attr, "TailoringContractions", &att_val) == 0)
    {
      assert (att_val != NULL);
      if (strcasecmp (att_val, "use") == 0)
	{
	  curr_coll_tail->uca_opt.sett_contr_policy |= CONTR_TAILORING_USE;
	}
    }

  att_val = NULL;
  if (xml_get_att_value (attr, "CUBRIDExpansions", &att_val) == 0)
    {
      assert (att_val != NULL);
      if (strcasecmp (att_val, "use") == 0)
	{
	  curr_coll_tail->uca_opt.sett_expansions = true;
	}
    }

  att_val = NULL;
  if (xml_get_att_value (attr, "MatchContractionBoundary", &att_val) == 0)
    {
      assert (att_val != NULL);
      if (strcasecmp (att_val, "true") == 0)
	{
	  curr_coll_tail->uca_opt.sett_match_contr = MATCH_CONTR_BOUND_ALLOW;
	}
    }

  PRINT_DEBUG_START (data, attr, "", 0);
  return 0;
}

/*
 * start_collation_reset() - XML element start function
 * "ldml collations collation rules reset"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_collation_reset (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  char *att_val = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  *(ld->last_anchor_buf) = '\0';

  ld->last_rule_dir = TAILOR_AFTER;
  ld->last_rule_pos_type = RULE_POS_BUFFER;
  ld->last_rule_level = TAILOR_UNDEFINED;

  if (xml_get_att_value (attr, "before", &att_val) == 0)
    {
      if (strcmp (att_val, "primary") == 0)
	{
	  ld->last_rule_level = TAILOR_PRIMARY;
	}
      else if (strcmp (att_val, "secondary") == 0)
	{
	  ld->last_rule_level = TAILOR_SECONDARY;
	}
      else if (strcmp (att_val, "tertiary") == 0)
	{
	  ld->last_rule_level = TAILOR_TERTIARY;
	}
      else if (strcmp (att_val, "quaternary") == 0)
	{
	  ld->last_rule_level = TAILOR_QUATERNARY;
	}
      else
	{
	  assert (false);
	  PRINT_DEBUG_START (data, attr, "Invalid value for before", -1);
	  return -1;
	}

      ld->last_rule_dir = TAILOR_BEFORE;
    }

  ld->last_r_buf_p = NULL;
  ld->last_r_buf_size = 0;

  PRINT_DEBUG_START (data, attr, "", 0);
  return 0;
}

/*
 * end_collation_reset() - XML element end function
 * "ldml collations collation rules reset"
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_collation_reset (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  assert (ld->data_buf_count < LOC_DATA_COLL_TWO_CHARS);

  if (ld->data_buf_count < LOC_DATA_COLL_TWO_CHARS)
    {
      strcpy (ld->last_anchor_buf, ld->data_buffer);
      clear_data_buffer ((XML_PARSER_DATA *) data);
      PRINT_DEBUG_END (data, "", 0);
      return 0;
    }

  clear_data_buffer ((XML_PARSER_DATA *) data);

  PRINT_DEBUG_END (data, "Too much data", -1);
  return -1;
}

/*
 * start_collation_rule() - XML element start function
 * "ldml collations collation rules p"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_collation_rule (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  TAILOR_RULE *t_rule = NULL;
  int status;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  t_rule = new_collation_rule (ld);

  if (t_rule == NULL)
    {
      pd->xml_error = XML_CUB_OUT_OF_MEMORY;
      PRINT_DEBUG_START (data, attr, "memory allocation failed", -1);
      return -1;
    }

  if (ld->last_rule_pos_type != RULE_POS_BUFFER)
    {
      /* nothing do to for logical position reference */
      assert (t_rule->r_buf == NULL);
      assert (t_rule->r_buf_size == 0);

      PRINT_DEBUG_START (data, attr, "", 0);
      return 0;
    }

  status = common_collation_start_rule (data, attr, ld, t_rule);
  if (status != 0)
    {
      return status;
    }

  PRINT_DEBUG_START (data, attr, "", 0);
  return 0;
}

/*
 * start_collation_cubrid_rule() - XML element start function
 * "ldml collations collation cubridrules set"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_collation_cubrid_rule (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  CUBRID_TAILOR_RULE *ct_rule = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  ct_rule = new_collation_cubrid_rule (ld);

  if (ct_rule == NULL)
    {
      pd->xml_error = XML_CUB_OUT_OF_MEMORY;
      PRINT_DEBUG_START (data, attr, "OUT OF MEMORY", -1);
      return -1;
    }

  PRINT_DEBUG_START (data, attr, "", 0);
  clear_data_buffer (pd);
  return 0;
}

/*
 * start_collation_cubrid_rule_set_wr - XML element start function
 * "ldml collations collation cubridrules set wr"
 *
 * return: NO_ERROR if it's OK to enter this element,
 *         ER_LOC_GEN if STEP attribute is a string exceeding the max size.
 * data(in/out): user data
 * attr(in): attribute/value pair array
 */
static int
start_collation_cubrid_rule_set_wr (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  char *att_val = NULL;
  CUBRID_TAILOR_RULE *ct_rule;
  COLL_TAILORING *curr_coll_tail = NULL;
  int rule_id;
  int err_status = NO_ERROR;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  curr_coll_tail = LOC_CURRENT_COLL_TAIL (ld);

  rule_id = curr_coll_tail->cub_count_rules;
  ct_rule = &(curr_coll_tail->cub_rules[rule_id]);

  if (xml_get_att_value (attr, "step", &att_val) == 0)
    {
      if (strlen (att_val) > MAX_STRLEN_FOR_COLLATION_ELEMENT)
	{
	  err_status = ER_LOC_GEN;
	  LOG_LOCALE_ERROR ("Data buffer too big in <wr> attribute STEP.", ER_LOC_GEN, true);
	  goto exit;
	}
      strcpy (ct_rule->step, att_val);
      ct_rule->step[strlen (att_val)] = '\0';
    }

exit:
  PRINT_DEBUG_START (data, attr, "", 0);
  return err_status;
}

/*
 * end_collation_cubrid_rule_set() - XML element end function
 * "ldml collations collation cubridrules ..."
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_collation_cubrid_rule_set (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  COLL_TAILORING *curr_coll_tail = NULL;
  int rule_id;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);
  curr_coll_tail = LOC_CURRENT_COLL_TAIL (ld);

  rule_id = curr_coll_tail->cub_count_rules;

  /* rule finished, increase count */
  curr_coll_tail->cub_count_rules++;

  if (pd->verbose)
    {
      char msg[ERR_MSG_SIZE];

      snprintf (msg, sizeof (msg) - 1, " * SET Rule no. %d, tag name :%s *", (rule_id + 1), el_name);
      PRINT_DEBUG_END (data, msg, 0);
    }

  return 0;
}

/*
 * end_collation_cubrid_rule_set_end_cp_ch - XML element end function for
 *					    cp, ch, scp, sch
 * "ldml collations collation cubridrules ..."
 *
 * return: NO_ERROR(0) if parser OK,
 *	   ER_LOC_GEN if data buffer for codepoint is too big.
 * data(in/out): user data
 * el_name(in): element name
 */
static int
end_collation_cubrid_rule_set_cp_ch (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  CUBRID_TAILOR_RULE *ct_rule = NULL;
  COLL_TAILORING *curr_coll_tail = NULL;
  int rule_id;
  int err_status = NO_ERROR;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);
  curr_coll_tail = LOC_CURRENT_COLL_TAIL (ld);

  rule_id = curr_coll_tail->cub_count_rules;
  ct_rule = &(curr_coll_tail->cub_rules[rule_id]);

  if (ld->data_buf_count > LOC_DATA_BUFF_SIZE)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("Data buffer too large in codepoint starting label.", ER_LOC_GEN, true);
      goto exit;
    }

  memcpy (ct_rule->start_cp_buf, ld->data_buffer, ld->data_buf_count * sizeof (char));
  ct_rule->start_cp_buf[ld->data_buf_count] = '\0';

  memcpy (ct_rule->end_cp_buf, ld->data_buffer, ld->data_buf_count * sizeof (char));
  ct_rule->end_cp_buf[ld->data_buf_count] = '\0';

  if (strcmp (el_name, "ch") == 0 || strcmp (el_name, "sch") == 0)
    {
      ct_rule->start_cp_buf_type = BUF_TYPE_CHAR;
      ct_rule->end_cp_buf_type = BUF_TYPE_CHAR;
    }
  else if (strcmp (el_name, "cp") == 0 || strcmp (el_name, "scp") == 0)
    {
      ct_rule->start_cp_buf_type = BUF_TYPE_CODE;
      ct_rule->end_cp_buf_type = BUF_TYPE_CODE;
    }

  if (pd->verbose)
    {
      char msg[ERR_MSG_SIZE];

      snprintf (msg, sizeof (msg) - 1, " * SET Rule no. %d, tag name :%s *", (rule_id + 1), el_name);
      PRINT_DEBUG_END (data, msg, 0);
    }

exit:
  clear_data_buffer (pd);
  return err_status;
}

/*
 * end_collation_cubrid_rule_set_end_ecp - XML element end function
 *					   for ech/ecp.
 * "ldml collations collation cubridrules ..."
 *
 * return: NO_ERROR if parser OK
 *	   ER_LOC_GEN if buffer data too big
 * data(in/out): user data
 * el_name(in): element name
 */
static int
end_collation_cubrid_rule_set_ech_ecp (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  CUBRID_TAILOR_RULE *ct_rule = NULL;
  COLL_TAILORING *curr_coll_tail = NULL;
  int rule_id;
  int err_status = NO_ERROR;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);
  curr_coll_tail = LOC_CURRENT_COLL_TAIL (ld);

  if (ld->data_buf_count > LOC_DATA_BUFF_SIZE)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("Data buffer too large in codepoint end label.", ER_LOC_GEN, true);
      goto exit;
    }
  rule_id = curr_coll_tail->cub_count_rules;
  ct_rule = &(curr_coll_tail->cub_rules[rule_id]);

  memcpy (ct_rule->end_cp_buf, ld->data_buffer, ld->data_buf_count * sizeof (char));
  ct_rule->end_cp_buf[ld->data_buf_count] = '\0';

  if (strcmp (el_name, "ech") == 0)
    {
      ct_rule->end_cp_buf_type = BUF_TYPE_CHAR;
    }
  else if (strcmp (el_name, "ecp") == 0)
    {
      ct_rule->end_cp_buf_type = BUF_TYPE_CODE;
    }

  if (pd->verbose)
    {
      char msg[ERR_MSG_SIZE];

      snprintf (msg, sizeof (msg) - 1, "* SET Rule no. %d, tag name :%s *", (rule_id + 1), el_name);
      PRINT_DEBUG_END (data, msg, 0);
    }

exit:
  clear_data_buffer (pd);
  return err_status;
}

/*
 * end_collation_cubrid_rule_set_w_wr() - XML element end function for w / wr.
 * "ldml collations collation cubridrules ..."
 *
 * return: NO_ERROR if parser OK,
 *	   ER_LOC_GEN if parser NOK and stop parsing.
 * data(in/out): user data
 * el_name(in): element name
 */
static int
end_collation_cubrid_rule_set_w_wr (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  CUBRID_TAILOR_RULE *ct_rule = NULL;
  COLL_TAILORING *curr_coll_tail = NULL;
  int rule_id;
  int err_status = NO_ERROR;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);
  curr_coll_tail = LOC_CURRENT_COLL_TAIL (ld);

  rule_id = curr_coll_tail->cub_count_rules;
  ct_rule = &(curr_coll_tail->cub_rules[rule_id]);

  if (ld->data_buf_count > LOC_DATA_BUFF_SIZE)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("Data buffer too large in weight label.", ER_LOC_GEN, true);
      goto exit;
    }

  memcpy (ct_rule->start_weight, ld->data_buffer, ld->data_buf_count * sizeof (char));
  ct_rule->start_weight[ld->data_buf_count] = '\0';

  if (pd->verbose)
    {
      char msg[ERR_MSG_SIZE];

      snprintf (msg, sizeof (msg) - 1, "* SET Rule no. %d, tag name :%s *", (rule_id + 1), el_name);
      PRINT_DEBUG_END (data, msg, 0);
    }

exit:
  clear_data_buffer (pd);
  return err_status;
}

/*
 * handle_data_collation_rule() - XML element data content handle function
 * "ldml collations collation rules p"
 *
 * return: 0 handling OK, non-zero if handling NOK and stop parsing
 * (data): user data
 * (s): content buffer
 * (len): length of buffer
 */
static int
handle_data_collation_rule (void *data, const char *s, int len)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  TAILOR_RULE *t_rule = NULL;
  COLL_TAILORING *curr_coll_tail = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);
  curr_coll_tail = LOC_CURRENT_COLL_TAIL (ld);

  assert (len >= 0);

  t_rule = &(curr_coll_tail->rules[curr_coll_tail->count_rules]);
  char *const realloc_t_buf = (char *) realloc (t_rule->t_buf, t_rule->t_buf_size + len);
  if (realloc_t_buf == NULL)
    {
      pd->xml_error = XML_CUB_OUT_OF_MEMORY;
      PRINT_DEBUG_DATA (data, "memory allocation failed", -1);
      return -1;
    }
  else
    {
      t_rule->t_buf = realloc_t_buf;
    }

  /* copy partial data to data buffer */
  memcpy (t_rule->t_buf + t_rule->t_buf_size, s, len);
  t_rule->t_buf_size += len;

  if (pd->verbose)
    {
      char msg[ERR_MSG_SIZE];

      if (len < 32 && len < ERR_MSG_SIZE)
	{
	  memcpy (msg, s, len);
	  msg[len] = '\0';
	}
      else
	{
	  char msg2[33];

	  memcpy (msg2, s, 32);
	  msg2[32] = '\0';
	  snprintf (msg, sizeof (msg) - 1, "%s | %d bytes", msg2, len);
	}
      PRINT_DEBUG_DATA (data, msg, 0);
    }

  return 0;
}

/*
 * end_collation_rule() - XML element end function
 * "ldml collations collation rules p"
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_collation_rule (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  TAILOR_RULE *t_rule = NULL;
  COLL_TAILORING *curr_coll_tail = NULL;
  int rule_id;
  int status;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);
  curr_coll_tail = LOC_CURRENT_COLL_TAIL (ld);

  /* rule finished, increase count */
  rule_id = curr_coll_tail->count_rules++;
  t_rule = &(curr_coll_tail->rules[rule_id]);

  if (strcmp (el_name, "p") == 0)
    {
      t_rule->level = TAILOR_PRIMARY;
    }
  else if (strcmp (el_name, "s") == 0)
    {
      t_rule->level = TAILOR_SECONDARY;
    }
  else if (strcmp (el_name, "t") == 0)
    {
      t_rule->level = TAILOR_TERTIARY;
    }
  else if (strcmp (el_name, "i") == 0)
    {
      t_rule->level = TAILOR_IDENTITY;
    }
  else if (strcmp (el_name, "pc") == 0)
    {
      t_rule->level = TAILOR_PRIMARY;
      t_rule->multiple_chars = true;
    }
  else if (strcmp (el_name, "sc") == 0)
    {
      t_rule->level = TAILOR_SECONDARY;
      t_rule->multiple_chars = true;
    }
  else if (strcmp (el_name, "tc") == 0)
    {
      t_rule->level = TAILOR_TERTIARY;
      t_rule->multiple_chars = true;
    }
  else if (strcmp (el_name, "ic") == 0)
    {
      t_rule->level = TAILOR_IDENTITY;
      t_rule->multiple_chars = true;
    }
  else
    {
      assert (false);
      pd->xml_error = XML_CUB_ERR_PARSER;
      PRINT_DEBUG_END (data, "Invalid element name", -1);
      return -1;
    }

  status = common_collation_end_rule (data, ld, rule_id, t_rule);
  if (status != 0)
    {
      return status;
    }

  return 0;
}

/*
 * start_collation_x() - XML element start function
 * "ldml collations collation rules x"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_collation_x (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  TAILOR_RULE *t_rule = NULL;
  int status;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  t_rule = new_collation_rule (ld);

  if (t_rule == NULL)
    {
      pd->xml_error = XML_CUB_OUT_OF_MEMORY;
      PRINT_DEBUG_START (data, attr, "memory allocation failed", -1);
      return -1;
    }

  if (ld->last_rule_pos_type != RULE_POS_BUFFER)
    {
      pd->xml_error = XML_CUB_ERR_PARSER;
      PRINT_DEBUG_START (data, attr, "Last <reset> tag is invalid", -1);
      return -1;
    }

  status = common_collation_start_rule (data, attr, ld, t_rule);
  if (status != 0)
    {
      return status;
    }

  PRINT_DEBUG_START (data, attr, "", 0);
  return 0;
}

/*
 * end_collation_x - XML element end function
 * "ldml collations collation rules x"
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_collation_x (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  TAILOR_RULE *t_rule = NULL;
  COLL_TAILORING *curr_coll_tail = NULL;
  int rule_id;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);
  curr_coll_tail = LOC_CURRENT_COLL_TAIL (ld);

  /* rule finished, increase count */
  rule_id = curr_coll_tail->count_rules++;
  t_rule = &(curr_coll_tail->rules[rule_id]);
  /* BEFORE applies only for the first rule after <reset> */
  ld->last_rule_dir = TAILOR_AFTER;

  if (pd->verbose)
    {
      char msg[ERR_MSG_SIZE];

      snprintf (msg, sizeof (msg) - 1, " * X_Rule %d, L :%d, Dir:%d, PosType:%d, Mc:%d *", rule_id, t_rule->level,
		t_rule->direction, t_rule->r_pos_type, t_rule->multiple_chars);
      PRINT_DEBUG_END (data, msg, 0);
    }

  return 0;
}

/*
 * end_collation_x_rule - XML element end function
 * "ldml collations collation rules x p"
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_collation_x_rule (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  TAILOR_RULE *t_rule = NULL;
  COLL_TAILORING *curr_coll_tail = NULL;
  int rule_id;
  int status;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);
  curr_coll_tail = LOC_CURRENT_COLL_TAIL (ld);

  rule_id = curr_coll_tail->count_rules;
  t_rule = &(curr_coll_tail->rules[rule_id]);

  assert (strlen (ld->data_buffer) == ld->data_buf_count);

  char *const realloc_t_buf = (char *) realloc (t_rule->t_buf, t_rule->t_buf_size + ld->data_buf_count);
  if (realloc_t_buf == NULL)
    {
      pd->xml_error = XML_CUB_OUT_OF_MEMORY;
      PRINT_DEBUG_END (data, "memory allocation failed", -1);
      return -1;
    }
  else
    {
      t_rule->t_buf = realloc_t_buf;
    }

  /* copy partial data to rule tailoring buffer (character to be modified) */
  memcpy (t_rule->t_buf + t_rule->t_buf_size, ld->data_buffer, ld->data_buf_count);
  t_rule->t_buf_size += ld->data_buf_count;

  if (strcmp (el_name, "p") == 0)
    {
      t_rule->level = TAILOR_PRIMARY;
    }
  else if (strcmp (el_name, "s") == 0)
    {
      t_rule->level = TAILOR_SECONDARY;
    }
  else if (strcmp (el_name, "t") == 0)
    {
      t_rule->level = TAILOR_TERTIARY;
    }
  else if (strcmp (el_name, "i") == 0)
    {
      t_rule->level = TAILOR_IDENTITY;
    }
  else
    {
      assert (false);
      pd->xml_error = XML_CUB_ERR_PARSER;
      PRINT_DEBUG_END (data, "Invalid element name", -1);
      return -1;
    }

  status = common_collation_end_rule (data, ld, rule_id, t_rule);
  if (status != 0)
    {
      return status;
    }

  clear_data_buffer ((XML_PARSER_DATA *) data);

  return 0;
}

/*
 * end_collation_x_extend - XML element end function
 * "ldml collations collation rules x extend"
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_collation_x_extend (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  TAILOR_RULE *t_rule = NULL;
  COLL_TAILORING *curr_coll_tail = NULL;
  int rule_id;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);
  curr_coll_tail = LOC_CURRENT_COLL_TAIL (ld);

  rule_id = curr_coll_tail->count_rules;
  t_rule = &(curr_coll_tail->rules[rule_id]);

  assert (t_rule->r_buf != NULL);
  assert (ld->data_buf_count > 0);

  char *const realloc_r_buf = (char *) realloc (t_rule->r_buf, t_rule->r_buf_size + ld->data_buf_count);
  if (realloc_r_buf == NULL)
    {
      pd->xml_error = XML_CUB_OUT_OF_MEMORY;
      PRINT_DEBUG_END (data, "memory allocation failed", -1);
      return -1;
    }
  else
    {
      t_rule->r_buf = realloc_r_buf;
    }

  memcpy (t_rule->r_buf + t_rule->r_buf_size, ld->data_buffer, ld->data_buf_count);

  t_rule->r_buf_size += ld->data_buf_count;

  clear_data_buffer ((XML_PARSER_DATA *) data);

  PRINT_DEBUG_END (data, "", 0);

  return 0;
}

/*
 * end_collation_x_context - XML element end function
 * "ldml collations collation rules x context"
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_collation_x_context (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  TAILOR_RULE *t_rule = NULL;
  COLL_TAILORING *curr_coll_tail = NULL;
  int rule_id;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);
  curr_coll_tail = LOC_CURRENT_COLL_TAIL (ld);

  rule_id = curr_coll_tail->count_rules;
  t_rule = &(curr_coll_tail->rules[rule_id]);

  assert (strlen (ld->data_buffer) < LOC_DATA_COLL_TWO_CHARS);
  if (strlen (ld->data_buffer) >= LOC_DATA_COLL_TWO_CHARS)
    {
      pd->xml_error = XML_CUB_ERR_PARSER;
      PRINT_DEBUG_END (data, "Too much data", -1);
      return -1;
    }

  if (t_rule->t_buf_size < ld->data_buf_count)
    {
      char *const realloc_t_buf = (char *) realloc (t_rule->t_buf, ld->data_buf_count);
      if (realloc_t_buf == NULL)
	{
	  pd->xml_error = XML_CUB_OUT_OF_MEMORY;
	  PRINT_DEBUG_END (data, "memory allocation failed", -1);
	  return -1;
	}
      else
	{
	  t_rule->t_buf = realloc_t_buf;
	}
    }

  memcpy (t_rule->t_buf, ld->data_buffer, ld->data_buf_count);
  t_rule->t_buf_size = ld->data_buf_count;

  clear_data_buffer ((XML_PARSER_DATA *) data);
  if (pd->verbose)
    {
      char msg[ERR_MSG_SIZE];
      int rule_id = curr_coll_tail->count_rules;

      snprintf (msg, sizeof (msg) - 1, "* Rule %d *", rule_id);
      PRINT_DEBUG_END (data, msg, 0);
    }
  else
    {
      PRINT_DEBUG_END (data, "", 0);
    }

  return 0;
}

/*
 * start_collation_logical_pos() - XML element start function
 * "ldml collations collation rules reset first_variable"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_collation_logical_pos (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  PRINT_DEBUG_START (data, attr, "", 0);
  return 0;
}

/*
 * end_collation_logical_pos() - XML element end function
 * "ldml collations collation rules reset first_variable"
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_collation_logical_pos (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  if (strcmp (el_name, "first_variable") == 0)
    {
      ld->last_rule_pos_type = RULE_POS_FIRST_VAR;
    }
  else if (strcmp (el_name, "last_variable") == 0)
    {
      ld->last_rule_pos_type = RULE_POS_LAST_VAR;
    }
  else if (strcmp (el_name, "first_primary_ignorable") == 0)
    {
      ld->last_rule_pos_type = RULE_POS_FIRST_PRI_IGN;
    }
  else if (strcmp (el_name, "last_primary_ignorable") == 0)
    {
      ld->last_rule_pos_type = RULE_POS_LAST_PRI_IGN;
    }
  else if (strcmp (el_name, "first_secondary_ignorable") == 0)
    {
      ld->last_rule_pos_type = RULE_POS_FIRST_SEC_IGN;
    }
  else if (strcmp (el_name, "last_secondary_ignorable") == 0)
    {
      ld->last_rule_pos_type = RULE_POS_LAST_SEC_IGN;
    }
  else if (strcmp (el_name, "first_tertiary_ignorable") == 0)
    {
      ld->last_rule_pos_type = RULE_POS_FIRST_TERT_IGN;
    }
  else if (strcmp (el_name, "last_tertiary_ignorable") == 0)
    {
      ld->last_rule_pos_type = RULE_POS_LAST_TERT_IGN;
    }
  else if (strcmp (el_name, "first_non_ignorable") == 0)
    {
      ld->last_rule_pos_type = RULE_POS_FIRST_NON_IGN;
    }
  else if (strcmp (el_name, "last_non_ignorable") == 0)
    {
      ld->last_rule_pos_type = RULE_POS_LAST_NON_IGN;
    }
  else if (strcmp (el_name, "first_trailing") == 0)
    {
      ld->last_rule_pos_type = RULE_POS_FIRST_TRAIL;
    }
  else if (strcmp (el_name, "last_trailing") == 0)
    {
      ld->last_rule_pos_type = RULE_POS_LAST_TRAIL;
    }
  else
    {
      assert (false);
      pd->xml_error = XML_CUB_ERR_PARSER;
      PRINT_DEBUG_END (data, "Internal LDML parser error", -1);
      return -1;
    }

  PRINT_DEBUG_END (data, "", 0);

  return 0;
}

/* Alphabet XML elements handling */
/*
 * start_one_alphabet() - XML element start function
 * "ldml alphabets alphabet"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_one_alphabet (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  char *att_val = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  if (ld->alpha_tailoring.alphabet_mode != 0 || ld->alpha_tailoring.count_rules != 0
      || ld->alpha_tailoring.sett_max_letters != -1)
    {
      PRINT_DEBUG_START (data, attr, "Only one alphabet is allowed", -1);
      return -1;
    }

  att_val = NULL;
  if (xml_get_att_value (attr, "CUBRIDMaxLetters", &att_val) == 0)
    {
      assert (att_val != NULL);
      ld->alpha_tailoring.sett_max_letters = atoi (att_val);
    }

  att_val = NULL;
  if (xml_get_att_value (attr, "CUBRIDAlphabetMode", &att_val) == 0)
    {
      assert (att_val != NULL);
      if (strcasecmp (att_val, "ASCII") == 0)
	{
	  ld->alpha_tailoring.alphabet_mode = 2;
	}
      else if (strcasecmp (att_val, "UNICODEDATAFILE") == 0)
	{
	  ld->alpha_tailoring.alphabet_mode = 0;
	}
      else
	{
	  PRINT_DEBUG_START (data, attr, "Invalid value for CUBRIDAlphabetMode." "Expected ASCII or UNICODEDATAFILE",
			     -1);
	  return -1;
	}
    }

  ld->alpha_tailoring.ldml_context.ldml_file = strdup (ld->ldml_context.ldml_file);
  ld->alpha_tailoring.ldml_context.line_no = XML_GetCurrentLineNumber (pd->xml_parser);

  PRINT_DEBUG_START (data, attr, "", 0);
  return 0;
}

/*
 * start_upper_case_rule() - XML element start function
 * "ldml alphabets alphabet u"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_upper_case_rule (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  TRANSFORM_RULE *tf_rule = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  tf_rule = new_transform_rule (ld);

  if (tf_rule == NULL)
    {
      pd->xml_error = XML_CUB_OUT_OF_MEMORY;
      PRINT_DEBUG_START (data, attr, "OUT OF MEMORY", -1);
      return -1;
    }

  tf_rule->type = TR_UPPER;

  PRINT_DEBUG_START (data, attr, "", 0);
  return 0;
}

/*
 * end_case_rule() - XML element end function
 * "ldml alphabets alphabet u"
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_case_rule (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  TRANSFORM_RULE *tf_rule = NULL;
  int rule_id;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  /* rule finished, increase count */
  rule_id = ld->alpha_tailoring.count_rules++;
  tf_rule = &(ld->alpha_tailoring.rules[rule_id]);

  if (pd->verbose)
    {
      char msg[64];
      unsigned char *dummy = NULL;
      unsigned int cp_src = intl_utf8_to_cp ((unsigned char *) tf_rule->src, tf_rule->src_size,
					     &dummy);
      unsigned int cp_dest = intl_utf8_to_cp ((unsigned char *) tf_rule->dest, tf_rule->dest_size,
					      &dummy);

      sprintf (msg, " * Case Rule %d, Src :%0X, Dest:%0X *", rule_id, cp_src, cp_dest);
      PRINT_DEBUG_END (data, msg, 0);
    }
  else
    {
      PRINT_DEBUG_END (data, "", 0);
    }

  return 0;
}

/*
 * start_lower_case_rule() - XML element start function
 * "ldml alphabets alphabet l"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_lower_case_rule (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  TRANSFORM_RULE *tf_rule = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  tf_rule = new_transform_rule (ld);

  if (tf_rule == NULL)
    {
      pd->xml_error = XML_CUB_OUT_OF_MEMORY;
      PRINT_DEBUG_START (data, attr, "OUT OF MEMORY", -1);
      return -1;
    }

  tf_rule->type = TR_LOWER;

  PRINT_DEBUG_START (data, attr, "", 0);
  return 0;
}

/*
 * end_transform_buffer() - XML element end function
 * "ldml alphabets alphabet u s"
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * data: user data
 * el_name: element name
 */
static int
end_transform_buffer (void *data, const char *el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  TRANSFORM_RULE *tf_rule = NULL;
  char *tf_buf = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  assert (ld->alpha_tailoring.count_rules < ld->alpha_tailoring.max_rules);
  tf_rule = &(ld->alpha_tailoring.rules[ld->alpha_tailoring.count_rules]);

  if (ld->data_buf_count <= 0)
    {
      PRINT_DEBUG_END (data, "Expecting non-empty string", -1);
      pd->xml_error = XML_CUB_ERR_PARSER;
      goto error_exit;
    }

  tf_buf = (char *) malloc (ld->data_buf_count);
  if (tf_buf == NULL)
    {
      pd->xml_error = XML_CUB_OUT_OF_MEMORY;
      PRINT_DEBUG_END (data, "memory allocation failed", -1);
      goto error_exit;
    }

  memcpy (tf_buf, ld->data_buffer, ld->data_buf_count);

  assert (tf_rule->type == TR_LOWER || tf_rule->type == TR_UPPER);

  if (strcmp (el_name, "s") == 0)
    {
      if (tf_rule->src != NULL || tf_rule->src_size != 0)
	{
	  PRINT_DEBUG_END (data, "duplicate source for case rule", -1);
	  pd->xml_error = XML_CUB_ERR_PARSER;
	  goto error_exit;
	}

      tf_rule->src = tf_buf;
      tf_rule->src_size = ld->data_buf_count;
    }
  else if (strcmp (el_name, "d") == 0)
    {
      if (tf_rule->dest != NULL || tf_rule->dest_size != 0)
	{
	  PRINT_DEBUG_END (data, "duplicate destination for case rule", -1);
	  pd->xml_error = XML_CUB_ERR_PARSER;
	  goto error_exit;
	}

      tf_rule->dest = tf_buf;
      tf_rule->dest_size = ld->data_buf_count;
    }
  else
    {
      assert (false);
      PRINT_DEBUG_END (data, "Internal LDML parser error", -1);
      pd->xml_error = XML_CUB_ERR_PARSER;
      goto error_exit;
    }

  PRINT_DEBUG_END (data, "", 0);

  clear_data_buffer ((XML_PARSER_DATA *) data);

  return 0;

error_exit:

  clear_data_buffer ((XML_PARSER_DATA *) data);

  return -1;
}

/*
 * start_consoleconversion() - XML element start function
 * "ldml consoleconversion"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_consoleconversion (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  char *att_val = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  if (*(ld->txt_conv_prm.nl_lang_str) != '\0' || *(ld->txt_conv_prm.conv_file) != '\0'
      || *(ld->txt_conv_prm.win_codepages) != '\0')
    {
      PRINT_DEBUG_START (data, attr, "Only one console conversion is allowed", -1);
      return -1;
    }

  att_val = NULL;
  if (xml_get_att_value (attr, "type", &att_val) == 0)
    {
      assert (att_val != NULL);
      if (strcasecmp (att_val, "ISO") == 0)
	{
	  ld->txt_conv_prm.conv_type = TEXT_CONV_GENERIC_1BYTE;
	}
      else if (strcasecmp (att_val, "DBCS") == 0)
	{
	  ld->txt_conv_prm.conv_type = TEXT_CONV_GENERIC_2BYTE;
	}
      else if (strcasecmp (att_val, "ISO88591") == 0)
	{
	  ld->txt_conv_prm.conv_type = TEXT_CONV_ISO_88591_BUILTIN;
	}
      else if (strcasecmp (att_val, "ISO88599") == 0)
	{
	  ld->txt_conv_prm.conv_type = TEXT_CONV_ISO_88599_BUILTIN;
	}
      else
	{
	  PRINT_DEBUG_START (data, attr, "Invalid value for type." "Expecting ISO88591 or ISO or EUC", -1);
	  return -1;
	}
    }

  att_val = NULL;
  if (xml_get_att_value (attr, "windows_codepage", &att_val) == 0)
    {
      assert (att_val != NULL);

      if (strlen (att_val) > (int) sizeof (ld->txt_conv_prm.win_codepages) - 1)
	{
	  PRINT_DEBUG_START (data, attr, "Invalid attribute value", -1);
	  return -1;
	}

      strcpy (ld->txt_conv_prm.win_codepages, att_val);
    }

  att_val = NULL;
  if (xml_get_att_value (attr, "linux_charset", &att_val) == 0)
    {
      assert (att_val != NULL);

      if (strlen (att_val) > (int) sizeof (ld->txt_conv_prm.nl_lang_str) - 1)
	{
	  PRINT_DEBUG_START (data, attr, "Invalid attribute value", -1);
	  return -1;
	}

      strcpy (ld->txt_conv_prm.nl_lang_str, att_val);
    }

  att_val = NULL;
  if (xml_get_att_value (attr, "file", &att_val) == 0)
    {
      assert (att_val != NULL);

      if (strlen (att_val) > (int) sizeof (ld->txt_conv_prm.conv_file) - 1)
	{
	  PRINT_DEBUG_START (data, attr, "Invalid attribute value", -1);
	  return -1;
	}

      strcpy (ld->txt_conv_prm.conv_file, att_val);
    }

  PRINT_DEBUG_START (data, attr, "", 0);
  return 0;
}

/* General LDML handling functions */
/*
 * handle_data() - XML element data content handle function
 *		   generic data handling function
 *
 * return: 0 handling OK, non-zero if handling NOK and stop parsing
 * (data): user data
 * (s): content buffer
 * (len): length of buffer
 */
static int
handle_data (void *data, const char *s, int len)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  char msg[64];

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  assert (len >= 0);

  if (ld->data_buf_count + len >= LOC_DATA_BUFF_SIZE)
    {
      pd->xml_error = XML_CUB_ERR_PARSER;
      sprintf (msg, "Too much data : %d + %d bytes", ld->data_buf_count, len);
      PRINT_DEBUG_DATA (data, msg, -1);
      return -1;
    }

  /* copy partial data to data buffer */
  memcpy (ld->data_buffer + ld->data_buf_count, s, len);
  ld->data_buf_count += len;
  ld->data_buffer[ld->data_buf_count] = '\0';

  if (pd->verbose)
    {
      char msg[64];

      if (len < 64)
	{
	  memcpy (msg, s, len);
	  msg[len] = '\0';
	}
      else
	{
	  char msg2[33];

	  memcpy (msg2, s, 32);
	  msg2[32] = '\0';
	  sprintf (msg, "%s | %d bytes", msg2, len);
	}
      PRINT_DEBUG_DATA (data, msg, 0);
    }

  return 0;
}

/*
 * clear_data_buffer() - clears the data content buffer
 *
 * return:
 * pd(in): parser data
 */
static void
clear_data_buffer (XML_PARSER_DATA * pd)
{
  LOCALE_DATA *ld = NULL;

  assert (pd != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  ld->data_buf_count = 0;
  *(ld->data_buffer) = '\0';
}

/*
 * new_locale_collation() - creates new collation in the locale struct
 *
 * return: rule
 * ld(in): locale data
 */
static LOCALE_COLLATION *
new_locale_collation (LOCALE_DATA * ld)
{
  LOCALE_COLLATION *loc_collation = NULL;

  assert (ld != NULL);

  /* check number of rules, increase array if necessary */
  LOCALE_COLLATION *const realloc_collations
    = (LOCALE_COLLATION *) realloc (ld->collations, sizeof (LOCALE_COLLATION) * (ld->coll_cnt + 1));
  if (realloc_collations == NULL)
    {
      return NULL;
    }
  else
    {
      ld->collations = realloc_collations;
    }

  loc_collation = &(ld->collations[ld->coll_cnt]);
  memset (loc_collation, 0, sizeof (LOCALE_COLLATION));

  loc_collation->tail_coll.sett_max_cp = -1;
  loc_collation->tail_coll.uca_opt.sett_contr_policy = CONTR_IGNORE;

  return loc_collation;
}

/*
 * new_collation_rule() - creates new collation tailoring rule
 *
 * return: rule
 * ld(in): locale data
 */
static TAILOR_RULE *
new_collation_rule (LOCALE_DATA * ld)
{
  TAILOR_RULE *t_rule = NULL;
  COLL_TAILORING *curr_coll_tail = NULL;

  assert (ld != NULL);

  curr_coll_tail = LOC_CURRENT_COLL_TAIL (ld);

  /* check number of rules, increase array if necessary */
  if (curr_coll_tail->count_rules + 1 >= curr_coll_tail->max_rules)
    {
      TAILOR_RULE *const realloc_rules = (TAILOR_RULE *) realloc (curr_coll_tail->rules,
								  sizeof (TAILOR_RULE) * (curr_coll_tail->max_rules +
											  LOC_DATA_TAILOR_RULES_COUNT_GROW));
      if (realloc_rules == NULL)
	{
	  return NULL;
	}
      else
	{
	  curr_coll_tail->rules = realloc_rules;
	}

      curr_coll_tail->max_rules += LOC_DATA_TAILOR_RULES_COUNT_GROW;
    }

  /* will use slot indicated by 'coll->count_rules' */
  t_rule = &(curr_coll_tail->rules[curr_coll_tail->count_rules]);
  memset (t_rule, 0, sizeof (TAILOR_RULE));

  return t_rule;
}

/*
 * new_transform_rule() - creates new transform rule
 *
 * return: rule
 * ld(in): locale data
 */
static TRANSFORM_RULE *
new_transform_rule (LOCALE_DATA * ld)
{
  TRANSFORM_RULE *tf_rule = NULL;

  assert (ld != NULL);

  /* check number of rules, increase array if necessary */
  if (ld->alpha_tailoring.count_rules + 1 >= ld->alpha_tailoring.max_rules)
    {
      TRANSFORM_RULE *const realloc_alpha_tailoring_rules = (TRANSFORM_RULE *) realloc (ld->alpha_tailoring.rules,
											sizeof (TRANSFORM_RULE) *
											(ld->alpha_tailoring.max_rules +
											 LOC_DATA_TAILOR_RULES_COUNT_GROW));
      if (realloc_alpha_tailoring_rules == NULL)
	{
	  return NULL;
	}
      else
	{
	  ld->alpha_tailoring.rules = realloc_alpha_tailoring_rules;
	}

      ld->alpha_tailoring.max_rules += LOC_DATA_TAILOR_RULES_COUNT_GROW;
    }

  /* will use slot indicated by 'alpha_tailoring->count_rules' */
  tf_rule = &(ld->alpha_tailoring.rules[ld->alpha_tailoring.count_rules]);
  memset (tf_rule, 0, sizeof (TRANSFORM_RULE));

  return tf_rule;
}

/*
 * new_collation_cubrid_rule - creates new collation tailoring rule
 *
 * return: rule
 * ld(in): locale data
 */
static CUBRID_TAILOR_RULE *
new_collation_cubrid_rule (LOCALE_DATA * ld)
{
  CUBRID_TAILOR_RULE *ct_rule = NULL;
  COLL_TAILORING *curr_coll_tail = NULL;

  assert (ld != NULL);
  curr_coll_tail = LOC_CURRENT_COLL_TAIL (ld);

  /* check number of absolute rules, increase array if necessary */
  if (curr_coll_tail->cub_count_rules + 1 >= curr_coll_tail->cub_max_rules)
    {
      CUBRID_TAILOR_RULE *const realloc_cub_rules = (CUBRID_TAILOR_RULE *) realloc (curr_coll_tail->cub_rules,
										    sizeof (CUBRID_TAILOR_RULE) *
										    (curr_coll_tail->max_rules +
										     LOC_DATA_COLL_CUBRID_TAILOR_COUNT_GROW));
      if (realloc_cub_rules == NULL)
	{
	  return NULL;
	}
      else
	{
	  curr_coll_tail->cub_rules = realloc_cub_rules;
	}

      curr_coll_tail->cub_max_rules += LOC_DATA_COLL_CUBRID_TAILOR_COUNT_GROW;
    }

  /* will use slot indicated by 'coll->cub_count_rules' */
  ct_rule = &(curr_coll_tail->cub_rules[curr_coll_tail->cub_count_rules]);
  memset (ct_rule, 0, sizeof (CUBRID_TAILOR_RULE));

  return ct_rule;
}

/*
 * print_debug_start_el() - print debug info for start of XML element
 *
 * return:
 * data(in):
 * attrs(in):
 * status(in): status returned from start function
 */
static void
print_debug_start_el (void *data, const char **attrs, const char *msg, const int status)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  const char **curr_att;

  assert (data != NULL);
  assert (attrs != NULL);

  if (pd->verbose)
    {
      int i;

      assert (pd->ce != NULL);
      assert (pd->ce->def != NULL);

      for (i = 0; i < pd->ce->def->depth; i++)
	{
	  printf ("   ");
	}
      printf ("<%s", pd->ce->short_name);

      for (curr_att = attrs; *curr_att != NULL; curr_att++, curr_att++)
	{
	  printf (" %s=\"%s\"", curr_att[0], curr_att[1]);
	}

      if (*msg == '\0' && status == 0)
	{
	  printf (">\n");
	}
      else
	{
	  printf (">    " XML_COMMENT_START " %s ", msg);

	  if (status == 0)
	    {
	      printf (XML_COMMENT_END "\n");
	    }
	  else if (status == -1)
	    {
	      printf (" * NOK, Aborting ! *" XML_COMMENT_END "\n");
	    }
	  else
	    {
	      printf (" * Ignore *" XML_COMMENT_END "\n");
	    }
	}
    }
}

/*
 * print_debug_end_el() - print debug info for end of XML element
 *
 * return:
 * data(in):
 * status(in): status returned from start function
 */
static void
print_debug_end_el (void *data, const char *msg, const int status)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;

  assert (data != NULL);

  if (pd->verbose)
    {
      int i;

      assert (pd->ce != NULL);
      assert (pd->ce->def != NULL);

      for (i = 0; i < pd->ce->def->depth; i++)
	{
	  printf ("   ");
	}

      printf ("<\\%s>  ", pd->ce->short_name);

      if (*msg == '\0' && status == 0)
	{
	  printf ("\n");
	}
      else
	{
	  printf (XML_COMMENT_START " %s ", msg);

	  if (status != 0)
	    {
	      printf (" * NOK, Aborting ! *" XML_COMMENT_END "\n");
	    }
	  else
	    {
	      printf (XML_COMMENT_END "\n");
	    }
	}
    }
}

/*
 * print_debug_data_content() - print debug info data content of XML element
 *
 * return:
 * data(in):
 * status(in): status returned from start function
 */
static void
print_debug_data_content (void *data, const char *msg, const int status)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;

  assert (data != NULL);

  if (pd->verbose)
    {
      int i;

      assert (pd->ce != NULL);
      assert (pd->ce->def != NULL);

      for (i = 0; i < pd->ce->def->depth + 1; i++)
	{
	  printf ("   ");
	}

      assert (msg != NULL);

      printf ("%s ", msg);

      if (status != 0)
	{
	  printf (XML_COMMENT_START "* NOK, Aborting ! *" XML_COMMENT_END);
	}

      printf ("\n");
    }
}

/*
 * locale_alloc_collation_id() - allocates collation id
 *
 * return:
 *
 *  Note : Ranges of collation id:
 *	   0 - 31(*)  : built-in collations  (32)
 *	   32(*) - 46 : generic collations (language independent) (15)
 *	   47 - 255 : language dependent collations (208 = 26 * 8)
 *	   Collation is name is in format: charset_lang_desc1_desc2_..
 *	   utf8_de_exp : Collation specific to de (German) with expansions
 *	   utf8_gen_ai_ci : Generic collation, accent and case insensitive
 *
 */
static void
locale_alloc_collation_id (COLL_TAILORING * coll_tail)
{
#define ID_PER_RANGE 8
#define START_GENERIC_RANGE LANG_MAX_BUILTIN_COLLATIONS
#define START_LANG_RANGE 47

  int coll_id = 0;
  char coll_name[COLL_NAME_SIZE];
  const char *charset_part, *lang_part, *desc_part;
  int range_start, range_size;

  strcpy (coll_name, coll_tail->coll_name);

  charset_part = coll_name;
  lang_part = strchr (coll_name, '_');
  if (lang_part != NULL)
    {
      lang_part++;
      desc_part = strchr (lang_part, '_');
      if (desc_part != NULL)
	{
	  desc_part++;
	}
    }
  else
    {
      desc_part = NULL;
    }

  if (lang_part != NULL)
    {
      if (strncasecmp (lang_part, "gen", 3) == 0)
	{
	  range_start = START_GENERIC_RANGE;
	  range_size = (START_LANG_RANGE - START_GENERIC_RANGE) + 1;
	}
      else
	{
	  int s = (int) *lang_part;
	  s = (char_isupper (s)) ? (s + ('a' - 'A')) : s;
	  s = (s > 'z') ? 'z' : ((s < 'a') ? 'a' : s);
	  s -= 'a';
	  range_start = START_LANG_RANGE + s * ID_PER_RANGE;
	  range_size = ID_PER_RANGE;
	}

      if (desc_part != NULL)
	{
	  while (*desc_part)
	    {
	      coll_id += *desc_part++;
	    }
	}

      coll_id %= range_size;
      coll_id += range_start;
    }

  coll_tail->coll_id = coll_id;

#undef ID_PER_RANGE
#undef START_GENERIC_RANGE
#undef START_LANG_RANGE
}

/*
 * locale_check_collation_id() - checks a collation id
 *
 * return:
 *
 *
 */
static int
locale_check_collation_id (const COLL_TAILORING * coll_tail)
{
  char err_msg[ERR_MSG_SIZE];
  static int taken_collations[LANG_MAX_COLLATIONS] = { 0 };

  assert (coll_tail->coll_id >= LANG_MAX_BUILTIN_COLLATIONS && coll_tail->coll_id < LANG_MAX_COLLATIONS);

  if (taken_collations[coll_tail->coll_id] != 0)
    {
      snprintf (err_msg, sizeof (err_msg) - 1,
		"Invalid collation numeric identifier : %d" " for collation '%s'. Id is already taken.",
		coll_tail->coll_id, coll_tail->coll_name);
      LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
      return ER_LOC_GEN;
    }

  taken_collations[coll_tail->coll_id] = 1;
  return NO_ERROR;
}

/*
 * load_console_conv_data() - Loads the console conversion file (standardised
 *			      and availabe at Unicode.org).
 * Returns: error code
 * ld(in/out) : locale data
 * is_verbose(in) :
 */
static int
load_console_conv_data (LOCALE_DATA * ld, bool is_verbose)
{
  TXT_CONV_ITEM *txt_conv_array = NULL;
  FILE *fp = NULL;
  char *end_p;
  char err_msg[ERR_MSG_SIZE];
  char str[TXT_CONV_LINE_SIZE];
  char conv_file_name[PATH_MAX];
  int status = NO_ERROR;
  int line_count = 0;
  int txt_conv_max_items = 0;
  int txt_conv_count_items = 0;
  unsigned int i;
  unsigned int cp_text = 0;
  unsigned int cp_unicode = 0;
  TXT_CONV_ITEM min_values = { 0xffff, 0xffff };
  TXT_CONV_ITEM max_values = { 0, 0 };

  assert (ld != NULL);

  if (ld->txt_conv_prm.conv_type == TEXT_CONV_NO_CONVERSION)
    {
      if (is_verbose)
	{
	  printf ("No console conversion data\n");
	}
      return status;
    }

  if (ld->txt_conv_prm.conv_type == TEXT_CONV_ISO_88591_BUILTIN
      || ld->txt_conv_prm.conv_type == TEXT_CONV_ISO_88599_BUILTIN)
    {
      if (is_verbose)
	{
	  printf ("Built-in console conversion\n");
	}
      ld->txt_conv.conv_type = ld->txt_conv_prm.conv_type;
      return status;
    }

  strncpy (conv_file_name, ld->txt_conv_prm.conv_file, sizeof (conv_file_name) - 1);
  conv_file_name[sizeof (conv_file_name) - 1] = '\0';

  fp = fopen_ex (conv_file_name, "rt");
  if (fp == NULL)
    {
      envvar_codepagedir_file (conv_file_name, sizeof (conv_file_name), ld->txt_conv_prm.conv_file);
      fp = fopen_ex (conv_file_name, "rt");
    }

  if (fp == NULL)
    {
      snprintf_dots_truncate (err_msg, sizeof (err_msg) - 1, "Cannot open file %s", ld->txt_conv_prm.conv_file);
      LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
      status = ER_LOC_GEN;
      goto error;
    }

  if (is_verbose)
    {
      printf ("Using file: %s\n", conv_file_name);
    }

  memset (ld->txt_conv.byte_flag, 0, sizeof (ld->txt_conv.byte_flag));
  while (fgets (str, sizeof (str), fp))
    {
      char *s;

      line_count++;

      s = str;
      /* skip white spaces */
      while (char_isspace (*s))
	{
	  s++;
	}

      if (*s == '\0' || *s == '#')
	{
	  continue;
	}

      str_to_uint32 (&cp_text, &end_p, s, 16);

      /* skip codepoints values above 0xFFFF */
      if (cp_text > 0xffff || (cp_text > 0xff && ld->txt_conv_prm.conv_type == TEXT_CONV_GENERIC_1BYTE))
	{
	  snprintf_dots_truncate (err_msg, sizeof (err_msg) - 1, "Codepoint value too big" " in file :%s at line %d",
				  ld->txt_conv_prm.conv_file, line_count);
	  LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
	  status = ER_LOC_GEN;
	  goto error;
	}

      /* skip first token */
      while (!char_isspace (*s))
	{
	  s++;
	}
      /* skip white spaces */
      while (char_isspace (*s))
	{
	  s++;
	}

      if (*s == '\0' || *s == '#')
	{
	  if (ld->txt_conv_prm.conv_type == TEXT_CONV_GENERIC_2BYTE)
	    {
	      assert (cp_text <= 0xff);
	      if (strncasecmp (s, "#DBCS", 5) == 0)
		{
		  ld->txt_conv.byte_flag[cp_text] = 1;
		}
	      else
		{
		  ld->txt_conv.byte_flag[cp_text] = 2;
		}
	    }
	  continue;
	}

      str_to_uint32 (&cp_unicode, &end_p, s, 16);

      /* first codepoints which maps the same character are not included */
      if (((ld->txt_conv_prm.conv_type == TEXT_CONV_GENERIC_1BYTE && cp_text < 0x80)
	   || ld->txt_conv_prm.conv_type == TEXT_CONV_GENERIC_2BYTE) && (cp_text == cp_unicode
									 && min_values.text_cp == 0xffff
									 && min_values.unicode_cp == 0xffff))
	{
	  continue;
	}

      if (cp_unicode > 0xffff)
	{
	  snprintf_dots_truncate (err_msg, sizeof (err_msg) - 1, "Codepoint value too big" " in file :%s at line %d",
				  ld->txt_conv_prm.conv_file, line_count);
	  LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
	  status = ER_LOC_GEN;
	  goto error;
	}

      if (txt_conv_count_items >= txt_conv_max_items)
	{
	  TXT_CONV_ITEM *const realloc_txt_conv_array = (TXT_CONV_ITEM *) realloc (txt_conv_array,
										   sizeof (TXT_CONV_ITEM) *
										   (txt_conv_max_items +
										    TXT_CONV_ITEM_GROW_COUNT));
	  if (realloc_txt_conv_array == NULL)
	    {
	      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
	      status = ER_LOC_GEN;
	      goto error;
	    }
	  else
	    {
	      txt_conv_array = realloc_txt_conv_array;
	    }

	  txt_conv_max_items += TXT_CONV_ITEM_GROW_COUNT;
	}

      txt_conv_array[txt_conv_count_items].text_cp = cp_text;
      txt_conv_array[txt_conv_count_items].unicode_cp = cp_unicode;

      min_values.text_cp = MIN (min_values.text_cp, cp_text);
      min_values.unicode_cp = MIN (min_values.unicode_cp, cp_unicode);

      max_values.text_cp = MAX (max_values.text_cp, cp_text);
      max_values.unicode_cp = MAX (max_values.unicode_cp, cp_unicode);

      txt_conv_count_items++;
    }

  if (is_verbose)
    {
      printf ("Building %d console conversion rules\n", txt_conv_count_items);
    }

  /* build conversions */
  ld->txt_conv.text_first_cp = min_values.text_cp;
  ld->txt_conv.text_last_cp = max_values.text_cp;

  ld->txt_conv.utf8_first_cp = min_values.unicode_cp;
  ld->txt_conv.utf8_last_cp = max_values.unicode_cp;

  assert (ld->txt_conv.text_first_cp < ld->txt_conv.text_last_cp);
  assert (ld->txt_conv.utf8_first_cp < ld->txt_conv.utf8_last_cp);

  if (ld->txt_conv.text_last_cp == 0 || ld->txt_conv.utf8_last_cp == 0)
    {
      LOG_LOCALE_ERROR ("Invalid console mapping", ER_LOC_GEN, true);
      status = ER_LOC_GEN;
      goto error;
    }

  ld->txt_conv.text_to_utf8 =
    (CONV_CP_TO_BYTES *) malloc (sizeof (CONV_CP_TO_BYTES) *
				 (ld->txt_conv.text_last_cp - ld->txt_conv.text_first_cp + 1));
  memset (ld->txt_conv.text_to_utf8, 0,
	  sizeof (CONV_CP_TO_BYTES) * (ld->txt_conv.text_last_cp - ld->txt_conv.text_first_cp + 1));

  ld->txt_conv.utf8_to_text =
    (CONV_CP_TO_BYTES *) malloc (sizeof (CONV_CP_TO_BYTES) *
				 (ld->txt_conv.utf8_last_cp - ld->txt_conv.utf8_first_cp + 1));
  memset (ld->txt_conv.utf8_to_text, 0,
	  sizeof (CONV_CP_TO_BYTES) * (ld->txt_conv.utf8_last_cp - ld->txt_conv.utf8_first_cp + 1));

  if (ld->txt_conv.text_to_utf8 == NULL || ld->txt_conv.utf8_to_text == NULL)
    {
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      status = ER_LOC_GEN;
      goto error;
    }

  for (i = 0; i < ld->txt_conv.text_last_cp - ld->txt_conv.text_first_cp + 1; i++)
    {
      ld->txt_conv.text_to_utf8[i].size = 1;
      *(ld->txt_conv.text_to_utf8[i].bytes) = '?';
    }

  for (i = 0; i < ld->txt_conv.utf8_last_cp - ld->txt_conv.utf8_first_cp + 1; i++)
    {
      ld->txt_conv.utf8_to_text[i].size = 1;
      *(ld->txt_conv.utf8_to_text[i].bytes) = '?';
    }

  for (i = 0; (int) i < txt_conv_count_items; i++)
    {
      CONV_CP_TO_BYTES *text_to_utf8_item = NULL;
      CONV_CP_TO_BYTES *utf8_to_text_item = NULL;

      cp_text = txt_conv_array[i].text_cp;
      cp_unicode = txt_conv_array[i].unicode_cp;

      assert (cp_text >= ld->txt_conv.text_first_cp && cp_text <= ld->txt_conv.text_last_cp);

      assert (cp_unicode >= ld->txt_conv.utf8_first_cp && cp_unicode <= ld->txt_conv.utf8_last_cp);

      text_to_utf8_item = &(ld->txt_conv.text_to_utf8[cp_text - ld->txt_conv.text_first_cp]);
      utf8_to_text_item = &(ld->txt_conv.utf8_to_text[cp_unicode - ld->txt_conv.utf8_first_cp]);

      text_to_utf8_item->size = intl_cp_to_utf8 (cp_unicode, text_to_utf8_item->bytes);

      if (ld->txt_conv_prm.conv_type == TEXT_CONV_GENERIC_1BYTE)
	{
	  utf8_to_text_item->size = 1;
	  utf8_to_text_item->bytes[0] = cp_text;
	}
      else
	{
	  assert (ld->txt_conv_prm.conv_type == TEXT_CONV_GENERIC_2BYTE);
	  utf8_to_text_item->size = intl_cp_to_dbcs (cp_text, ld->txt_conv.byte_flag, utf8_to_text_item->bytes);
	}
    }

  assert (fp != NULL);
  fclose (fp);

  assert (txt_conv_array != NULL);
  free (txt_conv_array);

  ld->txt_conv.conv_type = ld->txt_conv_prm.conv_type;
  ld->txt_conv.win_codepages = strdup (ld->txt_conv_prm.win_codepages);
  ld->txt_conv.nl_lang_str = strdup (ld->txt_conv_prm.nl_lang_str);
  if (ld->txt_conv.nl_lang_str == NULL)
    {
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      status = ER_LOC_GEN;
      goto error;
    }

  return status;

error:

  if (fp != NULL)
    {
      fclose (fp);
    }

  if (txt_conv_array != NULL)
    {
      free (txt_conv_array);
    }

  return status;
}

/*
 * comp_func_parse_order_index - compare function for sorting parse order
 *		    indexes for month and weekday names, based on the reversed
 *		    binary order of the LDML tokens
 *
 *  Note: this function is used by the genlocale tool
 *	  The elements in the arrays should be NUL-terminated strings, stored
 *	  in fixed length char arrays. Encoding is not relevant, the purpose
 *	  of this comparison is to make sure that the tokenizer first tries
 *	  to match some tokens before others which can be prefixes for them.
 *	  E.g. In Vietnamese(vi_VN), the October translation followed by
 *	  a space is a prefix for the translations of November and December.
 */
static int
comp_func_parse_order_index (const void *arg1, const void *arg2)
{
  char *s1;
  char *s2;
  char pos1;
  char pos2;
  int cmp;

  pos1 = *((char *) arg1);
  pos2 = *((char *) arg2);
  s1 = (char *) (cmp_token_name_array + pos1 * cmp_token_name_size);
  s2 = (char *) (cmp_token_name_array + pos2 * cmp_token_name_size);

  cmp = strcmp (s2, s1);

  return (cmp == 0) ? (pos1 - pos2) : cmp;
}

/*
 * locale_make_calendar_parse_order() - computes the order in which the
 *		      string tokenizer should search for matches with
 *		      weekday and month names, both wide and abbreviated.
 *
 * return:
 * ld(in/out): LOCALE_DATA for which to do the processing
 */
static void
locale_make_calendar_parse_order (LOCALE_DATA * ld)
{
  int i;
  for (i = 0; i < CAL_MONTH_COUNT; i++)
    {
      ld->month_names_abbr_parse_order[i] = i;
      ld->month_names_wide_parse_order[i] = i;
    }
  for (i = 0; i < CAL_DAY_COUNT; i++)
    {
      ld->day_names_abbr_parse_order[i] = i;
      ld->day_names_wide_parse_order[i] = i;
    }
  for (i = 0; i < CAL_AM_PM_COUNT; i++)
    {
      ld->am_pm_parse_order[i] = i;
    }

  cmp_token_name_array = (char *) (ld->month_names_abbreviated);
  cmp_token_name_size = LOC_DATA_MONTH_ABBR_SIZE;
  qsort (ld->month_names_abbr_parse_order, CAL_MONTH_COUNT, sizeof (char), comp_func_parse_order_index);

  cmp_token_name_array = (char *) (ld->month_names_wide);
  cmp_token_name_size = LOC_DATA_MONTH_WIDE_SIZE;
  qsort (ld->month_names_wide_parse_order, CAL_MONTH_COUNT, sizeof (char), comp_func_parse_order_index);

  cmp_token_name_array = (char *) (ld->day_names_abbreviated);
  cmp_token_name_size = LOC_DATA_DAY_ABBR_SIZE;
  qsort (ld->day_names_abbr_parse_order, CAL_DAY_COUNT, sizeof (char), comp_func_parse_order_index);

  cmp_token_name_array = (char *) (ld->day_names_wide);
  cmp_token_name_size = LOC_DATA_DAY_WIDE_SIZE;
  qsort (ld->day_names_wide_parse_order, CAL_DAY_COUNT, sizeof (char), comp_func_parse_order_index);

  cmp_token_name_array = (char *) (ld->am_pm);
  cmp_token_name_size = LOC_DATA_AM_PM_SIZE;
  qsort (ld->am_pm_parse_order, CAL_AM_PM_COUNT, sizeof (char), comp_func_parse_order_index);
}

/*
 * locale_init_data() - initializes one locale data
 *
 * return:
 * ld(in/out):
 * locale_name(in):
 */
void
locale_init_data (LOCALE_DATA * ld, const char *locale_name)
{
  memset (ld, 0, sizeof (LOCALE_DATA));
  ld->curr_period = -1;

  assert (strlen (locale_name) < (int) sizeof (ld->locale_name));
  strcpy (ld->locale_name, locale_name);

  /* default number symbols */
  ld->number_decimal_sym = ',';
  ld->number_group_sym = '.';
  ld->default_currency_code = DB_CURRENCY_NULL;

  ld->alpha_tailoring.sett_max_letters = -1;
}

/*
 * locale_destroy_data() - frees memory allocated for one locale data
 *
 * return:
 * ld(in/out):
 */
void
locale_destroy_data (LOCALE_DATA * ld)
{
  int i;

  assert (ld != NULL);

  /* collations */
  for (i = 0; i < ld->coll_cnt; i++)
    {
      locale_destroy_collation_data (&(ld->collations[i].opt_coll));
      locale_destroy_collation_tailorings (&(ld->collations[i].tail_coll));
    }

  free (ld->collations);
  ld->collations = NULL;
  ld->coll_cnt = 0;

  /* alphabet tailoring */
  locale_destroy_alphabet_tailoring (&(ld->alpha_tailoring));

  /* alphabets */
  locale_destroy_alphabet_data (&(ld->alphabet));
  locale_destroy_alphabet_data (&(ld->identif_alphabet));

  locale_destroy_console_conversion (&(ld->txt_conv));

  locale_destroy_normalization_data (&(ld->unicode_normalization));

  /* ldml context file path */
  if (ld->ldml_context.ldml_file != NULL)
    {
      free (ld->ldml_context.ldml_file);
    }
}

/*
 * locale_destroy_alphabet_data() - frees memory for one locale alphabet
 *    return:
 *    a(in/out):
 *
 *    Note : Alphabets that are common are not freed
 */
void
locale_destroy_alphabet_data (const ALPHABET_DATA * a)
{
  assert (a != NULL);

  if (a->lower_cp != NULL)
    {
      free (a->lower_cp);
    }

  if (a->upper_cp != NULL)
    {
      free (a->upper_cp);
    }

  memset ((void *) a, 0, sizeof (ALPHABET_DATA));
}

/*
 * locale_destroy_collation_tailorings() - frees memory for one locale
 *					   collation tailorings
 *    return:
 *    ct(in/out):
 */
static void
locale_destroy_collation_tailorings (const COLL_TAILORING * ct)
{
  int i;

  assert (ct != NULL);

  /* collation tailorings */
  for (i = 0; i < ct->count_rules; i++)
    {
      TAILOR_RULE *t_rule = &(ct->rules[i]);

      assert (t_rule != NULL);
      assert (t_rule->t_buf != NULL);

      if (t_rule->t_buf != NULL)
	{
	  assert (t_rule->t_buf_size > 0);
	  free (t_rule->t_buf);
	}

      if (t_rule->r_buf != NULL)
	{
	  assert (t_rule->r_buf_size > 0);
	  free (t_rule->r_buf);
	}

      memset (t_rule, 0, sizeof (TAILOR_RULE));
    }

  if (ct->rules != NULL)
    {
      assert (ct->max_rules > 0);
      free (ct->rules);
    }

  if (ct->cub_rules != NULL)
    {
      assert (ct->cub_max_rules > 0);
      free (ct->cub_rules);
    }

  if (ct->ldml_context.ldml_file != NULL)
    {
      free (ct->ldml_context.ldml_file);
    }

  memset ((void *) ct, 0, sizeof (COLL_TAILORING));
}

/*
 * locale_destroy_collation_data() - frees memory for collation data
 *    return:
 *    cd(in/out):
 */
static void
locale_destroy_collation_data (const COLL_DATA * cd)
{
  assert (cd != NULL);

  /* collation data */
  if (cd->weights != NULL)
    {
      free (cd->weights);
    }

  if (cd->next_cp != NULL)
    {
      free (cd->next_cp);
    }

  /* contractions */
  if (cd->uca_num != NULL)
    {
      free (cd->uca_num);
    }

  if (cd->uca_w_l13 != NULL)
    {
      free (cd->uca_w_l13);
    }

  if (cd->uca_w_l4 != NULL)
    {
      free (cd->uca_w_l4);
    }

  if (cd->cp_first_contr_array != NULL)
    {
      free (cd->cp_first_contr_array);
    }

  if (cd->contr_list != NULL)
    {
      free (cd->contr_list);
    }

  memset ((void *) cd, 0, sizeof (COLL_DATA));
}

/*
 * locale_destroy_alphabet_tailoring() - frees memory for alphabet tailorings
 *    return:
 *    at(in/out):
 */
static void
locale_destroy_alphabet_tailoring (const ALPHABET_TAILORING * at)
{
  int i;

  assert (at != 0);

  /* case tailoring */
  for (i = 0; i < at->count_rules; i++)
    {
      TRANSFORM_RULE *tf_rule = &(at->rules[i]);

      assert (tf_rule != NULL);
      assert (tf_rule->src != NULL);
      assert (tf_rule->dest != NULL);

      if (tf_rule->src != NULL)
	{
	  assert (tf_rule->src_size > 0);
	  free (tf_rule->src);
	}

      if (tf_rule->dest != NULL)
	{
	  assert (tf_rule->dest_size > 0);
	  free (tf_rule->dest);
	}

      memset (tf_rule, 0, sizeof (TRANSFORM_RULE));
    }

  if (at->rules != NULL)
    {
      assert (at->max_rules > 0);
      free (at->rules);
    }

  if (at->ldml_context.ldml_file != NULL)
    {
      free (at->ldml_context.ldml_file);
    }

  memset ((void *) at, 0, sizeof (ALPHABET_TAILORING));
}

/*
 * locale_destroy_console_conversion() - frees memory for console conversion
 *    return:
 *    tc(in/out):
 */
static void
locale_destroy_console_conversion (const TEXT_CONVERSION * tc)
{
  assert (tc != 0);

  /* console conversion */
  if (tc->nl_lang_str != NULL)
    {
      free (tc->nl_lang_str);
    }

  if (tc->win_codepages != NULL)
    {
      free (tc->win_codepages);
    }

  if (tc->text_to_utf8 != NULL)
    {
      free (tc->text_to_utf8);
    }

  if (tc->utf8_to_text != NULL)
    {
      free (tc->utf8_to_text);
    }

  memset ((void *) tc, 0, sizeof (TEXT_CONVERSION));
}

/*
 * locale_compile_locale() - process locale data loaded from LDML file(s)
 *
 * return: error code
 * lf(in): structure containing file paths
 * ld(in): locale data to be processed
 * is_verbose(in):
 */
int
locale_compile_locale (LOCALE_FILE * lf, LOCALE_DATA * ld, bool is_verbose)
{
  XML_PARSER_DATA ldml_parser;
  LOC_SHARED_DATA *norm_sh_entry = NULL;
  char *locale_str = NULL;
  int er_status = NO_ERROR;
  int i;

  assert (lf != NULL);
  locale_str = lf->locale_name;

  if (is_verbose)
    {
      printf ("\n*** Parsing LDML\n");
    }

  locale_init_data (ld, locale_str);
  ldml_parser.ud = ld;
  ldml_parser.verbose = is_verbose;

  ld->ldml_context.ldml_file = strdup (lf->ldml_file);
  ld->ldml_context.line_no = 1;

  ldml_parser.xml_parser =
    xml_init_parser (&ldml_parser, lf->ldml_file, "UTF-8", ldml_elements,
		     sizeof (ldml_elements) / sizeof (XML_ELEMENT_DEF *));

  if (ldml_parser.xml_parser == NULL)
    {
      LOG_LOCALE_ERROR ("Cannot init XML parser", ER_LOC_GEN, true);

      er_status = ER_LOC_GEN;
      goto exit;
    }

  xml_parser_exec (&ldml_parser);

  if (ldml_parser.xml_error == XML_CUB_NO_ERROR)
    {
      if (is_verbose)
	{
	  printf ("Parsing finished.\n");
	  printf ("Date format: %s\n", ld->dateFormat);
	  printf ("Time format: %s\n", ld->timeFormat);
	  printf ("Datetime format: %s\n", ld->datetimeFormat);
	  printf ("Timestamp format: %s\n", ld->timestampFormat);
	  printf ("Timetz format: %s\n", ld->timetzFormat);
	  printf ("Datetimetz format: %s\n", ld->datetimetzFormat);
	  printf ("Timestamptz format: %s\n", ld->timestamptzFormat);
	}
    }
  else
    {
      char msg[ERR_MSG_SIZE];
      const char *xml_err_text = (char *) XML_ErrorString (XML_GetErrorCode (ldml_parser.xml_parser));

      snprintf_dots_truncate (msg, sizeof (msg) - 1,
			      "Error parsing file %s, " "line : %d, column : %d. Internal XML: %s",
			      ldml_parser.filepath, ldml_parser.xml_error_line, ldml_parser.xml_error_column,
			      xml_err_text);

      LOG_LOCALE_ERROR (msg, ER_LOC_GEN, true);
      er_status = ER_LOC_GEN;
      goto exit;
    }

  locale_make_calendar_parse_order (ld);

  if (ld->alpha_tailoring.sett_max_letters == -1)
    {
      ld->alpha_tailoring.sett_max_letters = MAX_UNICODE_CHARS;
    }

  if (is_verbose)
    {
      printf ("\n*** Processing alphabet ***\nNumber of letters  = %d\n", ld->alpha_tailoring.sett_max_letters);
    }

  if (ld->alpha_tailoring.sett_max_letters == 0)
    {
      ld->alphabet.l_count = ld->alpha_tailoring.sett_max_letters;
    }
  else
    {
      LOC_SHARED_DATA *alphabet_sh_entry = NULL;
      LDML_CONTEXT *ldml_context = &(ld->alpha_tailoring.ldml_context);

      er_status = unicode_process_alphabet (ld, is_verbose);
      if (er_status != NO_ERROR)
	{
	  goto exit;
	}

      if (ld->alphabet.a_type == ALPHABET_UNICODE)
	{
	  er_status =
	    locale_check_and_set_shared_data (LOC_SHARED_ALPHABET, "unicode", NULL, ldml_context, &alphabet_sh_entry);
	}
      else if (ld->alphabet.a_type == ALPHABET_ASCII)
	{
	  er_status =
	    locale_check_and_set_shared_data (LOC_SHARED_ALPHABET, "ascii", NULL, ldml_context, &alphabet_sh_entry);
	}

      if (er_status != NO_ERROR)
	{
	  goto exit;
	}

      ld->alphabet.do_not_save = (alphabet_sh_entry != NULL) ? true : false;

      alphabet_sh_entry = NULL;
      if (ld->identif_alphabet.a_type == ALPHABET_UNICODE)
	{
	  er_status =
	    locale_check_and_set_shared_data (LOC_SHARED_ALPHABET, "unicode", NULL, ldml_context, &alphabet_sh_entry);
	}
      else if (ld->identif_alphabet.a_type == ALPHABET_ASCII)
	{
	  er_status =
	    locale_check_and_set_shared_data (LOC_SHARED_ALPHABET, "ascii", NULL, ldml_context, &alphabet_sh_entry);
	}

      if (er_status != NO_ERROR)
	{
	  goto exit;
	}

      ld->identif_alphabet.do_not_save = (alphabet_sh_entry != NULL) ? true : false;
      if (ld->identif_alphabet.do_not_save == false)
	{
	  /* check that casing data matches the rule of INTL_IDENTIFIER_CASING_SIZE_MULTIPLIER: the ratio between the
	   * size in bytes of lower (or upper) case character (or sequence of characters) and the size in bytes of
	   * original character is less than INTL_IDENTIFIER_CASING_SIZE_MULTIPLIER */
	  unsigned int cp;
	  ALPHABET_DATA *a = &(ld->identif_alphabet);
	  unsigned char dummy[INTL_UTF8_MAX_CHAR_SIZE];

	  for (cp = 0; (int) cp < a->l_count; cp++)
	    {
	      int case_cnt, case_size, cp_size;
	      unsigned int *lower_cp = &(a->lower_cp[cp * a->lower_multiplier]);
	      unsigned int *upper_cp = &(a->upper_cp[cp * a->upper_multiplier]);

	      for (case_size = 0, case_cnt = 0; case_cnt < a->lower_multiplier && *lower_cp != 0;
		   case_cnt++, lower_cp++)
		{
		  if (*lower_cp == cp)
		    {
		      /* character does not have lower case */
		      case_size = 0;
		      break;
		    }
		  case_size += intl_cp_to_utf8 (*lower_cp, dummy);
		}

	      if (case_size > 0)
		{
		  cp_size = intl_cp_to_utf8 (cp, dummy);
		  if (case_size > cp_size * INTL_IDENTIFIER_CASING_SIZE_MULTIPLIER)
		    {
		      char msg[ERR_MSG_SIZE];

		      snprintf (msg, sizeof (msg) - 1,
				"Lower case " "sequence for codepoint Ux%04X is too "
				"big: %d bytes; expecting at most %d", cp, case_size,
				cp_size * INTL_IDENTIFIER_CASING_SIZE_MULTIPLIER);
		      LOG_LOCALE_ERROR (msg, ER_LOC_GEN, true);
		      er_status = ER_LOC_GEN;
		      goto exit;
		    }
		}

	      for (case_size = 0, case_cnt = 0; case_cnt < a->upper_multiplier && *upper_cp != 0;
		   case_cnt++, upper_cp++)
		{
		  if (*upper_cp == cp)
		    {
		      /* character does not have upper case */
		      case_size = 0;
		      break;
		    }
		  case_size += intl_cp_to_utf8 (*upper_cp, dummy);
		}

	      if (case_size > 0)
		{
		  cp_size = intl_cp_to_utf8 (cp, dummy);
		  if (case_size > cp_size * INTL_IDENTIFIER_CASING_SIZE_MULTIPLIER)
		    {
		      char msg[ERR_MSG_SIZE];

		      snprintf (msg, sizeof (msg) - 1,
				"Upper case " "sequence for codepoint Ux%04X is too "
				"big: %d bytes; expecting at most %d", cp, case_size,
				cp_size * INTL_IDENTIFIER_CASING_SIZE_MULTIPLIER);
		      LOG_LOCALE_ERROR (msg, ER_LOC_GEN, true);
		      er_status = ER_LOC_GEN;
		      goto exit;
		    }
		}
	    }
	}
    }

  if (is_verbose)
    {
      printf ("\n*** Processing Unicode normalization data ***\n");
    }

  er_status = locale_check_and_set_shared_data (LOC_SHARED_NORMALIZATION, "normalization", NULL, NULL, &norm_sh_entry);

  ld->unicode_normalization.do_not_save = (norm_sh_entry != NULL) ? true : false;

  if (norm_sh_entry == NULL)
    {
      /* not shared, process normalization */
      er_status = unicode_process_normalization (ld, is_verbose);
      if (er_status != NO_ERROR)
	{
	  goto exit;
	}
    }

  if (is_verbose)
    {
      printf ("\n*** Processing console conversion data ***\n");
    }

  er_status = load_console_conv_data (ld, is_verbose);
  if (er_status != NO_ERROR)
    {
      goto exit;
    }

  if (ld->coll_cnt == 0 && is_verbose)
    {
      printf ("\n*** No collation defined ***\n");
    }

  for (i = 0; i < ld->coll_cnt; i++)
    {
      LOC_SHARED_DATA *coll_sh_entry = NULL;
      LOCALE_COLLATION *lc = &(ld->collations[i]);
      char shared_coll_data[COLL_SHARED_DATA_SIZE];


      strncpy (lc->opt_coll.coll_name, lc->tail_coll.coll_name, COLL_NAME_SIZE);
      lc->opt_coll.coll_name[COLL_NAME_SIZE - 1] = '\0';

      if (!lang_is_coll_name_allowed (lc->opt_coll.coll_name))
	{
	  char msg[ERR_MSG_SIZE];

	  snprintf (msg, sizeof (msg) - 1,
		    "Collation name %s " "not allowed. It cannot contain white-spaces "
		    "or have same name as a built-in collation", lc->opt_coll.coll_name);

	  LOG_LOCALE_ERROR (msg, ER_LOC_GEN, true);
	  er_status = ER_LOC_GEN;
	  goto exit;
	}

      snprintf (shared_coll_data, sizeof (shared_coll_data) - 1, "%d", lc->tail_coll.coll_id);

      er_status =
	locale_check_and_set_shared_data (LOC_SHARED_COLLATION, lc->tail_coll.coll_name, &shared_coll_data,
					  &(lc->tail_coll.ldml_context), &coll_sh_entry);
      if (er_status != NO_ERROR)
	{
	  goto exit;
	}

      lc->do_not_save = (coll_sh_entry != NULL) ? true : false;

      if (coll_sh_entry != NULL)
	{
	  /* check found entry */
	  const char *coll_sh_data = (const char *) coll_sh_entry->data;

	  assert (coll_sh_data != NULL);

	  if (strcmp (shared_coll_data, coll_sh_data) != 0)
	    {
	      /* found collation with same name buf different collation shared data (collation id) */
	      char msg[ERR_MSG_SIZE + 2 * PATH_MAX];

	      snprintf (msg, sizeof (msg) - 1,
			"File: %s, line: %d : " "collation name %s (id:%d) already defined in "
			"file: %s, line: %d with id: %s", lc->tail_coll.ldml_context.ldml_file,
			lc->tail_coll.ldml_context.line_no, lc->tail_coll.coll_name, lc->tail_coll.coll_id,
			coll_sh_entry->ldml_context.ldml_file, coll_sh_entry->ldml_context.line_no, coll_sh_data);

	      LOG_LOCALE_ERROR (msg, ER_LOC_GEN, true);
	      er_status = ER_LOC_GEN;
	      goto exit;
	    }
	  else if (strcmp (lc->tail_coll.ldml_context.ldml_file, coll_sh_entry->ldml_context.ldml_file) != 0
		   || lc->tail_coll.ldml_context.line_no != coll_sh_entry->ldml_context.line_no)
	    {
	      /* same collation, just put an debug info */
	      printf ("Warning : Definition of collation %s (id: %d) " "ignored in file: %s, line: %d. "
		      "Using the definition from file: %s, line: %d.\n", lc->tail_coll.coll_name, lc->tail_coll.coll_id,
		      lc->tail_coll.ldml_context.ldml_file, lc->tail_coll.ldml_context.line_no,
		      coll_sh_entry->ldml_context.ldml_file, coll_sh_entry->ldml_context.line_no);
	    }

	  continue;
	}

      if (lc->tail_coll.sett_max_cp == -1)
	{
	  lc->tail_coll.sett_max_cp = MAX_UNICODE_CHARS;
	}

      if (is_verbose)
	{
	  printf ("\n*** Processing collation : %s ***\n" "Number of weights  = %d\n", lc->tail_coll.coll_name,
		  lc->tail_coll.sett_max_cp);
	}

      if (lc->tail_coll.sett_max_cp == 0)
	{
	  lc->opt_coll.w_count = lc->tail_coll.sett_max_cp;
	}
      else
	{
	  er_status = uca_process_collation (lc, is_verbose);
	  if (er_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  if (lc->tail_coll.coll_id <= 0)
	    {
	      locale_alloc_collation_id (&lc->tail_coll);
	    }

	  er_status = locale_check_collation_id (&lc->tail_coll);
	  if (er_status != NO_ERROR)
	    {
	      goto exit;
	    }
	  lc->opt_coll.coll_id = lc->tail_coll.coll_id;
	}

      er_status = locale_compute_coll_checksum (&(lc->opt_coll));
      if (er_status != NO_ERROR)
	{
	  goto exit;
	}
      if (is_verbose)
	{
	  printf ("ID : %d | Checksum : %s\n", lc->opt_coll.coll_id, lc->opt_coll.checksum);
	}
    }

  er_status = locale_compute_locale_checksum (ld);
  if (er_status != NO_ERROR)
    {
      goto exit;
    }

  if (is_verbose)
    {
      printf ("\n Locale checksum :%s\n", ld->checksum);
    }

exit:
  xml_destroy_parser (&ldml_parser);
  uca_free_data ();
  unicode_free_data ();

  if (is_verbose)
    {
      printf ("\n\nLocale finished\n\n\n");
    }

  return er_status;
}

/*
 * locale_mark_duplicate_collations - finds duplicate arrays within all
 *			collations and uses string identifiers from
 *			LOCALE_DATA structure to mark the arrays which will
 *			not be exported to the shared library, but another
 *			identical array will be loaded instead.
 *
 * return: error status
 *
 * ld(in): all locale data loaded from LDML and compiled
 * start_index(in): index of the first working locale (in ld)
 * end_index(in): index of the last working locale stored in the ld array
 */
void
locale_mark_duplicate_collations (LOCALE_DATA ** ld, int start_index, int end_index, bool is_verbose)
{
#define PRINT_DUPLICATE_SYMBOL_NAME(symbol, dupl_coll, orig_coll, do_print) \
  if (do_print)								    \
    {									    \
      printf ("\n *** Duplicate array: %s in collation %s is a duplicate "  \
	      "of %s from collation %s", (symbol),			    \
	      (dupl_coll)->opt_coll.coll_name,				    \
	      (symbol), (orig_coll)->opt_coll.coll_name);		    \
    }

  int i1, i2, j1, j2;
  LOCALE_COLLATION *cur_coll, *comp_coll;
  LOCALE_DATA *cur_ld, *comp_ld;

  /* Search for duplicate arrays. In order to avoid errors, search must be done backwards for the cursor collation,
   * while searching forward in the locales; the rule of determining the identifier to be stored is that the first
   * occurence is considered as the original, and all identical arrays that follow are duplicates */
  for (i1 = end_index - 1; i1 >= start_index; i1--)
    {
      cur_ld = ld[i1];
      for (j1 = cur_ld->coll_cnt - 1; j1 >= 0; j1--)
	{
	  cur_coll = &(cur_ld->collations[j1]);

	  for (i2 = start_index; i2 <= i1; i2++)
	    {
	      comp_ld = ld[i2];
	      for (j2 = 0; (i1 == i2 && j2 < j1) || (i1 > i2 && j2 < comp_ld->coll_cnt); j2++)
		{
		  comp_coll = &(comp_ld->collations[j2]);
		  /* Two COLL_INFO structures are refferenced.Compare them. */

		  if (cur_coll->opt_coll.w_count != comp_coll->opt_coll.w_count)
		    {
		      continue;
		    }

		  if (cur_coll->opt_coll.uca_opt.sett_expansions == comp_coll->opt_coll.uca_opt.sett_expansions)
		    {
		      if (!cur_coll->opt_coll.uca_opt.sett_expansions)
			{
			  if (*(cur_coll->coll_ref.coll_weights_ref) == '\0'
			      && memcmp (cur_coll->opt_coll.weights, comp_coll->opt_coll.weights,
					 sizeof (comp_coll->opt_coll.weights[0]) * cur_coll->opt_coll.w_count) == 0)
			    {
			      sprintf (cur_coll->coll_ref.coll_weights_ref, "%s_%s", "coll_weights",
				       comp_coll->opt_coll.coll_name);
			      PRINT_DUPLICATE_SYMBOL_NAME ("coll_weights", cur_coll, comp_coll, is_verbose);
			    }
			}
		      else
			{
			  if (cur_coll->opt_coll.uca_exp_num != comp_coll->opt_coll.uca_exp_num)
			    {
			      continue;
			    }

			  if (*(cur_coll->coll_ref.coll_uca_num_ref) == '\0'
			      && memcmp (cur_coll->opt_coll.uca_num, comp_coll->opt_coll.uca_num,
					 sizeof (comp_coll->opt_coll.uca_num[0]) * comp_coll->opt_coll.w_count) == 0)
			    {
			      sprintf (cur_coll->coll_ref.coll_uca_num_ref, "%s_%s", "coll_uca_num",
				       comp_coll->opt_coll.coll_name);
			      PRINT_DUPLICATE_SYMBOL_NAME ("coll_uca_num", cur_coll, comp_coll, is_verbose);
			    }

			  if (*(cur_coll->coll_ref.coll_uca_w_l13_ref) == '\0'
			      && memcmp (cur_coll->opt_coll.uca_w_l13, comp_coll->opt_coll.uca_w_l13,
					 sizeof (UCA_L13_W) * cur_coll->opt_coll.uca_exp_num *
					 cur_coll->opt_coll.w_count) == 0)
			    {
			      sprintf (cur_coll->coll_ref.coll_uca_w_l13_ref, "%s_%s", "coll_uca_w_l13",
				       comp_coll->opt_coll.coll_name);
			      PRINT_DUPLICATE_SYMBOL_NAME ("coll_uca_w_l13", cur_coll, comp_coll, is_verbose);
			    }
			  if (cur_coll->opt_coll.uca_opt.sett_strength >= TAILOR_QUATERNARY
			      && comp_coll->opt_coll.uca_opt.sett_strength >= TAILOR_QUATERNARY
			      && *(cur_coll->coll_ref.coll_uca_w_l4_ref) == '\0'
			      && memcmp (cur_coll->opt_coll.uca_w_l4, comp_coll->opt_coll.uca_w_l4,
					 sizeof (UCA_L4_W) * cur_coll->opt_coll.uca_exp_num *
					 cur_coll->opt_coll.w_count) == 0)
			    {
			      sprintf (cur_coll->coll_ref.coll_uca_w_l4_ref, "%s_%s", "coll_uca_w_l4",
				       comp_coll->opt_coll.coll_name);
			      PRINT_DUPLICATE_SYMBOL_NAME ("coll_uca_w_l4", cur_coll, comp_coll, is_verbose);
			    }
			}
		    }		/* endif equal sett_expansions */

		  if (*(cur_coll->coll_ref.coll_next_cp_ref) == '\0'
		      && memcmp (cur_coll->opt_coll.next_cp, comp_coll->opt_coll.next_cp,
				 sizeof (comp_coll->opt_coll.next_cp[0]) * comp_coll->opt_coll.w_count) == 0)
		    {
		      sprintf (cur_coll->coll_ref.coll_next_cp_ref, "%s_%s", "coll_next_cp",
			       comp_coll->opt_coll.coll_name);
		      PRINT_DUPLICATE_SYMBOL_NAME ("coll_next_cp", cur_coll, comp_coll, is_verbose);
		    }

		  /* compare contraction data */
		  if (cur_coll->opt_coll.count_contr > 0
		      && cur_coll->opt_coll.count_contr == comp_coll->opt_coll.count_contr)
		    {
		      if (cur_coll->opt_coll.contr_min_size == comp_coll->opt_coll.contr_min_size
			  && *(cur_coll->coll_ref.coll_contr_list_ref) == '\0'
			  && memcmp (cur_coll->opt_coll.contr_list, comp_coll->opt_coll.contr_list,
				     sizeof (COLL_CONTRACTION) * comp_coll->opt_coll.count_contr) == 0)
			{
			  sprintf (cur_coll->coll_ref.coll_contr_list_ref, "%s_%s", "coll_contr_list",
				   comp_coll->opt_coll.coll_name);
			  PRINT_DUPLICATE_SYMBOL_NAME ("coll_contr_list", cur_coll, comp_coll, is_verbose);
			}

		      if (cur_coll->opt_coll.cp_first_contr_offset == comp_coll->opt_coll.cp_first_contr_offset
			  && cur_coll->opt_coll.cp_first_contr_count == comp_coll->opt_coll.cp_first_contr_count
			  && *(cur_coll->coll_ref.coll_cp_first_contr_array_ref) == '\0'
			  && memcmp (cur_coll->opt_coll.cp_first_contr_array, comp_coll->opt_coll.cp_first_contr_array,
				     sizeof (comp_coll->opt_coll.cp_first_contr_array[0]) *
				     comp_coll->opt_coll.w_count) == 0)
			{
			  sprintf (cur_coll->coll_ref.coll_cp_first_contr_array_ref, "%s_%s",
				   "coll_cp_first_contr_array", comp_coll->opt_coll.coll_name);
			  PRINT_DUPLICATE_SYMBOL_NAME ("coll_cp_first_" "contr_array", cur_coll, comp_coll, is_verbose);
			}
		    }
		}
	    }
	}
    }
#undef PRINT_DUPLICATE_SYMBOL_NAME
}

/*
 * locale_save_all_to_C_file - saves all locale data to C source file, for
 *			    later use when compiling them into a locale shared
 *			    library.
 *
 * return: error code
 * ld(in): locale data
 * start_index(in): starting index inside ld array for locales to export
 * loc_count(in): number of locales stored in the ld array
 * lf(in): locale file info
 */
int
locale_save_all_to_C_file (LOCALE_DATA ** ld, int start_index, int end_index, LOCALE_FILE * lf)
{
  int er_status = NO_ERROR;
  int i;

  for (i = start_index; i < end_index; i++)
    {
      er_status = locale_save_to_C_file (lf[i], ld[i]);
      if (er_status != NO_ERROR)
	{
	  break;
	}
    }

  return er_status;
}

/*
 * locale_get_cfg_locales() - reads the locale strings and file paths configured
 *
 * return: error code
 * locale_files(in/out):
 * p_num_locales(out): number of user defined locales
 * is_lang_init(in): true if this is called in context of lang initialization
 *
 *  Note : This funtion is called in two contexts :
 *	    - language initialization (no error is set in this case)
 *	    - locale generation admin tool (error is generated)
 */
int
locale_get_cfg_locales (LOCALE_FILE ** p_locale_files, int *p_num_locales, bool is_lang_init)
{
  char locale_cfg_file[PATH_MAX];
  char line[1024];
  FILE *fp = NULL;
  LOCALE_FILE *locale_files = NULL;
  int num_locales;
  int max_locales = 10;
  char msg[ERR_MSG_SIZE];
  int err_status = NO_ERROR;

  assert (p_locale_files != NULL);
  assert (p_num_locales != NULL);

  envvar_confdir_file (locale_cfg_file, sizeof (locale_cfg_file), "cubrid_locales.txt");

  fp = fopen_ex (locale_cfg_file, "rt");
  if (fp == NULL)
    {
      if (is_lang_init)
	{
	  /* no error is recorded, 'cubrid_locales.txt' is optional */
	  goto exit;
	}
      else
	{
	  snprintf_dots_truncate (msg, sizeof (msg) - 1, "Cannot open file %s", locale_cfg_file);
	  LOG_LOCALE_ERROR (msg, ER_LOC_GEN, true);
	  err_status = ER_LOC_GEN;
	  goto exit;
	}
    }

  locale_files = NULL;
  num_locales = 0;

  locale_files = (LOCALE_FILE *) malloc (max_locales * sizeof (LOCALE_FILE));
  if (locale_files == NULL)
    {
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_INIT, true);
      err_status = ER_LOC_INIT;
      goto exit;
    }

  while (fgets (line, sizeof (line) - 1, fp) != NULL)
    {
      char *str, *next;
      LOCALE_FILE *loc;

      if (*line == '\0' || *line == '#' || char_isspace ((int) *line))
	{
	  continue;
	}

      num_locales++;

      if (num_locales >= max_locales)
	{
	  max_locales *= 2;
	  LOCALE_FILE *const realloc_locale_files
	    = (LOCALE_FILE *) realloc (locale_files, max_locales * sizeof (LOCALE_FILE));
	  if (realloc_locale_files == NULL)
	    {
	      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_INIT, true);
	      err_status = ER_LOC_INIT;
	      goto exit;
	    }
	  else
	    {
	      locale_files = realloc_locale_files;
	    }
	}

      loc = &(locale_files[num_locales - 1]);
      memset (loc, 0, sizeof (LOCALE_FILE));

      str = line;
      err_status = str_pop_token (str, &(loc->locale_name), &next);
      if (err_status != NO_ERROR)
	{
	  goto exit;
	}

      if (next == NULL)
	{
	  continue;
	}

      str = next;
      err_status = str_pop_token (str, &(loc->ldml_file), &next);
      if (err_status != NO_ERROR)
	{
	  goto exit;
	}

      if (next == NULL)
	{
	  continue;
	}

      str = next;
      err_status = str_pop_token (str, &(loc->lib_file), &next);
      if (err_status != NO_ERROR)
	{
	  goto exit;
	}
    }

  *p_locale_files = locale_files;
  *p_num_locales = num_locales;

exit:
  if (fp != NULL)
    {
      fclose (fp);
    }

  return err_status;
}

/*
 * locale_check_and_set_default_files() - checks if the files for locale is set
 *					  if not set, the default file paths are
 *					  computed
 *
 * return:
 * lf(in/out):
 * is_lang_init(in): true if this is called in context of lang initialization
 *
 *  Note : This funtion is called in two contexts :
 *	    - language initialization (no error is set in this case)
 *	    - locale generation admin tool (error is generated)
 */
int
locale_check_and_set_default_files (LOCALE_FILE * lf, bool is_lang_init)
{
  bool is_alloc_ldml_file = false;
  bool is_alloc_lib_file = false;
  int er_status = NO_ERROR;

  assert (lf != NULL);

  if (lf->locale_name == NULL || strlen (lf->locale_name) > LOC_LOCALE_STR_SIZE)
    {
      er_status = is_lang_init ? ER_LOC_INIT : ER_LOC_GEN;
      LOG_LOCALE_ERROR ("invalid locale name in 'cubrid_locales.txt'", er_status, true);
      goto error;
    }

  if (lf->ldml_file == NULL || *(lf->ldml_file) == '\0' || *(lf->ldml_file) == '*')
    {
      /* generate name for LDML file */
      char ldml_short_file[LOC_LOCALE_STR_SIZE + 13];

      snprintf (ldml_short_file, sizeof (ldml_short_file) - 1, "cubrid_%s.xml", lf->locale_name);
      ldml_short_file[sizeof (ldml_short_file) - 1] = '\0';

      if (lf->ldml_file != NULL)
	{
	  free (lf->ldml_file);
	}

      lf->ldml_file = (char *) malloc (PATH_MAX + 1);
      if (lf->ldml_file == NULL)
	{
	  er_status = is_lang_init ? ER_LOC_INIT : ER_LOC_GEN;
	  LOG_LOCALE_ERROR ("memory allocation failed", er_status, true);
	  goto error;
	}

      is_alloc_ldml_file = true;
      envvar_ldmldir_file (lf->ldml_file, PATH_MAX, ldml_short_file);
    }

  if (lf->lib_file == NULL || *(lf->lib_file) == '\0' || *(lf->lib_file) == '*')
    {
      /* generate name for locale lib file */
      char lib_short_file[PATH_MAX];

      snprintf (lib_short_file, sizeof (lib_short_file) - 1, "libcubrid_%s.%s", lf->locale_name, LOCLIB_FILE_EXT);
      lib_short_file[sizeof (lib_short_file) - 1] = '\0';

      if (lf->lib_file != NULL)
	{
	  free (lf->lib_file);
	}

      lf->lib_file = (char *) malloc (PATH_MAX + 1);
      if (lf->lib_file == NULL)
	{
	  er_status = is_lang_init ? ER_LOC_INIT : ER_LOC_GEN;
	  LOG_LOCALE_ERROR ("memory allocation failed", er_status, true);
	  goto error;
	}

      is_alloc_lib_file = true;
      envvar_libdir_file (lf->lib_file, PATH_MAX, lib_short_file);

      if (is_lang_init)
	{
	  FILE *fp;

	  /* check that default lib file exists, otherwise switch to the common libray locale */
	  fp = fopen_ex (lf->lib_file, "rb");
	  if (fp == NULL)
	    {
	      snprintf (lib_short_file, sizeof (lib_short_file) - 1, "libcubrid_all_locales.%s", LOCLIB_FILE_EXT);
	      lib_short_file[sizeof (lib_short_file) - 1] = '\0';

	      assert (lf->lib_file != NULL);

	      envvar_libdir_file (lf->lib_file, PATH_MAX, lib_short_file);
	    }
	  else
	    {
	      fclose (fp);
	    }
	}
    }

  return NO_ERROR;

error:

  if (is_alloc_ldml_file)
    {
      free (lf->ldml_file);
      lf->ldml_file = NULL;
    }

  if (is_alloc_lib_file)
    {
      free (lf->lib_file);
      lf->lib_file = NULL;
    }
  return er_status;
}

/*
 * save_contraction_to_C_file() - saves collation contraction data to C file
 *
 * return: zero if save is successful, non-zero otherwise
 * fp(in): file descriptor
 * c(in): contraction to save
 * use_expansion(in):
 * use_level_4(in):
 */
static int
save_contraction_to_C_file (FILE * fp, COLL_CONTRACTION * c, bool use_expansion, bool use_level_4)
{
  assert (c != NULL);
  assert (fp != NULL);

  fprintf (fp, "\t{\n");
  fprintf (fp, "\t\t%u, \n", c->next);
  fprintf (fp, "\t\t%u, \n", c->wv);

  if (use_expansion)
    {
      assert (c->uca_num > 0 || c->uca_num <= MAX_UCA_EXP_CE);

      PRINT_UNNAMED_NUM_ARRAY_TO_C_FILE (fp, "%uu", "\t\t", c->uca_num, c->uca_w_l13);
      fprintf (fp, ",\n");

      if (use_level_4)
	{
	  PRINT_UNNAMED_NUM_ARRAY_TO_C_FILE (fp, "%u", "\t\t", c->uca_num, c->uca_w_l4);
	}
      else
	{
	  fprintf (fp, "\t\t{ 0 }");
	}
    }
  else
    {
      fprintf (fp, "\t\t{ 0 },\n\t\t{ 0 }");
    }

  fprintf (fp, ",\n\t\t");
  PRINT_STRING_TO_C_FILE (fp, c->c_buf, strlen (c->c_buf));
  fprintf (fp, ", \n");
  fprintf (fp, "\t\t%u, \n", c->cp_count);
  fprintf (fp, "\t\t%u, \n", c->size);
  fprintf (fp, "\t\t%u \n", c->uca_num);

  fprintf (fp, "} \n");

  return 0;
}

/*
 * locale_prepare_C_file() - saves locale data to C source file, for
 *			    later use when generating the locale shared
 *			    library.
 *
 * return: error code
 * lf(in): locale file info
 * ld(in): locale data
 * c_file_path(in): the path to where to write the C file
 */
int
locale_prepare_C_file (void)
{
  FILE *fp;
  char c_file_path[PATH_MAX];
  char err_msg[ERR_MSG_SIZE];

  envvar_loclib_dir_file (c_file_path, sizeof (c_file_path), "locale.c");

  fp = fopen_ex (c_file_path, "w");
  if (fp == NULL)
    {
      goto error;
    }
  fprintf (fp, "/* GENERATED FILE - DO NOT EDIT */\n");

#if defined(WINDOWS)
  fprintf (fp, "#include <stdio.h>\n");
#else
  fprintf (fp, "#include <stddef.h>\n");
#endif
  fprintf (fp, "#include \"locale_lib_common.h\"\n\n");
  fclose (fp);
  return 0;
error:
  snprintf_dots_truncate (err_msg, sizeof (err_msg) - 1, "Error opening file %s for rewrite.", c_file_path);
  LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
  return ER_GENERIC_ERROR;
}

/*
 * locale_save_to_C_file() - saves locale data to C source file, for
 *			    later use when generating the locale shared
 *			    library.
 *
 * return: error code
 * lf(in): locale file info
 * ld(in): locale data
  */
static int
locale_save_to_C_file (LOCALE_FILE lf, LOCALE_DATA * ld)
{
  FILE *fp;
  char c_file_path[PATH_MAX];
  char err_msg[ERR_MSG_SIZE];
  int i;

  assert (ld != NULL);

  envvar_loclib_dir_file (c_file_path, sizeof (c_file_path), "locale.c");

  fp = fopen_ex (c_file_path, "a");
  if (fp == NULL)
    {
      goto error;
    }
  fprintf (fp, "/*\n * %s - generated from %s \n *\n */\n", ld->locale_name, lf.ldml_file);

  PRINT_STRING_VAR_TO_C_FILE (fp, "locale_name", ld->locale_name, ld->locale_name);

  locale_save_calendar_to_C_file (fp, ld);

  PRINT_VAR_TO_C_FILE (fp, "char", "number_decimal_sym", ld->number_decimal_sym, "'%c'", ld->locale_name);
  PRINT_VAR_TO_C_FILE (fp, "char", "number_group_sym", ld->number_group_sym, "'%c'", ld->locale_name);
  PRINT_VAR_TO_C_FILE (fp, "int", "default_currency_code", ld->default_currency_code, "%d", ld->locale_name);

  locale_save_alphabets_to_C_file (fp, ld);

  /* number of collation associated with this locale */
  PRINT_VAR_TO_C_FILE (fp, "int", "count_coll", ld->coll_cnt, "%d", ld->locale_name);

  for (i = 0; i < ld->coll_cnt; i++)
    {
      char coll_suffix[ERR_MSG_SIZE];

      /* Collation names are user defined: first save fixed name variables containing collations name included in the
       * LDML: collation_1_es_ES = "utf8_es_ES"; collation_2_es_ES = "utf8_es_ES_ci"; */
      snprintf (coll_suffix, sizeof (coll_suffix) - 1, "%d_%s", i, ld->locale_name);
      PRINT_STRING_VAR_TO_C_FILE (fp, "collation", ld->collations[i].opt_coll.coll_name, coll_suffix);

      if (ld->collations[i].do_not_save)
	{
	  continue;
	}

      /* the collation data is decorated with the user defined name of the collation : coll_weights_utf8_es_ES = { ..
       * }; coll_weights_utf8_es_ES_ci = { .. }; */
      locale_save_collation_data_to_C_file (fp, &(ld->collations[i]));
    }

  locale_save_console_conv_to_C_file (fp, ld);

  locale_save_normalization_to_C_file (fp, ld);

  PRINT_STRING_VAR_TO_C_FILE (fp, "locale_checksum", ld->checksum, ld->locale_name);

  fclose (fp);
  return 0;

error:
  snprintf_dots_truncate (err_msg, sizeof (err_msg) - 1, "Error opening file %s for append.", c_file_path);
  LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
  return ER_GENERIC_ERROR;
}

/*
 * locale_save_calendar_to_C_file() - saves calendar data to C source file,
 *			    as variables, for later use when generating the
 *			    locale shared library.
 *
 * return: error code
 * fp(in): file pointer where to write the data
 * ld(in): locale data
 */
static int
locale_save_calendar_to_C_file (FILE * fp, LOCALE_DATA * ld)
{
  assert (ld != NULL);
  assert (fp != NULL);

  /* calendar format strings */
  PRINT_STRING_VAR_TO_C_FILE (fp, "date_format", ld->dateFormat, ld->locale_name);
  PRINT_STRING_VAR_TO_C_FILE (fp, "time_format", ld->timeFormat, ld->locale_name);
  PRINT_STRING_VAR_TO_C_FILE (fp, "datetime_format", ld->datetimeFormat, ld->locale_name);
  PRINT_STRING_VAR_TO_C_FILE (fp, "timestamp_format", ld->timestampFormat, ld->locale_name);
  PRINT_STRING_VAR_TO_C_FILE (fp, "timetz_format", ld->timetzFormat, ld->locale_name);
  PRINT_STRING_VAR_TO_C_FILE (fp, "datetimetz_format", ld->datetimetzFormat, ld->locale_name);
  PRINT_STRING_VAR_TO_C_FILE (fp, "timestamptz_format", ld->timestamptzFormat, ld->locale_name);

  /* calendar data arrays */
  PRINT_STRING_ARRAY_TO_C_FILE (fp, "month_names_abbreviated", CAL_MONTH_COUNT, ld->month_names_abbreviated,
				ld->locale_name);
  PRINT_STRING_ARRAY_TO_C_FILE (fp, "month_names_wide", CAL_MONTH_COUNT, ld->month_names_wide, ld->locale_name);
  PRINT_STRING_ARRAY_TO_C_FILE (fp, "day_names_abbreviated", CAL_DAY_COUNT, ld->day_names_abbreviated, ld->locale_name);
  PRINT_STRING_ARRAY_TO_C_FILE (fp, "day_names_wide", CAL_DAY_COUNT, ld->day_names_wide, ld->locale_name);
  PRINT_STRING_ARRAY_TO_C_FILE (fp, "am_pm", CAL_AM_PM_COUNT, ld->am_pm, ld->locale_name);

  /* calendar parse order arrays */
  PRINT_NUM_ARRAY_TO_C_FILE (fp, "day_names_abbr_parse_order", "char", "%u", CAL_DAY_COUNT,
			     ld->day_names_abbr_parse_order, ld->locale_name);
  PRINT_NUM_ARRAY_TO_C_FILE (fp, "day_names_wide_parse_order", "char", "%u", CAL_DAY_COUNT,
			     ld->day_names_wide_parse_order, ld->locale_name);
  PRINT_NUM_ARRAY_TO_C_FILE (fp, "month_names_abbr_parse_order", "char", "%u", CAL_MONTH_COUNT,
			     ld->month_names_abbr_parse_order, ld->locale_name);
  PRINT_NUM_ARRAY_TO_C_FILE (fp, "month_names_wide_parse_order", "char", "%u", CAL_MONTH_COUNT,
			     ld->month_names_wide_parse_order, ld->locale_name);
  PRINT_NUM_ARRAY_TO_C_FILE (fp, "am_pm_parse_order", "char", "%u", CAL_AM_PM_COUNT, ld->am_pm_parse_order,
			     ld->locale_name);

  return 0;
}

/*
 * locale_save_alphabets_to_C_file() - saves alphabet and identifier alphabet
 *			    to C source file as variables, for later use
 *			    when generating the locale shared library.
 *
 * return: error code
 * fp(in): file pointer where to write the data
 * a(in): alphabet data
 * save_w_identier_name(in): true if alphabet is to be saved as "identifier"
 *			     name
 * alpha_suffix(in): suffix to be applied to variables names
 */
static int
locale_save_one_alphabet_to_C_file (FILE * fp, ALPHABET_DATA * a, bool save_w_identier_name, const char *alpha_suffix)
{
  assert (a != NULL);
  assert (fp != NULL);

  if (save_w_identier_name)
    {
      PRINT_VAR_TO_C_FILE (fp, "int", "ident_alphabet_l_count", a->l_count, "%d", alpha_suffix);

      PRINT_VAR_TO_C_FILE (fp, "int", "ident_alphabet_lower_multiplier", a->lower_multiplier, "%d", alpha_suffix);
      PRINT_VAR_TO_C_FILE (fp, "int", "ident_alphabet_upper_multiplier", a->upper_multiplier, "%d", alpha_suffix);
      PRINT_NUM_ARRAY_TO_C_FILE (fp, "ident_alphabet_lower_cp", "unsigned int", "%u", a->l_count * a->lower_multiplier,
				 a->lower_cp, alpha_suffix);
      PRINT_NUM_ARRAY_TO_C_FILE (fp, "ident_alphabet_upper_cp", "unsigned int", "%u", a->l_count * a->upper_multiplier,
				 a->upper_cp, alpha_suffix);
    }
  else
    {
      PRINT_VAR_TO_C_FILE (fp, "int", "alphabet_l_count", a->l_count, "%d", alpha_suffix);
      PRINT_VAR_TO_C_FILE (fp, "int", "alphabet_lower_multiplier", a->lower_multiplier, "%d", alpha_suffix);
      PRINT_VAR_TO_C_FILE (fp, "int", "alphabet_upper_multiplier", a->upper_multiplier, "%d", alpha_suffix);
      PRINT_NUM_ARRAY_TO_C_FILE (fp, "alphabet_lower_cp", "unsigned int", "%u", a->l_count * a->lower_multiplier,
				 a->lower_cp, alpha_suffix);
      PRINT_NUM_ARRAY_TO_C_FILE (fp, "alphabet_upper_cp", "unsigned int", "%u", a->l_count * a->upper_multiplier,
				 a->upper_cp, alpha_suffix);
    }

  return 0;
}

/*
 * locale_save_alphabets_to_C_file() - saves alphabet and identifier alphabet
 *			    to C source file as variables, for later use
 *			    when generating the locale shared library.
 *
 * return: error code
 * fp(in): file pointer where to write the data
 * ld(in): locale data
 */
static int
locale_save_alphabets_to_C_file (FILE * fp, LOCALE_DATA * ld)
{
  assert (ld != NULL);
  assert (fp != NULL);

  /* alphabet data */
  PRINT_VAR_TO_C_FILE (fp, "int", "alphabet_a_type", ld->alphabet.a_type, "%d", ld->locale_name);

  if (!(ld->alphabet.do_not_save))
    {
      const char *alpha_suffix;

      if (ld->alphabet.a_type == ALPHABET_UNICODE)
	{
	  alpha_suffix = "unicode";
	}
      else if (ld->alphabet.a_type == ALPHABET_ASCII)
	{
	  alpha_suffix = "ascii";
	}
      else
	{
	  alpha_suffix = (const char *) (ld->locale_name);
	}

      (void) locale_save_one_alphabet_to_C_file (fp, &(ld->alphabet), false, alpha_suffix);
    }

  /* identifier alphabet data */
  PRINT_VAR_TO_C_FILE (fp, "int", "ident_alphabet_a_type", ld->identif_alphabet.a_type, "%d", ld->locale_name);

  if (!(ld->identif_alphabet.do_not_save))
    {
      const char *alpha_suffix;
      bool save_w_identif = false;

      if (ld->identif_alphabet.a_type == ALPHABET_UNICODE)
	{
	  alpha_suffix = "unicode";
	}
      else if (ld->identif_alphabet.a_type == ALPHABET_ASCII)
	{
	  alpha_suffix = "ascii";
	}
      else
	{
	  alpha_suffix = (const char *) (ld->locale_name);
	  save_w_identif = true;
	}

      (void) locale_save_one_alphabet_to_C_file (fp, &(ld->identif_alphabet), save_w_identif, alpha_suffix);
    }

  return 0;
}

/*
 * locale_save_collation_data_to_C_file() - saves collation data to C source
 *			    file as variables, for later use when generating
 *			    the locale shared library.
 *
 * return: error code
 * fp(in): file pointer where to write the data
 * cd(in): collation data
 */
static int
locale_save_collation_data_to_C_file (FILE * fp, LOCALE_COLLATION * lc)
{
  int i;
  COLL_DATA *cd = NULL;

  assert (lc != NULL);
  assert (fp != NULL);

  cd = &(lc->opt_coll);

  /* collation constants */
  PRINT_VAR_TO_C_FILE (fp, "int", "coll_id", cd->coll_id, "%d", cd->coll_name);
  PRINT_STRING_VAR_TO_C_FILE (fp, "coll_name", cd->coll_name, cd->coll_name);

  /* OPT_COLL.UCA_OPT members */
  PRINT_VAR_TO_C_FILE (fp, "int", "coll_sett_strength", cd->uca_opt.sett_strength, "%d", cd->coll_name);
  PRINT_VAR_TO_C_FILE (fp, "int", "coll_sett_backwards", cd->uca_opt.sett_backwards ? 1 : 0, "%d", cd->coll_name);
  PRINT_VAR_TO_C_FILE (fp, "int", "coll_sett_caseLevel", cd->uca_opt.sett_caseLevel ? 1 : 0, "%d", cd->coll_name);
  PRINT_VAR_TO_C_FILE (fp, "int", "coll_sett_caseFirst", cd->uca_opt.sett_caseFirst, "%d", cd->coll_name);
  PRINT_VAR_TO_C_FILE (fp, "int", "coll_sett_expansions", cd->uca_opt.sett_expansions ? 1 : 0, "%d", cd->coll_name);
  PRINT_VAR_TO_C_FILE (fp, "int", "coll_sett_contr_policy", cd->uca_opt.sett_contr_policy, "%d", cd->coll_name);
  PRINT_VAR_TO_C_FILE (fp, "int", "coll_match_contr", cd->uca_opt.sett_match_contr, "%d", cd->coll_name);

  /* other OPT_COLL members */
  PRINT_VAR_TO_C_FILE (fp, "int", "coll_w_count", cd->w_count, "%d", cd->coll_name);
  PRINT_VAR_TO_C_FILE (fp, "int", "coll_uca_exp_num", cd->uca_exp_num, "%d", cd->coll_name);
  PRINT_VAR_TO_C_FILE (fp, "int", "coll_count_contr", cd->count_contr, "%d", cd->coll_name);

  /* OPT_COLL pointer and array members */
  if (cd->w_count > 0)
    {
      if (!(cd->uca_opt.sett_expansions))
	{
	  if (*(lc->coll_ref.coll_weights_ref) == '\0')
	    {
	      sprintf (lc->coll_ref.coll_weights_ref, "coll_weights_%s", cd->coll_name);
	      PRINT_NUM_ARRAY_TO_C_FILE (fp, "coll_weights", "unsigned int", "%u", cd->w_count, cd->weights,
					 cd->coll_name);
	    }
	  PRINT_STRING_VAR_TO_C_FILE (fp, "coll_weights_ref", lc->coll_ref.coll_weights_ref, cd->coll_name);
	}
      else
	{
	  assert (cd->uca_exp_num > 1);

	  if (*(lc->coll_ref.coll_uca_w_l13_ref) == '\0')
	    {
	      sprintf (lc->coll_ref.coll_uca_w_l13_ref, "coll_uca_w_l13_%s", cd->coll_name);
	      PRINT_NUM_ARRAY_TO_C_FILE (fp, "coll_uca_w_l13", "unsigned int", "%u", cd->uca_exp_num * cd->w_count,
					 cd->uca_w_l13, cd->coll_name);
	    }
	  PRINT_STRING_VAR_TO_C_FILE (fp, "coll_uca_w_l13_ref", lc->coll_ref.coll_uca_w_l13_ref, cd->coll_name);

	  if (cd->uca_opt.sett_strength >= TAILOR_QUATERNARY)
	    {
	      if (*(lc->coll_ref.coll_uca_w_l4_ref) == '\0')
		{
		  sprintf (lc->coll_ref.coll_uca_w_l4_ref, "coll_uca_w_l4_%s", cd->coll_name);
		  PRINT_NUM_ARRAY_TO_C_FILE (fp, "coll_uca_w_l4", "unsigned short", "%u", cd->uca_exp_num * cd->w_count,
					     cd->uca_w_l4, cd->coll_name);
		}
	      PRINT_STRING_VAR_TO_C_FILE (fp, "coll_uca_w_l4_ref", lc->coll_ref.coll_uca_w_l4_ref, cd->coll_name);
	    }

	  if (*(lc->coll_ref.coll_uca_num_ref) == '\0')
	    {
	      sprintf (lc->coll_ref.coll_uca_num_ref, "coll_uca_num_%s", cd->coll_name);
	      PRINT_NUM_ARRAY_TO_C_FILE (fp, "coll_uca_num", "char", "%d", cd->w_count, cd->uca_num, cd->coll_name);
	    }
	  PRINT_STRING_VAR_TO_C_FILE (fp, "coll_uca_num_ref", lc->coll_ref.coll_uca_num_ref, cd->coll_name);
	}

      if (*(lc->coll_ref.coll_next_cp_ref) == '\0')
	{
	  sprintf (lc->coll_ref.coll_next_cp_ref, "coll_next_cp_%s", cd->coll_name);
	  PRINT_NUM_ARRAY_TO_C_FILE (fp, "coll_next_cp", "unsigned int", "%u", cd->w_count, cd->next_cp, cd->coll_name);
	}
      PRINT_STRING_VAR_TO_C_FILE (fp, "coll_next_cp_ref", lc->coll_ref.coll_next_cp_ref, cd->coll_name);
    }

  if (cd->count_contr > 0)
    {
      PRINT_VAR_TO_C_FILE (fp, "int", "coll_contr_min_size", cd->contr_min_size, "%d", cd->coll_name);
      PRINT_VAR_TO_C_FILE (fp, "unsigned int", "coll_cp_first_contr_offset", cd->cp_first_contr_offset, "%u",
			   cd->coll_name);
      PRINT_VAR_TO_C_FILE (fp, "unsigned int", "coll_cp_first_contr_count", cd->cp_first_contr_count, "%u",
			   cd->coll_name);

      if (*(lc->coll_ref.coll_contr_list_ref) == '\0')
	{
	  sprintf (lc->coll_ref.coll_contr_list_ref, "coll_contr_list_%s", cd->coll_name);
	  fprintf (fp, "" DLL_EXPORT_PREFIX "const COLL_CONTRACTION coll_contr_list_%s[] = {", cd->coll_name);
	  for (i = 0; i < cd->count_contr; i++)
	    {
	      fprintf (fp, "\n");
	      save_contraction_to_C_file (fp, &(cd->contr_list[i]), cd->uca_opt.sett_expansions,
					  (cd->uca_opt.sett_strength >= TAILOR_QUATERNARY));
	      if (i < cd->count_contr - 1)
		{
		  fprintf (fp, ", ");
		}
	    }
	  fprintf (fp, "};\n");
	}
      PRINT_STRING_VAR_TO_C_FILE (fp, "coll_contr_list_ref", lc->coll_ref.coll_contr_list_ref, cd->coll_name);

      if (*(lc->coll_ref.coll_cp_first_contr_array_ref) == '\0')
	{
	  sprintf (lc->coll_ref.coll_cp_first_contr_array_ref, "coll_cp_first_contr_array_%s", cd->coll_name);
	  PRINT_NUM_ARRAY_TO_C_FILE (fp, "coll_cp_first_contr_array", "int", "%d", (int) cd->cp_first_contr_count,
				     cd->cp_first_contr_array, cd->coll_name);
	}
      PRINT_STRING_VAR_TO_C_FILE (fp, "coll_cp_first_contr_array_ref", lc->coll_ref.coll_cp_first_contr_array_ref,
				  cd->coll_name);

    }

  PRINT_STRING_VAR_TO_C_FILE (fp, "coll_checksum", cd->checksum, cd->coll_name);
  return 0;
}

/*
 * locale_save_console_conv_to_C_file() - saves console conversion data to
 *					  binary file
 *
 * return: zero if save is successful, non-zero otherwise
 * fp(in): file descriptor
 * tc(in): console conversion info
 */
static int
locale_save_console_conv_to_C_file (FILE * fp, LOCALE_DATA * ld)
{
  int i;

  TEXT_CONVERSION *tc;

  assert (ld != NULL);
  assert (fp != NULL);

  tc = &(ld->txt_conv);

  /* TEXT_CONVERSION non-array members */
  PRINT_VAR_TO_C_FILE (fp, "int", "tc_conv_type", tc->conv_type, "%d", ld->locale_name);

  if (tc->conv_type != TEXT_CONV_GENERIC_1BYTE && tc->conv_type != TEXT_CONV_GENERIC_2BYTE)
    {
      return 0;
    }

  PRINT_NUM_ARRAY_TO_C_FILE (fp, "tc_is_lead_byte", "unsigned char", "%u", 256, tc->byte_flag, ld->locale_name);
  PRINT_VAR_TO_C_FILE (fp, "unsigned int", "tc_utf8_first_cp", tc->utf8_first_cp, "%u", ld->locale_name);
  PRINT_VAR_TO_C_FILE (fp, "unsigned int", "tc_utf8_last_cp", tc->utf8_last_cp, "%u", ld->locale_name);
  PRINT_VAR_TO_C_FILE (fp, "unsigned int", "tc_text_first_cp", tc->text_first_cp, "%u", ld->locale_name);
  PRINT_VAR_TO_C_FILE (fp, "unsigned int", "tc_text_last_cp", tc->text_last_cp, "%u", ld->locale_name);

  assert (tc->win_codepages != NULL);
  PRINT_STRING_VAR_TO_C_FILE (fp, "tc_win_codepages", tc->win_codepages, ld->locale_name);

  assert (tc->nl_lang_str != NULL);
  PRINT_STRING_VAR_TO_C_FILE (fp, "tc_nl_lang_str", tc->nl_lang_str, ld->locale_name);

  assert (tc->utf8_last_cp > tc->utf8_first_cp);
  assert (tc->utf8_to_text != NULL);

  fprintf (fp, "" DLL_EXPORT_PREFIX "const CONV_CP_TO_BYTES tc_utf8_to_text_%s[] = { \n", ld->locale_name);
  for (i = 0; i < (int) (tc->utf8_last_cp - tc->utf8_first_cp + 1); i++)
    {
      fprintf (fp, "{ %u, ", tc->utf8_to_text[i].size);
      PRINT_STRING_TO_C_FILE (fp, tc->utf8_to_text[i].bytes, tc->utf8_to_text[i].size);
      fprintf (fp, "},\n");
    }
  fprintf (fp, "};\n");

  assert (tc->text_last_cp > tc->text_first_cp);
  assert (tc->text_to_utf8 != NULL);

  fprintf (fp, "" DLL_EXPORT_PREFIX "const CONV_CP_TO_BYTES tc_text_to_utf8_%s[] = { \n", ld->locale_name);
  for (i = 0; i < (int) (tc->text_last_cp - tc->text_first_cp + 1); i++)
    {
      fprintf (fp, "{ %u, ", tc->text_to_utf8[i].size);
      PRINT_STRING_TO_C_FILE (fp, tc->text_to_utf8[i].bytes, tc->text_to_utf8[i].size);
      fprintf (fp, "},\n");
    }
  fprintf (fp, "};\n");

  return 0;
}

/*
 * str_pop_token() - Extracts a token from a string;
 *		     A token is a sub-string surrounded by whitespaces.
 *    return: string token
 *    str_p(in): buffer with tokens
 *    token_p(in/out): returned token string
 *    next_p(in/out): pointer to next token or NULL if no more tokens
 *
 *    Note : When found the token characters are copied into a new string
 *           and returned.
 *           The pointer to the first character following the new token in
 *           the buffer is returned.
 */
static int
str_pop_token (char *str_p, char **token_p, char **next_p)
{
  char *p, *end, *token = NULL;
  int length;

  assert (str_p != NULL);
  assert (token_p != NULL);
  assert (next_p != NULL);

  p = str_p;
  while (char_isspace ((int) *p) && *p != '\0')
    {
      p++;
    }
  end = p;
  while (!char_isspace ((int) *end) && *end != '\0')
    {
      end++;
    }

  length = (int) (end - p);
  if (length > 0)
    {
      token = (char *) malloc (length + 1);
      if (token == NULL)
	{
	  LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_INIT, true);
	  return ER_LOC_INIT;
	}
      assert (token != NULL);

      strncpy (token, p, length);
      token[length] = '\0';
    }
  else
    {
      /* no more tokens */
      end = NULL;
    }

  *token_p = token;
  *next_p = end;
  return NO_ERROR;
}

/*
 * dump_locale_alphabet - dump the selected ALPHABET_DATA structure
 *			  in human-readable text format.
 * Returns : NO_ERROR.
 * ad(in)  : the ALPHABET_DATA to be dumped in text format.
 * dl_settings(in): the commmand line options encoded intoa binary masked int.
 * lower_bound(in): the starting codepoint for the range of items which
 *		    -w or -c will dump.
 * upper_bound(in)  : the ending codepoint for the range of items which
 *		    -w or -c will dump.
 */
static int
dump_locale_alphabet (ALPHABET_DATA * ad, int dl_settings, int lower_bound, int upper_bound)
{
#define DUMP_CP_BUF_SIZE 128
  int i, cp;
  unsigned char utf8_buf[INTL_UTF8_MAX_CHAR_SIZE + 1];
  unsigned int *case_buf;
  char out_case[DUMP_CP_BUF_SIZE];
  char out_cp[DUMP_CP_BUF_SIZE];

  assert (ad != NULL);
  assert (lower_bound <= upper_bound);

  printf ("Alphabet type: ");
  if (ad->a_type == ALPHABET_UNICODE)
    {
      printf ("Unicode\n");
    }
  else if (ad->a_type == ALPHABET_ASCII)
    {
      printf ("ASCII\n");
    }
  else
    {
      assert (ad->a_type == ALPHABET_TAILORED);
      printf ("Tailored\n");
    }

  printf ("Letter count: %d\n", ad->l_count);
  if (ad->l_count > 0)
    {
      printf ("Lower multiplier: %d\n", ad->lower_multiplier);
      printf ("Upper multiplier: %d\n", ad->upper_multiplier);
    }

  for (cp = lower_bound; cp < upper_bound; cp++)
    {
      memset (utf8_buf, 0, INTL_UTF8_MAX_CHAR_SIZE + 1);
      intl_cp_to_utf8 (cp, utf8_buf);
      printf ("CP: Ux%04X | %-4s", cp, (cp > 0x0020 ? utf8_buf : (unsigned char *) ""));
      if ((dl_settings & DUMPLOCALE_IS_ALPHABET_LOWER) != 0)
	{
	  bool print_case = true;
	  memset (out_cp, 0, sizeof (out_cp));
	  memset (out_case, 0, sizeof (out_case));

	  case_buf = &(ad->lower_cp[cp * ad->lower_multiplier]);

	  for (i = 0; i < ad->lower_multiplier; i++)
	    {
	      char temp_cp[8];

	      if (case_buf[i] == 0)
		{
		  break;
		}

	      if (case_buf[i] < 0x20)
		{
		  print_case = false;
		}
	      memset (utf8_buf, 0, sizeof (utf8_buf));
	      intl_cp_to_utf8 (case_buf[i], utf8_buf);
	      strcat (out_case, (char *) utf8_buf);
	      snprintf (temp_cp, sizeof (temp_cp) - 1, "Ux%04X", case_buf[i]);
	      strcat (out_cp, temp_cp);
	    }
	  printf (" | Lower :  CP(s): %-12s, lower char(s): %-12s", out_cp, (print_case ? out_case : ""));
	}
      if ((dl_settings & DUMPLOCALE_IS_ALPHABET_UPPER) != 0)
	{
	  bool print_case = true;
	  memset (out_cp, 0, sizeof (out_cp));
	  memset (out_case, 0, sizeof (out_case));

	  case_buf = &(ad->upper_cp[cp * ad->upper_multiplier]);

	  for (i = 0; i < ad->upper_multiplier; i++)
	    {
	      char temp_cp[8];

	      if (case_buf[i] == 0)
		{
		  break;
		}

	      if (case_buf[i] < 0x20)
		{
		  print_case = false;
		}

	      memset (utf8_buf, 0, sizeof (utf8_buf));
	      intl_cp_to_utf8 (case_buf[i], utf8_buf);
	      strcat (out_case, (char *) utf8_buf);
	      snprintf (temp_cp, sizeof (temp_cp) - 1, "Ux%04X", case_buf[i]);
	      strcat (out_cp, temp_cp);
	    }
	  printf (" | Upper :  CP(s): %-12s, upper char(s): %-12s", out_cp, (print_case ? out_case : ""));
	}
      printf ("\n");
    }
  return NO_ERROR;
}

/*
 * dump_locale_collation - dump the selected COLL_DATA structure
 *			   in human-readable text format.
 * Returns : NO_ERROR.
 * coll(in)  : the COLL_DATA to be dumped in text format.
 * dl_settings(in): the commmand line options encoded intoa binary masked int.
 * lower_bound(in): the starting codepoint for the range of items which
 *		    -w or -c will dump.
 * upper_bound(in)  : the ending codepoint for the range of items which
 *		    -w or -c will dump.
 */
static int
dump_locale_collation (COLL_DATA * coll, int dl_settings, int start_value, int end_value)
{
  unsigned int *coll_key_list = NULL;
  int coll_key_list_cnt = 0;
  int lower_bound = 0;
  int upper_bound = 0;
  int err_status = NO_ERROR;
  int cp, i;

  assert (coll != NULL);

  lower_bound = 0;
  upper_bound = coll->w_count;
  if (start_value > lower_bound)
    {
      lower_bound = start_value;
    }
  if (end_value > 0 && end_value < upper_bound)
    {
      /* if an upper bound is specified, use the "+1" to make sure that the upper bound codepoint is included in the
       * dump file. */
      upper_bound = end_value + 1;
    }

  printf ("\n");
  printf ("* Collation: %s | id: %d | checksum: %s *\n", coll->coll_name, coll->coll_id, coll->checksum);
  printf ("Max codepoints: %d\n", coll->w_count);
  printf ("Level: %d\n", coll->uca_opt.sett_strength);
  printf ("Expansions: %s\n", coll->uca_opt.sett_expansions ? "yes" : "no");
  if (coll->uca_opt.sett_expansions)
    {
      printf ("Expansion CE num: %d\n", coll->uca_exp_num);
    }
  printf ("Contractions: %d\n", coll->count_contr);
  if (coll->count_contr > 0)
    {
      printf ("Contraction min size: %d\n", coll->contr_min_size);
    }

  assert (coll->w_count > 0);

  if (upper_bound < lower_bound)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR (msgcat_message
			(MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_DUMPLOCALE, DUMPLOCALE_MSG_INVALID_CP_RANGE), ER_LOC_GEN,
			true);
      goto exit;
    }

  /* Dump collation information, cp and next_cp info, ordered by cp. */
  if ((dl_settings & DUMPLOCALE_IS_COLLATION_CP_ORDER) != 0)
    {
      printf ("\n");
      printf ("* Codepoint collation info (codepoint order) *\n");
      for (cp = lower_bound; cp < upper_bound; cp++)
	{
	  unsigned int next_id;

	  assert (cp >= 0 && cp < coll->w_count);

	  dump_collation_codepoint (coll, cp, true, true);

	  next_id = coll->next_cp[cp];
	  printf (" | Next : ");

	  dump_collation_key (coll, next_id, true, true);

	  printf ("\n");
	}
    }

  if (coll->count_contr > 0 && (dl_settings & DUMPLOCALE_IS_COLLATION_CP_ORDER) != 0)
    {
      printf ("\n");
      printf ("* Contraction collation info (contraction order) *\n");
      for (i = 0; i < coll->count_contr; i++)
	{
	  COLL_CONTRACTION *contr = &(coll->contr_list[i]);

	  dump_collation_contr (coll, contr, true, true);
	  printf (" | Next :");

	  dump_collation_key (coll, contr->next, true, true);

	  printf ("\n");
	}
    }

  /* Dump weight list and corresponding codepoints, ordered by weight. */
  if ((dl_settings & DUMPLOCALE_IS_COLLATION_WEIGHT_ORDER) != 0)
    {
      int keys_same_weight = 1;
      int cp_count = upper_bound - lower_bound;
      bool use_expansions;

      printf ("\n");
      printf ("* Collation info (weight order) *\n");
      coll_key_list_cnt = cp_count + coll->count_contr;
      coll_key_list = (unsigned int *) malloc (coll_key_list_cnt * sizeof (unsigned int));
      if (coll_key_list == NULL)
	{
	  err_status = ER_LOC_INIT;
	  LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_INIT, true);
	  goto exit;
	}

      for (cp = lower_bound; cp < upper_bound; cp++)
	{
	  coll_key_list[cp - lower_bound] = cp;
	}

      for (i = 0; i < coll->count_contr; i++)
	{
	  coll_key_list[i + cp_count] = i | INTL_MASK_CONTR;
	}

      /* Order by weight. */
      use_expansions = coll->uca_opt.sett_expansions;

      dump_coll_data = coll;
      if (use_expansions)
	{
	  qsort (coll_key_list, coll_key_list_cnt, sizeof (unsigned int), comp_func_coll_uca_exp_fo);
	}
      else
	{
	  qsort (coll_key_list, coll_key_list_cnt, sizeof (unsigned int), comp_func_coll_uca_simple_weights_fo);
	}

      /* Dump ordered (weight, key id) tuples. */
      for (i = 0; i < coll_key_list_cnt; i++)
	{
	  bool same_weights = false;

	  if (i > 0)
	    {
	      if (use_expansions)
		{
		  same_weights =
		    (comp_func_coll_uca_exp (&(coll_key_list[i]), &(coll_key_list[i - 1])) == 0) ? true : false;
		}
	      else
		{
		  same_weights =
		    (comp_func_coll_uca_simple_weights (&(coll_key_list[i]), &(coll_key_list[i - 1])) ==
		     0) ? true : false;
		}
	    }

	  if (i > 0 && !same_weights)
	    {
	      if (keys_same_weight > 1)
		{
		  printf (" Keys same weight : %d\n", keys_same_weight);
		}
	      else
		{
		  printf ("\n");
		}

	      dump_collation_key (coll, coll_key_list[i], true, false);
	      keys_same_weight = 1;
	    }
	  else
	    {
	      printf ("\n");
	      dump_collation_key (coll, coll_key_list[i], true, false);
	      keys_same_weight++;
	    }

	  printf (" | ");

	  dump_collation_key (coll, coll_key_list[i], false, true);
	}
    }
exit:
  if (coll_key_list != NULL)
    {
      free (coll_key_list);
    }

  return err_status;
}

/*
 * locale_dump - output the selected information on locale data in
 *		 human-readable text format.
 * Returns : error status.
 * data(in): the locale data to be dumped in text format.
 * lf(in)  : the LOCALE_FILE i.e. binary file from which ld was imported.
 * dl_settings(in): the commmand line options encoded intoa binary masked int.
 * start_value(in): the starting codepoint for the range of items which
 *		    -w or -c will dump.
 * end_value(in)  : the ending codepoint for the range of items which
 *		    -w or -c will dump.
 */
int
locale_dump (void *data, LOCALE_FILE * lf, int dl_settings, int start_value, int end_value)
{
  int i;
  int lower_bound = 0;
  int upper_bound = 0;
  int alphabet_settings;
  int err_status = NO_ERROR;
  LANG_LOCALE_DATA *lld = NULL;

  assert (data != NULL);
  assert (lf != NULL);
  assert (start_value <= end_value);

  lld = (LANG_LOCALE_DATA *) data;

  printf ("*************************************************\n");
  printf ("Locale data for: %s\nLibrary file: %s\n", lf->locale_name, lf->lib_file);
  printf ("Locale string: %s\n", lld->lang_name);
  printf ("Locale checksum: %s\n", lld->checksum);

  if ((dl_settings & DUMPLOCALE_IS_CALENDAR) != 0)
    {
      printf ("\n * Calendar *\n");
      printf ("Date format: %s\n", lld->date_format);
      printf ("Time format: %s\n", lld->time_format);
      printf ("Datetime format: %s\n", lld->datetime_format);
      printf ("Timestamp format: %s\n", lld->timestamp_format);
      printf ("Datetime_tz format: %s\n", lld->datetimetz_format);
      printf ("Timestamp_tz format: %s\n", lld->timestamptz_format);

      printf ("\nAbbreviated month names:\n");
      for (i = 0; i < CAL_MONTH_COUNT; i++)
	{
	  printf ("%d. %s\n", (i + 1), lld->month_short_name[i]);
	}
      printf ("\nAbbreviated month names, sorted for tokenizer:\n");
      for (i = 0; i < CAL_MONTH_COUNT; i++)
	{
	  printf ("%d. %s = month %d\n", (i + 1), lld->month_short_name[(int) (lld->month_short_parse_order[i])],
		  (int) (lld->month_short_parse_order[i]) + 1);
	}

      printf ("\nWide month names:\n");
      for (i = 0; i < CAL_MONTH_COUNT; i++)
	{
	  printf ("%d. %s\n", (i + 1), lld->month_name[i]);
	}
      printf ("\nWide month names, sorted for tokenizer:\n");
      for (i = 0; i < CAL_MONTH_COUNT; i++)
	{
	  printf ("%d. %s = month %d\n", (i + 1), lld->month_name[(int) (lld->month_parse_order[i])],
		  (int) (lld->month_parse_order[i]) + 1);
	}

      printf ("\nAbbreviated weekday names:\n");
      for (i = 0; i < CAL_DAY_COUNT; i++)
	{
	  printf ("%d. %s\n", (i + 1), lld->day_short_name[i]);
	}
      printf ("\nAbbreviated weekday names, sorted for parse order:\n");
      for (i = 0; i < CAL_DAY_COUNT; i++)
	{
	  printf ("%d. %s = weekday %d\n", (i + 1), lld->day_short_name[(int) (lld->day_short_parse_order[i])],
		  (int) (lld->day_short_parse_order[i]) + 1);
	}

      printf ("\nWide weekday names:\n");
      for (i = 0; i < CAL_DAY_COUNT; i++)
	{
	  printf ("%d. %s\n", (i + 1), lld->day_name[i]);
	}
      printf ("\nWide weekday names, sorted for tokenizer:\n");
      for (i = 0; i < CAL_DAY_COUNT; i++)
	{
	  printf ("%d. %s = weekday %d\n", (i + 1), lld->day_name[(int) (lld->day_parse_order[i])],
		  (int) (lld->day_parse_order[i]) + 1);
	}

      printf ("\nDay periods:\n");
      for (i = 0; i < CAL_AM_PM_COUNT; i++)
	{
	  printf ("%d. %s\n", (i + 1), lld->am_pm[i]);
	}
      printf ("\nDay periods, sorted for tokenizer:\n");
      for (i = 0; i < CAL_AM_PM_COUNT; i++)
	{
	  printf ("%d. %s = day period %d\n", (i + 1), lld->am_pm[(int) (lld->am_pm_parse_order[i])],
		  (int) (lld->am_pm_parse_order[i]) + 1);
	}
    }

  /* Display numbering information. */
  if ((dl_settings & DUMPLOCALE_IS_NUMBERING) != 0)
    {
      printf ("\n * Numbers *\n");
      printf ("Decimal separator: <%c>\n", lld->number_decimal_sym);
      printf ("Separator for digit grouping: <%c>\n", lld->number_group_sym);
      printf ("Default currency ISO code: %s\n", intl_get_money_ISO_symbol (lld->default_currency_code));
    }

  if ((dl_settings & DUMPLOCALE_IS_ALPHABET) != 0)
    {
      lower_bound = 0;
      upper_bound = lld->alphabet.l_count;
      if (start_value > lower_bound)
	{
	  lower_bound = start_value;
	}
      if (end_value > 0 && end_value < upper_bound)
	{
	  /* if an upper bound is specified, use the "+1" to make sure that the upper bound codepoint is included in
	   * the dump file. */
	  upper_bound = end_value + 1;
	}
    }

  if (upper_bound < lower_bound)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR (msgcat_message
			(MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_DUMPLOCALE, DUMPLOCALE_MSG_INVALID_CP_RANGE), ER_LOC_GEN,
			true);
      goto exit;
    }

  /* Dump alphabet data. */
  if ((dl_settings & DUMPLOCALE_IS_ALPHABET) != 0)
    {
      alphabet_settings = 0;
      alphabet_settings |= (dl_settings & DUMPLOCALE_IS_ALPHABET_LOWER);
      alphabet_settings |= (dl_settings & DUMPLOCALE_IS_ALPHABET_UPPER);
      printf ("\n * Alphabet data *\n");
      dump_locale_alphabet (&(lld->alphabet), alphabet_settings, lower_bound, upper_bound);
    }

  /* Dump identifier alphabet data. */
  if ((dl_settings & DUMPLOCALE_IS_IDENTIFIER_ALPHABET) != 0)
    {
      alphabet_settings = 0;
      if ((dl_settings & DUMPLOCALE_IS_IDENTIFIER_ALPHABET_LOWER) != 0)
	{
	  alphabet_settings |= DUMPLOCALE_IS_ALPHABET_LOWER;
	}
      if ((dl_settings & DUMPLOCALE_IS_IDENTIFIER_ALPHABET_UPPER) != 0)
	{
	  alphabet_settings |= DUMPLOCALE_IS_ALPHABET_UPPER;
	}

      printf ("\n");
      printf ("* Identifier alphabet data *\n");
      dump_locale_alphabet (&(lld->ident_alphabet), alphabet_settings, lower_bound, upper_bound);
    }

  /* Dump normalization data. */
  if ((dl_settings & DUMPLOCALE_IS_NORMALIZATION) != 0)
    {
      printf ("\n");
      printf ("* Normalization data *\n");
      dump_locale_normalization (&(lld->unicode_norm));
    }

  /* Dump normalization data. */
  if ((dl_settings & DUMPLOCALE_IS_TEXT_CONV) != 0)
    {
      printf ("\n");
      printf ("* Console conversion data *\n");
      dump_console_conversion (lld->txt_conv);
    }

exit:
  return err_status;
}

/*
 * locale_dump_lib_collations() - output the selected information on
 *				  collations data in human-readable text
 *				  format.
 * return: error code
 * lib_handle(in):
 * lf(in): locale file info
 * dl_settings(in):
 * lower_bound(in):
 * upper_bound(in):
 */
int
locale_dump_lib_collations (void *lib_handle, const LOCALE_FILE * lf, int dl_settings, int start_value, int end_value)
{
  int err_status = NO_ERROR;
  int i, count_coll_to_load;
  COLL_DATA coll;

  assert (lib_handle != NULL);
  assert (lf != NULL);
  assert (lf->locale_name != NULL);

  /* collation data */
  err_status = lang_load_count_coll_from_lib (&count_coll_to_load, lib_handle, lf);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

  printf ("\n\n* Collations: %d collations found *\n", count_coll_to_load);

  for (i = 0; i < count_coll_to_load; i++)
    {
      /* get name of collation */
      char *coll_name = NULL;

      memset (&coll, 0, sizeof (COLL_DATA));

      err_status = lang_load_get_coll_name_from_lib (i, &coll_name, lib_handle, lf);
      if (err_status != NO_ERROR)
	{
	  goto exit;
	}

      assert (strlen (coll_name) < (int) sizeof (coll.coll_name));
      strncpy (coll.coll_name, coll_name, sizeof (coll.coll_name) - 1);

      err_status = lang_load_coll_from_lib (&coll, lib_handle, lf);
      if (err_status != NO_ERROR)
	{
	  goto exit;
	}

      err_status = dump_locale_collation (&coll, dl_settings, start_value, end_value);
      if (err_status != NO_ERROR)
	{
	  goto exit;
	}
      printf ("\n\n\n");
    }

exit:
  return err_status;
}

/*
 * comp_func_coll_uca_simple_weights_fo - compare function for sorting
 *		      collatable elements according to simple weights order
 *		      but applies full order
 *
 *  Note: This function uses 'comp_func_coll_uca_simple_weights' but applies
 *	  a full order.
 *	  The purpose is to provide a 'deterministic comparison' to eliminate
 *	  unpredictable results of sort algorithm (qsort).
 */
static int
comp_func_coll_uca_simple_weights_fo (const void *arg1, const void *arg2)
{
  unsigned int pos1;
  unsigned int pos2;
  int cmp;

  pos1 = *((unsigned int *) arg1);
  pos2 = *((unsigned int *) arg2);

  cmp = comp_func_coll_uca_simple_weights (arg1, arg2);

  return (cmp == 0) ? (int) (pos1 - pos2) : cmp;
}

/*
 * comp_func_coll_uca_simple_weights - compare function for sorting collatable
 *		      elements according to simple weights order
 *
 *  Note: this function is used by dump_locale tool
 *	  The elements in array are 32 bit unsigned integers, keys which
 *	  may be Unicode points or contractions (when highest bit is set)
 *
 */
static int
comp_func_coll_uca_simple_weights (const void *arg1, const void *arg2)
{
  unsigned int pos1;
  unsigned int pos2;
  unsigned int wv1, wv2;
  COLL_DATA *coll = dump_coll_data;

  pos1 = *((unsigned int *) arg1);
  pos2 = *((unsigned int *) arg2);

  assert (coll != NULL);

  if (INTL_IS_NEXT_CONTR (pos1))
    {
      wv1 = coll->contr_list[INTL_GET_NEXT_CONTR_ID (pos1)].wv;
    }
  else
    {
      wv1 = coll->weights[pos1];
    }

  if (INTL_IS_NEXT_CONTR (pos2))
    {
      wv2 = coll->contr_list[INTL_GET_NEXT_CONTR_ID (pos2)].wv;
    }
  else
    {
      wv2 = coll->weights[pos2];
    }

  return wv1 - wv2;
}

/*
 * comp_func_coll_uca_exp_fo - compare function for sorting collatable
 *			       elements according to UCA algorithm,
 *			       with full order
 *
 *  Note: This function uses 'comp_func_coll_uca_exp' but applies a full order
 *	  The purpose is to provide a 'deterministic comparison' to eliminate
 *	   unpredictable results of sort algorithm (qsort).
 */
static int
comp_func_coll_uca_exp_fo (const void *arg1, const void *arg2)
{
  unsigned int pos1;
  unsigned int pos2;
  int cmp;

  pos1 = *((unsigned int *) arg1);
  pos2 = *((unsigned int *) arg2);

  cmp = comp_func_coll_uca_exp (arg1, arg2);

  return (cmp == 0) ? (int) (pos1 - pos2) : cmp;
}

/*
 * comp_func_coll_uca_exp - compare function for sorting collatable elements
 *			    according to UCA algorithm
 *
 *  Note: this function is used by dump_locale tool
 *	  The elements in array are 32 bit unsigned integers, keys which
 *	  may be Unicode points or contractions (when highest bit is set)
 *
 */
static int
comp_func_coll_uca_exp (const void *arg1, const void *arg2)
{
  unsigned int pos1;
  unsigned int pos2;
  COLL_DATA *coll = dump_coll_data;
  unsigned char utf8_buf_1[INTL_UTF8_MAX_CHAR_SIZE + 1];
  unsigned char utf8_buf_2[INTL_UTF8_MAX_CHAR_SIZE + 1];
  char *str1;
  char *str2;
  int size1, size2;

  pos1 = *((unsigned int *) arg1);
  pos2 = *((unsigned int *) arg2);

  assert (coll != NULL);

  /* build strings from the two keys and use the UCA collation function */
  if (INTL_IS_NEXT_CONTR (pos1))
    {
      str1 = coll->contr_list[INTL_GET_NEXT_CONTR_ID (pos1)].c_buf;
      size1 = coll->contr_list[INTL_GET_NEXT_CONTR_ID (pos1)].size;
    }
  else
    {
      size1 = intl_cp_to_utf8 (pos1, utf8_buf_1);
      utf8_buf_1[size1] = '\0';
      str1 = (char *) utf8_buf_1;
    }

  if (INTL_IS_NEXT_CONTR (pos2))
    {
      str2 = coll->contr_list[INTL_GET_NEXT_CONTR_ID (pos2)].c_buf;
      size2 = coll->contr_list[INTL_GET_NEXT_CONTR_ID (pos2)].size;
    }
  else
    {
      size2 = intl_cp_to_utf8 (pos2, utf8_buf_2);
      utf8_buf_2[size2] = '\0';
      str2 = (char *) utf8_buf_2;
    }

  return lang_strmatch_utf8_uca_w_coll_data (coll, false, (const unsigned char *) str1, size1,
					     (const unsigned char *) str2, size2, NULL, false, NULL, false);
}

/*
 * dump_collation_key - prints information on collation key
 *			(codepoint or contraction)
 * Returns :
 * coll(in) : collation data
 * key(in) : key to print
 * print_weight(in): true if weight should be printed
 * print_contr(in): true if contraction info should be printed
 */
static void
dump_collation_key (COLL_DATA * coll, const unsigned int key, bool print_weight, bool print_key)
{
  if (INTL_IS_NEXT_CONTR (key))
    {
      unsigned int contr_id = INTL_GET_NEXT_CONTR_ID (key);
      COLL_CONTRACTION *contr = &(coll->contr_list[contr_id]);

      dump_collation_contr (coll, contr, print_weight, print_key);
    }
  else
    {
      dump_collation_codepoint (coll, key, print_weight, print_key);
    }
}

/*
 * dump_collation_contr - prints information on collation contraction
 * Returns :
 * coll(in) : collation data
 * contr(in) : contraction
 * print_weight(in): true if weight should be printed
 * print_contr(in): true if contraction info should be printed
 */
static void
dump_collation_contr (COLL_DATA * coll, const COLL_CONTRACTION * contr, bool print_weight, bool print_contr)
{
  int i;

  assert (contr != NULL);

  assert (contr->cp_count <= LOC_MAX_UCA_CHARS_SEQ);

  if (print_contr)
    {
      unsigned int cp_list[LOC_MAX_UCA_CHARS_SEQ];
      bool print_utf8 = true;
      int cp_count;

      intl_utf8_to_cp_list ((const unsigned char *) contr->c_buf, strlen (contr->c_buf), cp_list, sizeof (cp_list),
			    &cp_count);

      assert (cp_count == contr->cp_count);

      printf ("Contr: ");
      for (i = 0; i < cp_count; i++)
	{
	  printf ("Ux%04X ", cp_list[i]);
	  if (cp_list[i] < 0x20)
	    {
	      print_utf8 = false;
	    }
	}

      if (print_utf8)
	{
	  printf (" | %s", contr->c_buf);
	}
    }

  if (!print_weight)
    {
      return;
    }

  if (coll->uca_exp_num <= 1)
    {
      printf (" | Weight : %04x", contr->wv);
      return;
    }

  assert (contr->uca_num > 0 && contr->uca_num <= MAX_UCA_EXP_CE);

  printf (" | Weight : ");
  for (i = 0; i < (int) contr->uca_num; i++)
    {
      printf ("[%04X.", UCA_GET_L1_W (contr->uca_w_l13[i]));
      printf ("%04X.", UCA_GET_L2_W (contr->uca_w_l13[i]));
      printf ("%04X.", UCA_GET_L3_W (contr->uca_w_l13[i]));
      if (coll->uca_opt.sett_strength >= TAILOR_QUATERNARY)
	{
	  printf ("%04X]", contr->uca_w_l4[i]);
	}
      else
	{
	  printf ("]");
	}
    }
}

/*
 * dump_collation_codepoint - prints information on collation codepoint
 * Returns :
 * coll(in)  : collation data
 * cp(in) : codepoint
 * print_weight(in): true if weight should be printed
 */
static void
dump_collation_codepoint (COLL_DATA * coll, const unsigned int cp, bool print_weight, bool print_cp)
{
  assert (cp <= INTL_MAX_UNICODE_CP_ALLOWED);

  if (print_cp)
    {
      unsigned char utf8_buf[INTL_UTF8_MAX_CHAR_SIZE + 1];

      memset (utf8_buf, 0, INTL_UTF8_MAX_CHAR_SIZE + 1);

      intl_cp_to_utf8 (cp, utf8_buf);

      printf ("CP: Ux%04X | %-4s", cp, ((cp > 0x20) ? utf8_buf : (unsigned char *) ""));
    }

  if (!print_weight)
    {
      return;
    }

  if (coll->uca_exp_num <= 1)
    {
      printf (" | Weight%s: %04x", ((int) cp < coll->w_count) ? "" : "(Computed)",
	      (((int) cp < coll->w_count) ? (coll->weights[cp]) : (cp + 1)));
      return;
    }

  assert (coll->uca_exp_num > 1);
  assert (coll->uca_exp_num <= MAX_UCA_EXP_CE);

  if ((int) cp < coll->w_count)
    {
      int i;
      UCA_L13_W *uca_w_l13 = &(coll->uca_w_l13[cp * coll->uca_exp_num]);

      printf (" | Weight : ");

      assert (coll->uca_num[cp] >= 1);
      assert (coll->uca_num[cp] <= MAX_UCA_EXP_CE);

      for (i = 0; i < coll->uca_num[cp]; i++)
	{
	  printf ("[%04X.", UCA_GET_L1_W (uca_w_l13[i]));
	  printf ("%04X.", UCA_GET_L2_W (uca_w_l13[i]));
	  printf ("%04X.", UCA_GET_L3_W (uca_w_l13[i]));
	  if (coll->uca_opt.sett_strength >= TAILOR_QUATERNARY)
	    {
	      printf ("%04X]", coll->uca_w_l4[cp * coll->uca_exp_num + i]);
	    }
	  else
	    {
	      printf ("]");
	    }
	}
    }
  else
    {
      printf (" | Weight(Computed) : %04X", cp + 1);
    }
}

/*
 * locale_check_and_set_shared_data - checks if data has already been taken
 *				      into account as shared data, otherwise
 *				      adds the new key as shared data
 * Returns : error status
 * lsd_type(in): shared data type
 * lsd_key(in) : key (name) of data
 * data(in) : data to be stored for key
 * ldml_context(in): context of LDML file
 * found_entry(out): shread data entry if found
 *
 *  Note : this is used in context of genlocale tool, to check for duplicate
 *	   collations, normalization or alphabet data
 */
static int
locale_check_and_set_shared_data (const LOC_SHARED_DATA_TYPE lsd_type, const char *lsd_key, const void *data,
				  LDML_CONTEXT * ldml_context, LOC_SHARED_DATA ** found_entry)
{
#define SHARED_DATA_INCR_SIZE 32

  int status = NO_ERROR;
  int i;

  assert (lsd_key != NULL);
  assert (found_entry != NULL);

  *found_entry = NULL;

  for (i = 0; i < count_shared_data; i++)
    {
      if (lsd_type == shared_data[i].lsd_type && strcmp (lsd_key, shared_data[i].lsd_key) == 0)
	{
	  *found_entry = &(shared_data[i]);
	  goto exit;
	}
    }

  /* set new shared data */
  if (alloced_shared_data <= count_shared_data)
    {
      LOC_SHARED_DATA *const realloc_shared_data = (LOC_SHARED_DATA *) realloc (shared_data,
										sizeof (LOC_SHARED_DATA) *
										(alloced_shared_data +
										 SHARED_DATA_INCR_SIZE));
      if (realloc_shared_data == NULL)
	{
	  LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
	  status = ER_LOC_GEN;
	  goto exit;
	}
      else
	{
	  shared_data = realloc_shared_data;
	}
      alloced_shared_data += SHARED_DATA_INCR_SIZE;
    }

  memset (&(shared_data[count_shared_data]), 0, sizeof (LOC_SHARED_DATA));
  shared_data[count_shared_data].lsd_type = lsd_type;
  strncpy (shared_data[count_shared_data].lsd_key, lsd_key, COLL_NAME_SIZE - 1);
  shared_data[count_shared_data].lsd_key[COLL_NAME_SIZE - 1] = '\0';

  if (data != NULL)
    {
      shared_data[count_shared_data].data = strdup ((const char *) data);
      if (shared_data[count_shared_data].data == NULL)
	{
	  LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
	  status = ER_LOC_GEN;
	  goto exit;
	}
    }

  if (ldml_context != NULL)
    {
      shared_data[count_shared_data].ldml_context.ldml_file = strdup (ldml_context->ldml_file);
      if (shared_data[count_shared_data].ldml_context.ldml_file == NULL)
	{
	  LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
	  status = ER_LOC_GEN;
	  goto exit;
	}
      shared_data[count_shared_data].ldml_context.line_no = ldml_context->line_no;
    }

  count_shared_data++;

exit:
  return status;

#undef SHARED_DATA_INCR_SIZE
}

/*
 * locale_free_shared_data -
 *
 *  Note : this is used in context of genlocale tool.
 */
void
locale_free_shared_data (void)
{
  if (shared_data != NULL)
    {
      int i;
      assert (count_shared_data > 0);
      assert (alloced_shared_data > 0);

      for (i = 0; i < count_shared_data; i++)
	{
	  if (shared_data[i].data != NULL)
	    {
	      free (shared_data[i].data);
	      shared_data[i].data = NULL;
	    }
	  if (shared_data[i].ldml_context.ldml_file != NULL)
	    {
	      free (shared_data[i].ldml_context.ldml_file);
	      shared_data[i].ldml_context.ldml_file = NULL;
	    }
	}

      free (shared_data);
      shared_data = NULL;
    }

  alloced_shared_data = 0;
  count_shared_data = 0;
}

/*
 * start_unicode_file() - XML element start function
 * "ldml unicodefile"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_unicode_file (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  char *att_val = NULL;

  assert (data != NULL);

  ld = (LOCALE_DATA *) XML_USER_DATA (pd);

  if (strlen (ld->unicode_data_file) != 0)
    {
      PRINT_DEBUG_START (data, attr, "Only one unicodefile tag is allowed", -1);
      return -1;
    }

  att_val = NULL;
  if (xml_get_att_value (attr, "CUBRIDUnicodeDataFilePath", &att_val) == 0)
    {
      assert (att_val != NULL);

      strncpy (ld->unicode_data_file, att_val, sizeof (ld->unicode_data_file) - 1);
      ld->unicode_data_file[sizeof (ld->unicode_data_file) - 1] = '\0';
      ld->alpha_tailoring.alphabet_mode = 1;
      ld->unicode_mode = 1;
    }

  PRINT_DEBUG_START (data, attr, "", 0);

  return 0;
}

/*
 * locale_destroy_normalization_data() - frees memory used by the specified
 *			  locale for storing its computed normalization data.
 *    return:
 *    ld(in/out): locale to use
 */
void
locale_destroy_normalization_data (UNICODE_NORMALIZATION * norm)
{
  if (norm == NULL)
    {
      return;
    }
  if (norm->unicode_mappings != NULL)
    {
      free (norm->unicode_mappings);
      norm->unicode_mappings = NULL;
    }

  if (norm->list_full_decomp != NULL)
    {
      free (norm->list_full_decomp);
      norm->list_full_decomp = NULL;
    }

  if (norm->unicode_mapping_index != NULL)
    {
      free (norm->unicode_mapping_index);
      norm->unicode_mapping_index = NULL;
    }
}

/*
 * locale_save_normalization_to_C_file() - saves normalization data
 *			    to C source file as variables, for later use
 *			    when generating the locale shared library.
 *
 * return: error code
 * fp(in): file pointer where to write the data
 * ld(in): locale data
 */
static int
locale_save_normalization_to_C_file (FILE * fp, LOCALE_DATA * ld)
{
  int i;
  UNICODE_MAPPING *um;
  UNICODE_NORMALIZATION *norm;

  assert (ld != NULL);
  assert (fp != NULL);

  norm = &(ld->unicode_normalization);

  if (norm->do_not_save)
    {
      goto exit;
    }

  PRINT_VAR_TO_C_FILE (fp, "int", "unicode_mappings_count", norm->unicode_mappings_count, "%d",
		       UNICODE_NORMALIZATION_DECORATOR);

  PRINT_NUM_ARRAY_TO_C_FILE (fp, "unicode_mapping_index", "int", "%d", MAX_UNICODE_CHARS, norm->unicode_mapping_index,
			     UNICODE_NORMALIZATION_DECORATOR);
  PRINT_NUM_ARRAY_TO_C_FILE (fp, "list_full_decomp", "int", "%d", MAX_UNICODE_CHARS, norm->list_full_decomp,
			     UNICODE_NORMALIZATION_DECORATOR);

  fprintf (fp, "" DLL_EXPORT_PREFIX "const UNICODE_MAPPING unicode_mappings_%s[] = {", UNICODE_NORMALIZATION_DECORATOR);
  for (i = 0; i < norm->unicode_mappings_count; i++)
    {
      um = &(norm->unicode_mappings[i]);
      fprintf (fp, "\t{%u, %d, ", um->cp, um->size);
      PRINT_STRING_TO_C_FILE (fp, um->buffer, um->size);
      fprintf (fp, "}%s\n", (i < norm->unicode_mappings_count - 1) ? "," : "");
    }
  fprintf (fp, "};");

exit:

  return 0;
}

#define NORM_MAPPING_DUMP_MODE_FULL 0
#define NORM_MAPPING_DUMP_MODE_COMP 1
#define NORM_MAPPING_DUMP_MODE_DECOMP 2
/*
 * dump_locale_normalization - dump the selected UNICODE_NORMALIZATION
 *			       structure in human-readable text format.
 * Returns : NO_ERROR.
 * norm(in)  : the UNICODE_NORMALIZATION to be dumped in text format.
 */
static void
dump_locale_normalization (UNICODE_NORMALIZATION * norm)
{
  int i;

  printf ("Sorted list of unicode mappings: \n");
  for (i = 0; i < norm->unicode_mappings_count; i++)
    {
      printf ("%d.\t ", i + 1);
      dump_unicode_mapping (&(norm->unicode_mappings[i]), NORM_MAPPING_DUMP_MODE_FULL);
      printf ("\n");
    }

  printf ("\n Unicode composition mappings \n");
  for (i = 0; i < MAX_UNICODE_CHARS; i++)
    {
      int j, comp_start, comp_end;

      printf ("CP: %04X | \t Comp: ", i);

      if (!CP_HAS_MAPPINGS (norm->unicode_mapping_index[i]))
	{
	  printf ("NO\n");
	  continue;
	}

      comp_start = GET_MAPPING_OFFSET (norm->unicode_mapping_index[i]);
      comp_end = GET_MAPPING_OFFSET (norm->unicode_mapping_index[i + 1]);

      printf ("(%2d) From: %d To: %d | \t", comp_end - comp_start, comp_start, comp_end);

      for (j = comp_start; j < comp_end; j++)
	{
	  dump_unicode_mapping (&(norm->unicode_mappings[j]), NORM_MAPPING_DUMP_MODE_COMP);
	  printf (" | ");
	}
      printf ("\n");
    }

  printf ("\n Unicode decomposition mappings\n");
  for (i = 0; i < MAX_UNICODE_CHARS; i++)
    {
      int decomp_idx = norm->list_full_decomp[i];

      printf ("CP: %04X | Decomp : ", i);

      if (decomp_idx < 0)
	{
	  printf ("NO\n");
	  continue;
	}

      assert (decomp_idx >= 0);

      dump_unicode_mapping (&(norm->unicode_mappings[decomp_idx]), NORM_MAPPING_DUMP_MODE_DECOMP);

      printf ("\n");
    }
}

/*
 * dump_unicode_mapping() - Dump a UNICODE_MAPPING to the console.
 *
 * Returns:
 * um(in) : unicode mapping to dump
 */
static void
dump_unicode_mapping (UNICODE_MAPPING * um, const int mode)
{
  unsigned int cp;
  unsigned char *next;
  unsigned char *cur_str;

  if (um == NULL)
    {
      printf ("Null mapping.\n");
      return;
    }

  if (mode == NORM_MAPPING_DUMP_MODE_FULL)
    {
      printf ("CP: %04X", um->cp);
      if (um->size > 0)
	{
	  printf (" ->");
	}
    }

  cur_str = um->buffer;
  next = cur_str;
  while (next < um->buffer + um->size)
    {
      cp = intl_utf8_to_cp (cur_str, (int) INTL_UTF8_MAX_CHAR_SIZE, &next);
      printf ("%04X ", cp);
      cur_str = next;
    }

  if (mode == NORM_MAPPING_DUMP_MODE_COMP)
    {
      printf (" -> CP: %04X", um->cp);
    }
}

/*
 * dump_console_conversion - dump Console text conversion data structure
 *			     in human-readable text format.
 * Returns : NO_ERROR.
 * tc(in): the TEXT_CONVERSION to be dumped in text format.
 */
static int
dump_console_conversion (TEXT_CONVERSION * tc)
{
  unsigned char utf8_seq[INTL_UTF8_MAX_CHAR_SIZE + 1];
  unsigned char cnv_utf8_buf[2 * 3 + 1];
  unsigned char *cnv_utf8;
  CONV_CP_TO_BYTES *c_item;
  unsigned int utf8_cp, con_cp;
  unsigned char *next;
  int utf8_size;
  unsigned char *char_to_print = NULL;
  int err;

  if (tc == NULL || tc->conv_type == TEXT_CONV_NO_CONVERSION)
    {
      printf ("\nNo console conversion for this locale.\n\n");
      goto exit;
    }

  printf ("\nType: ");
  if (tc->conv_type == TEXT_CONV_ISO_88591_BUILTIN || tc->conv_type == TEXT_CONV_ISO_88599_BUILTIN)
    {
      printf ("built-in %s console to UTF-8 encoding \n",
	      (tc->conv_type == TEXT_CONV_ISO_88591_BUILTIN) ? "ISO 8859-1" : "ISO 8859-9");
    }
  else
    {
      printf ("%s byte console to UTF-8 encoding \n", (tc->conv_type == TEXT_CONV_GENERIC_1BYTE) ? "single" : "double");
    }

  printf ("Windows codepages: %s\n", tc->win_codepages);
  printf ("Linux LANG charset values: %s\n", tc->nl_lang_str);

  if (tc->conv_type != TEXT_CONV_GENERIC_1BYTE && tc->conv_type != TEXT_CONV_GENERIC_2BYTE)
    {
      goto exit;
    }

  printf ("\nConsole to UTF-8 conversion:\n");
  printf ("Console codepoint -> Unicode codepoint | Character (UTF-8) \n");
  for (con_cp = 0; con_cp <= tc->text_last_cp; con_cp++)
    {
      unsigned char dbcs_seq[2 + 1];
      int dbcs_size;

      if (tc->conv_type == TEXT_CONV_GENERIC_2BYTE && con_cp <= 0xff && tc->byte_flag[con_cp] != 0)
	{
	  printf ("%02X -> Undefined or leading byte\n", con_cp);
	  continue;
	}
      utf8_cp = con_cp;

      if (con_cp >= tc->text_first_cp)
	{
	  c_item = &(tc->text_to_utf8[con_cp - tc->text_first_cp]);
	  utf8_cp = intl_utf8_to_cp (c_item->bytes, c_item->size, &next);
	  assert ((unsigned char *) next - c_item->bytes == c_item->size);

	  if (utf8_cp == 0x3f)
	    {
	      assert (con_cp != 0x3f);
	      printf ("%04X -> Undefined codepoint\n", con_cp);
	      continue;
	    }

	  utf8_size = intl_cp_to_utf8 (utf8_cp, utf8_seq);
	  assert ((unsigned int) utf8_size < sizeof (utf8_seq));
	  utf8_seq[utf8_size] = '\0';

	  char_to_print = (utf8_cp > 0x20) ? utf8_seq : (unsigned char *) "";

	  dbcs_size = intl_cp_to_dbcs (con_cp, tc->byte_flag, dbcs_seq);
	  dbcs_seq[dbcs_size] = '\0';

	  cnv_utf8 = cnv_utf8_buf;
	  utf8_size = sizeof (cnv_utf8_buf);

	  if (tc->conv_type == TEXT_CONV_GENERIC_1BYTE)
	    {
	      err = intl_text_single_byte_to_utf8_ext (tc, dbcs_seq, dbcs_size, &cnv_utf8, &utf8_size);
	      assert (err == NO_ERROR);
	    }
	  else
	    {
	      if (dbcs_size == 2 && tc->byte_flag[dbcs_seq[0]] != 1)
		{
		  /* invalid console codepoint */
		  cnv_utf8 = NULL;
		}
	      else
		{
		  err = intl_text_dbcs_to_utf8_ext (tc, dbcs_seq, dbcs_size, &cnv_utf8, &utf8_size);
		  assert (err == NO_ERROR);
		}
	    }

	  if (cnv_utf8 != NULL)
	    {
	      assert ((unsigned int) utf8_size <= sizeof (cnv_utf8_buf));
	      char_to_print = cnv_utf8;
	    }
	}
      else
	{
	  utf8_size = intl_cp_to_utf8 (utf8_cp, utf8_seq);
	  assert ((unsigned int) utf8_size < sizeof (utf8_seq));
	  utf8_seq[utf8_size] = '\0';

	  char_to_print = (utf8_cp > 0x20) ? utf8_seq : (unsigned char *) "";
	}

      if (con_cp <= 0xff)
	{
	  printf ("%02X -> Ux%04X | %s\n", con_cp, utf8_cp, char_to_print);
	}
      else
	{
	  printf ("%04X -> Ux%04X | %s\n", con_cp, utf8_cp, char_to_print);
	}
    }

  if (tc->text_last_cp < ((tc->conv_type == TEXT_CONV_GENERIC_1BYTE) ? (unsigned int) 0xff : (unsigned int) 0xffff))
    {
      if (tc->conv_type == TEXT_CONV_GENERIC_1BYTE)
	{
	  printf ("Range %02X - FF is not mapped\n", tc->text_last_cp + 1);
	}
      else
	{
	  printf ("Range %04X - FFFF is not mapped\n", tc->text_last_cp + 1);
	}
    }

  printf ("\n\nUTF-8 to console conversion:\n");
  printf ("Unicode codepoint [Unicode character] ->" " Console codepoint | Character (UTF-8 encoding)\n");
  for (utf8_cp = 0; utf8_cp <= tc->utf8_last_cp; utf8_cp++)
    {
      if (utf8_cp > 0x20)
	{
	  utf8_size = intl_cp_to_utf8 (utf8_cp, utf8_seq);
	  assert ((unsigned int) utf8_size < sizeof (utf8_seq));
	  utf8_seq[utf8_size] = '\0';
	}
      else
	{
	  utf8_seq[0] = ' ';
	  utf8_seq[1] = '\0';
	}

      con_cp = utf8_cp;
      char_to_print = utf8_seq;

      if (utf8_cp >= tc->utf8_first_cp)
	{
	  c_item = &(tc->utf8_to_text[utf8_cp - tc->utf8_first_cp]);

	  con_cp = intl_dbcs_to_cp (c_item->bytes, c_item->size, tc->byte_flag, &next);
	  assert (next - c_item->bytes == c_item->size);

	  if (con_cp == 0x3f)
	    {
	      assert (utf8_cp != 0x3f);
	      printf ("Ux%04X [%s] -> Not mapped\n", utf8_cp, utf8_seq);
	      continue;
	    }

	  cnv_utf8 = cnv_utf8_buf;
	  utf8_size = sizeof (cnv_utf8_buf);

	  if (tc->conv_type == TEXT_CONV_GENERIC_1BYTE)
	    {
	      err = intl_text_single_byte_to_utf8_ext (tc, c_item->bytes, c_item->size, &cnv_utf8, &utf8_size);
	      assert (err == NO_ERROR);
	    }
	  else
	    {
	      err = intl_text_dbcs_to_utf8_ext (tc, c_item->bytes, c_item->size, &cnv_utf8, &utf8_size);
	      assert (err == NO_ERROR);
	    }
	  assert ((unsigned int) utf8_size <= sizeof (cnv_utf8_buf));
	  char_to_print = cnv_utf8 ? cnv_utf8 : utf8_seq;
	}

      if (tc->conv_type == TEXT_CONV_GENERIC_1BYTE)
	{
	  printf ("Ux%04X [%s] -> %02X | %s\n", utf8_cp, utf8_seq, con_cp, char_to_print);
	}
      else
	{
	  printf ("Ux%04X [%s] -> %04X | %s\n", utf8_cp, utf8_seq, con_cp, char_to_print);
	}
    }

  printf ("Codepoints above Ux%04X are not mapped\n", tc->utf8_last_cp);

exit:
  return 0;
}

#define BUF_PUT_INT16(buf,v)				     \
  do {							     \
    unsigned short nv = htons(v);			     \
    *((unsigned char *) (buf)) = ((unsigned char *) &nv)[1]; \
    buf = (char *) (buf) + 1;				     \
    *((unsigned char *) (buf)) = ((unsigned char *) &nv)[0]; \
    buf = (char *) (buf) + 1;				     \
    } while (0)

#define BUF_PUT_INT32(buf,v)				     \
  do {							     \
    unsigned int nv = htonl(v);				     \
    *((unsigned char *) (buf)) = ((unsigned char *) &nv)[3]; \
    buf = (char *) (buf) + 1;				     \
    *((unsigned char *) (buf)) = ((unsigned char *) &nv)[2]; \
    buf = (char *) (buf) + 1;				     \
    *((unsigned char *) (buf)) = ((unsigned char *) &nv)[1]; \
    buf = (char *) (buf) + 1;				     \
    *((unsigned char *) (buf)) = ((unsigned char *) &nv)[0]; \
    buf = (char *) (buf) + 1;				     \
    } while (0)

#define BUF_ALIGN(buf, align) \
        ((char *)((((UINTPTR)(buf) + ((UINTPTR)((align)-1)))) \
                  & ~((UINTPTR)((align)-1))))

/*
 * locale_compute_coll_checksum() - Computes the MD5 checksum of collation
 *
 * Returns: error status
 * coll_data(in/out):
 */
static int
locale_compute_coll_checksum (COLL_DATA * cd)
{
  int input_size = 0;
  char *input_buf = NULL;
  char *buf_pos;
  int error_code = NO_ERROR;
  int cp, w;

  if (cd->uca_opt.sett_expansions)
    {
      /* Weights L1-L3 */
      input_size += cd->uca_exp_num * cd->w_count * sizeof (cd->uca_w_l13[0]);
      if (cd->uca_opt.sett_strength >= TAILOR_QUATERNARY)
	{
	  /* Weights L4 */
	  input_size += cd->uca_exp_num * cd->w_count * sizeof (cd->uca_w_l4[0]);
	}
    }
  else
    {
      /* single level weights */
      input_size += cd->w_count * sizeof (cd->weights[0]);
    }

  /* next_cp */
  input_size += cd->w_count * sizeof (cd->next_cp[0]);

  if (cd->count_contr > 0)
    {
      /* contractions list */
      input_size += cd->count_contr * sizeof (COLL_CONTRACTION);

      input_size += sizeof (cd->contr_min_size);
      input_size += sizeof (cd->cp_first_contr_offset);
      input_size += sizeof (cd->cp_first_contr_count);
    }

  input_size += sizeof (UCA_OPTIONS);

  /* build buffer */
  input_buf = (char *) malloc (input_size);
  if (input_buf == NULL)
    {
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      return ER_LOC_GEN;
    }

  memset (input_buf, 0, input_size);
  buf_pos = input_buf;

  if (cd->uca_opt.sett_expansions)
    {
      for (cp = 0; cp < cd->w_count; cp++)
	{
	  for (w = 0; w < cd->uca_exp_num; w++)
	    {
	      BUF_PUT_INT32 (buf_pos, cd->uca_w_l13[cp * cd->uca_exp_num + w]);
	    }
	}

      if (cd->uca_opt.sett_strength >= TAILOR_QUATERNARY)
	{
	  /* Weights L4 */
	  for (cp = 0; cp < cd->w_count; cp++)
	    {
	      for (w = 0; w < cd->uca_exp_num; w++)
		{
		  BUF_PUT_INT16 (buf_pos, cd->uca_w_l4[cp * cd->uca_exp_num + w]);
		}
	    }
	}
    }
  else
    {
      for (cp = 0; cp < cd->w_count; cp++)
	{
	  BUF_PUT_INT32 (buf_pos, cd->weights[cp]);
	}
    }

  for (cp = 0; cp < cd->w_count; cp++)
    {
      BUF_PUT_INT32 (buf_pos, cd->next_cp[cp]);
    }

  if (cd->count_contr > 0)
    {
      /* contractions list */
      for (cp = 0; cp < cd->count_contr; cp++)
	{
	  COLL_CONTRACTION *c = &(cd->contr_list[cp]);

	  BUF_PUT_INT32 (buf_pos, c->next);
	  BUF_PUT_INT32 (buf_pos, c->wv);
	  for (w = 0; w < MAX_UCA_EXP_CE; w++)
	    {
	      BUF_PUT_INT32 (buf_pos, c->uca_w_l13[w]);
	    }
	  for (w = 0; w < MAX_UCA_EXP_CE; w++)
	    {
	      BUF_PUT_INT16 (buf_pos, c->uca_w_l4[w]);
	    }

	  memcpy (buf_pos, c->c_buf, sizeof (c->c_buf));
	  buf_pos += sizeof (c->c_buf);

	  *buf_pos++ = c->cp_count;
	  *buf_pos++ = c->size;
	  *buf_pos++ = c->uca_num;

	  buf_pos +=
	    sizeof (COLL_CONTRACTION) - (2 * sizeof (int) + MAX_UCA_EXP_CE * sizeof (int) +
					 MAX_UCA_EXP_CE * sizeof (short) + sizeof (c->c_buf) + 3 * sizeof (char));
	}

      BUF_PUT_INT32 (buf_pos, cd->contr_min_size);

      BUF_PUT_INT32 (buf_pos, cd->cp_first_contr_offset);

      BUF_PUT_INT32 (buf_pos, cd->cp_first_contr_count);
    }

  BUF_PUT_INT32 (buf_pos, cd->uca_opt.sett_strength);
  *buf_pos++ = (unsigned char) (cd->uca_opt.sett_backwards);
  *buf_pos++ = (unsigned char) (cd->uca_opt.sett_caseLevel);
  buf_pos = BUF_ALIGN (buf_pos, sizeof (int));
  BUF_PUT_INT32 (buf_pos, cd->uca_opt.sett_caseFirst);
  *buf_pos++ = (unsigned char) (cd->uca_opt.sett_expansions);
  buf_pos = BUF_ALIGN (buf_pos, sizeof (int));
  BUF_PUT_INT32 (buf_pos, cd->uca_opt.sett_contr_policy);
  *buf_pos++ = (unsigned char) (cd->uca_opt.use_only_first_ce);
  buf_pos = BUF_ALIGN (buf_pos, sizeof (int));
  BUF_PUT_INT32 (buf_pos, cd->uca_opt.sett_match_contr);

  if (buf_pos - input_buf != input_size)
    {
      LOG_LOCALE_ERROR ("Error on collation checksum", ER_LOC_GEN, true);
      free (input_buf);
      return ER_LOC_GEN;
    }
  assert (buf_pos - input_buf == input_size);

  memset (cd->checksum, 0, sizeof (cd->checksum));
  error_code = crypt_md5_buffer_hex (input_buf, input_size, cd->checksum);

  free (input_buf);
  return error_code;
}

/*
 * locale_alphabet_data_size() - Computes the size required by alphabet data
 *
 * Returns: size in bytes
 * a(in):
 */
static int
locale_alphabet_data_size (ALPHABET_DATA * a)
{
  int input_size = 0;

  input_size += sizeof (a->a_type);
  input_size += a->l_count * a->lower_multiplier * sizeof (a->lower_cp[0]);
  input_size += a->l_count * a->upper_multiplier * sizeof (a->upper_cp[0]);

  return input_size;
}

/*
 * locale_alphabet_data_to_buf() - Saves alphabet data to a buffer
 *
 * Returns: size in bytes
 * a(in):
 * buf(out):
 *
 *  Note : this is used in for data checksum purpose
 */
static int
locale_alphabet_data_to_buf (ALPHABET_DATA * a, char *buf)
{
  char *buf_pos = buf;
  int cp, m;

  BUF_PUT_INT32 (buf_pos, a->a_type);

  for (cp = 0; cp < a->l_count; cp++)
    {
      for (m = 0; m < a->lower_multiplier; m++)
	{
	  BUF_PUT_INT32 (buf_pos, a->lower_cp[cp * a->lower_multiplier + m]);
	}
    }

  for (cp = 0; cp < a->l_count; cp++)
    {
      for (m = 0; m < a->upper_multiplier; m++)
	{
	  BUF_PUT_INT32 (buf_pos, a->upper_cp[cp * a->upper_multiplier + m]);
	}
    }

  return CAST_BUFLEN (buf_pos - buf);
}

/*
 * locale_compute_locale_checksum() - Computes the MD5 checksum of locale data
 *
 * Returns: error status
 * ld(in/out):
 */
static int
locale_compute_locale_checksum (LOCALE_DATA * ld)
{
  int input_size = 0;
  char *input_buf = NULL;
  char *buf_pos;
  int error_code = NO_ERROR;
  int cp;

  input_size += sizeof (ld->dateFormat);
  input_size += sizeof (ld->timeFormat);
  input_size += sizeof (ld->datetimeFormat);
  input_size += sizeof (ld->timestampFormat);
  input_size += sizeof (ld->timetzFormat);
  input_size += sizeof (ld->datetimetzFormat);
  input_size += sizeof (ld->timestamptzFormat);

  input_size += sizeof (ld->month_names_abbreviated);
  input_size += sizeof (ld->month_names_wide);
  input_size += sizeof (ld->day_names_abbreviated);
  input_size += sizeof (ld->day_names_wide);
  input_size += sizeof (ld->am_pm);

  input_size += sizeof (ld->day_names_abbr_parse_order);
  input_size += sizeof (ld->day_names_wide_parse_order);
  input_size += sizeof (ld->month_names_abbr_parse_order);
  input_size += sizeof (ld->month_names_wide_parse_order);
  input_size += sizeof (ld->am_pm_parse_order);

  input_size += sizeof (ld->number_decimal_sym);
  input_size += sizeof (ld->number_group_sym);
  input_size += sizeof (ld->default_currency_code);

  input_size += locale_alphabet_data_size (&(ld->alphabet));
  input_size += locale_alphabet_data_size (&(ld->identif_alphabet));

  input_size += sizeof (ld->txt_conv.conv_type);
  if (ld->txt_conv.conv_type == TEXT_CONV_GENERIC_1BYTE || ld->txt_conv.conv_type == TEXT_CONV_GENERIC_2BYTE)
    {
      TEXT_CONVERSION *tc = &(ld->txt_conv);

      input_size += sizeof (tc->byte_flag);
      input_size += sizeof (tc->utf8_first_cp);
      input_size += sizeof (tc->utf8_last_cp);
      input_size += sizeof (tc->text_first_cp);
      input_size += sizeof (tc->text_last_cp);

      input_size += strlen (tc->nl_lang_str);
      input_size += strlen (tc->win_codepages);

      input_size += (tc->utf8_last_cp - tc->utf8_first_cp + 1) * sizeof (CONV_CP_TO_BYTES);
      input_size += (tc->text_last_cp - tc->text_first_cp + 1) * sizeof (CONV_CP_TO_BYTES);
    }

  if (!(ld->unicode_normalization.do_not_save))
    {
      UNICODE_NORMALIZATION *un = &(ld->unicode_normalization);

      input_size += MAX_UNICODE_CHARS * sizeof (un->unicode_mapping_index[0]);
      input_size += MAX_UNICODE_CHARS * sizeof (un->list_full_decomp[0]);

      input_size += un->unicode_mappings_count * sizeof (UNICODE_MAPPING);
    }

  /* build buffer */
  input_buf = (char *) malloc (input_size);
  if (input_buf == NULL)
    {
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      return ER_LOC_GEN;
    }

  buf_pos = input_buf;
  memset (input_buf, 0, input_size);

  /* formats */
  memcpy (buf_pos, ld->dateFormat, sizeof (ld->dateFormat));
  buf_pos += sizeof (ld->dateFormat);

  memcpy (buf_pos, ld->timeFormat, sizeof (ld->timeFormat));
  buf_pos += sizeof (ld->timeFormat);

  memcpy (buf_pos, ld->datetimeFormat, sizeof (ld->datetimeFormat));
  buf_pos += sizeof (ld->datetimeFormat);

  memcpy (buf_pos, ld->timestampFormat, sizeof (ld->timestampFormat));
  buf_pos += sizeof (ld->timestampFormat);

  memcpy (buf_pos, ld->timetzFormat, sizeof (ld->timetzFormat));
  buf_pos += sizeof (ld->timetzFormat);

  memcpy (buf_pos, ld->datetimetzFormat, sizeof (ld->datetimetzFormat));
  buf_pos += sizeof (ld->datetimetzFormat);

  memcpy (buf_pos, ld->timestamptzFormat, sizeof (ld->timestamptzFormat));
  buf_pos += sizeof (ld->timestamptzFormat);

  /* calendar names */
  memcpy (buf_pos, ld->month_names_abbreviated, sizeof (ld->month_names_abbreviated));
  buf_pos += sizeof (ld->month_names_abbreviated);

  memcpy (buf_pos, ld->month_names_wide, sizeof (ld->month_names_wide));
  buf_pos += sizeof (ld->month_names_wide);

  memcpy (buf_pos, ld->day_names_abbreviated, sizeof (ld->day_names_abbreviated));
  buf_pos += sizeof (ld->day_names_abbreviated);

  memcpy (buf_pos, ld->day_names_wide, sizeof (ld->day_names_wide));
  buf_pos += sizeof (ld->day_names_wide);

  memcpy (buf_pos, ld->am_pm, sizeof (ld->am_pm));
  buf_pos += sizeof (ld->am_pm);

  /* calendar parsing order */
  memcpy (buf_pos, ld->day_names_abbr_parse_order, sizeof (ld->day_names_abbr_parse_order));
  buf_pos += sizeof (ld->day_names_abbr_parse_order);

  memcpy (buf_pos, ld->day_names_wide_parse_order, sizeof (ld->day_names_wide_parse_order));
  buf_pos += sizeof (ld->day_names_wide_parse_order);

  memcpy (buf_pos, ld->month_names_abbr_parse_order, sizeof (ld->month_names_abbr_parse_order));
  buf_pos += sizeof (ld->month_names_abbr_parse_order);

  memcpy (buf_pos, ld->month_names_wide_parse_order, sizeof (ld->month_names_wide_parse_order));
  buf_pos += sizeof (ld->month_names_wide_parse_order);

  memcpy (buf_pos, ld->am_pm_parse_order, sizeof (ld->am_pm_parse_order));
  buf_pos += sizeof (ld->am_pm_parse_order);

  /* number symbols */
  memcpy (buf_pos, &(ld->number_decimal_sym), sizeof (ld->number_decimal_sym));
  buf_pos += sizeof (ld->number_decimal_sym);

  memcpy (buf_pos, &(ld->number_group_sym), sizeof (ld->number_group_sym));
  buf_pos += sizeof (ld->number_group_sym);

  BUF_PUT_INT32 (buf_pos, ld->default_currency_code);

  /* alphabets */
  buf_pos += locale_alphabet_data_to_buf (&(ld->alphabet), buf_pos);
  buf_pos += locale_alphabet_data_to_buf (&(ld->identif_alphabet), buf_pos);

  /* text conversion */
  BUF_PUT_INT32 (buf_pos, ld->txt_conv.conv_type);

  if (ld->txt_conv.conv_type == TEXT_CONV_GENERIC_1BYTE || ld->txt_conv.conv_type == TEXT_CONV_GENERIC_2BYTE)
    {
      TEXT_CONVERSION *tc = &(ld->txt_conv);

      memcpy (buf_pos, tc->byte_flag, sizeof (tc->byte_flag));
      buf_pos += sizeof (tc->byte_flag);

      BUF_PUT_INT32 (buf_pos, tc->utf8_first_cp);
      BUF_PUT_INT32 (buf_pos, tc->utf8_last_cp);
      BUF_PUT_INT32 (buf_pos, tc->text_first_cp);
      BUF_PUT_INT32 (buf_pos, tc->text_last_cp);

      memcpy (buf_pos, tc->nl_lang_str, strlen (tc->nl_lang_str));
      buf_pos += strlen (tc->nl_lang_str);

      memcpy (buf_pos, tc->win_codepages, strlen (tc->win_codepages));
      buf_pos += strlen (tc->win_codepages);

      memcpy (buf_pos, tc->utf8_to_text, (tc->utf8_last_cp - tc->utf8_first_cp + 1) * sizeof (CONV_CP_TO_BYTES));
      buf_pos += (tc->utf8_last_cp - tc->utf8_first_cp + 1) * sizeof (CONV_CP_TO_BYTES);

      memcpy (buf_pos, tc->text_to_utf8, (tc->text_last_cp - tc->text_first_cp + 1) * sizeof (CONV_CP_TO_BYTES));
      buf_pos += (tc->text_last_cp - tc->text_first_cp + 1) * sizeof (CONV_CP_TO_BYTES);
    }

  if (!(ld->unicode_normalization.do_not_save))
    {
      UNICODE_NORMALIZATION *un = &(ld->unicode_normalization);

      for (cp = 0; cp < MAX_UNICODE_CHARS; cp++)
	{
	  BUF_PUT_INT32 (buf_pos, un->unicode_mapping_index[cp]);
	}

      for (cp = 0; cp < MAX_UNICODE_CHARS; cp++)
	{
	  BUF_PUT_INT32 (buf_pos, un->list_full_decomp[cp]);
	}

      for (cp = 0; cp < un->unicode_mappings_count; cp++)
	{
	  UNICODE_MAPPING *um = &(un->unicode_mappings[cp]);
	  BUF_PUT_INT32 (buf_pos, um->cp);
	  BUF_PUT_INT32 (buf_pos, um->size);
	  memcpy (buf_pos, um->buffer, sizeof (um->buffer));
	  buf_pos += sizeof (um->buffer);

	  buf_pos += sizeof (UNICODE_MAPPING) - (sizeof (um->buffer) + 2 * sizeof (int));
	}
    }

  if (buf_pos - input_buf != input_size)
    {
      LOG_LOCALE_ERROR ("Error on locale checksum", ER_LOC_GEN, true);
      free (input_buf);
      return ER_LOC_GEN;
    }
  assert (buf_pos - input_buf == input_size);

  memset (ld->checksum, 0, sizeof (ld->checksum));
  error_code = crypt_md5_buffer_hex (input_buf, input_size, ld->checksum);

  free (input_buf);
  return error_code;
}

/*
 * common_collation_end_rule() - finishes a collation rule
 *
 * Returns: error status
 * data(in/out): user data
 * ld(in/out): locale data
 * rule_id(in): collation rule id (position in rule list)
 * t_rule(in/out): collation rule
 */
static int
common_collation_end_rule (void *data, LOCALE_DATA * ld, const int rule_id, TAILOR_RULE * t_rule)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;

  assert (data != NULL);
  assert (ld != NULL);
  assert (t_rule != NULL);

  if (ld->last_rule_level != TAILOR_UNDEFINED)
    {
      if (ld->last_rule_level != t_rule->level)
	{
	  /* first rule must match the level of anchor */
	  pd->xml_error = XML_CUB_ERR_PARSER;
	  PRINT_DEBUG_END (data, "Rule level doesn't match reset level", -1);
	  return -1;
	}

      /* reset level of anchor : don't need to check for following rules */
      ld->last_rule_level = TAILOR_UNDEFINED;
    }

  t_rule->direction = ld->last_rule_dir;
  t_rule->r_pos_type = ld->last_rule_pos_type;

  /* last tailor character is the new reference */
  if (!(t_rule->multiple_chars))
    {
      /* reset rule position */
      ld->last_rule_pos_type = RULE_POS_BUFFER;

      assert (t_rule->t_buf_size > 0);

      ld->last_r_buf_size = t_rule->t_buf_size;
      ld->last_r_buf_p = t_rule->t_buf;

      memcpy (ld->last_anchor_buf, t_rule->t_buf, t_rule->t_buf_size);
      ld->last_anchor_buf[t_rule->t_buf_size] = '\0';
    }

  /* BEFORE applies only for the first rule after <reset> */
  ld->last_rule_dir = TAILOR_AFTER;

  if (pd->verbose)
    {
      char msg[ERR_MSG_SIZE];
      int xml_line_no = XML_GetCurrentLineNumber (pd->xml_parser);
      int xml_col_no = XML_GetCurrentColumnNumber (pd->xml_parser);

      snprintf (msg, sizeof (msg) - 1, "* Rule %d, L :%d, Dir:%d, PosType:%d, Mc:%d ;" " XML: line: %d , col:%d *",
		rule_id, t_rule->level, t_rule->direction, t_rule->r_pos_type, t_rule->multiple_chars, xml_line_no,
		xml_col_no);
      PRINT_DEBUG_END (data, msg, 0);
    }

  return 0;
}

/*
 * common_collation_start_rule() - starts a collation rule
 *
 * Returns: error status
 * data(in/out): user data
 * attr(in): element name
 * ld(in/out): locale data
 * t_rule(in/out): collation rule
 */
static int
common_collation_start_rule (void *data, const char **attr, LOCALE_DATA * ld, TAILOR_RULE * t_rule)
{
  char *ref_buf_p = NULL;
  int ref_buf_size = 0;
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;

  assert (data != NULL);
  assert (ld != NULL);
  assert (t_rule != NULL);

  assert (ld->last_rule_pos_type == RULE_POS_BUFFER);

  strcpy (t_rule->anchor_buf, ld->last_anchor_buf);

  /* reference is last reference or anchor if no last refence is available */
  if (ld->last_r_buf_p == NULL)
    {
      ref_buf_p = ld->last_anchor_buf;
      ref_buf_size = strlen (ld->last_anchor_buf);
    }
  else
    {
      ref_buf_p = ld->last_r_buf_p;
      ref_buf_size = ld->last_r_buf_size;
    }

  assert (ref_buf_size > 0);
  assert (ref_buf_p != NULL);

  t_rule->r_buf = (char *) malloc (ref_buf_size);
  if (t_rule->r_buf == NULL)
    {
      pd->xml_error = XML_CUB_OUT_OF_MEMORY;
      PRINT_DEBUG_START (data, attr, "memory allocation failed", -1);
      return -1;
    }
  memcpy (t_rule->r_buf, ref_buf_p, ref_buf_size);
  t_rule->r_buf_size = ref_buf_size;

  return 0;
}

/*
 * start_include_collation() - XML element start function
 * "ldml collations include"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_include_collation (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  XML_PARSER_DATA *new_pd = NULL;
  char *att_val = NULL;
  char include_file_path[PATH_MAX] = { 0 };
  char *prev_ldml_file = NULL;
  int prev_line_no = 0;
  LDML_CONTEXT *context = NULL;

  assert (data != NULL);

  if (xml_get_att_value (attr, "file", &att_val) != 0)
    {
      LOG_LOCALE_ERROR ("<include> tag requires a valid FILE attribute.", ER_LOC_GEN, true);
      PRINT_DEBUG_START (data, attr, "<include> tag requires a valid " "FILE attribute.", -1);

      return -1;
    }

  /* build a path and check if the file exists/is accessible */
  envvar_ldmldir_file (include_file_path, PATH_MAX, att_val);
  new_pd = xml_create_subparser (pd, include_file_path);

  ld = (LOCALE_DATA *) XML_USER_DATA (new_pd);
  assert (ld != NULL);

  context = &(ld->ldml_context);
  prev_ldml_file = context->ldml_file;
  prev_line_no = context->line_no;
  context->ldml_file = strdup (include_file_path);
  context->line_no = 0;

  int ret;
  if (new_pd == NULL)
    {
      char err_msg[ERR_MSG_SIZE] = { 0 };

      switch (pd->xml_error)
	{
	case XML_CUB_ERR_INCLUDE_LOOP:
	  ret = snprintf (err_msg, sizeof (err_msg) - 1, "Inclusion loop found for file %s.", include_file_path);
	  break;
	case XML_CUB_ERR_PARSER_INIT_FAIL:
	  ret = snprintf (err_msg, sizeof (err_msg) - 1, "Failed to initialize subparser for file path %s.",
			  include_file_path);
	  break;
	case XML_CUB_OUT_OF_MEMORY:
	  ret = snprintf (err_msg, sizeof (err_msg) - 1, "Memory exhausted when creating subparser for file %s.",
			  include_file_path);
	  break;
	}
      LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
      PRINT_DEBUG_START (data, attr, err_msg, -1);

      return -1;
    }

  xml_parser_exec (new_pd);

  if (new_pd->xml_error == XML_CUB_ERR_FILE_MISSING)
    {
      char err_msg[ERR_MSG_SIZE] = { 0 };

      snprintf_dots_truncate (err_msg, sizeof (err_msg) - 1, "Included file %s does not exist or " "is not accessible.",
			      include_file_path);
      PRINT_DEBUG_START (data, attr, err_msg, -1);
      LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);

      return -1;
    }
  else if (new_pd->xml_error != XML_CUB_NO_ERROR)
    {
      char msg[ERR_MSG_SIZE] = { 0 };
      const char *xml_err_text = (char *) XML_ErrorString (XML_GetErrorCode (new_pd->xml_parser));

      snprintf_dots_truncate (msg, sizeof (msg) - 1, "Error parsing file %s, " "line: %d, column: %d. Internal XML: %s",
			      new_pd->filepath, new_pd->xml_error_line, new_pd->xml_error_column, xml_err_text);
      LOG_LOCALE_ERROR (msg, ER_LOC_GEN, true);

      return -1;
    }
  free (context->ldml_file);
  context->ldml_file = prev_ldml_file;
  context->line_no = prev_line_no;

  pd->next = new_pd->next;
  xml_destroy_parser_data (new_pd);
  free (new_pd);

  return 0;
}
