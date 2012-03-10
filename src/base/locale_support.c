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
 * locale_support.c : Locale support using LDML (XML) files
 */

#ident "$Id$"


#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "porting.h"
#include "language_support.h"
#include "environment_variable.h"
#include "utility.h"
#include "xml_parser.h"
#include "chartype.h"
#include "error_manager.h"
#include "release_string.h"
#include "uca_support.h"
#include "unicode_support.h"
#include "locale_support.h"
#include "message_catalog.h"


#define TXT_CONV_LINE_SIZE 512
#define TXT_CONV_ITEM_GROW_COUNT 128

/* common data shared by all locales */
typedef struct locale_shared_data LOCALE_SHARED_DATA;
struct locale_shared_data
{
  ALPHABET_DATA *unicode_a;	/* Unicode alphabet */
  ALPHABET_DATA *ascii_a;	/* ASCII alphabet */
};

const char *ldml_ref_day_names[] =
  { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };

/* this must map to 'Am_Pm_name' from string_opfunc.c */
const char *ldml_ref_am_pm_names[] = { "am", "pm", "Am", "Pm", "AM", "PM",
  "a.m.", "p.m.", "A.m.", "P.m.", "A.M.", "P.M."
};

typedef struct
{
  unsigned int text_cp;
  unsigned int unicode_cp;
} TXT_CONV_ITEM;

static int start_element_ok (void *data, const char **attr);
static int end_element_ok (void *data, const char *el_name);
static int start_calendar (void *data, const char **attr);
static int end_dateFormatCUBRID (void *data, const char *el_name);
static int end_timeFormatCUBRID (void *data, const char *el_name);
static int end_datetimeFormatCUBRID (void *data, const char *el_name);
static int end_timestampFormatCUBRID (void *data, const char *el_name);
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
static int start_collation_setttings (void *data, const char **attr);
static int start_collation_reset (void *data, const char **attr);
static int end_collation_reset (void *data, const char *el_name);
static int start_collation_rule (void *data, const char **attr);
static int start_collation_cubrid_rule (void *data, const char **attr);
static int start_collation_cubrid_rule_set_wr (void *data, const char **attr);
static int end_collation_cubrid_rule_set (void *data, const char *el_name);
static int end_collation_cubrid_rule_set_cp_ch (void *data,
						const char *el_name);
static int end_collation_cubrid_rule_set_ech_ecp (void *data,
						  const char *el_name);
static int end_collation_cubrid_rule_set_w_wr (void *data,
					       const char *el_name);
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
static int start_consoleconversion (void *data, const char **attr);

static int handle_data (void *data, const char *s, int len);


static void clear_data_buffer (XML_PARSER_DATA * pd);
static TAILOR_RULE *new_collation_rule (LOCALE_DATA * ld);
static TRANSFORM_RULE *new_transform_rule (LOCALE_DATA * ld);
static CUBRID_TAILOR_RULE *new_collation_cubrid_rule (LOCALE_DATA * ld);

static void print_debug_start_el (void *data, const char **attrs,
				  const char *msg, const int status);
static void print_debug_end_el (void *data, const char *msg,
				const int status);
static void print_debug_data_content (void *data, const char *msg,
				      const int status);
static int load_console_conv_data (LOCALE_DATA * ld, bool is_verbose);
static int save_string_to_bin (FILE * fp, const char *s);
static int load_string_from_bin (FILE * fp, char *s, const int size);
static int save_int_to_bin (FILE * fp, const int i);
static int load_int_from_bin (FILE * fp, int *i);

static int locale_save_to_bin (LOCALE_FILE * lf, LOCALE_DATA * ld);
static int save_alphabet_to_bin (FILE * fp, ALPHABET_DATA * a);
static int load_alphabet_from_bin (LOCALE_FILE * lf, FILE * fp,
				   ALPHABET_DATA * a);
static int save_console_conv_to_bin (FILE * fp, TEXT_CONVERSION * tc);
static int load_console_conv_from_bin (LOCALE_FILE * lf, FILE * fp,
				       TEXT_CONVERSION * tc);
static int save_uca_opt_to_bin (FILE * fp, UCA_OPTIONS * uca_opt);
static int load_uca_opt_from_bin (LOCALE_FILE * lf, FILE * fp,
				  UCA_OPTIONS * uca_opt);
static int save_contraction_to_bin (FILE * fp, COLL_CONTRACTION * c,
				    bool use_expansion, bool use_level_4);
static int load_contraction_from_bin (LOCALE_FILE * lf, FILE * fp,
				      COLL_CONTRACTION * c,
				      bool use_expansion, bool use_level_4);
static void locale_destroy_collation_tailorings (const COLL_TAILORING * ct);
static void locale_destroy_collation_data (const COLL_DATA * cd);
static void locale_destroy_alphabet_tailoring (const ALPHABET_TAILORING * cd);
static void locale_destroy_console_conversion (const TEXT_CONVERSION * tc);
static int str_pop_token (char *str_p, char **token_p, char **next_p);
static int dump_locale_alphabet (ALPHABET_DATA * ad, int dl_settings,
				 int lower_bound, int upper_bound);

static void dump_collation_key (LOCALE_DATA * ld, const unsigned int key,
				bool print_weight, bool print_key);
static void dump_collation_contr (LOCALE_DATA * ld,
				  const COLL_CONTRACTION * contr,
				  bool print_weight, bool print_contr);
static void dump_collation_codepoint (LOCALE_DATA * ld,
				      const unsigned int cp,
				      bool print_weight, bool print_cp);

static int comp_func_coll_uca_exp (const void *arg1, const void *arg2);

static int comp_func_coll_uca_simple_weights (const void *arg1,
					      const void *arg2);
static int comp_func_parse_order_index (const void *arg1, const void *arg2);

#define PRINT_DEBUG_START(d, a, m, s) \
   do { \
      print_debug_start_el (d, a, m, s); \
   } while (0);

#define PRINT_DEBUG_END(d, m, s) \
   do { \
      print_debug_end_el (d, m, s); \
   } while (0);

#define PRINT_DEBUG_DATA(d, m, s) \
   do { \
      print_debug_data_content (d, m, s); \
   } while (0);

#define XML_COMMENT_START "<!-- "
#define XML_COMMENT_END " -->"

LOCALE_SHARED_DATA shared_data = { NULL, NULL };
static COLL_DATA *dump_coll_data = NULL;

static char *cmp_token_name_array = NULL;
static char cmp_token_name_size = 0;

/*
 * LDML element definition (XML schema)
 * full name is the "path" - the names of all parent elements down to - the element
 */
XML_ELEMENT_DEF ldml_elem_ldml =
  { "ldml", 1, (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_element_ok, NULL
};

XML_ELEMENT_DEF ldml_elem_dates =
  { "ldml dates", 2, (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_element_ok, NULL
};
XML_ELEMENT_DEF ldml_elem_calendars =
  { "ldml dates calendars", 3, (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_element_ok, NULL
};
XML_ELEMENT_DEF ldml_elem_calendar =
  { "ldml dates calendars calendar", 4, (ELEM_START_FUNC) & start_calendar,
  (ELEM_END_FUNC) & end_element_ok, NULL
};

XML_ELEMENT_DEF ldml_elem_dateFormatCUBRID =
  { "ldml dates calendars calendar dateFormatCUBRID", 5,
  (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_dateFormatCUBRID, (ELEM_DATA_FUNC) & handle_data
};
XML_ELEMENT_DEF ldml_timeFormatCUBRID =
  { "ldml dates calendars calendar timeFormatCUBRID", 5,
  (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_timeFormatCUBRID, (ELEM_DATA_FUNC) & handle_data
};
XML_ELEMENT_DEF ldml_datetimeFormatCUBRID =
  { "ldml dates calendars calendar datetimeFormatCUBRID", 5,
  (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_datetimeFormatCUBRID, (ELEM_DATA_FUNC) & handle_data
};
XML_ELEMENT_DEF ldml_timestampFormatCUBRID =
  { "ldml dates calendars calendar timestampFormatCUBRID", 5,
  (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_timestampFormatCUBRID,
  (ELEM_DATA_FUNC) & handle_data
};

XML_ELEMENT_DEF ldml_elem_months =
  { "ldml dates calendars calendar months", 5,
  (ELEM_START_FUNC) & start_element_ok, (ELEM_END_FUNC) & end_element_ok, NULL
};
XML_ELEMENT_DEF ldml_elem_monthContext =
  { "ldml dates calendars calendar months monthContext", 6,
  (ELEM_START_FUNC) & start_calendar_name_context,
  (ELEM_END_FUNC) & end_element_ok, NULL
};
XML_ELEMENT_DEF ldml_elem_monthWidth =
  { "ldml dates calendars calendar months monthContext monthWidth", 7,
  (ELEM_START_FUNC) & start_month_day_Width,
  (ELEM_END_FUNC) & end_month_day_Width, NULL
};
XML_ELEMENT_DEF ldml_elem_month =
  { "ldml dates calendars calendar months monthContext monthWidth month", 8,
  (ELEM_START_FUNC) & start_month, (ELEM_END_FUNC) & end_month,
  (ELEM_DATA_FUNC) & handle_data
};

XML_ELEMENT_DEF ldml_elem_days = { "ldml dates calendars calendar days", 5,
  (ELEM_START_FUNC) & start_element_ok, (ELEM_END_FUNC) & end_element_ok, NULL
};
XML_ELEMENT_DEF ldml_elem_dayContext =
  { "ldml dates calendars calendar days dayContext", 6,
  (ELEM_START_FUNC) & start_calendar_name_context,
  (ELEM_END_FUNC) & end_element_ok, NULL
};
XML_ELEMENT_DEF ldml_elem_dayWidth =
  { "ldml dates calendars calendar days dayContext dayWidth", 7,
  (ELEM_START_FUNC) & start_month_day_Width,
  (ELEM_END_FUNC) & end_month_day_Width, NULL
};
XML_ELEMENT_DEF ldml_elem_day =
  { "ldml dates calendars calendar days dayContext dayWidth day", 8,
  (ELEM_START_FUNC) & start_day, (ELEM_END_FUNC) & end_day,
  (ELEM_DATA_FUNC) & handle_data
};

XML_ELEMENT_DEF ldml_elem_dayPeriods =
  { "ldml dates calendars calendar dayPeriods", 5,
  (ELEM_START_FUNC) & start_element_ok, (ELEM_END_FUNC) & end_element_ok, NULL
};
XML_ELEMENT_DEF ldml_elem_dayPeriodContext =
  { "ldml dates calendars calendar dayPeriods dayPeriodContext", 6,
  (ELEM_START_FUNC) & start_calendar_name_context,
  (ELEM_END_FUNC) & end_element_ok, NULL
};
XML_ELEMENT_DEF ldml_elem_dayPeriodWidth = {
  "ldml dates calendars calendar dayPeriods dayPeriodContext dayPeriodWidth",
  7,
  (ELEM_START_FUNC) & start_dayPeriodWidth, (ELEM_END_FUNC) & end_element_ok,
  NULL
};
XML_ELEMENT_DEF ldml_elem_dayPeriod = {
  "ldml dates calendars calendar dayPeriods dayPeriodContext dayPeriodWidth dayPeriod",
  8,
  (ELEM_START_FUNC) & start_dayPeriod, (ELEM_END_FUNC) & end_dayPeriod,
  (ELEM_DATA_FUNC) & handle_data
};

XML_ELEMENT_DEF ldml_elem_numbers =
  { "ldml numbers", 2, (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_element_ok, NULL
};
XML_ELEMENT_DEF ldml_elem_numbers_symbols =
  { "ldml numbers symbols", 3, (ELEM_START_FUNC) & start_numbers_symbols,
  (ELEM_END_FUNC) & end_element_ok, NULL
};
XML_ELEMENT_DEF ldml_elem_symbol_decimal =
  { "ldml numbers symbols decimal", 4, (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_number_symbol, (ELEM_DATA_FUNC) & handle_data
};
XML_ELEMENT_DEF ldml_elem_symbol_group =
  { "ldml numbers symbols group", 4, (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_number_symbol, (ELEM_DATA_FUNC) & handle_data
};
XML_ELEMENT_DEF ldml_elem_currencies =
  { "ldml numbers currencies", 3, (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_element_ok, NULL
};
XML_ELEMENT_DEF ldml_elem_currency =
  { "ldml numbers currencies currency", 4, (ELEM_START_FUNC) & start_currency,
  (ELEM_END_FUNC) & end_element_ok, NULL
};

XML_ELEMENT_DEF ldml_elem_collations =
  { "ldml collations", 2, (ELEM_START_FUNC) & start_collations,
  (ELEM_END_FUNC) & end_element_ok, NULL
};
XML_ELEMENT_DEF ldml_elem_collation =
  { "ldml collations collation", 3, (ELEM_START_FUNC) & start_one_collation,
  (ELEM_END_FUNC) & end_element_ok, NULL
};
XML_ELEMENT_DEF ldml_elem_collation_rules =
  { "ldml collations collation rules", 4,
  (ELEM_START_FUNC) & start_element_ok, (ELEM_END_FUNC) & end_element_ok, NULL
};
XML_ELEMENT_DEF ldml_elem_collation_settings =
  { "ldml collations collation settings", 4,
  (ELEM_START_FUNC) & start_collation_setttings,
  (ELEM_END_FUNC) & end_element_ok, NULL
};
XML_ELEMENT_DEF ldml_elem_collation_reset =
  { "ldml collations collation rules reset", 5,
  (ELEM_START_FUNC) & start_collation_reset,
  (ELEM_END_FUNC) & end_collation_reset, (ELEM_DATA_FUNC) & handle_data
};
XML_ELEMENT_DEF ldml_elem_collation_p =
  { "ldml collations collation rules p", 5,
  (ELEM_START_FUNC) & start_collation_rule,
  (ELEM_END_FUNC) & end_collation_rule,
  (ELEM_DATA_FUNC) & handle_data_collation_rule
};
XML_ELEMENT_DEF ldml_elem_collation_s =
  { "ldml collations collation rules s", 5,
  (ELEM_START_FUNC) & start_collation_rule,
  (ELEM_END_FUNC) & end_collation_rule,
  (ELEM_DATA_FUNC) & handle_data_collation_rule
};
XML_ELEMENT_DEF ldml_elem_collation_t =
  { "ldml collations collation rules t", 5,
  (ELEM_START_FUNC) & start_collation_rule,
  (ELEM_END_FUNC) & end_collation_rule,
  (ELEM_DATA_FUNC) & handle_data_collation_rule
};
XML_ELEMENT_DEF ldml_elem_collation_i =
  { "ldml collations collation rules i", 5,
  (ELEM_START_FUNC) & start_collation_rule,
  (ELEM_END_FUNC) & end_collation_rule,
  (ELEM_DATA_FUNC) & handle_data_collation_rule
};
XML_ELEMENT_DEF ldml_elem_collation_pc =
  { "ldml collations collation rules pc", 5,
  (ELEM_START_FUNC) & start_collation_rule,
  (ELEM_END_FUNC) & end_collation_rule,
  (ELEM_DATA_FUNC) & handle_data_collation_rule
};
XML_ELEMENT_DEF ldml_elem_collation_sc =
  { "ldml collations collation rules sc", 5,
  (ELEM_START_FUNC) & start_collation_rule,
  (ELEM_END_FUNC) & end_collation_rule,
  (ELEM_DATA_FUNC) & handle_data_collation_rule
};
XML_ELEMENT_DEF ldml_elem_collation_tc =
  { "ldml collations collation rules tc", 5,
  (ELEM_START_FUNC) & start_collation_rule,
  (ELEM_END_FUNC) & end_collation_rule,
  (ELEM_DATA_FUNC) & handle_data_collation_rule
};
XML_ELEMENT_DEF ldml_elem_collation_ic =
  { "ldml collations collation rules ic", 5,
  (ELEM_START_FUNC) & start_collation_rule,
  (ELEM_END_FUNC) & end_collation_rule,
  (ELEM_DATA_FUNC) & handle_data_collation_rule
};

XML_ELEMENT_DEF ldml_elem_collation_x =
  { "ldml collations collation rules x", 5,
  (ELEM_START_FUNC) & start_collation_x, (ELEM_END_FUNC) & end_collation_x,
  NULL
};
XML_ELEMENT_DEF ldml_elem_collation_x_p =
  { "ldml collations collation rules x p", 6,
  (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_collation_x_rule,
  (ELEM_DATA_FUNC) & handle_data
};
XML_ELEMENT_DEF ldml_elem_collation_x_s =
  { "ldml collations collation rules x s", 6,
  (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_collation_x_rule,
  (ELEM_DATA_FUNC) & handle_data
};
XML_ELEMENT_DEF ldml_elem_collation_x_t =
  { "ldml collations collation rules x t", 6,
  (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_collation_x_rule,
  (ELEM_DATA_FUNC) & handle_data
};
XML_ELEMENT_DEF ldml_elem_collation_x_i =
  { "ldml collations collation rules x i", 6,
  (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_collation_x_rule,
  (ELEM_DATA_FUNC) & handle_data
};
XML_ELEMENT_DEF ldml_elem_collation_x_extend =
  { "ldml collations collation rules x extend", 6,
  (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_collation_x_extend, (ELEM_DATA_FUNC) & handle_data
};
XML_ELEMENT_DEF ldml_elem_collation_x_context =
  { "ldml collations collation rules x context", 6,
  (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_collation_x_context, (ELEM_DATA_FUNC) & handle_data
};

XML_ELEMENT_DEF ldml_elem_collation_reset_first_variable =
  { "ldml collations collation rules reset first_variable", 6,
  (ELEM_START_FUNC) & start_collation_logical_pos,
  (ELEM_END_FUNC) & end_collation_logical_pos, NULL
};
XML_ELEMENT_DEF ldml_elem_collation_reset_last_variable =
  { "ldml collations collation rules reset last_variable", 6,
  (ELEM_START_FUNC) & start_collation_logical_pos,
  (ELEM_END_FUNC) & end_collation_logical_pos, NULL
};
XML_ELEMENT_DEF ldml_elem_collation_reset_first_primary_ignorable =
  { "ldml collations collation rules reset first_primary_ignorable", 6,
  (ELEM_START_FUNC) & start_collation_logical_pos,
  (ELEM_END_FUNC) & end_collation_logical_pos, NULL
};
XML_ELEMENT_DEF ldml_elem_collation_reset_last_primary_ignorable =
  { "ldml collations collation rules reset last_primary_ignorable", 6,
  (ELEM_START_FUNC) & start_collation_logical_pos,
  (ELEM_END_FUNC) & end_collation_logical_pos, NULL
};
XML_ELEMENT_DEF ldml_elem_collation_reset_first_secondary_ignorable =
  { "ldml collations collation rules reset first_secondary_ignorable", 6,
  (ELEM_START_FUNC) & start_collation_logical_pos,
  (ELEM_END_FUNC) & end_collation_logical_pos, NULL
};
XML_ELEMENT_DEF ldml_elem_collation_reset_last_secondary_ignorable =
  { "ldml collations collation rules reset last_secondary_ignorable", 6,
  (ELEM_START_FUNC) & start_collation_logical_pos,
  (ELEM_END_FUNC) & end_collation_logical_pos, NULL
};
XML_ELEMENT_DEF ldml_elem_collation_reset_first_tertiary_ignorable =
  { "ldml collations collation rules reset first_tertiary_ignorable", 6,
  (ELEM_START_FUNC) & start_collation_logical_pos,
  (ELEM_END_FUNC) & end_collation_logical_pos, NULL
};
XML_ELEMENT_DEF ldml_elem_collation_reset_last_tertiary_ignorable =
  { "ldml collations collation rules reset last_tertiary_ignorable", 6,
  (ELEM_START_FUNC) & start_collation_logical_pos,
  (ELEM_END_FUNC) & end_collation_logical_pos, NULL
};
XML_ELEMENT_DEF ldml_elem_collation_reset_first_non_ignorable =
  { "ldml collations collation rules reset first_non_ignorable", 6,
  (ELEM_START_FUNC) & start_collation_logical_pos,
  (ELEM_END_FUNC) & end_collation_logical_pos, NULL
};
XML_ELEMENT_DEF ldml_elem_collation_reset_last_non_ignorable =
  { "ldml collations collation rules reset last_non_ignorable", 6,
  (ELEM_START_FUNC) & start_collation_logical_pos,
  (ELEM_END_FUNC) & end_collation_logical_pos, NULL
};
XML_ELEMENT_DEF ldml_elem_collation_reset_first_trailing =
  { "ldml collations collation rules reset first_trailing", 6,
  (ELEM_START_FUNC) & start_collation_logical_pos,
  (ELEM_END_FUNC) & end_collation_logical_pos, NULL
};
XML_ELEMENT_DEF ldml_elem_collation_reset_last_trailing =
  { "ldml collations collation rules reset last_trailing", 6,
  (ELEM_START_FUNC) & start_collation_logical_pos,
  (ELEM_END_FUNC) & end_collation_logical_pos, NULL
};
XML_ELEMENT_DEF ldml_elem_collation_cubrid_rules = {
  "ldml collations collation cubridrules", 4,
  (ELEM_START_FUNC) & start_element_ok, (ELEM_END_FUNC) & end_element_ok,
  NULL
};
XML_ELEMENT_DEF ldml_elem_collation_cubrid_rules_set =
  { "ldml collations collation cubridrules set", 5,
  (ELEM_START_FUNC) & start_collation_cubrid_rule,
  (ELEM_END_FUNC) & end_collation_cubrid_rule_set, NULL
};
XML_ELEMENT_DEF ldml_elem_collation_cubrid_rules_set_ch =
  { "ldml collations collation cubridrules set ch", 6,
  (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_collation_cubrid_rule_set_cp_ch,
  (ELEM_DATA_FUNC) & handle_data
};
XML_ELEMENT_DEF ldml_elem_collation_cubrid_rules_set_sch =
  { "ldml collations collation cubridrules set sch", 6,
  (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_collation_cubrid_rule_set_cp_ch,
  (ELEM_DATA_FUNC) & handle_data
};
XML_ELEMENT_DEF ldml_elem_collation_cubrid_rules_set_ech =
  { "ldml collations collation cubridrules set ech", 6,
  (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_collation_cubrid_rule_set_ech_ecp,
  (ELEM_DATA_FUNC) & handle_data
};
XML_ELEMENT_DEF ldml_elem_collation_cubrid_rules_set_cp =
  { "ldml collations collation cubridrules set cp", 6,
  (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_collation_cubrid_rule_set_cp_ch,
  (ELEM_DATA_FUNC) & handle_data
};
XML_ELEMENT_DEF ldml_elem_collation_cubrid_rules_set_scp =
  { "ldml collations collation cubridrules set scp", 6,
  (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_collation_cubrid_rule_set_cp_ch,
  (ELEM_DATA_FUNC) & handle_data
};
XML_ELEMENT_DEF ldml_elem_collation_cubrid_rules_set_ecp =
  { "ldml collations collation cubridrules set ecp", 6,
  (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_collation_cubrid_rule_set_ech_ecp,
  (ELEM_DATA_FUNC) & handle_data
};
XML_ELEMENT_DEF ldml_elem_collation_cubrid_rules_set_w =
  { "ldml collations collation cubridrules set w", 6,
  (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_collation_cubrid_rule_set_w_wr,
  (ELEM_DATA_FUNC) & handle_data
};
XML_ELEMENT_DEF ldml_elem_collation_cubrid_rules_set_wr =
  { "ldml collations collation cubridrules set wr", 6,
  (ELEM_START_FUNC) & start_collation_cubrid_rule_set_wr,
  (ELEM_END_FUNC) & end_collation_cubrid_rule_set_w_wr,
  (ELEM_DATA_FUNC) & handle_data
};
XML_ELEMENT_DEF ldml_elem_alphabets =
  { "ldml alphabets", 2, (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_element_ok, NULL
};
XML_ELEMENT_DEF ldml_elem_alphabet =
  { "ldml alphabets alphabet", 3, (ELEM_START_FUNC) & start_one_alphabet,
  (ELEM_END_FUNC) & end_element_ok, NULL
};
XML_ELEMENT_DEF ldml_elem_alphabet_upper =
  { "ldml alphabets alphabet u", 4, (ELEM_START_FUNC) & start_upper_case_rule,
  (ELEM_END_FUNC) & end_case_rule, NULL
};
XML_ELEMENT_DEF ldml_elem_alphabet_upper_src =
  { "ldml alphabets alphabet u s", 5, (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_transform_buffer, (ELEM_DATA_FUNC) & handle_data
};
XML_ELEMENT_DEF ldml_elem_alphabet_upper_dest =
  { "ldml alphabets alphabet u d", 5, (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_transform_buffer, (ELEM_DATA_FUNC) & handle_data
};
XML_ELEMENT_DEF ldml_elem_alphabet_lower =
  { "ldml alphabets alphabet l", 4, (ELEM_START_FUNC) & start_lower_case_rule,
  (ELEM_END_FUNC) & end_case_rule, NULL
};
XML_ELEMENT_DEF ldml_elem_alphabet_lower_src =
  { "ldml alphabets alphabet l s", 5, (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_transform_buffer, (ELEM_DATA_FUNC) & handle_data
};
XML_ELEMENT_DEF ldml_elem_alphabet_lower_dest =
  { "ldml alphabets alphabet l d", 5, (ELEM_START_FUNC) & start_element_ok,
  (ELEM_END_FUNC) & end_transform_buffer, (ELEM_DATA_FUNC) & handle_data
};

XML_ELEMENT_DEF ldml_elem_consoleconversion =
  { "ldml consoleconversion", 2, (ELEM_START_FUNC) & start_consoleconversion,
  (ELEM_END_FUNC) & end_element_ok, NULL
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

  &ldml_elem_consoleconversion
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

  ld = XML_USER_DATA (pd);

  /* copy data buffer to locale */
  assert (ld->data_buf_count < sizeof (ld->dateFormat));

  if (ld->data_buf_count < sizeof (ld->dateFormat))
    {
      strcpy (ld->dateFormat, ld->data_buffer);
    }
  else
    {
      PRINT_DEBUG_END (data, "Too much data", -1);
      return -1;
    }

  clear_data_buffer (data);

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

  ld = XML_USER_DATA (pd);

  /* copy data buffer to locale */
  assert (ld->data_buf_count < sizeof (ld->timeFormat));

  if (ld->data_buf_count < sizeof (ld->timeFormat))
    {
      strcpy (ld->timeFormat, ld->data_buffer);
    }
  else
    {
      PRINT_DEBUG_END (data, "Too much data", -1);
      return -1;
    }

  clear_data_buffer (data);

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

  ld = XML_USER_DATA (pd);

  /* copy data buffer to locale */
  assert (ld->data_buf_count < sizeof (ld->datetimeFormat));

  if (ld->data_buf_count < sizeof (ld->datetimeFormat))
    {
      strcpy (ld->datetimeFormat, ld->data_buffer);
    }
  else
    {
      PRINT_DEBUG_END (data, "Too much data", -1);
      return -1;
    }

  clear_data_buffer (data);

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

  ld = XML_USER_DATA (pd);

  /* copy data buffer to locale */
  assert (ld->data_buf_count < sizeof (ld->timestampFormat));

  if (ld->data_buf_count < sizeof (ld->timestampFormat))
    {
      strcpy (ld->timestampFormat, ld->data_buffer);
    }
  else
    {
      PRINT_DEBUG_END (data, "Too much data", -1);
      return -1;
    }

  clear_data_buffer (data);

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

  ld = XML_USER_DATA (pd);

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

  ld = XML_USER_DATA (pd);

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

  ld = XML_USER_DATA (pd);

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

  ld = XML_USER_DATA (pd);

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

  ld = XML_USER_DATA (pd);

  /* copy data buffer to locale */
  assert (ld->curr_period >= 0 && ld->curr_period < CAL_MONTH_COUNT);

  assert (ld->name_type == 2 || ld->name_type == 1);

  if (ld->name_type == 1)
    {
      assert (ld->data_buf_count < LOC_DATA_MONTH_ABBR_SIZE);

      if (ld->data_buf_count < LOC_DATA_MONTH_ABBR_SIZE)
	{
	  strcpy (ld->month_names_abbreviated[ld->curr_period],
		  ld->data_buffer);
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

  clear_data_buffer (data);

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

  ld = XML_USER_DATA (pd);

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

  ld = XML_USER_DATA (pd);

  /* copy data buffer to locale */
  assert (ld->curr_period >= 0 && ld->curr_period < CAL_DAY_COUNT);

  assert (ld->name_type == 2 || ld->name_type == 1);

  if (ld->name_type == 1)
    {
      assert (ld->data_buf_count < LOC_DATA_DAY_ABBR_SIZE);
      if (ld->data_buf_count < LOC_DATA_DAY_ABBR_SIZE)
	{
	  strcpy (ld->day_names_abbreviated[ld->curr_period],
		  ld->data_buffer);
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

  clear_data_buffer (data);

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

  ld = XML_USER_DATA (pd);

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

  ld = XML_USER_DATA (pd);

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

  clear_data_buffer (data);

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

  ld = XML_USER_DATA (pd);

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

  ld = XML_USER_DATA (pd);

  /* number symbol is exactly one character (ASCII compatible) */
  if (ld->data_buf_count == 1
      && (*(ld->data_buffer) >= ' '
	  && (unsigned char) *(ld->data_buffer) < 0x80))
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

  clear_data_buffer (data);

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

  ld = XML_USER_DATA (pd);

  /* all setting are optional */
  if (xml_get_att_value (attr, "type", &att_val) == 0)
    {
      /* Type attribute found, store it. */
      if (strlen (att_val) != LOC_DATA_CURRENCY_ISO_CODE_LEN)
	{
	  PRINT_DEBUG_START (data, attr,
			     "Currency ISO code invalid, "
			     "it must be 3 chars long.", -1);
	  return -1;
	}
      if (ld->default_currency_code != DB_CURRENCY_NULL)
	{
	  PRINT_DEBUG_START (data, attr, "Currency ISO code already set.",
			     -1);
	  return -1;
	}
      if (!intl_is_currency_symbol (att_val, &(ld->default_currency_code),
				    &currency_size, CURRENCY_CHECK_MODE_ISO))
	{
	  PRINT_DEBUG_START (data, attr, "Currency ISO code not supported",
			     -1);
	  return -1;
	}
    }
  else
    {
      /* If <currency> does not have a type attribute, parsing fails. */
      PRINT_DEBUG_START (data, attr,
			 "Currency tag does not have an ISO code type "
			 "attribute.", -1);
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

  ld = XML_USER_DATA (pd);

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
	  if (t_end - t_start == check_loc_len
	      && memcmp (ld->locale_name, t_start, t_end - t_start) == 0)
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

  assert (data != NULL);

  ld = XML_USER_DATA (pd);

  if (xml_check_att_value (attr, "type", "standard") != 0)
    {
      PRINT_DEBUG_START (data, attr, "", 1);
      return 1;
    }

  if (ld->coll.uca_opt.sett_strength != TAILOR_UNDEFINED
      || ld->coll.uca_opt.sett_backwards != false
      || ld->coll.uca_opt.sett_caseLevel != false
      || ld->coll.uca_opt.sett_caseFirst != 0
      || ld->coll.sett_max_cp != -1 || ld->coll.count_rules != 0)
    {
      PRINT_DEBUG_START (data, attr, "Only one collation is allowed", -1);
      return -1;
    }

  /* initialize default value for settings */
  ld->coll.uca_opt.sett_strength = TAILOR_QUATERNARY;
  ld->coll.uca_opt.sett_caseFirst = 1;

  PRINT_DEBUG_START (data, attr, "", 0);
  return 0;
}

/* 
 * start_collation_setttings() - XML element start function
 * "ldml collations collation settings"
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * data: user data
 * attr: attribute/value pair array
 */
static int
start_collation_setttings (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  LOCALE_DATA *ld = NULL;
  char *att_val = NULL;

  assert (data != NULL);

  ld = XML_USER_DATA (pd);

  /* all setting are optional */
  if (xml_get_att_value (attr, "strength", &att_val) == 0)
    {
      if (strcmp (att_val, "primary") == 0 || strcmp (att_val, "1") == 0)
	{
	  ld->coll.uca_opt.sett_strength = TAILOR_PRIMARY;
	}
      else if ((strcmp (att_val, "secondary") == 0)
	       || (strcmp (att_val, "2") == 0))
	{
	  ld->coll.uca_opt.sett_strength = TAILOR_SECONDARY;
	}
      else if ((strcmp (att_val, "tertiary") == 0)
	       || (strcmp (att_val, "3") == 0))
	{
	  ld->coll.uca_opt.sett_strength = TAILOR_TERTIARY;
	}
      else if ((strcmp (att_val, "quaternary") == 0)
	       || (strcmp (att_val, "4") == 0))
	{
	  ld->coll.uca_opt.sett_strength = TAILOR_QUATERNARY;
	}
      else if ((strcmp (att_val, "identical") == 0)
	       || (strcmp (att_val, "5") == 0))
	{
	  ld->coll.uca_opt.sett_strength = TAILOR_IDENTITY;
	}
    }

  att_val = NULL;

  if (xml_get_att_value (attr, "backwards", &att_val) == 0)
    {
      if (strcmp (att_val, "on") == 0)
	{
	  ld->coll.uca_opt.sett_backwards = true;
	}
    }

  att_val = NULL;

  if (xml_get_att_value (attr, "caseLevel", &att_val) == 0)
    {
      if (strcmp (att_val, "on") == 0)
	{
	  ld->coll.uca_opt.sett_caseLevel = true;
	}
    }

  att_val = NULL;

  if (xml_get_att_value (attr, "caseFirst", &att_val) == 0)
    {
      if (strcmp (att_val, "off") == 0)
	{
	  ld->coll.uca_opt.sett_caseFirst = 0;
	}
      else if (strcmp (att_val, "upper") == 0)
	{
	  ld->coll.uca_opt.sett_caseFirst = 1;
	}
      else if (strcmp (att_val, "lower") == 0)
	{
	  ld->coll.uca_opt.sett_caseFirst = 2;
	}
    }

  /* CUBRID specific collation settings */
  att_val = NULL;
  if (xml_get_att_value (attr, "CUBRIDMaxWeights", &att_val) == 0)
    {
      assert (att_val != NULL);
      ld->coll.sett_max_cp = atoi (att_val);
    }

  att_val = NULL;
  if (xml_get_att_value (attr, "DUCETContractions", &att_val) == 0)
    {
      assert (att_val != NULL);
      if (strcasecmp (att_val, "use") == 0)
	{
	  ld->coll.uca_opt.sett_contr_policy |= CONTR_DUCET_USE;
	}
    }

  att_val = NULL;
  if (xml_get_att_value (attr, "TailoringContractions", &att_val) == 0)
    {
      assert (att_val != NULL);
      if (strcasecmp (att_val, "use") == 0)
	{
	  ld->coll.uca_opt.sett_contr_policy |= CONTR_TAILORING_USE;
	}
    }

  att_val = NULL;
  if (xml_get_att_value (attr, "CUBRIDExpansions", &att_val) == 0)
    {
      assert (att_val != NULL);
      if (strcasecmp (att_val, "use") == 0)
	{
	  ld->coll.uca_opt.sett_expansions = true;
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

  ld = XML_USER_DATA (pd);

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

  ld = XML_USER_DATA (pd);

  assert (ld->data_buf_count < LOC_DATA_COLL_TWO_CHARS);

  if (ld->data_buf_count < LOC_DATA_COLL_TWO_CHARS)
    {
      strcpy (ld->last_anchor_buf, ld->data_buffer);
      clear_data_buffer (data);
      PRINT_DEBUG_END (data, "", 0);
      return 0;
    }

  clear_data_buffer (data);

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
  char *ref_buf_p = NULL;
  int ref_buf_size = 0;

  assert (data != NULL);

  ld = XML_USER_DATA (pd);

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
  char *ref_buf_p = NULL;
  int ref_buf_size = 0;

  assert (data != NULL);

  ld = XML_USER_DATA (pd);

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
  int rule_id;
  int err_status = NO_ERROR;

  assert (data != NULL);

  ld = XML_USER_DATA (pd);

  rule_id = ld->coll.cub_count_rules;
  ct_rule = &(ld->coll.cub_rules[rule_id]);

  if (xml_get_att_value (attr, "step", &att_val) == 0)
    {
      if (strlen (att_val) > MAX_STRLEN_FOR_COLLATION_ELEMENT)
	{
	  err_status = ER_LOC_GEN;
	  LOG_LOCALE_ERROR ("Data buffer too big in <wr> attribute STEP.",
			    ER_LOC_GEN, true);
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
  CUBRID_TAILOR_RULE *ct_rule = NULL;
  int rule_id;

  assert (data != NULL);

  ld = XML_USER_DATA (pd);

  rule_id = ld->coll.cub_count_rules;
  ct_rule = &(ld->coll.cub_rules[rule_id]);

  /* rule finished, increase count */
  ld->coll.cub_count_rules++;

  if (pd->verbose)
    {
      char msg[ERR_MSG_SIZE];

      snprintf (msg, sizeof (msg) - 1, " * SET Rule no. %d, tag name :%s *",
		(rule_id + 1), el_name);
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
  int rule_id;
  int err_status = NO_ERROR;

  assert (data != NULL);

  ld = XML_USER_DATA (pd);

  rule_id = ld->coll.cub_count_rules;
  ct_rule = &(ld->coll.cub_rules[rule_id]);

  if (ld->data_buf_count > LOC_DATA_BUFF_SIZE)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("Data buffer too large in codepoint starting label.",
			ER_LOC_GEN, true);
      goto exit;
    }

  memcpy (ct_rule->start_cp_buf,
	  ld->data_buffer, ld->data_buf_count * sizeof (char));
  ct_rule->start_cp_buf[ld->data_buf_count] = '\0';

  memcpy (ct_rule->end_cp_buf,
	  ld->data_buffer, ld->data_buf_count * sizeof (char));
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

      snprintf (msg, sizeof (msg) - 1, " * SET Rule no. %d, tag name :%s *",
		(rule_id + 1), el_name);
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
  int rule_id;
  int err_status = NO_ERROR;

  assert (data != NULL);

  ld = XML_USER_DATA (pd);

  if (ld->data_buf_count > LOC_DATA_BUFF_SIZE)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("Data buffer too large in codepoint end label.",
			ER_LOC_GEN, true);
      goto exit;
    }
  rule_id = ld->coll.cub_count_rules;
  ct_rule = &(ld->coll.cub_rules[rule_id]);

  memcpy (ct_rule->end_cp_buf,
	  ld->data_buffer, ld->data_buf_count * sizeof (char));
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

      snprintf (msg, sizeof (msg) - 1, "* SET Rule no. %d, tag name :%s *",
		(rule_id + 1), el_name);
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
  int rule_id;
  int err_status = NO_ERROR;

  assert (data != NULL);

  ld = XML_USER_DATA (pd);

  rule_id = ld->coll.cub_count_rules;
  ct_rule = &(ld->coll.cub_rules[rule_id]);

  if (ld->data_buf_count > LOC_DATA_BUFF_SIZE)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR ("Data buffer too large in weight label.",
			ER_LOC_GEN, true);
      goto exit;
    }

  memcpy (ct_rule->start_weight,
	  ld->data_buffer, ld->data_buf_count * sizeof (char));
  ct_rule->start_weight[ld->data_buf_count] = '\0';

  if (pd->verbose)
    {
      char msg[ERR_MSG_SIZE];

      snprintf (msg, sizeof (msg) - 1, "* SET Rule no. %d, tag name :%s *",
		(rule_id + 1), el_name);
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

  assert (data != NULL);

  ld = XML_USER_DATA (pd);

  assert (len >= 0);

  t_rule = &(ld->coll.rules[ld->coll.count_rules]);
  t_rule->t_buf = realloc (t_rule->t_buf, t_rule->t_buf_size + len);
  if (t_rule->t_buf == NULL)
    {
      pd->xml_error = XML_CUB_OUT_OF_MEMORY;
      PRINT_DEBUG_DATA (data, "memory allocation failed", -1);
      return -1;
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
  int rule_id;

  assert (data != NULL);

  ld = XML_USER_DATA (pd);

  /* rule finished, increase count */
  rule_id = ld->coll.count_rules++;
  t_rule = &(ld->coll.rules[rule_id]);

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

      snprintf (msg, sizeof (msg) - 1,
		"* Rule %d, L :%d, Dir:%d, PosType:%d, Mc:%d ;"
		" XML: line: %d , col:%d *",
		rule_id, t_rule->level, t_rule->direction,
		t_rule->r_pos_type, t_rule->multiple_chars, xml_line_no,
		xml_col_no);
      PRINT_DEBUG_END (data, msg, 0);
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
  char *ref_buf_p = NULL;
  int ref_buf_size = 0;
  int anchor_len;

  assert (data != NULL);

  ld = XML_USER_DATA (pd);

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

  assert (ld->last_rule_pos_type == RULE_POS_BUFFER);
  anchor_len = strlen (ld->last_anchor_buf);

  assert (anchor_len > 0);

  strcpy (t_rule->anchor_buf, ld->last_anchor_buf);

  t_rule->r_buf = (char *) malloc (anchor_len);
  if (t_rule->r_buf == NULL)
    {
      pd->xml_error = XML_CUB_OUT_OF_MEMORY;
      PRINT_DEBUG_START (data, attr, "memory allocation failed", -1);
      return -1;
    }

  memcpy (t_rule->r_buf, t_rule->anchor_buf, anchor_len);
  t_rule->r_buf_size = anchor_len;
  t_rule->direction = ld->last_rule_dir;

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
  int rule_id;

  assert (data != NULL);

  ld = XML_USER_DATA (pd);

  /* rule finished, increase count */
  rule_id = ld->coll.count_rules++;
  t_rule = &(ld->coll.rules[rule_id]);
  /* BEFORE applies only for the first rule after <reset> */
  ld->last_rule_dir = TAILOR_AFTER;

  if (pd->verbose)
    {
      char msg[ERR_MSG_SIZE];

      snprintf (msg, sizeof (msg) - 1,
		" * X_Rule %d, L :%d, Dir:%d, PosType:%d, Mc:%d *",
		rule_id, t_rule->level, t_rule->direction,
		t_rule->r_pos_type, t_rule->multiple_chars);
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
  int rule_id;

  assert (data != NULL);

  ld = XML_USER_DATA (pd);

  rule_id = ld->coll.count_rules;
  t_rule = &(ld->coll.rules[rule_id]);

  assert (strlen (ld->data_buffer) == ld->data_buf_count);

  t_rule->t_buf = realloc (t_rule->t_buf,
			   t_rule->t_buf_size + ld->data_buf_count);
  if (t_rule->t_buf == NULL)
    {
      pd->xml_error = XML_CUB_OUT_OF_MEMORY;
      PRINT_DEBUG_END (data, "memory allocation failed", -1);
      return -1;
    }

  /* copy partial data to rule tailoring buffer (character to be modified) */
  memcpy (t_rule->t_buf + t_rule->t_buf_size, ld->data_buffer,
	  ld->data_buf_count);
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

  if (pd->verbose)
    {
      char msg[ERR_MSG_SIZE];
      int xml_line_no = XML_GetCurrentLineNumber (pd->xml_parser);
      int xml_col_no = XML_GetCurrentColumnNumber (pd->xml_parser);

      snprintf (msg, sizeof (msg) - 1,
		"* Rule %d, L :%d, Dir:%d, PosType:%d, Mc:%d ;"
		" XML: line: %d , col:%d *",
		rule_id, t_rule->level, t_rule->direction,
		t_rule->r_pos_type, t_rule->multiple_chars, xml_line_no,
		xml_col_no);
      PRINT_DEBUG_END (data, msg, 0);
    }

  clear_data_buffer (data);

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
  int rule_id;

  assert (data != NULL);

  ld = XML_USER_DATA (pd);

  rule_id = ld->coll.count_rules;
  t_rule = &(ld->coll.rules[rule_id]);

  assert (t_rule->r_buf != NULL);
  assert (ld->data_buf_count > 0);

  t_rule->r_buf = realloc (t_rule->r_buf,
			   t_rule->r_buf_size + ld->data_buf_count);

  if (t_rule->r_buf == NULL)
    {
      pd->xml_error = XML_CUB_OUT_OF_MEMORY;
      PRINT_DEBUG_END (data, "memory allocation failed", -1);
      return -1;
    }

  memcpy (t_rule->r_buf + t_rule->r_buf_size, ld->data_buffer,
	  ld->data_buf_count);

  t_rule->r_buf_size += ld->data_buf_count;

  clear_data_buffer (data);

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
  int rule_id;

  assert (data != NULL);

  ld = XML_USER_DATA (pd);
  rule_id = ld->coll.count_rules;
  t_rule = &(ld->coll.rules[rule_id]);

  assert (strlen (ld->data_buffer) < LOC_DATA_COLL_TWO_CHARS);
  if (strlen (ld->data_buffer) >= LOC_DATA_COLL_TWO_CHARS)
    {
      pd->xml_error = XML_CUB_ERR_PARSER;
      PRINT_DEBUG_END (data, "Too much data", -1);
      return -1;
    }

  if (t_rule->t_buf_size < ld->data_buf_count)
    {
      t_rule->t_buf = (char *) realloc (t_rule->t_buf, ld->data_buf_count);
      if (t_rule->t_buf == NULL)
	{
	  pd->xml_error = XML_CUB_OUT_OF_MEMORY;
	  PRINT_DEBUG_END (data, "memory allocation failed", -1);
	  return -1;
	}
    }

  memcpy (t_rule->t_buf, ld->data_buffer, ld->data_buf_count);
  t_rule->t_buf_size = ld->data_buf_count;

  clear_data_buffer (data);
  if (pd->verbose)
    {
      char msg[ERR_MSG_SIZE];
      int rule_id = ld->coll.count_rules;
      TAILOR_RULE *t_rule = &(ld->coll.rules[rule_id]);

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
  char *att_val = NULL;

  assert (data != NULL);

  ld = XML_USER_DATA (pd);

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

  ld = XML_USER_DATA (pd);

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

  ld = XML_USER_DATA (pd);

  if (ld->alpha_tailoring.alphabet_mode != 0
      || ld->alpha_tailoring.count_rules != 0
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
	  PRINT_DEBUG_START (data, attr,
			     "Invalid value for CUBRIDAlphabetMode."
			     "Expected ASCII or UNICODEDATAFILE", -1);
	  return -1;
	}
    }

  att_val = NULL;
  if (xml_get_att_value (attr, "CUBRIDUnicodeDataFilePath", &att_val) == 0)
    {
      assert (att_val != NULL);

      strncpy (ld->alpha_tailoring.unicode_data_file, att_val,
	       sizeof (ld->alpha_tailoring.unicode_data_file) - 1);
      ld->alpha_tailoring.
	unicode_data_file[sizeof (ld->alpha_tailoring.unicode_data_file) -
			  1] = '\0';
      ld->alpha_tailoring.alphabet_mode = 1;
    }

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

  ld = XML_USER_DATA (pd);

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

  ld = XML_USER_DATA (pd);

  /* rule finished, increase count */
  rule_id = ld->alpha_tailoring.count_rules++;
  tf_rule = &(ld->alpha_tailoring.rules[rule_id]);

  if (pd->verbose)
    {
      char msg[64];
      unsigned char *dummy = NULL;
      unsigned int cp_src =
	intl_utf8_to_cp ((unsigned char *) tf_rule->src, tf_rule->src_size,
			 &dummy);
      unsigned int cp_dest =
	intl_utf8_to_cp ((unsigned char *) tf_rule->dest, tf_rule->dest_size,
			 &dummy);

      sprintf (msg, " * Case Rule %d, Src :%0X, Dest:%0X *",
	       rule_id, cp_src, cp_dest);
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

  ld = XML_USER_DATA (pd);

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

  ld = XML_USER_DATA (pd);

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

  clear_data_buffer (data);

  return 0;

error_exit:

  clear_data_buffer (data);

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

  ld = XML_USER_DATA (pd);

  if (*(ld->txt_conv_prm.nl_lang_str) != '\0'
      || *(ld->txt_conv_prm.conv_file) != '\0'
      || *(ld->txt_conv_prm.win_codepages) != '\0')
    {
      PRINT_DEBUG_START (data, attr, "Only one console conversion is allowed",
			 -1);
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
	  PRINT_DEBUG_START (data, attr, "Invalid value for type."
			     "Expecting ISO88591 or ISO or EUC", -1);
	  return -1;
	}
    }

  att_val = NULL;
  if (xml_get_att_value (attr, "windows_codepage", &att_val) == 0)
    {
      assert (att_val != NULL);

      if (strlen (att_val) > sizeof (ld->txt_conv_prm.win_codepages) - 1)
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

      if (strlen (att_val) > sizeof (ld->txt_conv_prm.nl_lang_str) - 1)
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

      if (strlen (att_val) > sizeof (ld->txt_conv_prm.conv_file) - 1)
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

  ld = XML_USER_DATA (pd);

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

  ld = XML_USER_DATA (pd);

  ld->data_buf_count = 0;
  *(ld->data_buffer) = '\0';
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

  assert (ld != NULL);

  /* check number of rules, increase array if necessary */
  if (ld->coll.count_rules + 1 >= ld->coll.max_rules)
    {
      ld->coll.rules = realloc (ld->coll.rules, sizeof (TAILOR_RULE) *
				(ld->coll.max_rules +
				 LOC_DATA_TAILOR_RULES_COUNT_GROW));

      if (ld->coll.rules == NULL)
	{
	  return NULL;
	}

      ld->coll.max_rules += LOC_DATA_TAILOR_RULES_COUNT_GROW;
    }

  /* will use slot indicated by 'coll->count_rules' */
  t_rule = &(ld->coll.rules[ld->coll.count_rules]);
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
      ld->alpha_tailoring.rules =
	realloc (ld->alpha_tailoring.rules, sizeof (TRANSFORM_RULE) *
		 (ld->alpha_tailoring.max_rules +
		  LOC_DATA_TAILOR_RULES_COUNT_GROW));

      if (ld->alpha_tailoring.rules == NULL)
	{
	  return NULL;
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

  assert (ld != NULL);

  /* check number of absolute rules, increase array if necessary */
  if (ld->coll.cub_count_rules + 1 >= ld->coll.cub_max_rules)
    {
      ld->coll.cub_rules =
	realloc (ld->coll.cub_rules,
		 sizeof (CUBRID_TAILOR_RULE) *
		 (ld->coll.max_rules +
		  LOC_DATA_COLL_CUBRID_TAILOR_COUNT_GROW));

      if (ld->coll.cub_rules == NULL)
	{
	  return NULL;
	}

      ld->coll.cub_max_rules += LOC_DATA_COLL_CUBRID_TAILOR_COUNT_GROW;
    }

  /* will use slot indicated by 'coll->cub_count_rules' */
  ct_rule = &(ld->coll.cub_rules[ld->coll.cub_count_rules]);
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
print_debug_start_el (void *data, const char **attrs, const char *msg,
		      const int status)
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
  char err_msg[ERR_MSG_SIZE];
  char str[TXT_CONV_LINE_SIZE];
  char conv_file_name[LOC_FILE_PATH_SIZE];
  int status = NO_ERROR;
  int line_count = 0;
  int txt_conv_max_items = 0;
  int txt_conv_count_items = 0;
  unsigned int i;
  unsigned int cp_text = 0;
  unsigned int cp_unicode = 0;
  unsigned char first_lead_byte = 0;
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

  strncpy (conv_file_name, ld->txt_conv_prm.conv_file,
	   sizeof (conv_file_name) - 1);
  conv_file_name[sizeof (conv_file_name) - 1] = '\0';

  fp = fopen_ex (conv_file_name, "rt");
  if (fp == NULL)
    {
      envvar_confdir_file (conv_file_name, sizeof (conv_file_name),
			   ld->txt_conv_prm.conv_file);
      fp = fopen_ex (conv_file_name, "rt");
    }

  if (fp == NULL)
    {
      snprintf (err_msg, sizeof (err_msg) - 1, "Cannot open file %s",
		ld->txt_conv_prm.conv_file);
      LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
      status = ER_LOC_GEN;
      goto error;
    }

  if (is_verbose)
    {
      printf ("Using file: %s\n", conv_file_name);
    }

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

      cp_text = strtol (s, NULL, 16);

      /* skip codepoints values above 0xFFFF */
      if (cp_text > 0xffff
	  || (cp_text > 0xff &&
	      ld->txt_conv_prm.conv_type == TEXT_CONV_GENERIC_1BYTE))
	{
	  snprintf (err_msg, sizeof (err_msg) - 1, "Codepoint value too big"
		    " in file :%s at line %d", ld->txt_conv_prm.conv_file,
		    line_count);
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
	  if (ld->txt_conv_prm.conv_type == TEXT_CONV_GENERIC_2BYTE
	      && first_lead_byte == 0)
	    {
	      assert (cp_text < 0xff);
	      first_lead_byte = cp_text;
	    }
	  continue;
	}
      cp_unicode = strtol (s, NULL, 16);

      /* first codepoints which maps the same character are not included */
      if (((ld->txt_conv_prm.conv_type == TEXT_CONV_GENERIC_1BYTE
	    && cp_text < 0x80)
	   || ld->txt_conv_prm.conv_type == TEXT_CONV_GENERIC_2BYTE)
	  && (cp_text == cp_unicode && min_values.text_cp == 0xffff
	      && min_values.unicode_cp == 0xffff))
	{
	  continue;
	}

      if (cp_unicode > 0xffff)
	{
	  snprintf (err_msg, sizeof (err_msg) - 1, "Codepoint value too big"
		    " in file :%s at line %d", ld->txt_conv_prm.conv_file,
		    line_count);
	  LOG_LOCALE_ERROR (err_msg, ER_LOC_GEN, true);
	  status = ER_LOC_GEN;
	  goto error;
	}

      if (txt_conv_count_items >= txt_conv_max_items)
	{
	  txt_conv_array =
	    (TXT_CONV_ITEM *) realloc (txt_conv_array, sizeof (TXT_CONV_ITEM)
				       * (txt_conv_max_items +
					  TXT_CONV_ITEM_GROW_COUNT));

	  if (txt_conv_array == NULL)
	    {
	      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
	      status = ER_LOC_GEN;
	      goto error;
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

  ld->txt_conv.first_lead_byte = first_lead_byte;
  if (is_verbose && ld->txt_conv_prm.conv_type == TEXT_CONV_GENERIC_2BYTE)
    {
      printf ("DBCS lead bytes range : %02X - FF\n", first_lead_byte);
    }

  if (ld->txt_conv.text_last_cp == 0 || ld->txt_conv.utf8_last_cp == 0)
    {
      LOG_LOCALE_ERROR ("Invalid console mapping", ER_LOC_GEN, true);
      status = ER_LOC_GEN;
      goto error;
    }

  ld->txt_conv.text_to_utf8 =
    (CONV_CP_TO_BYTES *) malloc (sizeof (CONV_CP_TO_BYTES)
				 * (ld->txt_conv.text_last_cp -
				    ld->txt_conv.text_first_cp + 1));
  memset (ld->txt_conv.text_to_utf8, 0, sizeof (CONV_CP_TO_BYTES)
	  * (ld->txt_conv.text_last_cp - ld->txt_conv.text_first_cp + 1));

  ld->txt_conv.utf8_to_text =
    (CONV_CP_TO_BYTES *) malloc (sizeof (CONV_CP_TO_BYTES)
				 * (ld->txt_conv.utf8_last_cp -
				    ld->txt_conv.utf8_first_cp + 1));
  memset (ld->txt_conv.utf8_to_text, 0, sizeof (CONV_CP_TO_BYTES)
	  * (ld->txt_conv.utf8_last_cp - ld->txt_conv.utf8_first_cp + 1));

  if (ld->txt_conv.text_to_utf8 == NULL || ld->txt_conv.utf8_to_text == NULL)
    {
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_GEN, true);
      status = ER_LOC_GEN;
      goto error;
    }

  for (i = 0; i < ld->txt_conv.text_last_cp - ld->txt_conv.text_first_cp + 1;
       i++)
    {
      ld->txt_conv.text_to_utf8[i].size = 1;
      *(ld->txt_conv.text_to_utf8[i].bytes) = '?';
    }

  for (i = 0; i < ld->txt_conv.utf8_last_cp - ld->txt_conv.utf8_first_cp + 1;
       i++)
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

      assert (cp_text >= ld->txt_conv.text_first_cp
	      && cp_text <= ld->txt_conv.text_last_cp);

      assert (cp_unicode >= ld->txt_conv.utf8_first_cp
	      && cp_unicode <= ld->txt_conv.utf8_last_cp);

      text_to_utf8_item =
	&(ld->txt_conv.text_to_utf8[cp_text - ld->txt_conv.text_first_cp]);
      utf8_to_text_item =
	&(ld->txt_conv.utf8_to_text[cp_unicode - ld->txt_conv.utf8_first_cp]);

      text_to_utf8_item->size =
	intl_cp_to_utf8 (cp_unicode, text_to_utf8_item->bytes);

      if (ld->txt_conv_prm.conv_type == TEXT_CONV_GENERIC_1BYTE)
	{
	  utf8_to_text_item->size = 1;
	  utf8_to_text_item->bytes[0] = cp_text;
	}
      else
	{
	  assert (ld->txt_conv_prm.conv_type == TEXT_CONV_GENERIC_2BYTE);
	  utf8_to_text_item->size =
	    intl_cp_to_dbcs (cp_text, ld->txt_conv.first_lead_byte,
			     utf8_to_text_item->bytes);
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

  if (fp == NULL)
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

  pos1 = *((char *) arg1);
  pos2 = *((char *) arg2);
  s1 = (char *) (cmp_token_name_array + pos1 * cmp_token_name_size);
  s2 = (char *) (cmp_token_name_array + pos2 * cmp_token_name_size);

  return strcmp (s2, s1);
}

/*
 * locale_make_calendar_parse_order() - computes the order in which the 
 *		      string tokenizer should search for matches with 
 *		      weekday and month names, both wide and abbreviated.
 *
 * return:
 * ld(in/out): LOCALE_DATA for which to do the processing
 */
void
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
  qsort (ld->month_names_abbr_parse_order, CAL_MONTH_COUNT,
	 sizeof (char), comp_func_parse_order_index);

  cmp_token_name_array = (char *) (ld->month_names_wide);
  cmp_token_name_size = LOC_DATA_MONTH_WIDE_SIZE;
  qsort (ld->month_names_wide_parse_order, CAL_MONTH_COUNT,
	 sizeof (char), comp_func_parse_order_index);

  cmp_token_name_array = (char *) (ld->day_names_abbreviated);
  cmp_token_name_size = LOC_DATA_DAY_ABBR_SIZE;
  qsort (ld->day_names_abbr_parse_order, CAL_DAY_COUNT,
	 sizeof (char), comp_func_parse_order_index);

  cmp_token_name_array = (char *) (ld->day_names_wide);
  cmp_token_name_size = LOC_DATA_DAY_WIDE_SIZE;
  qsort (ld->day_names_wide_parse_order, CAL_DAY_COUNT,
	 sizeof (char), comp_func_parse_order_index);

  cmp_token_name_array = (char *) (ld->am_pm);
  cmp_token_name_size = LOC_DATA_AM_PM_SIZE;
  qsort (ld->am_pm_parse_order, CAL_AM_PM_COUNT,
	 sizeof (char), comp_func_parse_order_index);
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

  assert (strlen (locale_name) < sizeof (ld->locale_name));
  strcpy (ld->locale_name, locale_name);

  /* default number symbols */
  ld->number_decimal_sym = ',';
  ld->number_group_sym = '.';
  ld->default_currency_code = DB_CURRENCY_NULL;

  ld->alpha_tailoring.sett_max_letters = -1;
  ld->coll.sett_max_cp = -1;
  ld->coll.uca_opt.sett_contr_policy = CONTR_IGNORE;
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
  assert (ld != NULL);

  locale_destroy_collation_tailorings (&(ld->coll));
  locale_destroy_collation_data (&(ld->opt_coll));
  locale_destroy_alphabet_tailoring (&(ld->alpha_tailoring));

  /* alphabets */
  locale_destroy_alphabet_data (&(ld->alphabet), false);
  locale_destroy_alphabet_data (&(ld->identif_alphabet), false);

  locale_destroy_console_conversion (&(ld->txt_conv));
}

/*
 * locale_destroy_alphabet_data() - frees memory for one locale alphabet
 *    return:
 *    a(in/out):
 *
 *    Note : Alphabets that are common are not freed
 */
void
locale_destroy_alphabet_data (const ALPHABET_DATA * a, bool destroy_shared)
{
  assert (a != NULL);

  /* non-tailored alphabets are shared by all locales */
  if (!destroy_shared && a->is_shared)
    {
      return;
    }

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
 * locale_set_shared_data() -
 * return:
 * type(in): type of shared data
 * p_data(in): pointer to a data to be shared
 *
 */
void
locale_set_shared_data (const LOCALE_SHARED_DATA_TYPE type, void *p_data)
{
  assert (p_data != NULL);

  switch (type)
    {
    case SHARED_ALPHABET_ASCII:
      shared_data.ascii_a = p_data;
      break;

    case SHARED_ALPHABET_UNICODE:
      shared_data.unicode_a = p_data;
      break;

    default:
      assert (false);
    }
}

/*
 * locale_get_shared_data() -
 * return: pointer to shared data
 * type(in): type of shared data
 *
 */
void *
locale_get_shared_data (const LOCALE_SHARED_DATA_TYPE type)
{
  switch (type)
    {
    case SHARED_ALPHABET_ASCII:
      return shared_data.ascii_a;
      break;

    case SHARED_ALPHABET_UNICODE:
      return shared_data.unicode_a;
      break;

    default:
      assert (false);
    }

  return NULL;
}

/*
 * locale_destroy_shared_data() - frees memory for common
 *    return:
 *
 */
void
locale_destroy_shared_data (void)
{
  if (shared_data.ascii_a != NULL)
    {
      locale_destroy_alphabet_data (shared_data.ascii_a, true);
      shared_data.ascii_a = NULL;
    }

  if (shared_data.unicode_a != NULL)
    {
      locale_destroy_alphabet_data (shared_data.unicode_a, true);
      shared_data.unicode_a = NULL;
    }
}

/*
 * locale_compile_locale() - converts LDML file into binary file
 *
 * return: error code
 * lf(in): structure containing file paths
 * is_verbose(in):
 */
int
locale_compile_locale (LOCALE_FILE * lf, bool is_verbose)
{
  FILE *fp = NULL;
  XML_PARSER_DATA ldml_parser;
  LOCALE_DATA ld;
  char *ldml_filename = NULL;
  char *locale_str = NULL;
  char *bin_filename = NULL;
  int er_status = NO_ERROR;
  bool is_finished = false;

  assert (lf != NULL);
  locale_str = lf->locale_name;
  ldml_filename = lf->ldml_file;
  bin_filename = lf->bin_file;

  fp = fopen_ex (ldml_filename, "rb");
  if (fp == NULL)
    {
      char msg[ERR_MSG_SIZE];

      snprintf (msg, sizeof (msg) - 1, "Error opening file: %s",
		ldml_filename);
      LOG_LOCALE_ERROR (msg, ER_LOC_GEN, true);

      return ER_LOC_GEN;
    }

  locale_init_data (&ld, locale_str);
  ldml_parser.ud = &ld;
  ldml_parser.verbose = is_verbose;

  ldml_parser.xml_parser =
    xml_init_parser (&ldml_parser, "UTF-8", ldml_elements,
		     sizeof (ldml_elements) / sizeof (XML_ELEMENT_DEF *));

  if (ldml_parser.xml_parser == NULL)
    {
      assert (fp != NULL);
      fclose (fp);

      LOG_LOCALE_ERROR ("Cannot init XML parser", ER_LOC_GEN, true);

      er_status = ER_LOC_GEN;
      goto exit;
    }

  if (is_verbose)
    {
      printf ("\n*** Parsing LDML\n");
    }

  for (; ldml_parser.xml_error == XML_CUB_NO_ERROR && !is_finished;)
    {
      if (xml_parse (&ldml_parser, fp, &is_finished) != XML_CUB_NO_ERROR)
	{
	  break;
	}
    }

  if (is_verbose)
    {
      if (ldml_parser.xml_error == XML_CUB_NO_ERROR)
	{
	  printf ("Parsing finished.\n");
	  printf ("Date format: %s\n", ld.dateFormat);
	  printf ("Time format: %s\n", ld.timeFormat);
	  printf ("Datetime format: %s\n", ld.datetimeFormat);
	  printf ("Timestamp format: %s\n", ld.timestampFormat);
	}
      else
	{
	  char msg[512];
	  const char *xml_err_text = (char *)
	    XML_ErrorString (XML_GetErrorCode (ldml_parser.xml_parser));

	  snprintf (msg, sizeof (msg) - 1, "Error parsing. "
		    "line : %d, column : %d. Internal XML: %s",
		    ldml_parser.xml_error_line, ldml_parser.xml_error_column,
		    xml_err_text);

	  LOG_LOCALE_ERROR (msg, ER_LOC_GEN, true);
	  er_status = ER_LOC_GEN;
	  goto exit;
	}
    }

  locale_make_calendar_parse_order (&ld);

  if (ld.alpha_tailoring.sett_max_letters == -1)
    {
      ld.alpha_tailoring.sett_max_letters = MAX_UNICODE_CHARS;
    }

  if (is_verbose)
    {
      printf ("\n*** Processing alphabet ***\nNumber of letters  = %d\n",
	      ld.alpha_tailoring.sett_max_letters);
    }

  if (ld.alpha_tailoring.sett_max_letters == 0)
    {
      ld.alphabet.l_count = ld.alpha_tailoring.sett_max_letters;
    }
  else
    {
      er_status = unicode_process_alphabet (&ld, is_verbose);
      if (er_status != NO_ERROR)
	{
	  goto exit;
	}
    }

  if (is_verbose)
    {
      printf ("\n*** Processing console conversion data ***\n");
    }

  er_status = load_console_conv_data (&ld, is_verbose);
  if (er_status != NO_ERROR)
    {
      goto exit;
    }

  if (ld.coll.sett_max_cp == -1)
    {
      ld.coll.sett_max_cp = MAX_UNICODE_CHARS;
    }

  if (is_verbose)
    {
      printf ("\n*** Processing collation ***\nNumber of weights  = %d\n",
	      ld.coll.sett_max_cp);
    }

  if (ld.coll.sett_max_cp == 0)
    {
      ld.opt_coll.w_count = ld.coll.sett_max_cp;
    }
  else
    {
      er_status = uca_process_collation (&ld, is_verbose);
      if (er_status != NO_ERROR)
	{
	  goto exit;
	}
    }

  if (is_verbose)
    {
      printf ("\n*** Saving locale to file: %s\n", lf->bin_file);
    }
  er_status = locale_save_to_bin (lf, &ld);

exit:
  locale_destroy_data (&ld);
  xml_destroy_parser (&ldml_parser);
  uca_free_data ();
  unicode_free_data ();

  assert (fp != NULL);
  fclose (fp);

  if (is_verbose)
    {
      printf ("\n\nLocale finished\n\n\n");
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
locale_get_cfg_locales (LOCALE_FILE ** p_locale_files, int *p_num_locales,
			bool is_lang_init)
{
  char locale_cfg_file[LOC_FILE_PATH_SIZE];
  char line[1024];
  FILE *fp = NULL;
  LOCALE_FILE *locale_files = NULL;
  int num_locales;
  int max_locales = 10;
  char msg[ERR_MSG_SIZE];
  int err_status = NO_ERROR;

  assert (p_locale_files != NULL);
  assert (p_num_locales != NULL);

  envvar_confdir_file (locale_cfg_file, sizeof (locale_cfg_file),
		       "cubrid_locales.txt");

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
	  snprintf (msg, sizeof (msg) - 1, "Cannot open file %s",
		    locale_cfg_file);
	  LOG_LOCALE_ERROR (msg, ER_LOC_GEN, true);
	  err_status = ER_LOC_GEN;
	  goto exit;
	}
    }

  locale_files = NULL;
  num_locales = 0;

  locale_files = malloc (max_locales * sizeof (LOCALE_FILE));
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
	  locale_files = realloc (locale_files,
				  max_locales * sizeof (LOCALE_FILE));

	  if (locale_files == NULL)
	    {
	      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_INIT,
				true);
	      err_status = ER_LOC_INIT;
	      goto exit;
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
      err_status = str_pop_token (str, &(loc->bin_file), &next);
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
  bool is_alloc_bin_file = false;
  int er_status = NO_ERROR;

  assert (lf != NULL);

  if (lf->locale_name == NULL
      || strlen (lf->locale_name) > LOC_LOCALE_STR_SIZE)
    {
      er_status = is_lang_init ? ER_LOC_INIT : ER_LOC_GEN;
      LOG_LOCALE_ERROR ("invalid locale name in 'cubrid_locales.txt'",
			er_status, true);
      goto error;
    }

  if (lf->ldml_file == NULL || *(lf->ldml_file) == '\0'
      || *(lf->ldml_file) == '*')
    {
      /* generate name for LDML file */
      char ldml_short_file[LOC_LOCALE_STR_SIZE + 13];

      sprintf (ldml_short_file, "cubrid_%s.xml", lf->locale_name);
      lf->ldml_file = malloc (PATH_MAX + 1);
      if (lf->ldml_file == NULL)
	{
	  er_status = is_lang_init ? ER_LOC_INIT : ER_LOC_GEN;
	  LOG_LOCALE_ERROR ("memory allocation failed", er_status, true);
	  goto error;
	}

      is_alloc_ldml_file = true;
      envvar_confdir_file (lf->ldml_file, PATH_MAX, ldml_short_file);
    }

  if (lf->bin_file == NULL || *(lf->bin_file) == '\0'
      || *(lf->bin_file) == '*')
    {
      /* generate name for binary file */
      char *ext = NULL;
      int len;

      assert (lf->ldml_file != NULL);

      ext = strstr (lf->ldml_file, ".xml");

      if (ext == NULL)
	{
	  len = strlen (lf->ldml_file);
	}
      else
	{
	  len = ext - (lf->ldml_file);
	}

      /* if .xml ext not found, suffix with .locale */
      lf->bin_file = malloc (len + 8);
      if (lf->bin_file == NULL)
	{
	  er_status = is_lang_init ? ER_LOC_INIT : ER_LOC_GEN;
	  LOG_LOCALE_ERROR ("memory allocation failed", er_status, true);
	  goto error;
	}

      is_alloc_bin_file = true;
      strncpy (lf->bin_file, lf->ldml_file, len);
      lf->bin_file[len] = '\0';
      strcat (lf->bin_file, ".locale");
    }

  return NO_ERROR;

error:

  if (is_alloc_ldml_file)
    {
      free (lf->ldml_file);
      lf->ldml_file = NULL;
    }

  if (is_alloc_bin_file)
    {
      free (lf->bin_file);
      lf->bin_file = NULL;
    }

  return er_status;
}


/*
 * save_string_to_bin() - saves a string to binary file
 *
 * return: zero if write successful, non-zero otherwise
 * fp(in): file descriptor
 * s(in): string
 */
static int
save_string_to_bin (FILE * fp, const char *s)
{
  int len;
  int res;

  assert (fp != NULL);
  assert (s != NULL);

  len = strlen (s);
  res = save_int_to_bin (fp, len);
  if (res != 0)
    {
      return -1;
    }
  res = fwrite (s, 1, len, fp);
  if (res != len)
    {
      return -1;
    }

  return 0;
}

/*
 * load_string_from_bin() - loads a string from binary file
 *
 * return: zero if load is successful, non-zero otherwise
 * fp(in): file descriptor
 * s(out): buffer to store string
 * size(in): size of buffer
 */
static int
load_string_from_bin (FILE * fp, char *s, const int size)
{
  int len;
  int res;

  assert (fp != NULL);
  assert (s != NULL);

  res = load_int_from_bin (fp, &len);
  if (res != 0 || len >= size)
    {
      return -1;
    }
  res = fread (s, 1, len, fp);
  if (res != len)
    {
      return -1;
    }

  s[len] = '\0';

  return 0;
}

/*
 * save_int_to_bin() - saves an integer to binary file
 *
 * return: zero if write successful, non-zero otherwise
 * fp(in): file descriptor
 * s(in): int
 */
static int
save_int_to_bin (FILE * fp, const int i)
{
  int res;

  assert (fp != NULL);

  res = fwrite (&i, sizeof (int), 1, fp);
  if (res != 1)
    {
      return -1;
    }

  return 0;
}

/*
 * load_int_from_bin() - loads an integer from binary file
 *
 * return: zero if load is successful, non-zero otherwise
 * fp(in): file descriptor
 * s(out): buffer to store string
 * size(in): size of buffer
 */
static int
load_int_from_bin (FILE * fp, int *i)
{
  int res;

  assert (fp != NULL);
  assert (i != NULL);

  res = fread (i, sizeof (int), 1, fp);
  if (res != 1)
    {
      return -1;
    }

  return 0;
}

/*
 * save_alphabet_to_bin() - saves alphabet data to binary file
 *
 * return: zero if save is successful, non-zero otherwise
 * fp(in): file descriptor
 * a(in): alphabet data to save
 */
static int
save_alphabet_to_bin (FILE * fp, ALPHABET_DATA * a)
{
  int res = 0;

  assert (a != NULL);
  assert (fp != NULL);

  /* alphabet */
  res = save_int_to_bin (fp, a->a_type);
  if (res != 0)
    {
      goto error;
    }

  res = save_int_to_bin (fp, a->l_count);
  if (res != 0)
    {
      goto error;
    }

  if (a->l_count > 0)
    {
      assert (a->lower_multiplier > 0);

      res = save_int_to_bin (fp, a->lower_multiplier);
      if (res != 0)
	{
	  goto error;
	}

      res = fwrite (a->lower_cp, sizeof (unsigned int),
		    a->l_count * a->lower_multiplier, fp);
      if (res != a->l_count * a->lower_multiplier)
	{
	  goto error;
	}

      assert (a->upper_multiplier > 0);

      res = save_int_to_bin (fp, a->upper_multiplier);
      if (res != 0)
	{
	  goto error;
	}

      res = fwrite (a->upper_cp, sizeof (unsigned int),
		    a->l_count * a->upper_multiplier, fp);
      if (res != a->l_count * a->upper_multiplier)
	{
	  goto error;
	}
    }

  return 0;

error:

  return -1;
}

/*
 * load_alphabet_from_bin() - loads alphabet data from binary file
 *
 * return: zero if load is successful, non-zero otherwise
 * lf(in): locale file info
 * fp(in): file descriptor
 * a(in/out): alphabet data to fill
 */
static int
load_alphabet_from_bin (LOCALE_FILE * lf, FILE * fp, ALPHABET_DATA * a)
{
  int res = 0;
  char msg[ERR_MSG_SIZE];
  ALPHABET_DATA *shared_a = NULL;
  int i_value;
  bool use_shared = false;

  assert (a != NULL);
  assert (fp != NULL);

  res = load_int_from_bin (fp, &i_value);
  if (res != 0 || i_value < ALPHABET_UNICODE || i_value > ALPHABET_TAILORED)
    {
      goto error_load;
    }

  a->a_type = (ALPHABET_TYPE) i_value;

  if (a->a_type == ALPHABET_UNICODE)
    {
      shared_a = locale_get_shared_data (SHARED_ALPHABET_UNICODE);
    }
  else if (a->a_type == ALPHABET_ASCII)
    {
      shared_a = locale_get_shared_data (SHARED_ALPHABET_ASCII);
    }

  res = load_int_from_bin (fp, &(a->l_count));
  if (a->l_count < 0 || res != 0)
    {
      goto error_load;
    }

  if (a->l_count > 0)
    {
      if (shared_a != NULL)
	{
	  if (a->l_count <= shared_a->l_count)
	    {
	      use_shared = true;
	      a->is_shared = true;
	    }
	  else
	    {
	      assert (use_shared == false);
	    }
	}

      /* lower info */
      res = load_int_from_bin (fp, &(a->lower_multiplier));
      if (res != 0)
	{
	  goto error_load;
	}

      if (a->lower_multiplier < 1
	  || a->lower_multiplier > INTL_CASING_EXPANSION_MULTIPLIER)
	{
	  goto error_load;
	}

      if (use_shared)
	{
	  assert (shared_a != NULL);

	  if (a->lower_multiplier != shared_a->lower_multiplier)
	    {
	      goto er_corrupted;
	    }
	  a->lower_cp = shared_a->lower_cp;
	  assert (a->lower_multiplier == shared_a->lower_multiplier);

	  /* skip file entries : */
	  if (fseek (fp, (a->l_count) * (a->lower_multiplier) *
		     sizeof (unsigned int), SEEK_CUR) != 0)
	    {
	      goto error_load;
	    }
	}
      else
	{
	  a->lower_cp = malloc ((a->l_count) * (a->lower_multiplier) *
				sizeof (unsigned int));

	  if (a->lower_cp == NULL)
	    {
	      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_INIT,
				true);
	      goto error;
	    }

	  res = fread (a->lower_cp, sizeof (unsigned int),
		       (a->l_count) * (a->lower_multiplier), fp);
	  if (res != (a->l_count) * (a->lower_multiplier))
	    {
	      goto error_load;
	    }
	}

      /* upper info */
      res = load_int_from_bin (fp, &(a->upper_multiplier));
      if (res != 0)
	{
	  goto error_load;
	}

      if (a->upper_multiplier < 1
	  || a->upper_multiplier > INTL_CASING_EXPANSION_MULTIPLIER)
	{
	  goto error_load;
	}

      if (use_shared)
	{
	  assert (shared_a != NULL);

	  if (a->upper_multiplier != shared_a->upper_multiplier)
	    {
	      goto er_corrupted;
	    }
	  a->upper_cp = shared_a->upper_cp;
	  assert (a->upper_multiplier == shared_a->upper_multiplier);

	  /* skip file entries : */
	  if (fseek (fp, (a->l_count) * (a->upper_multiplier) *
		     sizeof (unsigned int), SEEK_CUR) != 0)
	    {
	      goto error_load;
	    }
	}
      else
	{
	  a->upper_cp = malloc ((a->l_count) * (a->upper_multiplier) *
				sizeof (unsigned int));

	  if (a->upper_cp == NULL)
	    {
	      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_INIT,
				true);
	      goto error;
	    }

	  res = fread (a->upper_cp, sizeof (unsigned int),
		       (a->l_count) * (a->upper_multiplier), fp);
	  if (res != (a->l_count) * (a->upper_multiplier))
	    {
	      goto error_load;
	    }
	}
    }

  /* save common alphabet */
  if (shared_a == NULL)
    {
      if (a->a_type == ALPHABET_UNICODE)
	{
	  locale_set_shared_data (SHARED_ALPHABET_UNICODE, a);
	  a->is_shared = true;
	}
      else if (a->a_type == ALPHABET_ASCII)
	{
	  locale_set_shared_data (SHARED_ALPHABET_ASCII, a);
	  a->is_shared = true;
	}
    }

  return 0;

error_load:
  snprintf (msg, sizeof (msg) - 1,
	    "loading alphabet data from file: %s", lf->bin_file);
  LOG_LOCALE_ERROR (msg, ER_LOC_INIT, true);

  return -1;

er_corrupted:
  snprintf (msg, sizeof (msg) - 1,
	    "corrupted alphabet data in file: %s", lf->bin_file);
  LOG_LOCALE_ERROR (msg, ER_LOC_INIT, true);

error:

  return -1;
}


/*
 * save_console_conv_to_bin() - saves console conversion data to binary file
 *
 * return: zero if save is successful, non-zero otherwise
 * fp(in): file descriptor
 * tc(in): console conversion info
 */
static int
save_console_conv_to_bin (FILE * fp, TEXT_CONVERSION * tc)
{
  int res = 0;

  assert (tc != NULL);
  assert (fp != NULL);

  res = save_int_to_bin (fp, tc->conv_type);
  if (res != 0)
    {
      goto error;
    }

  if (tc->conv_type != TEXT_CONV_GENERIC_1BYTE
      && tc->conv_type != TEXT_CONV_GENERIC_2BYTE)
    {
      return 0;
    }

  assert (tc->win_codepages != NULL);
  res = save_string_to_bin (fp, tc->win_codepages);
  if (res != 0)
    {
      goto error;
    }

  assert (tc->nl_lang_str != NULL);
  res = save_string_to_bin (fp, tc->nl_lang_str);
  if (res != 0)
    {
      goto error;
    }

  res = save_int_to_bin (fp, (int) tc->first_lead_byte);
  if (res != 0)
    {
      goto error;
    }

  res = save_int_to_bin (fp, (int) tc->utf8_first_cp);
  if (res != 0)
    {
      goto error;
    }

  res = save_int_to_bin (fp, (int) tc->utf8_last_cp);
  if (res != 0)
    {
      goto error;
    }

  assert (tc->utf8_last_cp > tc->utf8_first_cp);
  assert (tc->utf8_to_text != NULL);

  res = fwrite (tc->utf8_to_text, sizeof (CONV_CP_TO_BYTES),
		tc->utf8_last_cp - tc->utf8_first_cp + 1, fp);
  if (res != tc->utf8_last_cp - tc->utf8_first_cp + 1)
    {
      goto error;
    }

  res = save_int_to_bin (fp, (int) tc->text_first_cp);
  if (res != 0)
    {
      goto error;
    }

  res = save_int_to_bin (fp, (int) tc->text_last_cp);
  if (res != 0)
    {
      goto error;
    }

  assert (tc->text_last_cp > tc->text_first_cp);
  assert (tc->text_to_utf8 != NULL);

  res = fwrite (tc->text_to_utf8, sizeof (CONV_CP_TO_BYTES),
		tc->text_last_cp - tc->text_first_cp + 1, fp);
  if (res != tc->text_last_cp - tc->text_first_cp + 1)
    {
      goto error;
    }

  return 0;

error:

  return -1;
}

/*
 * load_console_conv_from_bin() - loads console conversion data from binary
 *				  file
 *
 * return: zero if load is successful, non-zero otherwise
 * lf(in): locale file info
 * fp(in): file descriptor
 * tc(in/out): console conversion info to fill
 */
static int
load_console_conv_from_bin (LOCALE_FILE * lf, FILE * fp, TEXT_CONVERSION * tc)
{
  int res = 0;
  char msg[ERR_MSG_SIZE];
  int i_value;

  assert (tc != NULL);
  assert (fp != NULL);

  res = load_int_from_bin (fp, &i_value);
  if (res != 0 || i_value < TEXT_CONV_NO_CONVERSION
      || i_value > TEXT_CONV_GENERIC_2BYTE)
    {
      goto error_load;
    }

  tc->conv_type = i_value;

  if (tc->conv_type != TEXT_CONV_GENERIC_1BYTE
      && tc->conv_type != TEXT_CONV_GENERIC_2BYTE)
    {
      return 0;
    }

  tc->win_codepages = (char *) malloc (TXT_CONV_SYSTEM_STR_SIZE);
  if (tc->win_codepages == NULL)
    {
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_INIT, true);
      goto error;
    }
  res = load_string_from_bin (fp, tc->win_codepages,
			      TXT_CONV_SYSTEM_STR_SIZE - 1);
  if (res != 0)
    {
      goto error;
    }

  tc->nl_lang_str = (char *) malloc (TXT_CONV_SYSTEM_STR_SIZE);
  if (tc->nl_lang_str == NULL)
    {
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_INIT, true);
      goto error;
    }
  res = load_string_from_bin (fp, tc->nl_lang_str,
			      TXT_CONV_SYSTEM_STR_SIZE - 1);
  if (res != 0)
    {
      goto error;
    }

  res = load_int_from_bin (fp, &i_value);
  if (res != 0 || i_value < 0 || i_value > 0xff)
    {
      goto error_load;
    }

  tc->first_lead_byte = i_value;

  res = load_int_from_bin (fp, (int *) &(tc->utf8_first_cp));
  if (res != 0)
    {
      goto error;
    }

  res = load_int_from_bin (fp, (int *) &(tc->utf8_last_cp));
  if (res != 0)
    {
      goto error;
    }

  if (tc->utf8_last_cp <= tc->utf8_first_cp)
    {
      goto er_corrupted;
    }

  tc->utf8_to_text =
    (CONV_CP_TO_BYTES *) malloc (sizeof (CONV_CP_TO_BYTES) *
				 (tc->utf8_last_cp - tc->utf8_first_cp + 1));

  if (tc->utf8_to_text == NULL)
    {
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_INIT, true);
      goto error;
    }

  res = fread (tc->utf8_to_text, sizeof (CONV_CP_TO_BYTES),
	       (tc->utf8_last_cp - tc->utf8_first_cp + 1), fp);
  if (res != tc->utf8_last_cp - tc->utf8_first_cp + 1)
    {
      goto error;
    }

  res = load_int_from_bin (fp, (int *) &(tc->text_first_cp));
  if (res != 0)
    {
      goto error;
    }

  res = load_int_from_bin (fp, (int *) &(tc->text_last_cp));
  if (res != 0)
    {
      goto error;
    }

  if (tc->text_last_cp <= tc->text_first_cp)
    {
      goto er_corrupted;
    }
  tc->text_to_utf8 =
    (CONV_CP_TO_BYTES *) malloc (sizeof (CONV_CP_TO_BYTES) *
				 (tc->text_last_cp - tc->text_first_cp + 1));

  if (tc->text_to_utf8 == NULL)
    {
      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_INIT, true);
      goto error;
    }

  res = fread (tc->text_to_utf8, sizeof (CONV_CP_TO_BYTES),
	       (tc->text_last_cp - tc->text_first_cp + 1), fp);
  if (res != tc->text_last_cp - tc->text_first_cp + 1)
    {
      goto error;
    }

  return 0;

error_load:
  snprintf (msg, sizeof (msg) - 1,
	    "loading console conversion data from file: %s", lf->bin_file);
  LOG_LOCALE_ERROR (msg, ER_LOC_INIT, true);

  return -1;

er_corrupted:
  snprintf (msg, sizeof (msg) - 1,
	    "corrupted console conversion data in file: %s", lf->bin_file);
  LOG_LOCALE_ERROR (msg, ER_LOC_INIT, true);

error:

  return -1;
}

/*
 * save_uca_opt_to_bin() - saves UCA options to binary file
 *
 * return: zero if save is successful, non-zero otherwise
 * fp(in): file descriptor
 * uca_opt(in): options to save
 */
static int
save_uca_opt_to_bin (FILE * fp, UCA_OPTIONS * uca_opt)
{
  int res = 0;

  assert (uca_opt != NULL);
  assert (fp != NULL);

  res = save_int_to_bin (fp, uca_opt->sett_strength);
  if (res != 0)
    {
      goto error;
    }

  res = save_int_to_bin (fp, uca_opt->sett_backwards);
  if (res != 0)
    {
      goto error;
    }

  res = save_int_to_bin (fp, uca_opt->sett_caseLevel);
  if (res != 0)
    {
      goto error;
    }

  res = save_int_to_bin (fp, uca_opt->sett_caseFirst);
  if (res != 0)
    {
      goto error;
    }

  res = save_int_to_bin (fp, uca_opt->sett_expansions);
  if (res != 0)
    {
      goto error;
    }

  res = save_int_to_bin (fp, uca_opt->sett_contr_policy);
  if (res != 0)
    {
      goto error;
    }

  return 0;

error:

  return -1;
}

/*
 * load_uca_opt_from_bin() - loads UCA options data from file
 *
 * return: zero if load is successful, non-zero otherwise
 * lf(in): locale file info
 * fp(in): file descriptor
 * uca_opt(in): UCA options to load into
 */
static int
load_uca_opt_from_bin (LOCALE_FILE * lf, FILE * fp, UCA_OPTIONS * uca_opt)
{
  char msg[ERR_MSG_SIZE];
  int res = 0;
  int i_value;

  assert (uca_opt != NULL);
  assert (fp != NULL);

  res = load_int_from_bin (fp, &i_value);
  if (res != 0)
    {
      goto error;
    }

  if (i_value < TAILOR_UNDEFINED || i_value > TAILOR_IDENTITY)
    {
      snprintf (msg, sizeof (msg) - 1,
		"corrupted collation options data in file: %s", lf->bin_file);
      LOG_LOCALE_ERROR (msg, ER_LOC_INIT, true);
      goto error;
    }
  uca_opt->sett_strength = (T_LEVEL) i_value;

  res = load_int_from_bin (fp, &i_value);
  if (res != 0)
    {
      goto error;
    }
  uca_opt->sett_backwards = (bool) i_value;

  res = load_int_from_bin (fp, &i_value);
  if (res != 0)
    {
      goto error;
    }
  uca_opt->sett_caseLevel = (bool) i_value;

  res = load_int_from_bin (fp, &(uca_opt->sett_caseFirst));
  if (res != 0)
    {
      goto error;
    }

  res = load_int_from_bin (fp, &i_value);
  if (res != 0)
    {
      goto error;
    }
  uca_opt->sett_expansions = (bool) i_value;

  res = load_int_from_bin (fp, &(uca_opt->sett_contr_policy));
  if (res != 0)
    {
      goto error;
    }

  return 0;

error:

  return -1;
}

/*
 * save_contraction_to_bin() - saves collation contraction data to binary file
 *
 * return: zero if save is successful, non-zero otherwise
 * fp(in): file descriptor
 * c(in): contraction to save
 * use_expansion(in):
 * use_level_4(in):
 */
static int
save_contraction_to_bin (FILE * fp, COLL_CONTRACTION * c, bool use_expansion,
			 bool use_level_4)
{
  int res = 0;

  assert (c != NULL);
  assert (fp != NULL);

  res = save_int_to_bin (fp, c->cp_count);
  if (res != 0)
    {
      goto error;
    }

  res = save_string_to_bin (fp, c->c_buf);
  if (res != 0)
    {
      goto error;
    }

  if (use_expansion)
    {
      assert (c->uca_num > 0 || c->uca_num <= MAX_UCA_EXP_CE);
      res = save_int_to_bin (fp, c->uca_num);
      if (res != 0)
	{
	  goto error;
	}

      res = fwrite (c->uca_w_l13, sizeof (UCA_L13_W), c->uca_num, fp);
      if (res != c->uca_num)
	{
	  goto error;
	}

      if (use_level_4)
	{
	  res = fwrite (c->uca_w_l4, sizeof (UCA_L4_W), c->uca_num, fp);
	  if (res != c->uca_num)
	    {
	      goto error;
	    }
	}
    }
  else
    {
      res = save_int_to_bin (fp, c->wv);
      if (res != 0)
	{
	  goto error;
	}
    }

  res = save_int_to_bin (fp, c->next);
  if (res != 0)
    {
      goto error;
    }

  return 0;

error:

  return -1;
}

/*
 * load_contraction_from_bin() - loads a collation contraction data from file
 *
 * return: zero if load is successful, non-zero otherwise
 * lf(in): locale file info
 * fp(in): file descriptor
 * c(in): contraction to load into
 * use_expansion (in):
 * use_level_4 (in):
 */
static int
load_contraction_from_bin (LOCALE_FILE * lf, FILE * fp, COLL_CONTRACTION * c,
			   bool use_expansion, bool use_level_4)
{
  char msg[ERR_MSG_SIZE];
  int res = 0;
  int i_value;

  assert (c != NULL);
  assert (fp != NULL);

  res = load_int_from_bin (fp, &(c->cp_count));
  if (res != 0)
    {
      goto error;
    }

  assert (c->cp_count > 1);

  res = load_string_from_bin (fp, c->c_buf, sizeof (c->c_buf) - 1);
  if (res != 0)
    {
      goto error;
    }

  c->size = strlen (c->c_buf);

  if (use_expansion)
    {
      res = load_int_from_bin (fp, &i_value);
      if (res != 0)
	{
	  goto error;
	}

      if (i_value < 1 || i_value > MAX_UCA_EXP_CE)
	{
	  snprintf (msg, sizeof (msg) - 1,
		    "corrupted collation contraction data in file: %s",
		    lf->bin_file);
	  LOG_LOCALE_ERROR (msg, ER_LOC_INIT, true);
	  goto error;
	}

      c->uca_num = i_value;

      res = fread (c->uca_w_l13, sizeof (UCA_L13_W), c->uca_num, fp);
      if (res != c->uca_num)
	{
	  goto error;
	}

      if (use_level_4)
	{
	  res = fread (c->uca_w_l4, sizeof (UCA_L4_W), c->uca_num, fp);
	  if (res != c->uca_num)
	    {
	      goto error;
	    }
	}
    }
  else
    {
      res = load_int_from_bin (fp, (int *) &(c->wv));
      if (res != 0)
	{
	  goto error;
	}
    }

  res = load_int_from_bin (fp, (int *) &(c->next));
  if (res != 0)
    {
      goto error;
    }

  return 0;

error:

  return -1;
}

/*
 * locale_save_to_bin() - saves locale data to binary file
 *
 * return: error code
 * lf(in): locale file info
 * ld(in): locale data
 */
static int
locale_save_to_bin (LOCALE_FILE * lf, LOCALE_DATA * ld)
{
  FILE *fp;
  int res, i;
  char msg[ERR_MSG_SIZE];

  assert (lf != NULL);
  assert (ld != NULL);

  fp = fopen_ex (lf->bin_file, "wb");
  if (fp == NULL)
    {
      goto error;
    }

  /* build number */
  res = save_string_to_bin (fp, rel_build_number ());
  if (res != 0)
    {
      goto error;
    }

  /* locale string */
  res = save_string_to_bin (fp, ld->locale_name);
  if (res != 0)
    {
      goto error;
    }

  /* date format */
  res = save_string_to_bin (fp, ld->dateFormat);
  if (res != 0)
    {
      goto error;
    }

  /* time format */
  res = save_string_to_bin (fp, ld->timeFormat);
  if (res != 0)
    {
      goto error;
    }

  /* datetime format */
  res = save_string_to_bin (fp, ld->datetimeFormat);
  if (res != 0)
    {
      goto error;
    }

  /* timestamp format */
  res = save_string_to_bin (fp, ld->timestampFormat);
  if (res != 0)
    {
      goto error;
    }

  /* months name */
  for (i = 0; i < CAL_MONTH_COUNT; i++)
    {
      res = save_string_to_bin (fp, ld->month_names_abbreviated[i]);
      if (res != 0)
	{
	  goto error;
	}
      res = save_string_to_bin (fp, ld->month_names_wide[i]);
      if (res != 0)
	{
	  goto error;
	}
    }

  /* week days name */
  for (i = 0; i < CAL_DAY_COUNT; i++)
    {
      res = save_string_to_bin (fp, ld->day_names_abbreviated[i]);
      if (res != 0)
	{
	  goto error;
	}
      res = save_string_to_bin (fp, ld->day_names_wide[i]);
      if (res != 0)
	{
	  goto error;
	}
    }

  if (fwrite
      (ld->day_names_abbr_parse_order, sizeof (char), CAL_DAY_COUNT,
       fp) != CAL_DAY_COUNT)
    {
      goto error;
    }
  if (fwrite
      (ld->day_names_wide_parse_order, sizeof (char), CAL_DAY_COUNT,
       fp) != CAL_DAY_COUNT)
    {
      goto error;
    }
  if (fwrite (ld->month_names_abbr_parse_order, sizeof (char),
	      CAL_MONTH_COUNT, fp) != CAL_MONTH_COUNT)
    {
      goto error;
    }
  if (fwrite (ld->month_names_wide_parse_order, sizeof (char),
	      CAL_MONTH_COUNT, fp) != CAL_MONTH_COUNT)
    {
      goto error;
    }
  if (fwrite (ld->am_pm_parse_order, sizeof (char),
	      CAL_AM_PM_COUNT, fp) != CAL_AM_PM_COUNT)
    {
      goto error;
    }

  /* day periods name */
  for (i = 0; i < CAL_AM_PM_COUNT; i++)
    {
      res = save_string_to_bin (fp, ld->am_pm[i]);
      if (res != 0)
	{
	  goto error;
	}
    }

  res = fwrite (&(ld->number_decimal_sym), sizeof (char), 1, fp);
  if (res != 1)
    {
      goto error;
    }

  res = fwrite (&(ld->number_group_sym), sizeof (char), 1, fp);
  if (res != 1)
    {
      goto error;
    }

  if (ld->default_currency_code == DB_CURRENCY_NULL)
    {
      ld->default_currency_code = DB_CURRENCY_DOLLAR;
    }
  res = save_int_to_bin (fp, ld->default_currency_code);
  if (res != 0)
    {
      goto error;
    }

  /* alphabet */
  res = save_alphabet_to_bin (fp, &(ld->alphabet));
  if (res != 0)
    {
      goto error;
    }

  /* alphabet for identifiers */
  res = save_alphabet_to_bin (fp, &(ld->identif_alphabet));
  if (res != 0)
    {
      goto error;
    }

  /* collation */
  res = save_uca_opt_to_bin (fp, &(ld->opt_coll.uca_opt));
  if (res != 0)
    {
      goto error;
    }

  res = save_int_to_bin (fp, ld->opt_coll.w_count);
  if (res != 0)
    {
      goto error;
    }

  res = save_int_to_bin (fp, ld->opt_coll.uca_exp_num);
  if (res != 0)
    {
      goto error;
    }

  if (ld->opt_coll.w_count > 0)
    {
      if (ld->opt_coll.uca_exp_num <= 1)
	{
	  res = fwrite (ld->opt_coll.weights, sizeof (unsigned int),
			ld->opt_coll.w_count, fp);
	  if (res != ld->opt_coll.w_count)
	    {
	      goto error;
	    }
	}
      else
	{
	  res = fwrite (ld->opt_coll.uca_w_l13, sizeof (UCA_L13_W)
			* ld->opt_coll.uca_exp_num, ld->opt_coll.w_count, fp);
	  if (res != ld->opt_coll.w_count)
	    {
	      goto error;
	    }

	  if (ld->opt_coll.uca_opt.sett_strength >= TAILOR_QUATERNARY)
	    {
	      res = fwrite (ld->opt_coll.uca_w_l4, sizeof (UCA_L4_W)
			    * ld->opt_coll.uca_exp_num,
			    ld->opt_coll.w_count, fp);
	      if (res != ld->opt_coll.w_count)
		{
		  goto error;
		}
	    }

	  res = fwrite (ld->opt_coll.uca_num, sizeof (char),
			ld->opt_coll.w_count, fp);
	  if (res != ld->opt_coll.w_count)
	    {
	      goto error;
	    }
	}

      res = fwrite (ld->opt_coll.next_cp, sizeof (unsigned int),
		    ld->opt_coll.w_count, fp);
      if (res != ld->opt_coll.w_count)
	{
	  goto error;
	}
    }

  /* contractions */
  res = save_int_to_bin (fp, ld->opt_coll.count_contr);
  if (res != 0)
    {
      goto error;
    }

  if (ld->opt_coll.count_contr > 0)
    {
      for (i = 0; i < ld->opt_coll.count_contr; i++)
	{
	  res = save_contraction_to_bin (fp, &(ld->opt_coll.contr_list[i]),
					 (ld->opt_coll.uca_exp_num >
					  1) ? true : false,
					 (ld->opt_coll.uca_opt.
					  sett_strength >=
					  TAILOR_QUATERNARY) ? true : false);
	  if (res != 0)
	    {
	      goto error;
	    }
	}

      res = save_int_to_bin (fp, ld->opt_coll.contr_min_size);
      if (res != 0)
	{
	  goto error;
	}

      res = save_int_to_bin (fp, ld->opt_coll.cp_first_contr_offset);
      if (res != 0)
	{
	  goto error;
	}

      assert (ld->opt_coll.cp_first_contr_offset >= 0);

      res = save_int_to_bin (fp, ld->opt_coll.cp_first_contr_count);
      if (res != 0)
	{
	  goto error;
	}

      assert (ld->opt_coll.cp_first_contr_count > 0);

      res = fwrite (ld->opt_coll.cp_first_contr_array, sizeof (int),
		    ld->opt_coll.cp_first_contr_count, fp);
      if (res != ld->opt_coll.cp_first_contr_count)
	{
	  goto error;
	}
    }

  res = save_console_conv_to_bin (fp, &(ld->txt_conv));
  if (res != 0)
    {
      goto error;
    }

  assert (fp != NULL);
  fclose (fp);

  return NO_ERROR;

error:
  if (fp != NULL)
    {
      fclose (fp);
    }

  snprintf (msg, sizeof (msg) - 1,
	    "Error saving binary locale data to file: %s", lf->bin_file);
  LOG_LOCALE_ERROR (msg, ER_LOC_GEN, true);

  return ER_LOC_GEN;
}

/*
 * locale_load_from_bin() - loads locale data from binary file
 *
 * return: error code
 * lf(in): locale file info
 * ld(out): locale data
 */
int
locale_load_from_bin (LOCALE_FILE * lf, LOCALE_DATA * ld)
{
  FILE *fp;
  int res, i;
  char bin_build_number[32];
  char msg[ERR_MSG_SIZE];

  assert (lf != NULL);
  assert (ld != NULL);

  fp = fopen_ex (lf->bin_file, "rb");
  if (fp == NULL)
    {
      goto error_load;
    }

  /* check build number of binary file */
  res = load_string_from_bin (fp, bin_build_number,
			      sizeof (bin_build_number));
  if (res != 0)
    {
      goto error_load;
    }

  if (strcmp (bin_build_number, rel_build_number ()))
    {
      snprintf (msg, sizeof (msg) - 1,
		"binary locale file: %s is not compatible", lf->bin_file);
      LOG_LOCALE_ERROR (msg, ER_LOC_INIT, true);
      goto error;
    }

  /* locale string */
  res = load_string_from_bin (fp, ld->locale_name, sizeof (ld->locale_name));
  if (res != 0)
    {
      goto error_load;
    }

  if (strcmp (lf->locale_name, ld->locale_name))
    {
      snprintf (msg, sizeof (msg) - 1,
		"binary locale file: %s . Locale strings don't match :"
		" found '%s', expecting '%s'",
		lf->bin_file, ld->locale_name, lf->locale_name);
      LOG_LOCALE_ERROR (msg, ER_LOC_INIT, true);
      goto error;
    }

  /* date format */
  res = load_string_from_bin (fp, ld->dateFormat, sizeof (ld->dateFormat));
  if (res != 0)
    {
      goto error_load;
    }

  /* time format */
  res = load_string_from_bin (fp, ld->timeFormat, sizeof (ld->timeFormat));
  if (res != 0)
    {
      goto error_load;
    }

  /* datetime format */
  res = load_string_from_bin (fp, ld->datetimeFormat,
			      sizeof (ld->datetimeFormat));
  if (res != 0)
    {
      goto error_load;
    }

  /* timestamp format */
  res = load_string_from_bin (fp, ld->timestampFormat,
			      sizeof (ld->timestampFormat));
  if (res != 0)
    {
      goto error_load;
    }

  /* months name */
  for (i = 0; i < CAL_MONTH_COUNT; i++)
    {
      res = load_string_from_bin (fp, ld->month_names_abbreviated[i],
				  LOC_DATA_MONTH_ABBR_SIZE);
      if (res != 0)
	{
	  goto error_load;
	}
      res = load_string_from_bin (fp, ld->month_names_wide[i],
				  LOC_DATA_MONTH_WIDE_SIZE);
      if (res != 0)
	{
	  goto error_load;
	}
    }

  /* week days name */
  for (i = 0; i < CAL_DAY_COUNT; i++)
    {
      res = load_string_from_bin (fp, ld->day_names_abbreviated[i],
				  LOC_DATA_DAY_ABBR_SIZE);
      if (res != 0)
	{
	  goto error_load;
	}
      res = load_string_from_bin (fp, ld->day_names_wide[i],
				  LOC_DATA_DAY_WIDE_SIZE);
      if (res != 0)
	{
	  goto error_load;
	}
    }

  if (fread (ld->day_names_abbr_parse_order, sizeof (char),
	     CAL_DAY_COUNT, fp) != CAL_DAY_COUNT)
    {
      goto error;
    }
  if (fread (ld->day_names_wide_parse_order, sizeof (char),
	     CAL_DAY_COUNT, fp) != CAL_DAY_COUNT)
    {
      goto error;
    }
  if (fread (ld->month_names_abbr_parse_order, sizeof (char),
	     CAL_MONTH_COUNT, fp) != CAL_MONTH_COUNT)
    {
      goto error;
    }
  if (fread (ld->month_names_wide_parse_order, sizeof (char),
	     CAL_MONTH_COUNT, fp) != CAL_MONTH_COUNT)
    {
      goto error;
    }
  if (fread (ld->am_pm_parse_order, sizeof (char),
	     CAL_AM_PM_COUNT, fp) != CAL_AM_PM_COUNT)
    {
      goto error;
    }

  /* day periods name */
  for (i = 0; i < CAL_AM_PM_COUNT; i++)
    {
      res = load_string_from_bin (fp, ld->am_pm[i], LOC_DATA_AM_PM_SIZE);
      if (res != 0)
	{
	  goto error_load;
	}
    }

  res = fread (&(ld->number_decimal_sym), sizeof (char), 1, fp);
  if (res != 1)
    {
      goto error_load;
    }

  res = fread (&(ld->number_group_sym), sizeof (char), 1, fp);
  if (res != 1)
    {
      goto error_load;
    }

  res = load_int_from_bin (fp, &i);
  if (res != 0)
    {
      goto error;
    }
  if (i < DB_CURRENCY_DOLLAR || i >= DB_CURRENCY_NULL)
    {
      LOG_LOCALE_ERROR ("Value for default currency ISO code is not in the "
			"range of supported currencies.", ER_LOC_INIT, true);
      goto error;
    }
  ld->default_currency_code = (DB_CURRENCY) i;

  /* alphabet */
  res = load_alphabet_from_bin (lf, fp, &(ld->alphabet));
  if (res != 0)
    {
      goto error;
    }

  /* alphabet for identifiers */
  res = load_alphabet_from_bin (lf, fp, &(ld->identif_alphabet));
  if (res != 0)
    {
      goto error;
    }

  /* collation */
  res = load_uca_opt_from_bin (lf, fp, &(ld->opt_coll.uca_opt));
  if (res != 0)
    {
      goto error_load;
    }

  res = load_int_from_bin (fp, &(ld->opt_coll.w_count));
  if (ld->opt_coll.w_count < 0 || res != 0)
    {
      goto error_load;
    }

  res = load_int_from_bin (fp, &(ld->opt_coll.uca_exp_num));
  if (res != 0)
    {
      goto error_load;
    }

  if (ld->opt_coll.uca_exp_num < 0
      || ld->opt_coll.uca_exp_num > MAX_UCA_EXP_CE)
    {
      LOG_LOCALE_ERROR ("Value for number of maximum number chars in "
			"expansion is out of range.", ER_LOC_INIT, true);
      goto error;
    }

  if (ld->opt_coll.w_count > 0)
    {
      if (ld->opt_coll.uca_exp_num > 1)
	{
	  /* Weight levels 1-3 */
	  ld->opt_coll.uca_w_l13 = (UCA_L13_W *)
	    malloc (ld->opt_coll.w_count * ld->opt_coll.uca_exp_num
		    * sizeof (UCA_L13_W));
	  if (ld->opt_coll.uca_w_l13 == NULL)
	    {
	      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_INIT,
				true);
	      goto error;
	    }

	  res = fread (ld->opt_coll.uca_w_l13, sizeof (UCA_L13_W)
		       * ld->opt_coll.uca_exp_num, ld->opt_coll.w_count, fp);
	  if (res != ld->opt_coll.w_count)
	    {
	      goto error_load;
	    }

	  /* Weight level 4 */
	  if (ld->opt_coll.uca_opt.sett_strength >= TAILOR_QUATERNARY)
	    {
	      ld->opt_coll.uca_w_l4 = (UCA_L4_W *)
		malloc (ld->opt_coll.w_count * ld->opt_coll.uca_exp_num
			* sizeof (UCA_L4_W));
	      if (ld->opt_coll.uca_w_l4 == NULL)
		{
		  LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_INIT,
				    true);
		  goto error;
		}

	      res = fread (ld->opt_coll.uca_w_l4, sizeof (UCA_L4_W)
			   * ld->opt_coll.uca_exp_num,
			   ld->opt_coll.w_count, fp);
	      if (res != ld->opt_coll.w_count)
		{
		  goto error_load;
		}
	    }

	  /* Weight levels CE num per CP */
	  ld->opt_coll.uca_num =
	    malloc (ld->opt_coll.w_count * sizeof (char));
	  if (ld->opt_coll.uca_num == NULL)
	    {
	      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_INIT,
				true);
	      goto error;
	    }

	  res = fread (ld->opt_coll.uca_num, sizeof (char),
		       ld->opt_coll.w_count, fp);
	  if (res != ld->opt_coll.w_count)
	    {
	      goto error_load;
	    }
	}
      else
	{
	  ld->opt_coll.weights = malloc (ld->opt_coll.w_count *
					 sizeof (unsigned int));
	  if (ld->opt_coll.weights == NULL)
	    {
	      LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_INIT,
				true);
	      goto error;
	    }

	  res = fread (ld->opt_coll.weights, sizeof (unsigned int),
		       ld->opt_coll.w_count, fp);
	  if (res != ld->opt_coll.w_count)
	    {
	      goto error_load;
	    }
	}

      ld->opt_coll.next_cp = malloc (ld->opt_coll.w_count *
				     sizeof (unsigned int));

      if (ld->opt_coll.next_cp == NULL)
	{
	  LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_INIT, true);
	  goto error;
	}

      res = fread (ld->opt_coll.next_cp, sizeof (unsigned int),
		   ld->opt_coll.w_count, fp);
      if (res != ld->opt_coll.w_count)
	{
	  goto error_load;
	}
    }

  /* contractions */
  res = load_int_from_bin (fp, &(ld->opt_coll.count_contr));
  if (res != 0)
    {
      goto error;
    }

  if (ld->opt_coll.count_contr > 0)
    {
      ld->opt_coll.contr_list = malloc (ld->opt_coll.count_contr *
					sizeof (COLL_CONTRACTION));

      if (ld->opt_coll.contr_list == NULL)
	{
	  LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_INIT, true);
	  goto error;
	}

      for (i = 0; i < ld->opt_coll.count_contr; i++)
	{
	  res =
	    load_contraction_from_bin (lf, fp, &(ld->opt_coll.contr_list[i]),
				       (ld->opt_coll.uca_exp_num >
					1) ? true : false,
				       (ld->opt_coll.uca_opt.sett_strength >=
					TAILOR_QUATERNARY) ? true : false);
	  if (res != 0)
	    {
	      goto error;
	    }
	}

      res = load_int_from_bin (fp, (int *) &(ld->opt_coll.contr_min_size));
      if (res != 0)
	{
	  goto error;
	}

      res = load_int_from_bin (fp,
			       (int *) &(ld->opt_coll.cp_first_contr_offset));
      if (res != 0)
	{
	  goto error;
	}

      assert (ld->opt_coll.cp_first_contr_offset >= 0);

      res = load_int_from_bin (fp,
			       (int *) &(ld->opt_coll.cp_first_contr_count));
      if (res != 0)
	{
	  goto error;
	}

      assert (ld->opt_coll.cp_first_contr_count > 0);

      ld->opt_coll.cp_first_contr_array =
	(int *) malloc (ld->opt_coll.cp_first_contr_count * sizeof (int));
      if (ld->opt_coll.cp_first_contr_array == NULL)
	{
	  LOG_LOCALE_ERROR ("memory allocation failed", ER_LOC_INIT, true);
	  goto error;
	}

      res = fread (ld->opt_coll.cp_first_contr_array, sizeof (int),
		   ld->opt_coll.cp_first_contr_count, fp);
      if (res != ld->opt_coll.cp_first_contr_count)
	{
	  goto error_load;
	}
    }

  /* console conversion */
  res = load_console_conv_from_bin (lf, fp, &(ld->txt_conv));
  if (res != 0)
    {
      goto error;
    }

  assert (fp != NULL);
  fclose (fp);

  return NO_ERROR;

error_load:
  snprintf (msg, sizeof (msg) - 1, "loading binary locale file: %s",
	    lf->bin_file);
  LOG_LOCALE_ERROR (msg, ER_LOC_INIT, true);

error:
  if (fp != NULL)
    {
      fclose (fp);
    }
  return ER_LOC_INIT;
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
dump_locale_alphabet (ALPHABET_DATA * ad, int dl_settings,
		      int lower_bound, int upper_bound)
{
  int i, cp;
  unsigned char utf8_buf[INTL_UTF8_MAX_CHAR_SIZE + 1];
  unsigned int *case_buf;
  char out_case[128];
  char out_cp[128];

  assert (ad != NULL);
  assert (lower_bound <= upper_bound);

  printf ("Alphabet type : ");
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
      printf ("CP: Ux%04X | %-4s", cp, (cp > 0x0020 ? utf8_buf : ""));
      if ((dl_settings & DUMPLOCALE_IS_ALPHABET_LOWER) != 0)
	{
	  bool print_case = true;
	  memset (out_cp, 0, 128);
	  memset (out_case, 0, 128);

	  case_buf = &(ad->lower_cp[cp * ad->lower_multiplier]);

	  for (i = 0; i < ad->lower_multiplier; i++)
	    {
	      char temp_cp[7];	/* Buffer for storing a codepoint as string, 
				 * e.g. Ux0000 -> UxFFFF, null-terminated, 
				 * length is 7. */
	      if (case_buf[i] == 0)
		{
		  break;
		}

	      if (case_buf[i] < 0x20)
		{
		  print_case = false;
		}
	      memset (utf8_buf, 0, INTL_UTF8_MAX_CHAR_SIZE + 1);
	      intl_cp_to_utf8 (case_buf[i], utf8_buf);
	      strcat (out_case, (char *) utf8_buf);
	      memset (temp_cp, 0, 7);
	      snprintf (temp_cp, sizeof (temp_cp) - 1, "Ux%04X", case_buf[i]);
	      strcat (out_cp, temp_cp);
	    }
	  printf (" | Lower :  CP(s): %-12s, lower char(s): %-12s",
		  out_cp, (print_case ? out_case : ""));
	}
      if ((dl_settings & DUMPLOCALE_IS_ALPHABET_UPPER) != 0)
	{
	  bool print_case = true;
	  memset (out_cp, 0, 128);
	  memset (out_case, 0, 128);

	  case_buf = &(ad->upper_cp[cp * ad->upper_multiplier]);

	  for (i = 0; i < ad->upper_multiplier; i++)
	    {
	      char temp_cp[7];	/* Buffer for storing a codepoint as string, 
				 * e.g. Ux0000 -> UxFFFF, null-terminated, 
				 * length is 7. */
	      if (case_buf[i] == 0)
		{
		  break;
		}

	      if (case_buf[i] < 0x20)
		{
		  print_case = false;
		}

	      memset (utf8_buf, 0, INTL_UTF8_MAX_CHAR_SIZE + 1);
	      intl_cp_to_utf8 (case_buf[i], utf8_buf);
	      strcat (out_case, (char *) utf8_buf);
	      memset (temp_cp, 0, 7);
	      snprintf (temp_cp, sizeof (temp_cp) - 1, "Ux%04X", case_buf[i]);
	      strcat (out_cp, temp_cp);
	    }
	  printf (" | Upper :  CP(s): %-12s, upper char(s): %-12s",
		  out_cp, (print_case ? out_case : ""));
	}
      printf ("\n");
    }
  return NO_ERROR;
}

/*
 * locale_dump - import the binary file(s) associated with the 
 *		 selected locale(s) and output the selected information
 *		 in human-readable text format.
 * Returns : error status.
 * ld(in)  : the LOCALE_DATA to be dumped in text format.
 * lf(in)  : the LOCALE_FILE i.e. binary file from which ld was imported.
 * dl_settings(in): the commmand line options encoded intoa binary masked int.
 * start_value(in): the starting codepoint for the range of items which 
 *		    -w or -c will dump.
 * end_value(in)  : the ending codepoint for the range of items which
 *		    -w or -c will dump.
 */
int
locale_dump (LOCALE_DATA * ld, LOCALE_FILE * lf, int dl_settings,
	     int start_value, int end_value)
{
  int i, cp;
  int lower_bound = 0;
  int upper_bound = 0;
  int alphabet_settings;
  int err_status = NO_ERROR;
  unsigned int *coll_key_list = NULL;
  int coll_key_list_cnt = 0;

  assert (ld != NULL);
  assert (lf != NULL);
  assert (start_value <= end_value);

  printf ("Locale data for: %s\nBinary file :%s\n",
	  lf->locale_name, lf->bin_file);
  printf ("*************************************************\n");
  printf ("Locale string: %s\n", ld->locale_name);

  if ((dl_settings & DUMPLOCALE_IS_CALENDAR) != 0)
    {
      printf ("\n");
      printf ("Date format: %s\n", ld->dateFormat);
      printf ("Time format: %s\n", ld->timeFormat);
      printf ("Datetime format: %s\n", ld->datetimeFormat);
      printf ("Timestamp format: %s\n", ld->timestampFormat);

      printf ("\nAbbreviated month names:\n");
      for (i = 0; i < CAL_MONTH_COUNT; i++)
	{
	  printf ("%d. %s\n", (i + 1), ld->month_names_abbreviated[i]);
	}
      printf ("\nAbbreviated month names, sorted for tokenizer:\n");
      for (i = 0; i < CAL_MONTH_COUNT; i++)
	{
	  printf ("%d. %s = month %d\n",
		  (i + 1),
		  ld->month_names_abbreviated[ld->
					      month_names_abbr_parse_order
					      [i]],
		  ld->month_names_abbr_parse_order[i] + 1);
	}

      printf ("\nWide month names:\n");
      for (i = 0; i < CAL_MONTH_COUNT; i++)
	{
	  printf ("%d. %s\n", (i + 1), ld->month_names_wide[i]);
	}
      printf ("\nWide month names, sorted for tokenizer:\n");
      for (i = 0; i < CAL_MONTH_COUNT; i++)
	{
	  printf ("%d. %s = month %d\n",
		  (i + 1),
		  ld->month_names_wide[ld->month_names_wide_parse_order[i]],
		  ld->month_names_wide_parse_order[i] + 1);
	}

      printf ("\nAbbreviated weekday names:\n");
      for (i = 0; i < CAL_DAY_COUNT; i++)
	{
	  printf ("%d. %s\n", (i + 1), ld->day_names_abbreviated[i]);
	}
      printf ("\nAbbreviated weekday names, sorted for parse order:\n");
      for (i = 0; i < CAL_DAY_COUNT; i++)
	{
	  printf ("%d. %s = weekday %d\n",
		  (i + 1),
		  ld->day_names_abbreviated[ld->
					    day_names_abbr_parse_order[i]],
		  ld->day_names_abbr_parse_order[i] + 1);
	}

      printf ("\nWide weekday names:\n");
      for (i = 0; i < CAL_DAY_COUNT; i++)
	{
	  printf ("%d. %s\n", (i + 1), ld->day_names_wide[i]);
	}
      printf ("\nWide weekday names, sorted for tokenizer:\n");
      for (i = 0; i < CAL_DAY_COUNT; i++)
	{
	  printf ("%d. %s = weekday %d\n",
		  (i + 1),
		  ld->day_names_wide[ld->day_names_wide_parse_order[i]],
		  ld->day_names_wide_parse_order[i] + 1);
	}

      printf ("\nDay periods:\n");
      for (i = 0; i < CAL_AM_PM_COUNT; i++)
	{
	  printf ("%d. %s\n", (i + 1), ld->am_pm[i]);
	}
      printf ("\nDay periods, sorted for tokenizer:\n");
      for (i = 0; i < CAL_AM_PM_COUNT; i++)
	{
	  printf ("%d. %s = day period %d\n",
		  (i + 1),
		  ld->am_pm[ld->am_pm_parse_order[i]],
		  ld->am_pm_parse_order[i] + 1);
	}
    }

  /* Display numbering information. */
  if ((dl_settings & DUMPLOCALE_IS_NUMBERING) != 0)
    {
      printf ("\n");
      printf ("Decimal separator: <%c>\n", ld->number_decimal_sym);
      printf ("Separator for digit grouping: <%c>\n", ld->number_group_sym);
      printf ("Default currency ISO code: %s",
	      intl_get_money_ISO_symbol (ld->default_currency_code));
    }

  if ((dl_settings & DUMPLOCALE_IS_ALPHABET) != 0)
    {
      lower_bound = 0;
      upper_bound = ld->alphabet.l_count;
      if (start_value > lower_bound)
	{
	  lower_bound = start_value;
	}
      if (end_value > 0 && end_value < upper_bound)
	{
	  /* if an upper bound is specified, use the "+1" to make sure that 
	   * the upper bound codepoint is included in the dump file. */
	  upper_bound = end_value + 1;
	}
    }

  if (upper_bound < lower_bound)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR (msgcat_message (MSGCAT_CATALOG_UTILS,
					MSGCAT_UTIL_SET_DUMPLOCALE,
					DUMPLOCALE_MSG_INVALID_CP_RANGE),
			ER_LOC_GEN, true);
      goto exit;
    }

  /* Dump alphabet data. */
  if ((dl_settings & DUMPLOCALE_IS_ALPHABET) != 0)
    {
      alphabet_settings = 0;
      alphabet_settings |= (dl_settings & DUMPLOCALE_IS_ALPHABET_LOWER);
      alphabet_settings |= (dl_settings & DUMPLOCALE_IS_ALPHABET_UPPER);
      printf ("\n");
      printf ("* Alphabet data *\n");
      dump_locale_alphabet (&(ld->alphabet), alphabet_settings, lower_bound,
			    upper_bound);
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
      dump_locale_alphabet (&(ld->identif_alphabet), alphabet_settings,
			    lower_bound, upper_bound);
    }


  if (((dl_settings & DUMPLOCALE_IS_COLLATION_CP_ORDER) != 0)
      || ((dl_settings & DUMPLOCALE_IS_COLLATION_WEIGHT_ORDER) != 0))
    {
      lower_bound = 0;
      upper_bound = ld->opt_coll.w_count;
      if (start_value > lower_bound)
	{
	  lower_bound = start_value;
	}
      if (end_value > 0 && end_value < upper_bound)
	{
	  /* if an upper bound is specified, use the "+1" to make sure that 
	   * the upper bound codepoint is included in the dump file. */
	  upper_bound = end_value + 1;
	}

      printf ("\n");
      printf ("* Collation data *\n");
      if (ld->opt_coll.w_count == 0)
	{
	  printf ("No collation defined\n");
	  goto exit;
	}
    }

  if (upper_bound < lower_bound)
    {
      err_status = ER_LOC_GEN;
      LOG_LOCALE_ERROR (msgcat_message (MSGCAT_CATALOG_UTILS,
					MSGCAT_UTIL_SET_DUMPLOCALE,
					DUMPLOCALE_MSG_INVALID_CP_RANGE),
			ER_LOC_GEN, true);
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

	  assert (cp >= 0 && cp < ld->opt_coll.w_count);

	  dump_collation_codepoint (ld, cp, true, true);

	  next_id = ld->opt_coll.next_cp[cp];
	  printf (" | Next : ");

	  dump_collation_key (ld, next_id, true, true);

	  printf ("\n");
	}
    }

  if (ld->opt_coll.count_contr > 0
      && (dl_settings & DUMPLOCALE_IS_COLLATION_CP_ORDER) != 0)
    {
      printf ("\n");
      printf ("* Contraction collation info (contraction order) *\n");
      for (i = 0; i < ld->opt_coll.count_contr; i++)
	{
	  COLL_CONTRACTION *contr = &(ld->opt_coll.contr_list[i]);

	  dump_collation_contr (ld, contr, true, true);
	  printf (" | Next :");

	  dump_collation_key (ld, contr->next, true, true);

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
      coll_key_list_cnt = cp_count + ld->opt_coll.count_contr;
      coll_key_list = (unsigned int *)
	malloc (coll_key_list_cnt * sizeof (unsigned int));
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

      for (i = 0; i < ld->opt_coll.count_contr; i++)
	{
	  coll_key_list[i + cp_count] = i | INTL_NEXT_MASK_CONTR;
	}

      /* Order by weight. */
      use_expansions = ld->opt_coll.uca_opt.sett_expansions;

      dump_coll_data = &(ld->opt_coll);
      if (use_expansions)
	{
	  qsort (coll_key_list, coll_key_list_cnt, sizeof (unsigned int),
		 comp_func_coll_uca_exp);
	}
      else
	{
	  qsort (coll_key_list, coll_key_list_cnt, sizeof (unsigned int),
		 comp_func_coll_uca_simple_weights);
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
		    (comp_func_coll_uca_exp (&(coll_key_list[i]),
					     &(coll_key_list[i - 1]))
		     == 0) ? true : false;
		}
	      else
		{
		  same_weights =
		    (comp_func_coll_uca_simple_weights (&(coll_key_list[i]),
							&(coll_key_list
							  [i - 1])) ==
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

	      dump_collation_key (ld, coll_key_list[i], true, false);
	      keys_same_weight = 1;
	    }
	  else
	    {
	      printf ("\n");
	      dump_collation_key (ld, coll_key_list[i], true, false);
	      keys_same_weight++;
	    }

	  printf (" | ");

	  dump_collation_key (ld, coll_key_list[i], false, true);
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
  char utf8_buf_1[INTL_UTF8_MAX_CHAR_SIZE + 1];
  char utf8_buf_2[INTL_UTF8_MAX_CHAR_SIZE + 1];
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
      str1 = utf8_buf_1;
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
      str2 = utf8_buf_2;
    }

  return intl_strcmp_utf8_uca_w_coll_data (coll, str1, size1, str2, size2);
}

/*
 * dump_collation_key - prints information on collation key
 *			(codepoint or contraction)
 * Returns :
 * ld(in) : locale data
 * key(in) : key to print
 * print_weight(in): true if weight should be printed
 * print_contr(in): true if contraction info should be printed
 */
static void
dump_collation_key (LOCALE_DATA * ld, const unsigned int key,
		    bool print_weight, bool print_key)
{
  if (INTL_IS_NEXT_CONTR (key))
    {
      unsigned int contr_id = INTL_GET_NEXT_CONTR_ID (key);
      COLL_CONTRACTION *contr = &(ld->opt_coll.contr_list[contr_id]);

      dump_collation_contr (ld, contr, print_weight, print_key);
    }
  else
    {
      dump_collation_codepoint (ld, key, print_weight, print_key);
    }
}

/*
 * dump_collation_contr - prints information on collation contraction
 * Returns :
 * ld(in) : locale data
 * contr(in) : contraction
 * print_weight(in): true if weight should be printed
 * print_contr(in): true if contraction info should be printed
 */
static void
dump_collation_contr (LOCALE_DATA * ld, const COLL_CONTRACTION * contr,
		      bool print_weight, bool print_contr)
{
  int i;

  assert (contr != NULL);

  assert (contr->cp_count <= LOC_MAX_UCA_CHARS_SEQ);

  if (print_contr)
    {
      unsigned int cp_list[LOC_MAX_UCA_CHARS_SEQ];
      bool print_utf8 = true;
      int cp_count;

      intl_utf8_to_cp_list ((const unsigned char *) contr->c_buf,
			    strlen (contr->c_buf), cp_list, sizeof (cp_list),
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

  if (ld->opt_coll.uca_exp_num <= 1)
    {
      printf (" | Weight : %04x", contr->wv);
      return;
    }

  assert (contr->uca_num > 0 && contr->uca_num <= MAX_UCA_EXP_CE);

  printf (" | Weight : ");
  for (i = 0; i < contr->uca_num; i++)
    {
      printf ("[%04X.", UCA_GET_L1_W (contr->uca_w_l13[i]));
      printf ("%04X.", UCA_GET_L2_W (contr->uca_w_l13[i]));
      printf ("%04X.", UCA_GET_L3_W (contr->uca_w_l13[i]));
      if (ld->opt_coll.uca_opt.sett_strength >= TAILOR_QUATERNARY)
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
 * ld(in)  : locale data
 * cp(in) : codepoint
 * print_weight(in): true if weight should be printed
 */
static void
dump_collation_codepoint (LOCALE_DATA * ld, const unsigned int cp,
			  bool print_weight, bool print_cp)
{
  assert (cp >= 0);

  if (print_cp)
    {
      unsigned char utf8_buf[INTL_UTF8_MAX_CHAR_SIZE + 1];

      memset (utf8_buf, 0, INTL_UTF8_MAX_CHAR_SIZE + 1);

      intl_cp_to_utf8 (cp, utf8_buf);

      printf ("CP: Ux%04X | %-4s", cp, ((cp > 0x20) ? utf8_buf : ""));
    }

  if (!print_weight)
    {
      return;
    }

  if (ld->opt_coll.uca_exp_num <= 1)
    {
      printf (" | Weight%s: %04x",
	      ((int) cp < ld->opt_coll.w_count) ? "" : "(Computed)",
	      (((int) cp < ld->opt_coll.w_count) ?
	       (ld->opt_coll.weights[cp]) : (cp + 1)));
      return;
    }

  assert (ld->opt_coll.uca_exp_num > 1);
  assert (ld->opt_coll.uca_exp_num <= MAX_UCA_EXP_CE);

  if ((int) cp < ld->opt_coll.w_count)
    {
      int i;
      UCA_L13_W *uca_w_l13 =
	&(ld->opt_coll.uca_w_l13[cp * ld->opt_coll.uca_exp_num]);

      printf (" | Weight : ");

      assert (ld->opt_coll.uca_num[cp] >= 1);
      assert (ld->opt_coll.uca_num[cp] <= MAX_UCA_EXP_CE);

      for (i = 0; i < ld->opt_coll.uca_num[cp]; i++)
	{
	  printf ("[%04X.", UCA_GET_L1_W (uca_w_l13[i]));
	  printf ("%04X.", UCA_GET_L2_W (uca_w_l13[i]));
	  printf ("%04X.", UCA_GET_L3_W (uca_w_l13[i]));
	  if (ld->opt_coll.uca_opt.sett_strength >= TAILOR_QUATERNARY)
	    {
	      printf ("%04X]",
		      ld->opt_coll.uca_w_l4[cp * ld->opt_coll.uca_exp_num +
					    i]);
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
