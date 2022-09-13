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
 * tz_compile.c : Functions for parsing and compiling IANA's timezone files
 */
#include "config.h"
#include <stdio.h>
#include <assert.h>

#include "tz_support.h"

#include "authenticate.h"
#include "porting.h"
#include "byte_order.h"
#include "utility.h"
#include "db_date.h"
#include "environment_variable.h"
#include "chartype.h"
#include "error_manager.h"

#include "memory_alloc.h"

#include "tz_compile.h"
#include "xml_parser.h"
#include "crypt_opfunc.h"
#include "db_query.h"
#include "dbtype.h"

#if defined (SA_MODE)
#include "db.h"
#endif /* SA_MODE */

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

#define TZ_FILENAME_MAX_LEN	    17
#define TZ_MAX_LINE_LEN		    512
#define TZ_OFFRULE_PREFIX_TAB_COUNT 3

#define TZ_COORDINATES_MAX_SIZE		  16
#define TZ_COMMENTS_MAX_SIZE		  92
#define TZ_RULE_LETTER_ABBREV_MAX_SIZE	  8
#define TZ_RULE_TYPE_MAX_SIZE		  4

#if defined(_WIN32) || defined(WINDOWS) || defined(WIN64)
#define PATH_PARTIAL_TIMEZONES_FILE "timezones\\tzlib\\timezones.c"
#else
#define PATH_PARTIAL_TIMEZONES_FILE "timezones/tzlib/timezones.c"
#endif

/*
 * Data structures
 */
typedef enum
{
  /* file types */
  TZF_COUNTRIES = 0,		/* tabbed country list (ISO3166) */
  TZF_ZONES,			/* tabbed time zones */
  TZF_RULES,			/* daylight saving rules */
  TZF_BACKWARD,			/* time zone aliases for backward compatibility */
#if defined(WINDOWS)
  TZF_LEAP,			/* leap data (leap seconds) */
  TZF_WINDOWS_IANA_ZONES_MAP
#else
  TZF_LEAP
#endif
} TZ_FILE_TYPE;

typedef struct tz_file_descriptor TZ_FILE_DESCRIPTOR;
struct tz_file_descriptor
{
  TZ_FILE_TYPE type;		/* type of tz file contents */
  char name[TZ_FILENAME_MAX_LEN];	/* file name */
};

/* The list of files from IANA's timezone database, in alphabetical order,
 * with attached description flags. This list is necessary for detecting file
 * addition or removal to/from future TZ releases by IANA. The reference for
 * building this list is IANA's tzdata2013b.tar.gz, released on 11 March 2013.
 * Visit http://www.iana.org/time-zones for the latest release.
 * NOTE: the array below is sorted by type. This order is used in
 *	 timezone_data_load(), so it must be preserved.
 */
static const TZ_FILE_DESCRIPTOR tz_Files[] = {
  {TZF_COUNTRIES, "iso3166.tab"},
  {TZF_ZONES, "zone.tab"},
  {TZF_RULES, "africa"},
  {TZF_RULES, "antarctica"},
  {TZF_RULES, "asia"},
  {TZF_RULES, "australasia"},
  {TZF_RULES, "europe"},
  {TZF_RULES, "northamerica"},
  {TZF_RULES, "southamerica"},
  {TZF_RULES, "etcetera"},
  {TZF_RULES, "pacificnew"},
  {TZF_BACKWARD, "backward"},
  {TZF_LEAP, "leapseconds"},
#if defined(WINDOWS)
  {TZF_LEAP, "leapseconds"},
  {TZF_WINDOWS_IANA_ZONES_MAP, "windowsZones.xml"}
#else
  {TZF_LEAP, "leapseconds"}
#endif
};

static int tz_File_count = DIM (tz_Files);

typedef struct tz_raw_country TZ_RAW_COUNTRY;
struct tz_raw_country
{
  int id;			/* this is not read, but assigned after reading all tz data */
  char code[TZ_COUNTRY_CODE_SIZE];
  char full_name[TZ_COUNTRY_NAME_SIZE];
  bool is_used;
};

typedef struct tz_raw_link TZ_RAW_LINK;
struct tz_raw_link
{
  char name[TZ_GENERIC_NAME_SIZE];
  char alias[TZ_GENERIC_NAME_SIZE];
  int zone_id;			/* this is not read, but assigned after reading all tz data */
};

typedef struct tz_raw_offset_rule TZ_RAW_OFFSET_RULE;
struct tz_raw_offset_rule
{
  int gmt_off;			/* time offset from UTC, in seconds */
  unsigned short until_year;
  unsigned char until_mon;	/* 0 - 11 */
  unsigned char until_day;	/* 0 - 27,30 */
  unsigned char until_hour;
  unsigned char until_min;
  unsigned char until_sec;
  char ds_ruleset_name[TZ_DS_RULESET_NAME_SIZE];
  char format[TZ_MAX_FORMAT_SIZE];
  TZ_TIME_TYPE until_time_type;	/* type for until: standard, wall, UTC */
  TZ_UNTIL_FLAG until_flag;	/* true if no ending time is specified; false otherwise */
};

typedef struct tz_raw_zone_info TZ_RAW_ZONE_INFO;
struct tz_raw_zone_info
{
  int offset_rule_count;
  TZ_RAW_OFFSET_RULE *offset_rules;
  int id;			/* this is not read, but assigned after reading all tz data */
  int country_id;		/* parent country ID */
  int alias_count;
  char **aliases;
  char clone_of[TZ_GENERIC_NAME_SIZE];	/* timezone from where to use rules and settings */
  int clone_of_id;
  char code[TZ_COUNTRY_CODE_SIZE];
  char coordinates[TZ_COORDINATES_MAX_SIZE];
  char full_name[TZ_GENERIC_NAME_SIZE];
  char comments[TZ_COMMENTS_MAX_SIZE];
};

/* TZ_DS_RULE is the representation of a daylight saving rule and tells
 * when and how the DS event occurs */
typedef struct tz_raw_ds_rule TZ_RAW_DS_RULE;
struct tz_raw_ds_rule
{
  short from_year;
  short to_year;		/* TZ_MAX_YEAR if column value is "max" e.g. up to now */
  char type[TZ_RULE_TYPE_MAX_SIZE];	/* always '-'; kept for possible future extensions */
  unsigned char in_month;	/* month when the daylight saving event occurs valid values : 0 - 11 */
  TZ_DS_CHANGE_ON change_on;	/* day of month, fixed or relative */
  int at_time;			/* time when DS event occurs */
  TZ_TIME_TYPE at_time_type;	/* type for at_time: local, absolute etc. */
  int save_time;		/* amount of time saved, in seconds */
  /* letter(s) to be used in the time zone string ID */
  char letter_abbrev[TZ_RULE_LETTER_ABBREV_MAX_SIZE];
};

typedef struct tz_raw_ds_ruleset TZ_RAW_DS_RULESET;
struct tz_raw_ds_ruleset
{
  int rule_count;
  TZ_RAW_DS_RULE *rules;
  char name[TZ_DS_RULESET_NAME_SIZE];
  bool is_used;
};

typedef struct tz_raw_context TZ_RAW_CONTEXT;
struct tz_raw_context
{
  int current_line;
  char current_file[PATH_MAX];
};

/*
 * TZ_RAW_DATA is a structure for holding the information read from the files
 * found in IANA's timezone database. After fully loading and interpreting the
 * data, time zone information will be processed, optimized and moved to a new
 * structure (TZ_DATA) to be used at runtime. (incl. TZ shared library)
 */
typedef struct tz_raw_data TZ_RAW_DATA;
struct tz_raw_data
{
  int country_count;
  TZ_RAW_COUNTRY *countries;
  int zone_count;
  TZ_RAW_ZONE_INFO *zones;
  int ruleset_count;
  TZ_RAW_DS_RULESET *ds_rulesets;
  int link_count;
  TZ_RAW_LINK *links;
  int leap_sec_count;
  TZ_LEAP_SEC *leap_sec;
  TZ_RAW_CONTEXT context;
};

typedef struct offset_rule_interval OFFSET_RULE_INTERVAL;
struct offset_rule_interval
{
  int original_offset_rule_start;
  int len;
  int final_offset_rule_start;
};

typedef struct query_buf QUERY_BUF;
struct query_buf
{
  char *buf;
  char *last;
  int size;
  int len;
};

#define TZ_CAL_ABBREV_SIZE 4
static const char MONTH_NAMES_ABBREV[TZ_MON_COUNT][TZ_CAL_ABBREV_SIZE] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
static const char DAY_NAMES_ABBREV[TZ_WEEK_DAY_COUNT][TZ_CAL_ABBREV_SIZE] =
  { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

#define STR_SKIP_LEADING_SPACES(str)	      \
  while ((str) != NULL && *(str) == ' ')      \
    {					      \
      (str)++;				      \
    }

/*
 * Time zone errors. For a more flexible error reporting feature, CUBRID's
 * error codes are not used inside the timezone feature. Instead, internal
 * custom errors are used, and are handled only in one place (when exiting)
 */
#ifndef NO_ERROR
#define NO_ERROR		    0
#endif

#define TZC_ERR_GENERIC			-1
#define TZC_ERR_INVALID_PACKAGE		-2
#define TZC_ERR_BAD_TZ_LINK		-3
#define TZC_ERR_OUT_OF_MEMORY		-4
#define TZC_ERR_INVALID_VALUE		-5
#define TZC_ERR_INVALID_TIME		-6
#define TZC_ERR_ZONE_RULE_UNORDERED	-7
#define TZC_ERR_INVALID_DATETIME	-8
#define TZC_ERR_INVALID_DS_RULE		-9
#define TZC_ERR_CANT_READ_VALUE		-10
#define TZC_ERR_PARSING_FAILED		-11
#define TZC_ERR_DS_INVALID_DATE		-12
#define TZC_ERR_FILE_NOT_ACCESSIBLE	-13
#define TZC_ERR_INVALID_COUNTRY		-14
#define TZC_ERR_INVALID_ZONE		-15
#define TZC_ERR_ADD_ZONE		-16
#define TZC_ERR_LINKING_TRUE_ZONES	-17
#define TZC_ERR_LAST_ERROR		-18

static const char *tzc_Err_messages[] = {
  /* NO_ERROR */
  "",
  /* TZC_ERR_GENERIC */
  "Error encountered when %s %s!",
  /* TZC_ERR_INVALID_PACKAGE */
  "Invalid timezone package! File %s not found in folder %s.",
  /* TZC_ERR_BAD_TZ_LINK */
  "Invalid link definition (s1: %s, s2: %s). " "Format error or invalid data encountered.",
  /* TZC_ERR_OUT_OF_MEMORY */
  "Memory exhausted when allocating %d items of type '%s'.",
  /* TZC_ERR_INVALID_VALUE */
  "Invalid %s. Value %s is empty or invalid.",
  /* TZC_ERR_INVALID_TIME */
  "Invalid or empty time value %s found in %s.",
  /* TZC_ERR_ZONE_RULE_UNORDERED */
  "Timezone offset rules are not fully sorted. Rule %s is out of order. %s",
  /* TZC_ERR_INVALID_DATETIME */
  "Invalid datetime value %s found in %s.",
  /* TZC_ERR_INVALID_DS_RULE */
  "Invalid daylight saving rule found: %s %s",
  /* TZC_ERR_CANT_READ_VALUE */
  "Unable to read %s value. Context: %s.",
  /* TZC_ERR_PARSING_FAILED */
  "Error encountered when parsing %. Context: %s.",
  /* TZC_ERR_DS_INVALID_DATE */
  "Invalid %d. The resulting date is not valid in the given context (%s).",
  /* TZC_ERR_FILE_NOT_ACCESSIBLE */
  "The file at %s is missing or not accessible for %s.",
  /* TZC_ERR_INVALID_COUNTRY */
  "Invalid line for country definition:%s. %s",
  /* TZC_ERR_INVALID_ZONE */
  "Invalid line for zone definition. Line: %s. Values: %s",
  /* TZC_ERR_ADD_ZONE */
  "Error encountered when adding zone %s %s",
  /* TZC_ERR_LINKING_TRUE_ZONES */
  "Error! Found a link between %s and %s, which are fully defined timezones."
};

static const int tzc_Err_message_count = -TZC_ERR_LAST_ERROR;

extern const char *tz_timezone_names[];
extern const TZ_COUNTRY tz_countries[];

#define LOG_TZC_SET_CURRENT_CONTEXT(tzd_raw, f, l)  \
  do {						    \
    strcpy (tzd_raw->context.current_file, f);	    \
    tzd_raw->context.current_line = l;		    \
  } while (0)

#define TZC_ERR_MSG_MAX_SIZE   512

#define TZC_CONTEXT(tzd_raw) (&((tzd_raw)->context))

#define TZC_LOG_ERROR_1ARG(context, err_code, s1) \
  tzc_log_error ((context), (err_code), (s1), "")

#define TZC_LOG_ERROR_2ARG(context, err_code, s1, s2) \
  tzc_log_error ((context), (err_code), (s1), (s2))

#define DUPLICATE_STR(a, b) \
  do { \
    (a) = strdup((b)); \
    if((a) == NULL) { \
    err_status = ER_OUT_OF_VIRTUAL_MEMORY; \
    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_status, 1, (size_t) 0); \
    goto exit; \
  } \
} while (0)

#define INIT_COUNTRY(dst, src) \
  do { \
    strcpy ((dst)->code, (src)->code); \
    strcpy ((dst)->full_name, (src)->full_name); \
  } while (0)

#define PRINT_STRING_TO_C_FILE(fp, val, len)                          \
  do {                                                                \
    int istr;                                                         \
    fprintf (fp, "\"");                                               \
    for (istr = 0; istr < len; istr++)                                \
      {                                                               \
	fprintf (fp, "\\x%02X", (unsigned char) val[istr]);           \
      }                                                               \
    fprintf (fp, "\"");						      \
  } while (0)

#define PRINT_STRING_VAR_TO_C_FILE(fp, valname, val)			  \
  do {                                                                    \
      fprintf (fp, "\n" SHLIB_EXPORT_PREFIX "const char " valname "[] = ");   \
      PRINT_STRING_TO_C_FILE (fp, val, strlen (val));			  \
      fprintf (fp, ";\n");                                                \
  } while (0);

#define BUF_PUT_INT32(buf,v)				       \
  do {							       \
      unsigned int nv = htonl(v);			       \
      *((unsigned char *) (buf)) = ((unsigned char *) &nv)[3]; \
      buf = (char *) (buf) + 1;				       \
      *((unsigned char *) (buf)) = ((unsigned char *) &nv)[2]; \
      buf = (char *) (buf) + 1;				       \
      *((unsigned char *) (buf)) = ((unsigned char *) &nv)[1]; \
      buf = (char *) (buf) + 1;				       \
      *((unsigned char *) (buf)) = ((unsigned char *) &nv)[0]; \
      buf = (char *) (buf) + 1;				       \
  } while (0)

#define BUF_PUT_INT16(buf,v)				      \
  do {							      \
      unsigned short nv = htons(v);			      \
      *((unsigned char *) (buf)) = ((unsigned char *) &nv)[1];\
      buf = (char *) (buf) + 1;				      \
      *((unsigned char *) (buf)) = ((unsigned char *) &nv)[0];\
      buf = (char *) (buf) + 1;				      \
  } while (0)

static int tzc_check_new_package_validity (const char *input_folder);
static int tzc_load_countries (TZ_RAW_DATA * tzd_raw, const char *input_folder);
static int tzc_load_zone_names (TZ_RAW_DATA * tzd_raw, const char *input_folder);
static int tzc_load_rule_file (TZ_RAW_DATA * tzd_raw, const int file_index, const char *input_folder);
static int tzc_load_backward_zones (TZ_RAW_DATA * tzd_raw, const char *input_folder);
static int tzc_load_leap_secs (TZ_RAW_DATA * tzd_raw, const char *input_folder);
static int tzc_get_zone (const TZ_RAW_DATA * tzd_raw, const char *zone_name, TZ_RAW_ZONE_INFO ** zone);
static int tzc_add_zone (const char *zone, const char *coord, const char *code, const char *comments,
			 TZ_RAW_DATA * tzd_raw, TZ_RAW_ZONE_INFO ** new_zone);
static int tzc_add_link (TZ_RAW_DATA * tzd_raw, const char *zone, const char *alias);
static int tzc_add_offset_rule (TZ_RAW_ZONE_INFO * zone, char *rule_text);
static int tzc_add_leap_sec (TZ_RAW_DATA * tzd_raw, int year, int month, int day, unsigned char hour, unsigned char min,
			     unsigned char sec, bool corr_minus, bool leap_is_rolling);
static int tzc_read_time_type (const char *str, const char **next, TZ_TIME_TYPE * time_type);
static int tzc_add_ds_rule (TZ_RAW_DATA * tzd_raw, char *rule_text);
static int tzc_parse_ds_change_on (TZ_RAW_DS_RULE * dest, const char *str);
static bool tzc_is_valid_date (const int day, const int month, const int year_start, const int year_end);
static int tzc_get_ds_ruleset_by_name (const TZ_DS_RULESET * ds_rulesets, int ds_ruleset_count, const char *ruleset);

static void tzc_free_raw_data (TZ_RAW_DATA * tzd_raw);

static int tzc_check_links_raw_data (TZ_RAW_DATA * tzd_raw);
static void tzc_sort_raw_data (TZ_RAW_DATA * tzd_raw);
static void tzc_index_raw_data (TZ_RAW_DATA * tzd_raw);
static int tzc_index_raw_data_w_static (TZ_RAW_DATA * tzd_raw, const TZ_GEN_TYPE mode);
static int tzc_index_raw_subdata (TZ_RAW_DATA * tzd_raw, const TZ_GEN_TYPE mode);
static int compare_ints (const void *a, const void *b);

static int tzc_compile_data (TZ_RAW_DATA * tzd_raw, TZ_DATA * tzd);
static int tzc_compile_ds_rules (TZ_RAW_DATA * tzd_raw, TZ_DATA * tzd);

static int str_to_offset_rule_until (TZ_RAW_OFFSET_RULE * offset_rule, char *str);
static int str_month_to_int (const char *month, int *month_num, const char **str_next);
static int str_day_to_int (const char *str_in, int *day_num, const char **str_next);
static int str_read_day_var (const char *str, const int month, int *type, int *day, int *bound, const char **str_next);

static int comp_func_raw_countries (const void *arg1, const void *arg2);
static int comp_func_raw_zones (const void *arg1, const void *arg2);
static int comp_func_raw_links (const void *arg1, const void *arg2);
static int comp_func_raw_offset_rules (const void *arg1, const void *arg2);
static int comp_func_raw_ds_rulesets (const void *arg1, const void *arg2);
static int comp_func_raw_ds_rules (const void *arg1, const void *arg2);
static int comp_func_tz_names (const void *arg1, const void *arg2);

static void print_seconds_as_time_hms_var (int seconds);

static void tzc_get_timezones_dot_c_filepath (size_t size, char *timezones_dot_c_file_path);
static int tzc_export_timezone_dot_c (const TZ_DATA * tzd, const char *tz_C_filepath);

static int tzc_load_raw_data (TZ_RAW_DATA * tzd_raw, const char *input_folder);
static int tzc_import_old_data (TZ_RAW_DATA * tzd_raw, const TZ_GEN_TYPE mode);
static int tzc_del_unused_raw_data (TZ_RAW_DATA * tzd_raw);
static int tzc_index_data (TZ_RAW_DATA * tzd_raw, const TZ_GEN_TYPE mode);
static void tzc_free_tz_data (TZ_DATA * tzd, bool full);

static void tzc_build_filepath (char *path, size_t size, const char *dir, const char *filename);
static void trim_comments_whitespaces (char *str);

static int tzc_get_timezone_aliases (const TZ_DATA * tzd, const int zone_id, int **aliases, int *alias_count);
static void tzc_dump_one_offset_rule (const TZ_DATA * tzd, const TZ_OFFSET_RULE * offset_rule);
static void tzc_dump_ds_ruleset (const TZ_DATA * tzd, const int ruleset_id);

static void tzc_log_error (const TZ_RAW_CONTEXT * context, const int code, const char *msg1, const char *msg2);

static void tzc_summary (TZ_RAW_DATA * tzd_raw, TZ_DATA * tzd);

static int tzc_find_timezone_names (const TZ_DATA * tzd, const char *timezone_name);
static int tzc_find_country_names (const TZ_COUNTRY * countries, const int country_count, const char *country_name);
static bool comp_ds_rules (const TZ_DS_RULE * rule1, const TZ_DS_RULE * rule2);
static bool comp_offset_rules (const TZ_OFFSET_RULE * rule1, const TZ_OFFSET_RULE * rule2);
static int copy_offset_rule (TZ_OFFSET_RULE * dst, const TZ_DATA * tzd, const int index);
static int init_ds_ruleset (TZ_DS_RULESET * dst_ruleset, const TZ_DATA * tzd, const int index, const int start);
static int copy_ds_rule (TZ_DS_RULE * dst, const TZ_DATA * tzd, const int index);
static int tz_data_partial_clone (char **timezone_names, TZ_TIMEZONE * timezones, TZ_NAME * names, const TZ_DATA * tzd);
static int init_tz_name (TZ_NAME * dst, TZ_NAME * src);
#if defined(SA_MODE)
static int tzc_extend (TZ_DATA * tzd);
static int tzc_update (TZ_DATA * tzd, const char *database_name);
static int tzc_update_internal (const char *database_name);
static QUERY_BUF *tz_write_query_string (QUERY_BUF * query, const char *format, ...);
#endif
static int tzc_compute_timezone_checksum (TZ_DATA * tzd, TZ_GEN_TYPE type);
static int get_day_of_week_for_raw_rule (const TZ_RAW_DS_RULE * rule, const int year);
#if defined (SA_MODE)
static int execute_query (const char *str, DB_QUERY_RESULT ** result);
#endif /* defined (SA_MODE) */

#if defined(WINDOWS)
static int comp_func_tz_windows_zones (const void *arg1, const void *arg2);
static int xml_start_mapZone (void *data, const char **attr);

static int tzc_load_windows_iana_map (TZ_DATA * tz_data, const char *input_folder);

XML_ELEMENT_DEF windows_zones_elem_supplementalData = { "supplementalData", 1, NULL,
  NULL, NULL
};

XML_ELEMENT_DEF windows_zones_elem_windowsZones = { "supplementalData windowsZones", 2, NULL,
  NULL, NULL
};

XML_ELEMENT_DEF windows_zones_elem_mapTimezones = { "supplementalData windowsZones mapTimezones", 3, NULL,
  NULL, NULL
};

XML_ELEMENT_DEF windows_zones_elem_mapZone = { "supplementalData windowsZones mapTimezones mapZone", 4,
  (ELEM_START_FUNC) & xml_start_mapZone,
  NULL, NULL
};

XML_ELEMENT_DEF *windows_zones_elements[] = {
  &windows_zones_elem_supplementalData,
  &windows_zones_elem_windowsZones,
  &windows_zones_elem_mapTimezones,
  &windows_zones_elem_mapZone
};
#endif

/*
 * tz_build_filepath () - concat a folder path and a file name to obtain a
 *			  full file path
 * Returns:
 * path(in/out): preallocated string where to store the full file path
 * size (in): size in bytes of th preallocated string
 * dir(in): folder part of the output full path
 * filename(in): file name part of the output full file path
 */
static void
tzc_build_filepath (char *path, size_t size, const char *dir, const char *filename)
{
  assert (path != NULL && size > 0);
  assert (dir != NULL);
  assert (filename != NULL);

#if !defined(WINDOWS)
  if (snprintf (path, size - 1, "%s/%s", dir, filename) < 0)
    {
      assert_release (false);
    }
#else
  if (snprintf (path, size - 1, "%s\\%s", dir, filename) < 0)
    {
      assert_release (false);
    }
#endif
}

/*
 * trim_comments_whitespaces() - remove whitespaces and comments found at the
 *			      end of a string. In this context, the
 *			      whitespaces to be removed are spaces, tabs and
 *			      the 0x0a character. Comments are identified as a
 *			      substring starting with '#' and stretching until
 *			      the end of the string/line.
 * Returns:
 * str(in/out): string from where to remove the whitespaces described above.
 *
 */
static void
trim_comments_whitespaces (char *str)
{
  int i, str_len = 0;
  char *sharp = NULL;

  if (IS_EMPTY_STR (str))
    {
      return;
    }

  sharp = strchr (str, '#');
  if (sharp != NULL)
    {
      *sharp = 0;
    }

  str_len = strlen (str);
  for (i = str_len - 1; i >= 0 && char_isspace (str[i]); i--)
    {
      str[i] = 0;
    }
}

/*
 * tzc_check_new_package_validity() - match the above list of files to the
 *				      list of files found in the input folder
 * Returns: 0(NO_ERROR) if the file lists are the same, TZ_ERR_INVALID_PACKAGE
 *	    if the input folder is missing any of the files marked above as
 *	    TZ_RULES, TZ_ZONES, TZ_COUNTRIES, TZ_BACKWARD or TZ_LEAP_DATA
 * input_folder(in): path to the input folder
 */
static int
tzc_check_new_package_validity (const char *input_folder)
{
  int err_status = NO_ERROR;
  FILE *fp;
  int i;
  char temp_path[PATH_MAX];

  for (i = 0; i < tz_File_count && err_status == NO_ERROR; i++)
    {
      tzc_build_filepath (temp_path, sizeof (temp_path), input_folder, tz_Files[i].name);

      fp = fopen_ex (temp_path, "rb");
      if (fp == NULL)
	{
	  err_status = TZC_ERR_INVALID_PACKAGE;
	  TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_INVALID_PACKAGE, tz_Files[i].name, input_folder);
	  goto exit;
	}
      else
	{
	  fclose (fp);
	  fp = NULL;
	}
    }
exit:
  return err_status;
}

#if defined (SA_MODE)
/*
 * timezone_compile_data() - loads data from all relevant tz files into
 *			     temporary structures, reorganizes & optimizes data
 *			     and generates TZ specific files
 * Returns: NO_ERROR if successful, internal error code otherwise
 * input_folder(in): path to the input folder containing timezone data
 * tz_gen_type(in): control flag (type of TZ build/gen to perform)
 * database_name(in): database name for which to do data migration if an update is necessary, if it is NULL
 *                    then data migration will be done for all the databases
 * timezones_dot_c_filepath(in): path for timezones.c output file. if NULL, it will be saved in default path (in CUBRID install
 *                       folder).
 * checksum(out): new checksum to write in the database when extend option is used
 */
int
timezone_compile_data (const char *input_folder, const TZ_GEN_TYPE tz_gen_type, char *database_name,
		       const char *timezones_dot_c_filepath, char *checksum)
{
  int err_status = NO_ERROR;
  TZ_RAW_DATA tzd_raw;
  TZ_DATA tzd;
  bool write_checksum = false;
  char default_output_file_path[PATH_MAX] = { 0 };

  memset (&tzd, 0, sizeof (tzd));
  memset (&tzd_raw, 0, sizeof (tzd_raw));

  err_status = tzc_check_new_package_validity (input_folder);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

  /* load raw data */
  err_status = tzc_load_raw_data (&tzd_raw, input_folder);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

  /* load old data */
  err_status = tzc_import_old_data (&tzd_raw, tz_gen_type);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

  err_status = tzc_del_unused_raw_data (&tzd_raw);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

  tzc_sort_raw_data (&tzd_raw);

  err_status = tzc_index_data (&tzd_raw, tz_gen_type);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

  err_status = tzc_compile_data (&tzd_raw, &tzd);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

#if defined(WINDOWS)
  /* load windows_iana_map */
  err_status = tzc_load_windows_iana_map (&tzd, input_folder);
  if (err_status != NO_ERROR)
    {
      /* failed to load file */
      goto exit;
    }
#endif

  if (tz_gen_type == TZ_GEN_TYPE_EXTEND)
    {
      write_checksum = true;
      err_status = tzc_extend (&tzd);
      if (err_status != NO_ERROR)
	{
	  /* In this case a data migration is needed because the data could not
	   * be made backward compatible
	   */
	  if (err_status == ER_TZ_COMPILE_ERROR)
	    {
	      err_status = tzc_update (&tzd, database_name);
	      if (err_status != NO_ERROR)
		{
		  goto exit;
		}
	    }
	  else
	    {
	      goto exit;
	    }
	}
    }

  err_status = tzc_compute_timezone_checksum (&tzd, tz_gen_type);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

  if (timezones_dot_c_filepath == NULL)
    {
      tzc_get_timezones_dot_c_filepath (sizeof (default_output_file_path), default_output_file_path);
      timezones_dot_c_filepath = default_output_file_path;
    }
  err_status = tzc_export_timezone_dot_c (&tzd, timezones_dot_c_filepath);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

  if (write_checksum == true)
    {
      strcpy (checksum, tzd.checksum);
    }

  tzc_summary (&tzd_raw, &tzd);

exit:
  tzc_free_raw_data (&tzd_raw);
  tzc_free_tz_data (&tzd, true);

  return err_status;
}
#endif

/*
 * tzc_free_tz_data () - frees members of a TZ_DATA* structure
 * Returns:
 * tzd(in/out): timezone data to free
 */
static void
tzc_free_tz_data (TZ_DATA * tzd, bool full)
{
  int i;

  if (tzd->countries != NULL)
    {
      free (tzd->countries);
    }
  if (tzd->timezones != NULL)
    {
      free (tzd->timezones);
    }
  if (tzd->timezone_names != NULL)
    {
      for (i = 0; i < tzd->timezone_count; i++)
	{
	  if (tzd->timezone_names[i] != NULL)
	    {
	      free (tzd->timezone_names[i]);
	    }
	}
      free (tzd->timezone_names);
    }
  if (tzd->offset_rules != NULL)
    {
      for (i = 0; i < tzd->offset_rule_count; i++)
	{
	  if (tzd->offset_rules[i].std_format != NULL)
	    {
	      free ((void *) (tzd->offset_rules[i].std_format));
	    }
	  if (tzd->offset_rules[i].save_format != NULL)
	    {
	      free ((void *) (tzd->offset_rules[i].save_format));
	    }
	  if (tzd->offset_rules[i].var_format != NULL)
	    {
	      free ((void *) (tzd->offset_rules[i].var_format));
	    }
	}
      free (tzd->offset_rules);
    }
  if (tzd->names != NULL)
    {
      for (i = 0; i < tzd->name_count; i++)
	{
	  if (tzd->names[i].name != NULL)
	    {
	      free ((void *) (tzd->names[i].name));
	    }
	}
      free (tzd->names);
    }

  if (tzd->ds_rulesets != NULL)
    {
      for (i = 0; i < tzd->ds_ruleset_count; i++)
	{
	  if (tzd->ds_rulesets[i].ruleset_name != NULL)
	    {
	      free ((void *) (tzd->ds_rulesets[i].ruleset_name));
	    }
	  if (tzd->ds_rulesets[i].default_abrev != NULL)
	    {
	      free ((void *) (tzd->ds_rulesets[i].default_abrev));
	    }
	}
      free (tzd->ds_rulesets);
    }
  if (tzd->ds_rules != NULL)
    {
      for (i = 0; i < tzd->ds_rule_count; i++)
	{
	  if (tzd->ds_rules[i].letter_abbrev != NULL)
	    {
	      free ((void *) (tzd->ds_rules[i].letter_abbrev));
	    }
	}
      free (tzd->ds_rules);
    }
  if (full == true)
    {
      if (tzd->ds_leap_sec != NULL)
	{
	  free (tzd->ds_leap_sec);
	}
#if defined(WINDOWS)
      if (tzd->windows_iana_map != NULL)
	{
	  free (tzd->windows_iana_map);
	}
#endif
    }

  memset (&tzd, 0, sizeof (tzd));
}

/*
 * tzc_load_raw_data() - loads data from all relevant tz files into
 *			 temporary structures
 * Returns: NO_ERROR if successful, internal error code otherwise
 * tzd_raw(out): loaded timezone data
 * input_folder(in): path to the input folder containing timezone data
 */
static int
tzc_load_raw_data (TZ_RAW_DATA * tzd_raw, const char *input_folder)
{
  int err_status = NO_ERROR;
  int i;

  /* load countries */
  err_status = tzc_load_countries (tzd_raw, input_folder);
  if (err_status != NO_ERROR)
    {
      /* failed to load country file */
      goto exit;
    }

  /* load zones */
  err_status = tzc_load_zone_names (tzd_raw, input_folder);
  if (err_status != NO_ERROR)
    {
      /* failed to load zone file */
      goto exit;
    }
  /* load zones (from the file names "backward" ) */
  err_status = tzc_load_backward_zones (tzd_raw, input_folder);
  if (err_status != NO_ERROR)
    {
      /* failed to load file */
      goto exit;
    }

  /* load daylight saving rules, zone aliases and zone offset information */
  for (i = 0; i < tz_File_count; i++)
    {
      if (tz_Files[i].type != TZF_RULES)
	{
	  continue;
	}
      err_status = tzc_load_rule_file (tzd_raw, i, input_folder);
      if (err_status != NO_ERROR)
	{
	  goto exit;
	}
    }

  /* load leap seconds */
  err_status = tzc_load_leap_secs (tzd_raw, input_folder);
  if (err_status != NO_ERROR)
    {
      /* failed to load file */
      goto exit;
    }

  LOG_TZC_SET_CURRENT_CONTEXT (tzd_raw, "", -1);

  err_status = tzc_check_links_raw_data (tzd_raw);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

exit:
  return err_status;
}

/*
 * tzc_import_old_data () - this function reads the existing timezone data and
 *			decides which data should be kept. The data to be kept
 *			consists of certain IDs, timezones and their
 *			associated offset rules and daylight saving rules.
 *			Choosing the data to keep is done based on the input
 *			parameter 'mode'.
 * Returns: 0 (NO_ERROR) if success, error code otherwise
 * tzd_raw(in/out) :raw timezone structure where to partially load the data to
 * mode(in): loading mode e.g. new, update or extend
 */
static int
tzc_import_old_data (TZ_RAW_DATA * tzd_raw, const TZ_GEN_TYPE mode)
{
  int err_status = NO_ERROR;

  if (mode == TZ_GEN_TYPE_NEW)
    {
      /* nothing to do */
      return NO_ERROR;
    }

  return err_status;
}

/*
 * tzc_del_unused_raw_data () - cleans up the loaded raw timezone data and
 *			    removes any unused information, such as daylight
 *			    saving rules and rulesets
 * Returns: 0 (NO_ERROR) if success, error code otherwise
 * tzd_raw(out): raw timezone data structure to cleanup
 */
static int
tzc_del_unused_raw_data (TZ_RAW_DATA * tzd_raw)
{
  int err_status = NO_ERROR;
  int i, j, k;
  bool found = false;
  TZ_RAW_DS_RULESET *ruleset = NULL;
  TZ_RAW_ZONE_INFO *zone = NULL;
  TZ_RAW_OFFSET_RULE *offset_rule = NULL;

  /* mark unused rulesets */
  for (i = 0; i < tzd_raw->ruleset_count; i++)
    {
      ruleset = &(tzd_raw->ds_rulesets[i]);
      found = false;
      for (j = 0; j < tzd_raw->zone_count && !found; j++)
	{
	  zone = &(tzd_raw->zones[j]);
	  for (k = 0; k < zone->offset_rule_count && !found; k++)
	    {
	      offset_rule = &(zone->offset_rules[k]);
	      if (strcmp (offset_rule->ds_ruleset_name, ruleset->name) == 0)
		{
		  ruleset->is_used = true;
		  found = true;
		}
	    }
	}
    }

  /* remove unused rulesets */
  i = 0;
  while (i < tzd_raw->ruleset_count)
    {
      if (tzd_raw->ds_rulesets[i].is_used == false)
	{
	  if (i < tzd_raw->ruleset_count - 1)
	    {
	      free (tzd_raw->ds_rulesets[i].rules);
	      memcpy (&(tzd_raw->ds_rulesets[i]), &(tzd_raw->ds_rulesets[tzd_raw->ruleset_count - 1]),
		      sizeof (TZ_RAW_DS_RULESET));
	    }
	  /* decrease count; tzd_raw->rulesets will be realloc'ed below */
	  tzd_raw->ruleset_count--;
	}
      else
	{
	  i++;
	}
    }

  assert (tzd_raw->ruleset_count > 0);

  /* realloc rulesets, in case some rulesets were removed above */
  ruleset = (TZ_RAW_DS_RULESET *) realloc (tzd_raw->ds_rulesets, tzd_raw->ruleset_count * sizeof (TZ_RAW_DS_RULESET));
  if (ruleset == NULL)
    {
      char err_msg[TZC_ERR_MSG_MAX_SIZE];

      sprintf (err_msg, "%d", tzd_raw->ruleset_count);
      err_status = TZC_ERR_OUT_OF_MEMORY;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_OUT_OF_MEMORY, err_msg, "TZ_RAW_DS_RULESET");
      goto exit;
    }
  tzd_raw->ds_rulesets = ruleset;

exit:
  return err_status;
}

/*
 * tzc_index_data () - build IDs for all time zone data
 * Returns: 0(NO_ERROR) if success, error code otherwise
 * tzd_raw (in/out): working timezone data structure
 * mode(in): control flag
 */
static int
tzc_index_data (TZ_RAW_DATA * tzd_raw, const TZ_GEN_TYPE mode)
{
  int err_status = NO_ERROR;
  char err_msg[TZC_ERR_MSG_MAX_SIZE];

  if (mode == TZ_GEN_TYPE_NEW || mode == TZ_GEN_TYPE_EXTEND)
    {
      tzc_index_raw_data (tzd_raw);
    }
  else
    {
      snprintf (err_msg, sizeof (err_msg) - 1, "UPDATE OPTION NOT IMPLEMENTED!");
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TZ_COMPILE_ERROR, 1, err_msg);
      err_status = ER_TZ_COMPILE_ERROR;
      goto exit;
    }

  err_status = tzc_index_raw_subdata (tzd_raw, mode);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

exit:
  return err_status;
}

/*
 * tzc_load_countries() - loads the list of countries from the files marked
 *			  as TZ_COUNTRIES type (e.g. iso3166.tab)
 * Returns: NO_ERROR(0) if success, error code or -1 otherwise
 * tzd_raw(out): timezone data structure to hold the loaded information
 * input_folder(in): folder containing IANA's timezone database
 */
static int
tzc_load_countries (TZ_RAW_DATA * tzd_raw, const char *input_folder)
{
  int err_status = NO_ERROR;
  int i, file_index = -1;
  char country_filepath[PATH_MAX] = { 0 };
  char str[256];
  char *str_country_name;
  FILE *fp = NULL;
  TZ_RAW_COUNTRY *temp_tz_country = NULL;

  for (i = 0; i < tz_File_count; i++)
    {
      if (tz_Files[i].type == TZF_COUNTRIES)
	{
	  /* Only one file containing country data is allowed */
	  assert (file_index == -1);
	  file_index = i;
	}
    }
  /* the list of files is hardcoded above, so if a file with TZ_COUNTRY flag is not found in tz_Files, code fixes are
   * needed */
  assert (file_index != -1);

  tzc_build_filepath (country_filepath, sizeof (country_filepath), input_folder, tz_Files[file_index].name);
  fp = fopen_ex (country_filepath, "rt");
  if (fp == NULL)
    {
      /* file not found or not accessible */
      err_status = TZC_ERR_FILE_NOT_ACCESSIBLE;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_FILE_NOT_ACCESSIBLE, country_filepath, "read");
      goto exit;
    }

  tzd_raw->country_count = 0;
  tzd_raw->countries = NULL;

  LOG_TZC_SET_CURRENT_CONTEXT (tzd_raw, tz_Files[file_index].name, 0);

  while (fgets (str, sizeof (str), fp))
    {
      tzd_raw->context.current_line++;

      trim_comments_whitespaces (str);

      if (IS_EMPTY_STR (str))
	{
	  continue;
	}

      str_country_name = strchr (str, '\t');
      if (str_country_name == NULL || strlen (str_country_name + 1) == 0
	  || str_country_name - str != TZ_COUNTRY_CODE_LEN)
	{
	  /* data formatting error on line <line_count> */
	  err_status = TZC_ERR_INVALID_COUNTRY;
	  TZC_LOG_ERROR_1ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_INVALID_COUNTRY, str);
	  goto exit;
	}

      temp_tz_country =
	(TZ_RAW_COUNTRY *) realloc (tzd_raw->countries, (tzd_raw->country_count + 1) * sizeof (TZ_RAW_COUNTRY));
      if (temp_tz_country == NULL)
	{
	  char err_msg[TZC_ERR_MSG_MAX_SIZE];

	  sprintf (err_msg, "%d", tzd_raw->country_count + 1);
	  err_status = TZC_ERR_OUT_OF_MEMORY;
	  TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_OUT_OF_MEMORY, err_msg, "TZ_RAW_COUNTRY");
	  goto exit;
	}
      tzd_raw->countries = temp_tz_country;

      /* grasp the newly added item */
      temp_tz_country = &(tzd_raw->countries[tzd_raw->country_count]);
      tzd_raw->country_count++;
      memset (temp_tz_country, 0, sizeof (temp_tz_country[0]));
      /* store parsed data */
      memcpy (temp_tz_country->code, str, TZ_COUNTRY_CODE_LEN);
      strncpy_bufsize (temp_tz_country->full_name, str_country_name + 1);
      temp_tz_country->id = -1;
    }

exit:
  if (fp != NULL)
    {
      fclose (fp);
    }

  return err_status;
}

/*
 * tzc_load_zones() - loads the list of countries from the files marked
 *		      as TZ_ZONES type (e.g. zone.tab)
 * Returns: 0 (NO_ERROR) if success, error code or -1 otherwise
 * tzd_raw(out): timezone data structure to hold the loaded information
 * input_folder(in): folder containing IANA's timezone database
 */
static int
tzc_load_zone_names (TZ_RAW_DATA * tzd_raw, const char *input_folder)
{
  int err_status = NO_ERROR;
  int i, file_index = -1;
  char zone_filepath[PATH_MAX] = { 0 };
  char str[256];
  FILE *fp = NULL;
  TZ_RAW_ZONE_INFO *temp_zone_info = NULL;
  char *col_code, *col_coord, *col_tz_name, *col_comments;

  col_code = col_coord = col_tz_name = col_comments = NULL;

  for (i = 0; i < tz_File_count; i++)
    {
      if (tz_Files[i].type == TZF_ZONES)
	{
	  /* Only one file containing zone data is allowed */
	  assert (file_index == -1);
	  file_index = i;
	}
    }
  /* the list of files is hardcoded above, so if a file with TZ_COUNTRY flag is not found in tz_Files, code fixes are
   * needed */
  assert (file_index != -1);

  tzc_build_filepath (zone_filepath, sizeof (zone_filepath), input_folder, tz_Files[file_index].name);
  fp = fopen_ex (zone_filepath, "rt");
  if (fp == NULL)
    {
      /* file not found or not accessible */
      err_status = TZC_ERR_FILE_NOT_ACCESSIBLE;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_FILE_NOT_ACCESSIBLE, zone_filepath, "read");
      goto exit;
    }

  tzd_raw->zone_count = 0;
  tzd_raw->zones = NULL;

  LOG_TZC_SET_CURRENT_CONTEXT (tzd_raw, tz_Files[file_index].name, 0);

  while (fgets (str, sizeof (str), fp))
    {
      tzd_raw->context.current_line++;

      trim_comments_whitespaces (str);


      if (IS_EMPTY_STR (str))
	{
	  continue;
	}

      col_code = strtok (str, "\t");
      col_coord = strtok (NULL, "\t");
      col_tz_name = strtok (NULL, "\t");
      col_comments = strtok (NULL, "\t");

      assert (col_code != NULL && col_coord != NULL && col_tz_name != NULL);

      if (col_code == NULL || strlen (col_code) != 2 || IS_EMPTY_STR (col_coord) || IS_EMPTY_STR (col_tz_name))
	{
	  /* data formatting error on line <line_count> */
	  char temp_msg[TZC_ERR_MSG_MAX_SIZE] = { 0 };

	  sprintf (temp_msg, "code: %s, coordinates: %s, name: %s, comments: %s", col_code == NULL ? "null" : col_code,
		   col_coord == NULL ? "null" : col_coord, col_tz_name == NULL ? "null" : col_tz_name,
		   col_comments == NULL ? "null/empty" : col_comments);
	  err_status = TZC_ERR_INVALID_ZONE;
	  TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_INVALID_ZONE, str, temp_msg);
	  goto exit;
	}

      /* new zone found, expand data structure and store it */
      err_status = tzc_add_zone (col_tz_name, col_code, col_coord, col_comments, tzd_raw, &temp_zone_info);
      if (err_status != NO_ERROR)
	{
	  goto exit;
	}
    }

exit:
  if (fp != NULL)
    {
      fclose (fp);
    }

  return err_status;
}

/*
 * tzc_load_rule_files() - loads the data from the files marked as TZ_RULES
 *			   (e.g. europe, asia etc.)
 * Returns: 0 (NO_ERROR) if success, error code or -1 otherwise
 * tzd_raw(out): timezone data structure to hold the loaded information
 * file_index(in): index in tz_Files file list for the file to be loaded
 * input_folder(in): folder containing IANA's timezone database
 *
 * NOTE: tz_load_rules() must be called after tz_load_zones(), because it
 *	 needs the data structures and loaded data created by tz_load_zones()
 */
static int
tzc_load_rule_file (TZ_RAW_DATA * tzd_raw, const int file_index, const char *input_folder)
{
  int err_status = NO_ERROR;
  char filepath[PATH_MAX] = { 0 };
  char str[TZ_MAX_LINE_LEN] = { 0 };
  FILE *fp = NULL;
  char *entry_type_str = NULL;
  TZ_RAW_ZONE_INFO *last_zone = NULL;
  char *next_token = NULL;
  bool check_zone = false;

  assert (tzd_raw != NULL);
  assert (input_folder != NULL);
  assert (file_index > 0 && file_index < tz_File_count);
  assert (tz_Files[file_index].type == TZF_RULES);

  tzc_build_filepath (filepath, sizeof (filepath), input_folder, tz_Files[file_index].name);
  fp = fopen_ex (filepath, "rt");
  if (fp == NULL)
    {
      /* file not found or not accessible */
      err_status = TZC_ERR_FILE_NOT_ACCESSIBLE;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_FILE_NOT_ACCESSIBLE, filepath, "read");
      goto exit;
    }

  LOG_TZC_SET_CURRENT_CONTEXT (tzd_raw, tz_Files[file_index].name, 0);

  while (fgets (str, sizeof (str), fp))
    {
      tzd_raw->context.current_line++;

      trim_comments_whitespaces (str);

      if (strlen (str) < TZ_OFFRULE_PREFIX_TAB_COUNT)
	{
	  continue;
	}

      if (check_zone)
	{
	  if (str[0] == '\t' && str[1] == '\t' && str[2] == '\t')
	    {
	      err_status = tzc_add_offset_rule (last_zone, str + 3);
	      if (err_status != NO_ERROR)
		{
		  goto exit;
		}
	      continue;
	    }
	  else
	    {
	      check_zone = false;
	    }
	}

      entry_type_str = strtok (str, " \t");

      if (strcmp (entry_type_str, "Link") == 0)
	{
	  char *zone_name = NULL, *alias = NULL;

	  zone_name = strtok (NULL, " \t");
	  alias = strtok (NULL, "\t");
	  if (IS_EMPTY_STR (zone_name) || IS_EMPTY_STR (alias))
	    {
	      /* error, empty or misformed line */
	      err_status = TZC_ERR_BAD_TZ_LINK;
	      TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_BAD_TZ_LINK, zone_name, alias);
	      goto exit;
	    }

	  err_status = tzc_add_link (tzd_raw, zone_name, alias);
	  if (err_status != NO_ERROR)
	    {
	      goto exit;
	    }
	  continue;
	}
      else if (strcmp (entry_type_str, "Zone") == 0)
	{
	  next_token = strtok (NULL, " \t");
	  if (IS_EMPTY_STR (next_token))
	    {
	      err_status = TZC_ERR_INVALID_VALUE;
	      TZC_LOG_ERROR_1ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_INVALID_VALUE, "timezone name");
	      goto exit;
	    }
	  err_status = tzc_get_zone (tzd_raw, next_token, &last_zone);
	  if (err_status != NO_ERROR)
	    {
	      /* some timezones (such as WET, CET, MET, EET) are not listed in the zone file, but are included in the
	       * rule files (ex.: europe file) in zone rules; they need to be added to the list of zones */
	      err_status = tzc_add_zone (next_token, NULL, NULL, NULL, tzd_raw, &last_zone);
	      if (err_status != NO_ERROR)
		{
		  goto exit;
		}
	    }
	  if (last_zone == NULL)
	    {
	      /* zones should have already been fully loaded */
	      err_status = TZC_ERR_ADD_ZONE;
	      TZC_LOG_ERROR_1ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_ADD_ZONE, next_token);
	      goto exit;
	    }

	  err_status = tzc_add_offset_rule (last_zone, next_token + strlen (next_token) + 1);
	  if (err_status != NO_ERROR)
	    {
	      goto exit;
	    }
	  check_zone = true;
	}
      else if (strcmp (entry_type_str, "Rule") == 0)
	{
	  err_status = tzc_add_ds_rule (tzd_raw, str + strlen (entry_type_str) + 1);
	  if (err_status != NO_ERROR)
	    {
	      goto exit;
	    }
	}
      else
	{
	  /* non-empty line, which is not a comment, nor a valid zone/rule/link definition */
	  assert (false);
	}
      /* reset line buffer */
      memset (str, 0, TZ_MAX_LINE_LEN);
    }

exit:
  if (fp != NULL)
    {
      fclose (fp);
    }

  return err_status;
}

/*
 * tzc_load_backward_zones() - loads the "backward" file containing links
 *		  between obsolete (more or less) zone names and their current
 *		  names
 * Returns: 0 (NO_ERROR) if success, error code or -1 otherwise
 * tzd_raw(out): timezone data structure to hold the loaded information
 * input_folder(in): folder containing IANA's timezone database
 *
 * NOTE: the "backward" file is actually usefull, because most SQL samples
 *	 found online sometimes use old zone names.
 */
static int
tzc_load_backward_zones (TZ_RAW_DATA * tzd_raw, const char *input_folder)
{
  int err_status = NO_ERROR;
  int i, file_index = -1;
  char filepath[PATH_MAX] = { 0 };
  char str[TZ_MAX_LINE_LEN] = { 0 };
  FILE *fp = NULL;
  char *entry_type_str = NULL;
  char *next_token = NULL, *zone_name = NULL, *alias = NULL;

  assert (tzd_raw != NULL);
  assert (input_folder != NULL);

  for (i = 0; i < tz_File_count; i++)
    {
      if (tz_Files[i].type == TZF_BACKWARD)
	{
	  file_index = i;
	  break;
	}
    }
  assert (file_index != -1);

  tzc_build_filepath (filepath, sizeof (filepath), input_folder, tz_Files[file_index].name);
  fp = fopen_ex (filepath, "rt");
  if (fp == NULL)
    {
      /* file not found or not accessible */
      err_status = TZC_ERR_FILE_NOT_ACCESSIBLE;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_FILE_NOT_ACCESSIBLE, filepath, "read");
      goto exit;
    }

  LOG_TZC_SET_CURRENT_CONTEXT (tzd_raw, tz_Files[file_index].name, 0);

  while (fgets (str, sizeof (str), fp))
    {
      tzd_raw->context.current_line++;

      trim_comments_whitespaces (str);

      if (IS_EMPTY_STR (str))
	{
	  continue;
	}

      entry_type_str = strtok (str, "\t");

      assert (entry_type_str != NULL && strcmp (entry_type_str, "Link") == 0);

      next_token = str + strlen (entry_type_str) + 1;
      zone_name = strtok (next_token, "\t");

      if (IS_EMPTY_STR (zone_name))
	{
	  err_status = TZC_ERR_BAD_TZ_LINK;
	  TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_BAD_TZ_LINK, zone_name, "<alias not parsed yet>");
	  goto exit;
	}
      next_token = zone_name + strlen (zone_name) + 1;
      while (*next_token == '\t')
	{
	  next_token++;
	}
      alias = strtok (next_token, "\t");
      if (IS_EMPTY_STR (alias))
	{
	  err_status = TZC_ERR_BAD_TZ_LINK;
	  TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_BAD_TZ_LINK, zone_name, alias);
	  goto exit;
	}
      err_status = tzc_add_link (tzd_raw, zone_name, alias);
      if (err_status != NO_ERROR)
	{
	  goto exit;
	}
      /* reset line buffer */
      memset (str, 0, TZ_MAX_LINE_LEN);
    }

exit:
  if (fp != NULL)
    {
      fclose (fp);
    }

  return err_status;
}

/*
 * tzc_load_leap_secs() - loads the "leapseconds" file containing leap
 *			  second information (date of occurence, type, etc.)
 * Returns: 0 (NO_ERROR) if success, error code or -1 otherwise
 * tzd_raw(out): timezone data structure to hold the loaded information
 * input_folder(in): folder containing IANA's timezone database
 */
static int
tzc_load_leap_secs (TZ_RAW_DATA * tzd_raw, const char *input_folder)
{
  int err_status = NO_ERROR;
  int i, file_index = -1, leap_year, leap_month_num, leap_day_num;
  int leap_time_h, leap_time_m, leap_time_s;
  char filepath[PATH_MAX] = { 0 };
  char str[TZ_MAX_LINE_LEN] = { 0 };
  FILE *fp = NULL;
  const char *next_token, *str_next, *entry_type_str;
  bool leap_corr_minus = false;
  bool leap_is_rolling = false;

  assert (tzd_raw != NULL);
  assert (input_folder != NULL);

  next_token = str_next = entry_type_str = NULL;
  leap_year = leap_month_num = leap_day_num = -1;
  leap_time_h = leap_time_m = leap_time_s = -1;

  for (i = 0; i < tz_File_count; i++)
    {
      if (tz_Files[i].type == TZF_LEAP)
	{
	  file_index = i;
	  break;
	}
    }
  assert (file_index != -1);

  tzc_build_filepath (filepath, sizeof (filepath), input_folder, tz_Files[file_index].name);
  fp = fopen_ex (filepath, "rt");
  if (fp == NULL)
    {
      /* file not found or not accessible */
      err_status = TZC_ERR_FILE_NOT_ACCESSIBLE;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_FILE_NOT_ACCESSIBLE, filepath, "read");
      goto exit;
    }

  tzd_raw->leap_sec_count = 0;
  tzd_raw->leap_sec = NULL;

  LOG_TZC_SET_CURRENT_CONTEXT (tzd_raw, tz_Files[file_index].name, 0);

  while (fgets (str, sizeof (str), fp))
    {
      const char *str_end;
      tzd_raw->context.current_line++;
      trim_comments_whitespaces (str);

      if (IS_EMPTY_STR (str))
	{
	  continue;
	}

      str_end = str + strlen (str);

      entry_type_str = strtok (str, "\t");
      assert (entry_type_str != NULL && strcmp (entry_type_str, "Leap") == 0);

      next_token = strtok (NULL, "\t");
      if (tz_str_read_number (next_token, str_end, true, false, &leap_year, &str_next) != NO_ERROR)
	{
	  err_status = TZC_ERR_INVALID_VALUE;
	  TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_INVALID_VALUE, "year",
			      next_token == NULL ? "" : next_token);
	  goto exit;
	}

      next_token = strtok (NULL, "\t");
      if (str_month_to_int (next_token, &leap_month_num, &str_next) != NO_ERROR)
	{
	  err_status = TZC_ERR_INVALID_VALUE;
	  TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_INVALID_VALUE, "month",
			      next_token == NULL ? "" : next_token);
	  goto exit;
	}

      next_token = strtok (NULL, "\t");
      if (tz_str_read_number (next_token, str_end, true, false, &leap_day_num, &str_next) != NO_ERROR)
	{
	  err_status = TZC_ERR_INVALID_VALUE;
	  TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_INVALID_VALUE, "day",
			      next_token == NULL ? "" : next_token);
	  goto exit;
	}

      next_token = strtok (NULL, "\t");
      if (tz_str_read_time (next_token, str_end, false, true, &leap_time_h, &leap_time_m, &leap_time_s, &str_next) !=
	  NO_ERROR)
	{
	  err_status = TZC_ERR_INVALID_VALUE;
	  TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_INVALID_VALUE, "time",
			      next_token == NULL ? "" : next_token);
	  goto exit;
	}

      next_token = strtok (NULL, "\t");
      if (strlen (next_token) != 1 || ((*next_token != '+') && (*next_token != '-')))
	{
	  err_status = TZC_ERR_INVALID_VALUE;
	  TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_INVALID_VALUE, "correction",
			      next_token == NULL ? "" : next_token);
	  goto exit;
	}
      if (*next_token == '-')
	{
	  leap_corr_minus = true;
	}

      next_token = strtok (NULL, "\t");
      if (strlen (next_token) != 1 || ((*next_token != 'R') && (*next_token != 'S')))
	{
	  err_status = TZC_ERR_INVALID_VALUE;
	  TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_INVALID_VALUE, "leap second type",
			      next_token == NULL ? "" : next_token);
	  goto exit;
	}
      if (*next_token == 'R')
	{
	  leap_is_rolling = true;
	}

      if (tzc_add_leap_sec (tzd_raw, leap_year, leap_month_num, leap_day_num, leap_time_h, leap_time_m, leap_time_s,
			    leap_corr_minus, leap_is_rolling) != NO_ERROR)
	{
	  err_status = TZC_ERR_INVALID_VALUE;
	  TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_INVALID_VALUE, "leap second data", "line");
	  goto exit;
	}
    }

exit:
  if (fp != NULL)
    {
      fclose (fp);
    }

  return err_status;
}

/*
 * tzc_add_link() - adds a link (zone alias) to the list of links in the
 *		    raw timezone data structure
 * Returns: error code if failure, 0 (NO_ERROR) otherwise
 * tzd_raw(out): timezone data structure where to store the new link
 * zone(in): zone name for the link to be created
 * alias(in): zone alias for the link to be created
 */
static int
tzc_add_link (TZ_RAW_DATA * tzd_raw, const char *zone, const char *alias)
{
  TZ_RAW_LINK *temp_link;
  int err_status = NO_ERROR;

  assert (tzd_raw != NULL);
  assert (!IS_EMPTY_STR (zone) && !IS_EMPTY_STR (alias));

  /* add link to the link list */
  assert (tzd_raw->link_count >= 0);

  temp_link = (TZ_RAW_LINK *) realloc (tzd_raw->links, (tzd_raw->link_count + 1) * sizeof (TZ_RAW_LINK));

  if (temp_link == NULL)
    {
      char err_msg[TZC_ERR_MSG_MAX_SIZE];

      sprintf (err_msg, "%d", tzd_raw->link_count + 1);
      err_status = TZC_ERR_OUT_OF_MEMORY;
      TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_OUT_OF_MEMORY, err_msg, "TZ_RAW_LINK");
      goto exit;
    }

  tzd_raw->links = temp_link;
  temp_link = &(tzd_raw->links[tzd_raw->link_count]);
  tzd_raw->link_count++;
  memset (temp_link, 0, sizeof (temp_link[0]));

  strcpy (temp_link->name, zone);
  strcpy (temp_link->alias, alias);

exit:
  return err_status;
}

/*
 * tzc_add_leap_sec() - adds leap second info to the raw timezone structure
 * Returns: error code if failure, 0 (NO_ERROR) otherwise
 * year(in), month(in), day(in): parts of the date when leap second occurs
 * time(in): time (number of seconds since 00:00) of day when leap sec occurs
 * corr_minus(in): true if leap second correction is negative, true if '+'
 * leap_is_rolling(in): true if given time is local, false if UTC
 * tzd_raw(in/out): timezone data structure where to store the new link
 */
static int
tzc_add_leap_sec (TZ_RAW_DATA * tzd_raw, int year, int month, int day, unsigned char hour, unsigned char min,
		  unsigned char sec, bool corr_minus, bool leap_is_rolling)
{
  TZ_LEAP_SEC *temp_leap_sec;
  int err_status = NO_ERROR;

  assert (tzd_raw != NULL);

  /* add leap second to the leap second list */
  assert (tzd_raw->leap_sec_count >= 0);

  temp_leap_sec = (TZ_LEAP_SEC *) realloc (tzd_raw->leap_sec, (tzd_raw->leap_sec_count + 1) * sizeof (TZ_LEAP_SEC));

  if (temp_leap_sec == NULL)
    {
      char err_msg[TZC_ERR_MSG_MAX_SIZE];

      sprintf (err_msg, "%d", tzd_raw->leap_sec_count + 1);
      err_status = TZC_ERR_OUT_OF_MEMORY;
      TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_OUT_OF_MEMORY, err_msg, "TZ_RAW_LEAP_SEC");
      goto exit;
    }

  tzd_raw->leap_sec = temp_leap_sec;
  temp_leap_sec = &(tzd_raw->leap_sec[tzd_raw->leap_sec_count]);
  tzd_raw->leap_sec_count++;
  memset (temp_leap_sec, 0, sizeof (temp_leap_sec[0]));

  /* set data */
  temp_leap_sec->year = (unsigned short) year;
  temp_leap_sec->month = (unsigned char) month;
  temp_leap_sec->day = (unsigned char) day;

  if (hour != 23 || min != 59 || sec != 60)
    {
      char err_msg[TZC_ERR_MSG_MAX_SIZE];
      sprintf (err_msg, "%d", tzd_raw->leap_sec_count + 1);
      err_status = TZC_ERR_INVALID_TIME;
      TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_INVALID_TIME, err_msg, "TZ_RAW_LEAP_SEC");
      goto exit;
    }
  temp_leap_sec->corr_negative = (unsigned char) (corr_minus ? 1 : 0);
  temp_leap_sec->is_rolling = (unsigned char) (leap_is_rolling ? 1 : 0);

exit:
  return err_status;
}

/*
 * tzc_add_zone() - adds a zone to the list of timezones in the raw
 *		    timezone data structure
 * Returns: 0 (NO_ERROR) if success, error code if failure
 * zone(in): name of the timezone to add
 * code (in): code of the time zone to add
 * coord(in): coordinates of the timezone to add
 * comments(in): comments for the timezone to add
 * tzd_raw(in/out): raw timezone data structure where to add the timezone to
 * new_zone(out): pointer to the newly added timezone
 */
static int
tzc_add_zone (const char *zone, const char *code, const char *coord, const char *comments, TZ_RAW_DATA * tzd_raw,
	      TZ_RAW_ZONE_INFO ** new_zone)
{
  TZ_RAW_ZONE_INFO *temp_zone_info = NULL;
  int err_status = NO_ERROR;

  assert (tzd_raw != NULL);
  assert (!IS_EMPTY_STR (zone));

  *new_zone = NULL;

  temp_zone_info = (TZ_RAW_ZONE_INFO *) realloc (tzd_raw->zones, (tzd_raw->zone_count + 1) * sizeof (TZ_RAW_ZONE_INFO));

  if (temp_zone_info == NULL)
    {
      char err_msg[TZC_ERR_MSG_MAX_SIZE];

      sprintf (err_msg, "%d", tzd_raw->zone_count + 1);
      err_status = TZC_ERR_OUT_OF_MEMORY;
      TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_OUT_OF_MEMORY, err_msg, "TZ_RAW_ZONE_INFO");
      goto exit;
    }
  tzd_raw->zones = temp_zone_info;

  /* grasp the newly added item */
  temp_zone_info = &(tzd_raw->zones[tzd_raw->zone_count]);
  tzd_raw->zone_count++;
  memset (temp_zone_info, 0, sizeof (temp_zone_info[0]));

  /* store the parsed data */
  if (code != NULL)
    {
      strcpy (temp_zone_info->code, code);
    }
  else
    {
      temp_zone_info->code[0] = '\0';
    }
  if (coord != NULL)
    {
      strcpy (temp_zone_info->coordinates, coord);
    }
  else
    {
      temp_zone_info->coordinates[0] = '\0';
    }

  strcpy (temp_zone_info->full_name, zone);

  if (comments != NULL)
    {
      strcpy (temp_zone_info->comments, comments);
    }
  else
    {
      temp_zone_info->comments[0] = '\0';
    }

  temp_zone_info->id = -1;

  *new_zone = temp_zone_info;
exit:
  return err_status;
}

/*
 * tzc_get_zone() - parse the input string to get a zone name and
 *		    searches for it in the already loaded zones from the
 *		    TZ_RAW_DATA variable tzd_raw.
 * Returns: 0 (NO_ERROR) if zone found, -1 otherwise
 * tzd_raw(in/out): raw timezone data structure where to search the zone in
 * zone_name(in): name of the timezone to search for
 * zone (out): reference to the timezone found
 */
static int
tzc_get_zone (const TZ_RAW_DATA * tzd_raw, const char *zone_name, TZ_RAW_ZONE_INFO ** zone)
{
  int i;

  assert (tzd_raw != NULL);

  for (i = 0; i < tzd_raw->zone_count; i++)
    {
      if (strcmp (tzd_raw->zones[i].full_name, zone_name) == 0)
	{
	  *zone = &(tzd_raw->zones[i]);
	  return NO_ERROR;
	}
    }
  *zone = NULL;

  return TZC_ERR_GENERIC;
}

/*
 * tzc_add_offset_rule() - parse the input string as an offset rule for a
 *		   timezone and attach it to the specified timezone
 * Returns: 0 (NO_ERROR) if success, or negative code if an error occurs
 * zone(out): raw timezone information structure where to store the parsed
 *	      offset rule
 * rule_text(in): string to parse as an offset rule
 */
static int
tzc_add_offset_rule (TZ_RAW_ZONE_INFO * zone, char *rule_text)
{
  char *gmt_off = NULL, *rules = NULL, *format = NULL, *until = NULL;
  int err_status = NO_ERROR;
  TZ_RAW_OFFSET_RULE *temp_rule = NULL;
  const char *str_dummy = NULL;
  char *rule_text_end;
  int gmt_off_num = 0;
  bool is_numeric_gmt_off = false;

  assert (zone != NULL);

  rule_text_end = rule_text + strlen (rule_text);

  gmt_off = strtok (rule_text, "\t ");

  if (IS_EMPTY_STR (gmt_off))
    {
      err_status = TZC_ERR_INVALID_VALUE;
      TZC_LOG_ERROR_1ARG (NULL, TZC_ERR_INVALID_VALUE, "zone offset");
      goto exit;
    }

  /* some offset rules have the GMTOFF column '0' instead of a valid time value in the format hh:mm */
  if (strcmp (gmt_off, "0") == 0)
    {
      is_numeric_gmt_off = true;
      gmt_off_num = 0;
    }
  else if (strchr (gmt_off, ':') == NULL)
    {
      /* string might not be a GMT offset rule; check if it's a number */
      err_status = tz_str_read_number (gmt_off, rule_text_end, true, true, &gmt_off_num, &str_dummy);
      if (err_status != NO_ERROR)
	{
	  err_status = TZC_ERR_INVALID_VALUE;
	  goto exit;
	}
      is_numeric_gmt_off = true;
    }
  rules = strtok (NULL, " \t");
  format = strtok (NULL, " \t");

  if (IS_EMPTY_STR (rules) || IS_EMPTY_STR (format))
    {
      err_status = TZC_ERR_INVALID_VALUE;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_INVALID_VALUE, "ruleset name or format",
			  IS_EMPTY_STR (rules) ? (IS_EMPTY_STR (format) ? "" : format) : rules);
      goto exit;
    }
  until = strtok (NULL, "\t");

  if (zone->offset_rule_count != 0 && zone->offset_rules[zone->offset_rule_count - 1].until_flag == UNTIL_INFINITE)
    {
      /* last existing rule still applies, no new rules are allowed after this one */
      err_status = TZC_ERR_ZONE_RULE_UNORDERED;
      TZC_LOG_ERROR_1ARG (NULL, TZC_ERR_ZONE_RULE_UNORDERED, rule_text);
      goto exit;
    }

  temp_rule =
    (TZ_RAW_OFFSET_RULE *) realloc (zone->offset_rules, (zone->offset_rule_count + 1) * sizeof (TZ_RAW_OFFSET_RULE));

  if (temp_rule == NULL)
    {
      char err_msg[TZC_ERR_MSG_MAX_SIZE];

      sprintf (err_msg, "%d", zone->offset_rule_count + 1);
      err_status = TZC_ERR_OUT_OF_MEMORY;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_OUT_OF_MEMORY, err_msg, "TZ_RAW_OFFSET_RULE");
      goto exit;
    }

  zone->offset_rules = temp_rule;
  temp_rule = &(zone->offset_rules[zone->offset_rule_count]);
  zone->offset_rule_count++;
  memset (temp_rule, 0, sizeof (temp_rule[0]));

  if (is_numeric_gmt_off)
    {
      temp_rule->gmt_off = gmt_off_num * 3600;
    }
  else
    {
      err_status = tz_str_to_seconds (gmt_off, rule_text_end, &(temp_rule->gmt_off), &str_dummy, false);
      if (err_status != NO_ERROR)
	{
	  TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_INVALID_TIME, gmt_off, "tz offset rule");
	  goto exit;
	}
    }

  assert (strlen (rules) < TZ_DS_RULESET_NAME_SIZE && strlen (format) < TZ_MAX_FORMAT_SIZE);

  strcpy (temp_rule->ds_ruleset_name, rules);
  strcpy (temp_rule->format, format);

  if (str_to_offset_rule_until (temp_rule, until) != NO_ERROR)
    {
      goto exit;
    }

exit:
  return err_status;
}

/*
 * tzc_read_time_type() -
 *
 * Returns: 0 (NO_ERROR) if success, or negative code if an error occurs
 * tzd_raw(in/out): raw timezone data structure
 * rule_text(in): string to parse as a daylight saving rule
 */
static int
tzc_read_time_type (const char *str, const char **next, TZ_TIME_TYPE * time_type)
{
  assert (time_type != NULL);
  assert (next != NULL);

  switch (*str)
    {
    case 's':
      /* local standard time (different from wall clock time when observing daylight saving time) */
      *time_type = TZ_TIME_TYPE_LOCAL_STD;
      break;
    case 'g':
    case 'u':
    case 'z':
      /* g/u/z stand for GMT/UTC/Zulu which are the same thing */
      *time_type = TZ_TIME_TYPE_UTC;
      break;
    case 'w':
    case '\0':
      /* wall clock time (including daylight savings, if any). This is the default, if no suffix is used. */
      *time_type = TZ_TIME_TYPE_LOCAL_WALL;
      break;
    default:
      /* No other suffixes are allowed, so this should never be hit */
      return TZC_ERR_GENERIC;
      break;
    }

  *next = (char *) str + 1;

  return NO_ERROR;
}

/*
 * tzc_add_ds_rule() - parse the input string as a daylight saving rule and
 *		       append it to the rule set identified by the rule name
 * Returns: 0 (NO_ERROR) if success, or negative code if an error occurs
 * tzd_raw(in/out): raw timezone data structure
 * rule_text(in): string to parse as a daylight saving rule
 */
static int
tzc_add_ds_rule (TZ_RAW_DATA * tzd_raw, char *rule_text)
{
  int i, val_read;
  int err_status = NO_ERROR;
  TZ_RAW_DS_RULESET *ds_ruleset = NULL;
  TZ_RAW_DS_RULE *ds_rule = NULL;
  char *col_name, *col_from, *col_to, *col_type, *col_in, *col_on, *col_at;
  char *col_save, *col_letters;
  const char *str_cursor = NULL;
  const char *rule_text_end;

  assert (tzd_raw != NULL);

  rule_text_end = rule_text + strlen (rule_text);

  col_name = strtok (rule_text, " \t");
  col_from = strtok (NULL, " \t");
  col_to = strtok (NULL, " \t");
  col_type = strtok (NULL, " \t");
  col_in = strtok (NULL, " \t");
  col_on = strtok (NULL, " \t");
  col_at = strtok (NULL, " \t");
  col_save = strtok (NULL, " \t");
  col_letters = strtok (NULL, " \t");

  /* all tokens above must be at least one character long */
  if (IS_EMPTY_STR (col_name) || IS_EMPTY_STR (col_from) || IS_EMPTY_STR (col_to) || IS_EMPTY_STR (col_type)
      || IS_EMPTY_STR (col_in) || IS_EMPTY_STR (col_on) || IS_EMPTY_STR (col_at) || IS_EMPTY_STR (col_save)
      || IS_EMPTY_STR (col_letters))
    {
      char temp_msg[TZC_ERR_MSG_MAX_SIZE] = { 0 };

      sprintf (temp_msg,
	       "NAME: '%s', FROM: '%s', TO: '%s', TYPE: '%s', "
	       "IN: '%s', ON: '%s', AT: '%s', SAVE: '%s', LETTER/S: '%s'", col_name == NULL ? "" : col_name,
	       col_from == NULL ? "" : col_from, col_to == NULL ? "" : col_to, col_type == NULL ? "" : col_type,
	       col_in == NULL ? "" : col_in, col_on == NULL ? "" : col_on, col_at == NULL ? "" : col_at,
	       col_save == NULL ? "" : col_save, col_letters == NULL ? "" : col_letters);
      err_status = TZC_ERR_INVALID_DS_RULE;
      TZC_LOG_ERROR_1ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_INVALID_DS_RULE, temp_msg);
      goto exit;
    }

  STR_SKIP_LEADING_SPACES (col_name);
  STR_SKIP_LEADING_SPACES (col_from);
  STR_SKIP_LEADING_SPACES (col_to);
  STR_SKIP_LEADING_SPACES (col_type);
  STR_SKIP_LEADING_SPACES (col_in);
  STR_SKIP_LEADING_SPACES (col_on);
  STR_SKIP_LEADING_SPACES (col_at);
  STR_SKIP_LEADING_SPACES (col_save);
  STR_SKIP_LEADING_SPACES (col_letters);

  /* find a rule set with the same name as the rule */
  for (i = 0; i < tzd_raw->ruleset_count; i++)
    {
      if (strcmp (tzd_raw->ds_rulesets[i].name, col_name) == 0)
	{
	  ds_ruleset = &(tzd_raw->ds_rulesets[i]);
	  break;
	}
    }

  if (ds_ruleset == NULL)
    {
      /* no rule set with the designated name was found; create one */
      ds_ruleset =
	(TZ_RAW_DS_RULESET *) realloc (tzd_raw->ds_rulesets, (tzd_raw->ruleset_count + 1) * sizeof (TZ_RAW_DS_RULESET));

      if (ds_ruleset == NULL)
	{
	  char err_msg[TZC_ERR_MSG_MAX_SIZE];

	  sprintf (err_msg, "%d", tzd_raw->ruleset_count + 1);
	  err_status = TZC_ERR_OUT_OF_MEMORY;
	  TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_OUT_OF_MEMORY, err_msg, "TZ_RAW_DS_RULESET");
	  goto exit;
	}

      tzd_raw->ds_rulesets = ds_ruleset;
      ds_ruleset = &(tzd_raw->ds_rulesets[tzd_raw->ruleset_count]);
      tzd_raw->ruleset_count++;
      memset (ds_ruleset, 0, sizeof (ds_ruleset[0]));
      strcpy (ds_ruleset->name, col_name);
      ds_ruleset->is_used = false;
    }

  /* add the daylight saving rule to the rule set (found or created) */
  ds_rule = (TZ_RAW_DS_RULE *) realloc (ds_ruleset->rules, (ds_ruleset->rule_count + 1) * sizeof (TZ_RAW_DS_RULE));

  if (ds_rule == NULL)
    {
      char err_msg[TZC_ERR_MSG_MAX_SIZE];

      sprintf (err_msg, "%d", ds_ruleset->rule_count + 1);
      err_status = TZC_ERR_OUT_OF_MEMORY;
      TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_OUT_OF_MEMORY, err_msg, "TZ_RAW_DS_RULE");
      goto exit;
    }

  ds_ruleset->rules = ds_rule;
  ds_rule = &(ds_ruleset->rules[ds_ruleset->rule_count]);
  ds_ruleset->rule_count++;
  memset (ds_rule, 0, sizeof (ds_rule[0]));

  /* process and save data into the new TZ_RULE item */
  val_read = 0;
  /* process and store "FROM" year */
  if (tz_str_read_number (col_from, rule_text_end, true, false, &val_read, &str_cursor) != NO_ERROR)
    {
      err_status = TZC_ERR_CANT_READ_VALUE;
      TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_CANT_READ_VALUE, "year", col_from);
      goto exit;
    }
  ds_rule->from_year = (short) val_read;

  /* process and store "TO" year; if field value is "only", copy "FROM" */
  if (strcmp (col_to, "only") == 0)
    {
      ds_rule->to_year = ds_rule->from_year;
    }
  else if (strcmp (col_to, "max") == 0)
    {
      ds_rule->to_year = TZ_MAX_YEAR;
    }
  else if (tz_str_read_number (col_to, rule_text_end, true, false, &val_read, &str_cursor) != NO_ERROR)
    {
      err_status = TZC_ERR_CANT_READ_VALUE;
      TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_CANT_READ_VALUE, "year", col_to);
      goto exit;
    }
  else
    {
      ds_rule->to_year = (short) val_read;
    }

  assert (col_type == NULL || strlen (col_type) < TZ_RULE_TYPE_MAX_SIZE);
  strcpy (ds_rule->type, col_type);

  /* process and store month ("IN") */
  if (str_month_to_int (col_in, &val_read, &str_cursor) != NO_ERROR)
    {
      err_status = TZC_ERR_CANT_READ_VALUE;
      TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_CANT_READ_VALUE, "month", col_in);
      goto exit;
    }
  if (val_read < TZ_MON_JAN || val_read > TZ_MON_DEC)
    {
      char temp_msg[TZC_ERR_MSG_MAX_SIZE] = { 0 };

      sprintf (temp_msg, "found value: %d", val_read);
      err_status = TZC_ERR_INVALID_VALUE;
      TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_INVALID_VALUE, "month value", temp_msg);
      goto exit;
    }
  ds_rule->in_month = (unsigned char) val_read;

  err_status = tzc_parse_ds_change_on (ds_rule, col_on);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

  /* some daylight saving rules use "0" instead of "0:00" in the AT column */
  if (strcmp (col_at, "0") == 0)
    {
      val_read = 0;
    }
  else if (tz_str_to_seconds (col_at, rule_text_end, &val_read, &str_cursor, false) != NO_ERROR)
    {
      err_status = TZC_ERR_CANT_READ_VALUE;
      TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_CANT_READ_VALUE, "AT column", col_at);
      goto exit;
    }
  ds_rule->at_time = val_read;

  err_status = tzc_read_time_type (str_cursor, &str_cursor, &(ds_rule->at_time_type));
  if (err_status != NO_ERROR)
    {
      return err_status;
    }

  /* In a daylight saving rule, the "Save" column is either 0 or 1 hour, given as a one char string ("0" or "1"), or an
   * amount of time specified as hh:mm. So first check if col_save == "<one_char>" */
  if (strlen (col_save) == 1)
    {
      if (tz_str_read_number (col_save, rule_text_end, true, false, &val_read, &str_cursor) != NO_ERROR)
	{
	  err_status = TZC_ERR_CANT_READ_VALUE;
	  TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_CANT_READ_VALUE, "SAVE column", col_save);
	  goto exit;
	}
      val_read *= 3600;
    }
  else if (tz_str_to_seconds (col_save, rule_text_end, &val_read, &str_cursor, false) != NO_ERROR)
    {
      err_status = TZC_ERR_CANT_READ_VALUE;
      TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_CANT_READ_VALUE, "SAVE column", col_save);
      goto exit;
    }
  ds_rule->save_time = val_read;
  strcpy (ds_rule->letter_abbrev, col_letters);

exit:
  return err_status;
}

/*
 * tzc_parse_ds_change_on() - parse the string corresponding to the "ON"
 *			      column in a daylight saving rule, and store
 *			      data in the destination TZ_DS_CHANGE_ON variable
 * Returns: 0 (NO_ERROR) if success, error code otherwise
 * dest(out): destination variable where to store the parsed data
 * str(in): input string to parse
 *
 * NOTE: The input string can be of the following forms: "21" (plain number),
 *	"Sun>=16", "lastSun" or other variations using other week days.
 */
static int
tzc_parse_ds_change_on (TZ_RAW_DS_RULE * dest, const char *str)
{
  int err_status = NO_ERROR;
  int day_num = 0;		/* day of week or day of month, depending on the context */
  int type = -1, bound = -1;
  const char *str_cursor = NULL;

  assert (str != NULL && strlen (str) > 0);
  assert (dest->in_month <= TZ_MON_DEC);

  str_cursor = str;

  err_status = str_read_day_var (str, dest->in_month, &type, &day_num, &bound, &str_cursor);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

  /* need to validate the day found; check if it is a valid value, according to the year(s) and month already read into
   * the input TZ_RAW_DS_RULE dest parameter */
  if (type == TZ_DS_TYPE_FIXED)
    {
      int upper_year;

      if (day_num < 0 || day_num > 30)
	{
	  char temp_msg[TZC_ERR_MSG_MAX_SIZE] = { 0 };

	  sprintf (temp_msg, "found value: %d", day_num);
	  err_status = TZC_ERR_DS_INVALID_DATE;
	  TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_DS_INVALID_DATE, "day of month", temp_msg);
	  goto exit;
	}

      upper_year = (dest->to_year == TZ_MAX_YEAR) ? dest->from_year : dest->to_year;

      if (!tzc_is_valid_date (day_num, dest->in_month, dest->from_year, upper_year + 1))
	{
	  char temp_msg[TZC_ERR_MSG_MAX_SIZE] = { 0 };

	  sprintf (temp_msg, "Day: %d, Month: %d, Year: %d", day_num, dest->in_month, dest->from_year);
	  err_status = TZC_ERR_DS_INVALID_DATE;
	  TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_DS_INVALID_DATE, "day of month", temp_msg);
	  goto exit;
	}

      /* This is a fixed day of month, store it as such */
      dest->change_on.type = TZ_DS_TYPE_FIXED;
      dest->change_on.day_of_month = (unsigned char) day_num;
      dest->change_on.day_of_week = TZ_WEEK_DAY_COUNT;	/* invalid value */
    }
  else if (type == TZ_DS_TYPE_VAR_SMALLER)
    {
      dest->change_on.type = TZ_DS_TYPE_VAR_SMALLER;
      dest->change_on.day_of_month = (unsigned char) bound;
      dest->change_on.day_of_week = (unsigned char) day_num;
    }
  else if (type == TZ_DS_TYPE_VAR_GREATER)
    {
      dest->change_on.type = TZ_DS_TYPE_VAR_GREATER;
      dest->change_on.day_of_month = (unsigned char) bound;
      dest->change_on.day_of_week = (unsigned char) day_num;
    }
  else
    {
      assert (false);
    }

exit:
  return err_status;
}

/*
 * tzc_is_valid_date () - check if a given date (day and month) is valid for
 *		  all the year values in a given range [year_start, year_end)
 * Returns: true if valid, false otherwise
 * day(int): day of month (0 to 27/28/29/30)
 * month(in): month (0-11)
 * year_start(in): starting year for the range
 * year_end(in): ending year for the range
*/
static bool
tzc_is_valid_date (const int day, const int month, const int year_start, const int year_end)
{
  int year;

  if (day < 0 || day > 30 || month < TZ_MON_JAN || month > TZ_MON_DEC || year_start < 0 || year_end <= year_start
      || year_end > TZ_MAX_YEAR)
    {
      return false;
    }

  if (month == TZ_MON_FEB)
    {
      for (year = year_start; year < year_end; year++)
	{
	  if (day >= (IS_LEAP_YEAR (year) ? 29 : 28))
	    {
	      return false;
	    }
	}
    }
  else
    {
      if (day >= DAYS_IN_MONTH (month))
	{
	  return false;
	}
    }

  return true;
}

/*
 * tzc_free_raw_data() - frees the structures used when loading the timezone
 *			 data from the input folder
 * Returns:
 * tzd_raw(in/out): time zone bulk data structure to free
 */
static void
tzc_free_raw_data (TZ_RAW_DATA * tzd_raw)
{
  int i, j;

  if (tzd_raw == NULL)
    {
      return;
    }
  if (tzd_raw->countries != NULL)
    {
      free_and_init (tzd_raw->countries);
    }
  if (tzd_raw->links != NULL)
    {
      free_and_init (tzd_raw->links);
    }
  if (tzd_raw->ds_rulesets != NULL)
    {
      for (i = 0; i < tzd_raw->ruleset_count; i++)
	{
	  free_and_init (tzd_raw->ds_rulesets[i].rules);
	}
      free_and_init (tzd_raw->ds_rulesets);
    }
  if (tzd_raw->zones != NULL)
    {
      TZ_RAW_ZONE_INFO *zone = NULL;

      for (i = 0; i < tzd_raw->zone_count; i++)
	{
	  zone = &(tzd_raw->zones[i]);

	  free_and_init (zone->offset_rules);

	  for (j = 0; j < zone->alias_count; j++)
	    {
	      if (zone->aliases[j] != NULL)
		{
		  free_and_init (zone->aliases[j]);
		}
	    }
	  free_and_init (zone->aliases);
	}

      free_and_init (tzd_raw->zones);
    }
  if (tzd_raw->leap_sec != NULL)
    {
      free_and_init (tzd_raw->leap_sec);
    }
}

/*
 * tzc_check_links_raw_data () - go through TZ_RAW_DATA.links and check if
 *			there is an actual link between two existing timezones
 *			(both defined in zone.tab) which should share the same
 *			time settings e.g. rules are defined for one timezone
 *			but not the other.
 * Returns: 0 (NO_ERROR) if success, error code otherwise
 * tzd_raw(in/out): time zone bulk data structure to free
 *
 * NOTE: an example of such a link between two timezones is
 *	 'Link	Europe/Zurich  Europe/Busingen'; Both zones are defined in
 *	 zone.tab, but GMT offset rules are defined only for Europe/Zurich;
 *	 in this case, the 'Link' described above is not a simple alias, but a
 *	 cloning rule.
 */
static int
tzc_check_links_raw_data (TZ_RAW_DATA * tzd_raw)
{
  int err_status = NO_ERROR;
  int i, j, tz_name_index, tz_alias_index;
  TZ_RAW_LINK *tz_link;
  TZ_RAW_ZONE_INFO *tz_zone;

  for (i = 0; i < tzd_raw->link_count; i++)
    {
      tz_link = &(tzd_raw->links[i]);
      tz_name_index = -1;
      tz_alias_index = -1;
      for (j = 0; j < tzd_raw->zone_count && (tz_name_index == -1 || tz_alias_index == -1); j++)
	{
	  tz_zone = &(tzd_raw->zones[j]);
	  if (strcmp (tz_zone->full_name, tz_link->name) == 0)
	    {
	      tz_name_index = j;
	      assert (tz_name_index != tz_alias_index);
	    }
	  if (strcmp (tz_zone->full_name, tz_link->alias) == 0)
	    {
	      tz_alias_index = j;
	      assert (tz_name_index != tz_alias_index);
	    }
	}

      if (tz_name_index != -1 && tz_alias_index != -1)
	{
	  /* Both zone name and alias from this 'Link' rule are timezones; Check which one does not have any attached
	   * rules and mark it as sharing the other one's rules */
	  bool remove_link = false;
	  tz_zone = &(tzd_raw->zones[tz_name_index]);
	  if (tz_zone->offset_rule_count > 0)
	    {
	      assert (tzd_raw->zones[tz_alias_index].offset_rule_count == 0);
	      strcpy (tzd_raw->zones[tz_alias_index].clone_of, tz_zone->full_name);
	      remove_link = true;
	    }
	  else if (tzd_raw->zones[tz_alias_index].offset_rule_count > 0)
	    {
	      assert (tz_zone->offset_rule_count == 0);
	      strcpy (tz_zone->clone_of, tzd_raw->zones[tz_alias_index].full_name);
	      remove_link = true;
	    }
	  else
	    {
	      /* fatal error; can't have two fully defined timezones with a 'Link' rule defined between them */
	      assert (false);
	      err_status = TZC_ERR_LINKING_TRUE_ZONES;
	      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_LINKING_TRUE_ZONES, tz_link->name, tz_link->alias);
	      goto exit;
	    }
	  if (remove_link)
	    {
	      if (i < tzd_raw->link_count - 1)
		{
		  tz_link = &(tzd_raw->links[i]);
		  strcpy (tz_link->alias, tzd_raw->links[tzd_raw->link_count - 1].alias);
		  strcpy (tz_link->name, tzd_raw->links[tzd_raw->link_count - 1].name);
		}
	      if (tzd_raw->link_count > 1)
		{
		  tz_link = (TZ_RAW_LINK *) realloc (tzd_raw->links, (tzd_raw->link_count - 1) * sizeof (TZ_RAW_LINK));
		  if (tz_link == NULL)
		    {
		      char err_msg[TZC_ERR_MSG_MAX_SIZE];

		      sprintf (err_msg, "%d", tzd_raw->link_count - 1);
		      err_status = TZC_ERR_OUT_OF_MEMORY;
		      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_OUT_OF_MEMORY, err_msg, "TZ_RAW_LINK");
		      goto exit;
		    }
		  tzd_raw->links = tz_link;
		}
	      else if (tzd_raw->link_count == 1)
		{
		  assert (i == 0);
		  free (tzd_raw->links);
		  tzd_raw->links = NULL;
		}
	      tzd_raw->link_count--;
	      /* the element at index i changed, i needs to be decreased so the next loop will analyze the new link at
	       * index i */
	      if (i > 0)
		{
		  i--;
		}
	    }
	}
    }

exit:
  return err_status;
}

/*
 * tzc_sort_raw_data () - perform some sorting on the information stored
 *			 inside the parameter of the TZ_RAW_DATA type
 * Returns:
 * tzd_raw(in/out): time zone bulk data structure to sort
 *
 * NOTE: tz_optimize_raw_data should be called immediately after or at the end
 *	 of the bulk data loader (timezone_load_data) in order to optimize the
 *	 loaded information for further operations. The most important
 *	 operation that requires optimized data is checking for differences
 *	 when compiling the timezone data library using another version of
 *	 IANA's timezone DB (newer or older) and properly identifying those
 *	 differences.
 */
static void
tzc_sort_raw_data (TZ_RAW_DATA * tzd_raw)
{
  int i;
  TZ_RAW_DS_RULESET *rs = NULL;

  /* sort countries by name */
  qsort (tzd_raw->countries, tzd_raw->country_count, sizeof (TZ_RAW_COUNTRY), comp_func_raw_countries);

  /* sort zones by name */
  qsort (tzd_raw->zones, tzd_raw->zone_count, sizeof (TZ_RAW_ZONE_INFO), comp_func_raw_zones);

  /* sort zone aliases by name (aliases = links & backward links) */
  qsort (tzd_raw->links, tzd_raw->link_count, sizeof (TZ_RAW_LINK), comp_func_raw_links);

  /* sort offset rules by date */
  for (i = 0; i < tzd_raw->zone_count; i++)
    {
      qsort (tzd_raw->zones[i].offset_rules, tzd_raw->zones[i].offset_rule_count, sizeof (TZ_RAW_OFFSET_RULE),
	     comp_func_raw_offset_rules);
    }

  /* sort DS rulesets by name */
  qsort (tzd_raw->ds_rulesets, tzd_raw->ruleset_count, sizeof (TZ_RAW_DS_RULESET), comp_func_raw_ds_rulesets);

  /* sort DS rules (in each set) by starting year */
  for (i = 0; i < tzd_raw->ruleset_count; i++)
    {
      rs = &(tzd_raw->ds_rulesets[i]);
      qsort (rs->rules, rs->rule_count, sizeof (TZ_RAW_DS_RULE), comp_func_raw_ds_rules);
    }
}

/*
 * tzc_index_raw_data - index the information identified as being higher in
 *		      the hierarchy of the raw timezone data
 * Returns:
 * tzd_raw(in): raw time zone data to process
 */
static void
tzc_index_raw_data (TZ_RAW_DATA * tzd_raw)
{
  int i;

  /* countries should have been already sorted with tz_sort_raw_data */
  for (i = 0; i < tzd_raw->country_count; i++)
    {
      tzd_raw->countries[i].id = i;
      tzd_raw->countries[i].is_used = true;
    }
  for (i = 0; i < tzd_raw->zone_count; i++)
    {
      tzd_raw->zones[i].id = i;
      tzd_raw->zones[i].clone_of_id = -1;
    }
}

/*
 * tzc_index_raw_data_w_static - index the information identified as being
 *		      higher in the hierarchy of the raw timezone data with
 *		      respect to the hardcoded ID arrays from timezone_list.c
 * Returns: 0 (NO_ERROR) if success, error code if something goes wrong
 * tzd_raw(in/out): raw time zone data to process
 * mode(in): control flag (type of activity to perform)
 *
 * NOTE: call this after tz_index_raw_data (which sorts the important data)
 */
static int
tzc_index_raw_data_w_static (TZ_RAW_DATA * tzd_raw, const TZ_GEN_TYPE mode)
{
  int i, cur_id;
  int err_status = NO_ERROR;

  /* set all IDs to -1, and is_used to FALSE (where needed) */
  for (i = 0; i < tzd_raw->country_count; i++)
    {
      tzd_raw->countries[i].id = -1;
      tzd_raw->countries[i].is_used = false;	/* explicitly initialize all */
    }
  for (i = 0; i < tzd_raw->zone_count; i++)
    {
      tzd_raw->zones[i].id = -1;
      tzd_raw->zones[i].clone_of_id = -1;
    }
  for (i = 0; i < tzd_raw->ruleset_count; i++)
    {
      tzd_raw->ds_rulesets[i].is_used = false;	/* explicitly initialize all */
    }

  /* implementation */
  /* compute country IDs */
  cur_id = -1;
  for (i = 0; i < tzd_raw->country_count; i++)
    {
      if (cur_id < tzd_raw->countries[i].id)
	{
	  cur_id = tzd_raw->countries[i].id;
	}
    }
  for (i = 0; i < tzd_raw->country_count; i++)
    {
      if (tzd_raw->countries[i].id == -1)
	{
	  tzd_raw->countries[i].id = ++cur_id;
	}
    }

  /* compute timezone IDs, with respect to the existing tz ID list */
  cur_id = -1;
  for (i = 0; i < tzd_raw->zone_count; i++)
    {
      if (cur_id < tzd_raw->zones[i].id)
	{
	  cur_id = tzd_raw->zones[i].id;
	}
    }
  /* leave IDs to -1 for zones not found in the predefined TIMEZONES array */
  for (i = 0; i < tzd_raw->zone_count; i++)
    {
      if (tzd_raw->zones[i].id == -1)
	{
	  tzd_raw->zones[i].id = ++cur_id;
	}
    }

  return err_status;
}

/*
 * tzc_index_raw_subdata - compute indexes for rulesets and update ruleset_id
 *			  for offset rules (e.g. index the data not processed
 *			  by tzc_index_raw_data/tzc_index_raw_data_w_static)
 * Returns: 0 (NO_ERROR) if success, error code if something goes wrong
 * tzd_raw(in): raw time zone data to process
 * mode(in): control flag (type of activity to perform)
 *
 * NOTE: call this after tz_index_raw_data / tz_index_raw_subdata
 */
static int
tzc_index_raw_subdata (TZ_RAW_DATA * tzd_raw, const TZ_GEN_TYPE mode)
{
  int i, j, found_id;
  int err_status = NO_ERROR;
  char *alias = NULL;
  char **temp_aliases = NULL;

  /* compute country_id for all timezones */
  for (i = 0; i < tzd_raw->zone_count; i++)
    {
      found_id = -1;
      for (j = 0; j < tzd_raw->country_count; j++)
	{
	  if (!tzd_raw->countries[j].is_used)
	    {
	      continue;
	    }
	  if (strcmp (tzd_raw->zones[i].code, tzd_raw->countries[j].code) == 0)
	    {
	      found_id = tzd_raw->countries[j].id;
	      break;
	    }
	}

      tzd_raw->zones[i].country_id = found_id;
    }

  /* Match aliases to the corresponding timezones */
  for (i = 0; i < tzd_raw->link_count; i++)
    {
      TZ_RAW_ZONE_INFO *zone = NULL;
      TZ_RAW_LINK *link = &(tzd_raw->links[i]);
      /* search for the corresponding timezone */
      found_id = -1;
      alias = NULL;

      for (j = 0; j < tzd_raw->zone_count && alias == NULL; j++)
	{
	  zone = &(tzd_raw->zones[j]);
	  if (strcmp (link->alias, zone->full_name) == 0)
	    {
	      alias = link->name;
	      break;
	    }
	  else if (strcmp (link->name, zone->full_name) == 0)
	    {
	      alias = link->alias;
	      break;
	    }
	}

      assert (alias != NULL);

      /* put the found alias into the corresponding TZ_RAW_ZONE_INFO */
      temp_aliases = (char **) realloc (zone->aliases, (zone->alias_count + 1) * sizeof (char *));

      if (temp_aliases == NULL)
	{
	  char err_msg[TZC_ERR_MSG_MAX_SIZE];

	  sprintf (err_msg, "%d", zone->alias_count + 1);
	  err_status = TZC_ERR_OUT_OF_MEMORY;
	  TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_OUT_OF_MEMORY, err_msg, "char *");
	  goto exit;
	}

      zone->aliases = temp_aliases;
      zone->aliases[zone->alias_count] = strdup (alias);

      if (zone->aliases[zone->alias_count] == NULL)
	{
	  char err_msg[TZC_ERR_MSG_MAX_SIZE];

	  sprintf (err_msg, "%d", zone->alias_count + 1);
	  err_status = TZC_ERR_OUT_OF_MEMORY;
	  TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_OUT_OF_MEMORY, err_msg, "char *");
	  goto exit;
	}

      zone->alias_count++;
    }

  /* for timezones which clone other time zone settings, compute cloned ID */
  for (i = 0; i < tzd_raw->zone_count; i++)
    {
      TZ_RAW_ZONE_INFO *zone = NULL;
      TZ_RAW_ZONE_INFO *zone_search = NULL;
      zone = &(tzd_raw->zones[i]);
      if (strlen (zone->clone_of) == 0)
	{
	  continue;
	}
      /* search for the zone with the name zone->clone_of, and get its ID */
      for (j = 0; j < tzd_raw->zone_count; j++)
	{
	  zone_search = &(tzd_raw->zones[j]);
	  if (strcmp (zone->clone_of, zone_search->full_name) == 0)
	    {
	      zone->clone_of_id = zone_search->id;
	    }
	}
    }

  if (mode == TZ_GEN_TYPE_UPDATE)
    {
      /* TODO: if a zone is renamed, create/update an alias for it, to keep the old name found in the predefined
       * TIMEZONE array */
      assert (false);
    }
  else
    {
      assert (mode == TZ_GEN_TYPE_NEW || mode == TZ_GEN_TYPE_EXTEND);
      /* do nothing */
    }

exit:
  return err_status;
}

static int
compare_ints (const void *a, const void *b)
{
  if (*((int *) a) == *((int *) b))
    {
      return 0;
    }
  else if (*((int *) a) < *((int *) b))
    {
      return -1;
    }

  return 1;
}

/*
 * tzc_check_ds_ruleset - Checks the validity of the daylight saving time
 *			  ruleset
 * tzd(in): timezone data
 * ds_rule_set(in): day-light saving time ruleset
 * ds_changes_cnt(out): total number of daylight saving time changes between
 *                      the start year and the end year
 * Return: error or no error
 */
static int
tzc_check_ds_ruleset (const TZ_DATA * tzd, const TZ_DS_RULESET * ds_rule_set, int *ds_changes_cnt)
{
#define ABS(i) ((i) >= 0 ? (i) : -(i))
#define MAX_DS_CHANGES_YEAR 1000
  int start_year, end_year, year;
  int i;
  int all_ds_changes_julian_date[MAX_DS_CHANGES_YEAR];
  int last_ds_change_julian_date;

  start_year = TZ_MAX_YEAR;
  end_year = 0;
  *ds_changes_cnt = 0;

  for (i = 0; i < ds_rule_set->count; i++)
    {
      TZ_DS_RULE *ds_rule;

      ds_rule = &(tzd->ds_rules[i + ds_rule_set->index_start]);

      if (ds_rule->from_year < start_year)
	{
	  start_year = ds_rule->from_year;
	}

      if (ds_rule->to_year > end_year && ds_rule->to_year != TZ_MAX_YEAR)
	{
	  end_year = ds_rule->to_year;
	}
    }

  if (end_year == 0)
    {
      end_year = start_year;
    }
  end_year = end_year + 1;

  if (end_year < 2038)
    {
      end_year = 2038;
    }

  /* check how many times a DS change occurs in a year */
  for (year = start_year; year < end_year; year++)
    {
      int count = 0;

      for (i = 0; i < ds_rule_set->count; i++)
	{
	  TZ_DS_RULE *ds_rule;
	  int ds_change_julian_date, first_day_year_julian, last_day_year_julian;

	  ds_rule = &(tzd->ds_rules[i + ds_rule_set->index_start]);

	  if (year >= ds_rule->from_year && year <= ds_rule->to_year)
	    {
	      count++;
	    }

	  if (year + 1 < ds_rule->from_year || year - 1 > ds_rule->to_year)
	    {
	      continue;
	    }

	  if ((year < ds_rule->from_year && ds_rule->in_month > TZ_MON_JAN)
	      || (year > ds_rule->to_year && ds_rule->in_month < TZ_MON_DEC))
	    {
	      continue;
	    }

	  if (tz_get_ds_change_julian_date_diff (0, ds_rule, year, &ds_change_julian_date, NULL) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }

	  assert (*ds_changes_cnt < MAX_DS_CHANGES_YEAR);
	  all_ds_changes_julian_date[(*ds_changes_cnt)++] = ds_change_julian_date;

	  first_day_year_julian = julian_encode (1, 1, year);
	  last_day_year_julian = julian_encode (31, 12, year);

	  if (ABS (ds_change_julian_date - first_day_year_julian) <= 1
	      || ABS (ds_change_julian_date - last_day_year_julian) <= 1)
	    {
	      printf ("DS ruleset: %s, Year: %d, Change on : %s \n", ds_rule_set->ruleset_name, year,
		      MONTH_NAMES_ABBREV[ds_rule->in_month]);
	    }
	}

      if (count > 2)
	{
	  printf ("DS ruleset: %s, Year: %d, found %d matching rules\n", ds_rule_set->ruleset_name, year, count);
	}
    }

  qsort (all_ds_changes_julian_date, *ds_changes_cnt, sizeof (int), compare_ints);
  last_ds_change_julian_date = all_ds_changes_julian_date[0];
  for (i = 1; i < *ds_changes_cnt; i++)
    {
      int date_diff;
      int month1, day1, year1;
      int month2, day2, year2;

      date_diff = all_ds_changes_julian_date[i] - last_ds_change_julian_date;
      assert (date_diff > 0);

      if (date_diff < 30)
	{
	  julian_decode (last_ds_change_julian_date, &month1, &day1, &year1, NULL);
	  julian_decode (all_ds_changes_julian_date[i], &month2, &day2, &year2, NULL);
	  printf ("DS ruleset: %s, DS change after %d days, Date1: %d-%d-%d," "Date1: %d-%d-%d\n",
		  ds_rule_set->ruleset_name, date_diff, day1, month1, year1, day2, month2, year2);
	}
    }

  printf ("Ruleset %s , total changes : %d (%d - %d)\n", ds_rule_set->ruleset_name, *ds_changes_cnt, start_year,
	  end_year);
  return NO_ERROR;
#undef ABS
}

/*
 * tzc_compile_data - process the raw data loaded from IANA's timezone
 *		      database files and put it into a TZ_DATA structure
 *		      to be later written into a compilable C file
 *
 * Returns: 0 (NO_ERROR) if success, error code if failure
 * tzd_raw(in): raw data to process
 * tzd(out): variable where to store the processed data
 */
static int
tzc_compile_data (TZ_RAW_DATA * tzd_raw, TZ_DATA * tzd)
{
  int i, j, err_status = NO_ERROR;
  int alias_count, offset_rule_id;

  assert (tzd != NULL);

  /* countries */
  assert (tzd->countries == NULL);
  tzd->countries = (TZ_COUNTRY *) malloc (tzd_raw->country_count * sizeof (tzd->countries[0]));
  if (tzd->countries == NULL)
    {
      char err_msg[TZC_ERR_MSG_MAX_SIZE];

      sprintf (err_msg, "%d", tzd_raw->country_count);
      err_status = TZC_ERR_OUT_OF_MEMORY;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_OUT_OF_MEMORY, err_msg, "TZ_COUNTRY");
      goto exit;
    }
  tzd->country_count = tzd_raw->country_count;
  memset (tzd->countries, 0, tzd->country_count * sizeof (tzd->countries[0]));
  for (i = 0; i < tzd->country_count; i++)
    {
      TZ_COUNTRY *tz_country;

      assert (tzd_raw->countries[i].id == i);
      tz_country = &(tzd->countries[i]);

      strcpy (tz_country->code, tzd_raw->countries[i].code);
      strcpy (tz_country->full_name, tzd_raw->countries[i].full_name);
    }

  err_status = tzc_compile_ds_rules (tzd_raw, tzd);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

  /* timezones and associated offset rules */
  tzd->timezones = NULL;
  tzd->timezone_names = NULL;
  tzd->names = NULL;
  tzd->timezone_count = tzd_raw->zone_count;

  tzd->timezones = (TZ_TIMEZONE *) malloc (tzd->timezone_count * sizeof (tzd->timezones[0]));
  if (tzd->timezones == NULL)
    {
      char err_msg[TZC_ERR_MSG_MAX_SIZE];

      sprintf (err_msg, "%d", tzd->timezone_count);
      err_status = TZC_ERR_OUT_OF_MEMORY;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_OUT_OF_MEMORY, err_msg, "TZ_TIMEZONE");
      goto exit;
    }
  memset (tzd->timezones, 0, tzd->timezone_count * sizeof (tzd->timezones[0]));

  /* count total number of offset rules */
  for (i = 0; i < tzd_raw->zone_count; i++)
    {
      tzd->offset_rule_count += tzd_raw->zones[i].offset_rule_count;
    }

  tzd->offset_rules = (TZ_OFFSET_RULE *) malloc (tzd->offset_rule_count * sizeof (tzd->offset_rules[0]));
  if (tzd->offset_rules == NULL)
    {
      char err_msg[TZC_ERR_MSG_MAX_SIZE];

      sprintf (err_msg, "%d", tzd->offset_rule_count);
      err_status = TZC_ERR_OUT_OF_MEMORY;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_OUT_OF_MEMORY, err_msg, "TZ_OFFSET_RULE");
      goto exit;
    }
  offset_rule_id = 0;

  memset (tzd->offset_rules, 0, tzd->offset_rule_count * sizeof (tzd->offset_rules[0]));

  for (i = 0; i < tzd_raw->zone_count; i++)
    {
      TZ_TIMEZONE *tz_zone = &(tzd->timezones[tzd_raw->zones[i].id]);
      TZ_RAW_ZONE_INFO *tz_raw_zone = &(tzd_raw->zones[i]);

      tz_zone->country_id = tz_raw_zone->country_id;
      tz_zone->zone_id = tz_raw_zone->id;

      tz_zone->gmt_off_rule_start = offset_rule_id;
      tz_zone->gmt_off_rule_count = tz_raw_zone->offset_rule_count;

      if (tz_zone->gmt_off_rule_count == 0)
	{
	  /* if zone is an alias continue */
	  if (tz_raw_zone->clone_of_id != -1)
	    {
	      continue;
	    }
	  /* else stop building the timezone library */
	  else
	    {
	      err_status = TZC_ERR_INVALID_ZONE;
	      TZC_LOG_ERROR_1ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_INVALID_ZONE, "Empty zone");
	      goto exit;
	    }
	}

      for (j = 0; j < tz_zone->gmt_off_rule_count; j++)
	{
	  TZ_OFFSET_RULE *offrule = &(tzd->offset_rules[offset_rule_id]);
	  TZ_RAW_OFFSET_RULE *raw_offrule = &(tz_raw_zone->offset_rules[j]);

	  if (strstr (raw_offrule->format, "%s") != NULL)
	    {
	      offrule->var_format = strdup (raw_offrule->format);
	      if (offrule->var_format == NULL)
		{
		  char err_msg[TZC_ERR_MSG_MAX_SIZE];

		  sprintf (err_msg, "%d", strlen (raw_offrule->format));
		  err_status = TZC_ERR_OUT_OF_MEMORY;
		  TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_OUT_OF_MEMORY, err_msg, "char");
		  goto exit;
		}
	      offrule->std_format = NULL;
	      offrule->save_format = NULL;
	    }
	  else
	    {
	      char *save_format;

	      save_format = strchr (raw_offrule->format, '/');
	      /* already checked by TZ parser */
	      if (save_format != NULL)
		{
		  *save_format = '\0';
		  save_format++;
		}

	      offrule->std_format = strdup (raw_offrule->format);
	      if (offrule->std_format == NULL)
		{
		  char err_msg[TZC_ERR_MSG_MAX_SIZE];

		  sprintf (err_msg, "%d", strlen (raw_offrule->format));
		  err_status = TZC_ERR_OUT_OF_MEMORY;
		  TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_OUT_OF_MEMORY, err_msg, "char");
		  goto exit;
		}

	      if (save_format != NULL)
		{
		  offrule->save_format = strdup (save_format);
		  if (offrule->save_format == NULL)
		    {
		      char err_msg[TZC_ERR_MSG_MAX_SIZE];

		      sprintf (err_msg, "%d", strlen (save_format));
		      err_status = TZC_ERR_OUT_OF_MEMORY;
		      TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_OUT_OF_MEMORY, err_msg, "char");
		      goto exit;
		    }
		}
	      else
		{
		  offrule->save_format = NULL;
		}

	      offrule->var_format = NULL;
	    }

	  offrule->gmt_off = raw_offrule->gmt_off;
	  offrule->until_flag = raw_offrule->until_flag;
	  offrule->until_year = raw_offrule->until_year;
	  offrule->until_mon = raw_offrule->until_mon;
	  offrule->until_day = raw_offrule->until_day;
	  offrule->until_hour = raw_offrule->until_hour;
	  offrule->until_min = raw_offrule->until_min;
	  offrule->until_sec = raw_offrule->until_sec;
	  offrule->until_time_type = raw_offrule->until_time_type;

	  /* seek ds ruleset metadata using string identifier raw-offrule->rules */
	  offrule->ds_ruleset =
	    tzc_get_ds_ruleset_by_name (tzd->ds_rulesets, tzd->ds_ruleset_count, raw_offrule->ds_ruleset_name);
	  if (offrule->ds_ruleset == -1)
	    {
	      const char *dummy = NULL;
	      /* raw_offrule->rules is not an identifier of a ruleset; check if it is '-' or time offset */
	      offrule->ds_type = DS_TYPE_FIXED;
	      if (strcmp (raw_offrule->ds_ruleset_name, "-") == 0)
		{
		  offrule->ds_ruleset = 0;
		}
	      else if (tz_str_to_seconds (raw_offrule->ds_ruleset_name,
					  raw_offrule->ds_ruleset_name + strlen (raw_offrule->ds_ruleset_name),
					  &(offrule->ds_ruleset), &dummy, false) != NO_ERROR)
		{
		  err_status = TZC_ERR_INVALID_TIME;
		  goto exit;
		}
	    }
	  else
	    {
	      offrule->ds_type = DS_TYPE_RULESET_ID;
	    }
	  offset_rule_id++;
	}
    }

  /* check cloned zones and properly set their offset rule references; NOTE: this can be done only after exporting all
   * zones and offset rules */
  for (i = 0; i < tzd_raw->zone_count; i++)
    {
      TZ_TIMEZONE *tz_zone_clone = &(tzd->timezones[tzd_raw->zones[i].id]);
      TZ_TIMEZONE *tz_zone_original = NULL;
      TZ_RAW_ZONE_INFO *tz_raw_zone = &(tzd_raw->zones[i]);

      if (tz_raw_zone->clone_of_id == -1)
	{
	  continue;
	}

      tz_zone_original = &(tzd->timezones[tz_raw_zone->clone_of_id]);
      tz_zone_clone->gmt_off_rule_start = tz_zone_original->gmt_off_rule_start;
      tz_zone_clone->gmt_off_rule_count = tz_zone_original->gmt_off_rule_count;
    }

  /* put aliases & timezone names into sorted arrays */

  /* build timezone_names */
  tzd->timezone_names = (char **) malloc (tzd->timezone_count * sizeof (tzd->timezone_names[0]));
  if (tzd->timezone_names == NULL)
    {
      char err_msg[TZC_ERR_MSG_MAX_SIZE];

      sprintf (err_msg, "%d", tzd->timezone_count);
      err_status = TZC_ERR_OUT_OF_MEMORY;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_OUT_OF_MEMORY, err_msg, "char *");
      goto exit;
    }
  memset (tzd->timezone_names, 0, tzd->timezone_count * sizeof (tzd->timezone_names[0]));
  for (i = 0; i < tzd_raw->zone_count; i++)
    {
      int zone_id = tzd_raw->zones[i].id;
      assert (!IS_EMPTY_STR (tzd_raw->zones[i].full_name));
      tzd->timezone_names[zone_id] = strdup (tzd_raw->zones[i].full_name);
      if (tzd->timezone_names[zone_id] == NULL)
	{
	  char err_msg[TZC_ERR_MSG_MAX_SIZE];

	  sprintf (err_msg, "%d", strlen (tzd_raw->zones[i].full_name));
	  err_status = TZC_ERR_OUT_OF_MEMORY;
	  TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_OUT_OF_MEMORY, err_msg, "char");
	  goto exit;
	}
    }
  /* build tzd->names */
  alias_count = 0;
  for (i = 0; i < tzd_raw->zone_count; i++)
    {
      alias_count += tzd_raw->zones[i].alias_count;
    }
  tzd->name_count = alias_count + tzd->timezone_count;
  tzd->names = (TZ_NAME *) malloc (tzd->name_count * sizeof (TZ_NAME));
  if (tzd->names == NULL)
    {
      char err_msg[TZC_ERR_MSG_MAX_SIZE];

      sprintf (err_msg, "%d", tzd->name_count);
      err_status = TZC_ERR_OUT_OF_MEMORY;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_OUT_OF_MEMORY, err_msg, "TZ_NAME");
      goto exit;
    }
  memset (tzd->names, 0, tzd->name_count * sizeof (tzd->names[0]));
  /* put timezone names first */
  for (i = 0; i < tzd->timezone_count; i++)
    {
      tzd->names[i].is_alias = 0;
      assert (tzd->timezones[i].zone_id == i);
      tzd->names[i].zone_id = i;
      tzd->names[i].name = strdup (tzd->timezone_names[i]);
      if (tzd->names[i].name == NULL)
	{
	  char err_msg[TZC_ERR_MSG_MAX_SIZE];

	  sprintf (err_msg, "%d", strlen (tzd->timezone_names[i]));
	  err_status = TZC_ERR_OUT_OF_MEMORY;
	  TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_OUT_OF_MEMORY, err_msg, "char");
	  goto exit;
	}
    }

  /* put aliases */
  offset_rule_id = tzd->timezone_count;
  for (i = 0; i < tzd_raw->zone_count; i++)
    {
      TZ_RAW_ZONE_INFO *tz_raw_zone = &(tzd_raw->zones[i]);
      if (tz_raw_zone->alias_count == 0)
	{
	  continue;
	}
      for (j = 0; j < tz_raw_zone->alias_count; j++)
	{
	  tzd->names[offset_rule_id].is_alias = 1;
	  assert (tz_raw_zone->id != -1);
	  tzd->names[offset_rule_id].zone_id = tz_raw_zone->id;
	  assert (!IS_EMPTY_STR (tz_raw_zone->aliases[j]));
	  tzd->names[offset_rule_id].name = strdup (tz_raw_zone->aliases[j]);
	  if (tzd->names[offset_rule_id].name == NULL)
	    {
	      char err_msg[TZC_ERR_MSG_MAX_SIZE];

	      sprintf (err_msg, "%d", strlen (tz_raw_zone->aliases[j]));
	      err_status = TZC_ERR_OUT_OF_MEMORY;
	      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_OUT_OF_MEMORY, err_msg, "char");
	      goto exit;
	    }
	  offset_rule_id++;
	}
    }
  /* sort zone timezone names/aliases */
  qsort (tzd->names, tzd->name_count, sizeof (TZ_NAME), comp_func_tz_names);

  {
    int total_ds_changes = 0;
    int max_ds_changes = 0;
    for (i = 0; i < tzd->ds_ruleset_count; i++)
      {
	int ds_changes = 0;
	(void) tzc_check_ds_ruleset (tzd, &(tzd->ds_rulesets[i]), &ds_changes);

	if (ds_changes > max_ds_changes)
	  {
	    max_ds_changes = ds_changes;
	  }
	total_ds_changes += ds_changes;
      }
    printf ("Total DS changes: %d; maximum changes in a ruleset :%d\n", total_ds_changes, max_ds_changes);
  }

  /* build leap second list */
  tzd->ds_leap_sec_count = tzd_raw->leap_sec_count;
  tzd->ds_leap_sec = (TZ_LEAP_SEC *) malloc (tzd->ds_leap_sec_count * sizeof (tzd->ds_leap_sec[0]));

  if (tzd->ds_leap_sec == NULL)
    {
      char err_msg[TZC_ERR_MSG_MAX_SIZE];

      sprintf (err_msg, "%d", tzd->ds_leap_sec_count);
      err_status = TZC_ERR_OUT_OF_MEMORY;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_OUT_OF_MEMORY, err_msg, "TZ_DS_LEAP_SEC *");
      goto exit;
    }
  memcpy (tzd->ds_leap_sec, tzd_raw->leap_sec, tzd_raw->leap_sec_count * sizeof (TZ_LEAP_SEC));
exit:
  return err_status;
}

/*
 * tzc_compile_ds_rules() - take the raw daylight saving rules and place them
 *			  into the output TZ_DATA variable, from where they
 *			  will be later exported into a source file (to be
 *			  compiled into the timezone shared library/object)
 * Returns: 0(NO_ERROR) if success, error code otherwise
 * tzd_raw(in): raw data to process
 * tzd(out): variable where to store the processed data
 */
static int
tzc_compile_ds_rules (TZ_RAW_DATA * tzd_raw, TZ_DATA * tzd)
{
  int err_status = NO_ERROR;
  int i, j, cur_rule_index = 0;
  TZ_DS_RULESET *ruleset;
  TZ_DS_RULE *rule;
  TZ_RAW_DS_RULE *rule_raw;

  assert (tzd->ds_rules == NULL);
  assert (tzd->ds_rulesets == NULL);

  /* compute total number of daylight saving rules */
  tzd->ds_rule_count = 0;
  for (i = 0; i < tzd_raw->ruleset_count; i++)
    {
      tzd->ds_rule_count += tzd_raw->ds_rulesets[i].rule_count;
    }

  tzd->ds_rules = (TZ_DS_RULE *) malloc (tzd->ds_rule_count * sizeof (TZ_DS_RULE));
  if (tzd->ds_rules == NULL)
    {
      char err_msg[TZC_ERR_MSG_MAX_SIZE];

      sprintf (err_msg, "%d", tzd->ds_rule_count);
      err_status = TZC_ERR_OUT_OF_MEMORY;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_OUT_OF_MEMORY, err_msg, "TZ_DS_RULE");
      goto exit;
    }

  /* alloc ruleset array */
  tzd->ds_ruleset_count = tzd_raw->ruleset_count;
  tzd->ds_rulesets = (TZ_DS_RULESET *) malloc (tzd->ds_ruleset_count * sizeof (TZ_DS_RULESET));
  if (tzd->ds_rulesets == NULL)
    {
      char err_msg[TZC_ERR_MSG_MAX_SIZE];

      sprintf (err_msg, "%d", tzd->ds_ruleset_count);
      err_status = TZC_ERR_OUT_OF_MEMORY;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_OUT_OF_MEMORY, err_msg, "TZ_DS_RULESET");
      goto exit;
    }

  memset (tzd->ds_rules, 0, tzd->ds_rule_count * sizeof (tzd->ds_rules[0]));
  memset (tzd->ds_rulesets, 0, tzd->ds_ruleset_count * sizeof (tzd->ds_rulesets[0]));

  for (i = 0; i < tzd->ds_ruleset_count; i++)
    {
      int to_year_max = 0;
      bool has_default_abbrev = true;
      const char *prev_letter_abbrev = NULL;

      ruleset = &(tzd->ds_rulesets[i]);
      ruleset->index_start = cur_rule_index;
      ruleset->count = tzd_raw->ds_rulesets[i].rule_count;
      ruleset->ruleset_name = strdup (tzd_raw->ds_rulesets[i].name);
      if (ruleset->ruleset_name == NULL)
	{
	  char err_msg[TZC_ERR_MSG_MAX_SIZE];

	  sprintf (err_msg, "%d", strlen (tzd_raw->ds_rulesets[i].name));
	  err_status = TZC_ERR_OUT_OF_MEMORY;
	  TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_OUT_OF_MEMORY, err_msg, "char");
	  goto exit;
	}

      for (j = 0; j < ruleset->count; j++)
	{
	  rule = &(tzd->ds_rules[cur_rule_index]);
	  rule_raw = &(tzd_raw->ds_rulesets[i].rules[j]);

	  rule->at_time = rule_raw->at_time;
	  rule->at_time_type = rule_raw->at_time_type;
	  memcpy (&(rule->change_on), &(rule_raw->change_on), sizeof (TZ_DS_CHANGE_ON));
	  rule->from_year = rule_raw->from_year;
	  rule->in_month = rule_raw->in_month;
	  rule->letter_abbrev = strdup (rule_raw->letter_abbrev);
	  if (rule->letter_abbrev == NULL)
	    {
	      char err_msg[TZC_ERR_MSG_MAX_SIZE];

	      sprintf (err_msg, "%d", strlen (rule_raw->letter_abbrev));
	      err_status = TZC_ERR_OUT_OF_MEMORY;
	      TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_OUT_OF_MEMORY, err_msg, "char");
	      goto exit;
	    }
	  if (has_default_abbrev == true && rule_raw->save_time == 0)
	    {
	      /* common single letter abbreviation having daylight save time zero (common standard time) */
	      if (strlen (rule->letter_abbrev) > 1
		  || (prev_letter_abbrev != NULL && strcmp (rule->letter_abbrev, prev_letter_abbrev) != 0))
		{
		  has_default_abbrev = false;
		}
	      else
		{
		  prev_letter_abbrev = (char *) rule->letter_abbrev;
		}
	    }

	  rule->save_time = rule_raw->save_time;
	  rule->to_year = rule_raw->to_year;
	  if (rule->to_year > to_year_max)
	    {
	      to_year_max = rule->to_year;
	    }

	  cur_rule_index++;
	}

      const char empty[2] = { '-', 0 };
      if (has_default_abbrev == true && prev_letter_abbrev != NULL)
	{
	  ruleset->default_abrev = strdup (prev_letter_abbrev);
	}
      else
	{
	  prev_letter_abbrev = empty;
	  ruleset->default_abrev = strdup (empty);
	}
      if (ruleset->default_abrev == NULL)
	{
	  char err_msg[TZC_ERR_MSG_MAX_SIZE];

	  sprintf (err_msg, "%d", strlen (prev_letter_abbrev));
	  err_status = TZC_ERR_OUT_OF_MEMORY;
	  TZC_LOG_ERROR_2ARG (TZC_CONTEXT (tzd_raw), TZC_ERR_OUT_OF_MEMORY, err_msg, "char");
	  goto exit;
	}
      ruleset->to_year_max = to_year_max;
    }

exit:
  return err_status;
}

/*
 * str_to_offset_rule_until() - parses a string representing the date (and
 *			maybe time) until an offset rule was in effect, and
 *			put the components into the corresponding members of
 *			a TZ_OFFSET_RULE output parameter
 * Returns: 0 (NO_ERROR) if success, negative code if an error occurs
 * offset_rule(out): the offset rule where to save the date/time parts
 * str(in): string to parse
 * NOTE: str may be in the following forms: empty/NULL, "1912", "1903 Mar",
 *	 "1979 Oct 14", "1975 Nov 25 2:00".
 */
static int
str_to_offset_rule_until (TZ_RAW_OFFSET_RULE * offset_rule, char *str)
{
  const char *str_cursor;
  const char *str_next;
  const char *str_end;
  int val_read = 0;
  int type = -1, day = -1, bound = -1;
  int hour = 0, min = 0, sec = 0;
  int err_status = NO_ERROR;

  assert (offset_rule != NULL);

  if (IS_EMPTY_STR (str))
    {
      offset_rule->until_flag = UNTIL_INFINITE;
      offset_rule->until_time_type = TZ_TIME_TYPE_LOCAL_WALL;
      return NO_ERROR;
    }

  str_end = str + strlen (str);

  str_cursor = strtok (str, " ");
  if (tz_str_read_number (str_cursor, str_end, true, false, &val_read, &str_next) != NO_ERROR && val_read > 0
      && val_read < TZ_MAX_YEAR)
    {
      err_status = TZC_ERR_CANT_READ_VALUE;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_CANT_READ_VALUE, "UNTIL (year)", str_cursor);
      goto exit;
    }
  else
    {
      offset_rule->until_year = (unsigned short) val_read;
      offset_rule->until_mon = 0;
      offset_rule->until_day = 0;
      offset_rule->until_hour = 0;
      offset_rule->until_min = 0;
      offset_rule->until_sec = 0;
    }

  offset_rule->until_flag = UNTIL_EXPLICIT;
  offset_rule->until_time_type = TZ_TIME_TYPE_LOCAL_WALL;

  /* read month */
  str_cursor = strtok (NULL, " ");
  if (IS_EMPTY_STR (str_cursor))
    {
      /* no more tokens; exit with NO_ERROR */
      return NO_ERROR;
    }
  if (str_month_to_int (str_cursor, &val_read, &str_next) != NO_ERROR)
    {
      err_status = TZC_ERR_CANT_READ_VALUE;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_CANT_READ_VALUE, "UNTIL month", str_cursor);
      goto exit;
    }
  if (val_read < TZ_MON_JAN || val_read > TZ_MON_DEC)
    {
      char temp_msg[TZC_ERR_MSG_MAX_SIZE] = { 0 };

      sprintf (temp_msg, "%d", val_read);
      err_status = TZC_ERR_INVALID_VALUE;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_INVALID_VALUE, "UNTIL month value", temp_msg);
      goto exit;
    }
  offset_rule->until_mon = (unsigned char) val_read;

  /* read day of month */
  str_cursor = strtok (NULL, " ");
  if (IS_EMPTY_STR (str_cursor))
    {
      /* no more tokens; exit with NO_ERROR */
      err_status = NO_ERROR;
      goto exit;
    }

  /* Some offset rules have the column UNTIL='1992 Sep lastSat 23:00' or '2012 Apr Sun>=1 4:00', instead of a fixed
   * date/time value. This is a special case, and needs to be transformed into a fixed date. */
  err_status = str_read_day_var (str_cursor, offset_rule->until_mon, &type, &day, &bound, &str_next);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }
  if (type == TZ_DS_TYPE_FIXED)
    {
      if (!tzc_is_valid_date (day, offset_rule->until_mon, offset_rule->until_year, offset_rule->until_year + 1))
	{
	  char temp_msg[TZC_ERR_MSG_MAX_SIZE] = { 0 };

	  sprintf (temp_msg, "Day: %d, Month: %d, Year: %d", day, offset_rule->until_mon, offset_rule->until_year);
	  err_status = TZC_ERR_DS_INVALID_DATE;
	  TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_DS_INVALID_DATE, "day of month (UNTIL)", temp_msg);
	  goto exit;
	}
    }
  else if (type == TZ_DS_TYPE_VAR_GREATER)
    {
      int month_day = tz_get_first_weekday_around_date (offset_rule->until_year,
							offset_rule->until_mon, day, bound,
							false);

      if (!tzc_is_valid_date (month_day, offset_rule->until_mon, offset_rule->until_year, offset_rule->until_year + 1))
	{
	  char temp_msg[TZC_ERR_MSG_MAX_SIZE] = { 0 };

	  sprintf (temp_msg, "Day: %d, Month: %d, Year: %d", month_day, offset_rule->until_mon,
		   offset_rule->until_year);
	  err_status = TZC_ERR_DS_INVALID_DATE;
	  TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_DS_INVALID_DATE, "day of month (UNTIL)", temp_msg);
	  goto exit;
	}

      day = month_day;
    }
  else if (type == TZ_DS_TYPE_VAR_SMALLER)
    {
      int month_day;

      if (bound > 27)
	{
	  int max_days_in_month;

	  if (offset_rule->until_mon == TZ_MON_FEB)
	    {
	      max_days_in_month = (IS_LEAP_YEAR (offset_rule->until_year) ? 29 : 28);
	    }
	  else
	    {
	      max_days_in_month = DAYS_IN_MONTH (offset_rule->until_mon);
	    }

	  bound = max_days_in_month - 1;
	}

      month_day = tz_get_first_weekday_around_date (offset_rule->until_year, offset_rule->until_mon, day, bound, true);

      if (!tzc_is_valid_date (month_day, offset_rule->until_mon, offset_rule->until_year, offset_rule->until_year + 1))
	{
	  char temp_msg[TZC_ERR_MSG_MAX_SIZE] = { 0 };

	  sprintf (temp_msg, "Day: %d, Month: %d, Year: %d", month_day, offset_rule->until_mon,
		   offset_rule->until_year);
	  err_status = TZC_ERR_DS_INVALID_DATE;
	  TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_DS_INVALID_DATE, "day of month (UNTIL)", temp_msg);
	  goto exit;
	}

      day = month_day;
    }
  else
    {
      assert (false);
      err_status = TZC_ERR_DS_INVALID_DATE;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_DS_INVALID_DATE, "value for UNTIL", str);
      goto exit;
    }
  offset_rule->until_day = (unsigned char) day;

  /* read time */
  str_cursor = strtok (NULL, " ");
  if (IS_EMPTY_STR (str_cursor))
    {
      /* no more tokens; exit with NO_ERROR */
      err_status = NO_ERROR;
      goto exit;
    }

  if (tz_str_read_time (str_cursor, str_end, false, false, &hour, &min, &sec, &str_next) != NO_ERROR)
    {
      char temp_msg[TZC_ERR_MSG_MAX_SIZE] = { 0 };

      sprintf (temp_msg, "[hour: %d, min: %d, sec: %d]", hour, min, sec);
      err_status = TZC_ERR_INVALID_TIME;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_INVALID_TIME, temp_msg, "UNTIL column");
      goto exit;
    }

  str_cursor = str_next;
  err_status = tzc_read_time_type (str_cursor, &str_cursor, &(offset_rule->until_time_type));
  if (err_status != NO_ERROR)
    {
      return err_status;
    }

  offset_rule->until_hour = (unsigned char) hour;
  offset_rule->until_min = (unsigned char) min;
  offset_rule->until_sec = (unsigned char) sec;

exit:
  return err_status;
}

/*
 * str_month_to_int() - get the corresponding integer value of a 3 letter
 *			month abbreviation
 * Returns: 0 (NO_ERROR) if success, error code otherwise
 * month(in): string to parse
 * month_num(out): numeric value for found month
 * str_next(out): char pointer to the remaining string after parsing month
 */
static int
str_month_to_int (const char *str_in, int *month_num, const char **str_next)
{
  int i;
  const char *str;

  assert (!IS_EMPTY_STR (str_in));

  str = str_in;

  /* strip leading spaces and tabs */
  while (*str != '\0' && char_isspace (*str))
    {
      str++;
    }

  if (strlen (str) < (int) sizeof (MONTH_NAMES_ABBREV[0]) - 1)
    {
      /* not enough characters to work with; exit with error */
      return TZC_ERR_INVALID_VALUE;
    }

  for (i = 0; i < TZ_MON_COUNT; i++)
    {
      if (strncasecmp (MONTH_NAMES_ABBREV[i], str, sizeof (MONTH_NAMES_ABBREV[0]) - 1) == 0)
	{
	  break;
	}
    }
  if (i >= TZ_MON_COUNT)
    {
      /* month abbreviation not valid, or an error occured */
      return TZC_ERR_INVALID_VALUE;
    }

  *month_num = i;
  *str_next = str + sizeof (MONTH_NAMES_ABBREV[0]) - 1;

  return NO_ERROR;
}

/*
 * str_day_to_int() - get the corresponding integer value of a 3 letter
 *		      week day abbreviation (0=Sunday, 6=Saturday)
 * Returns: negative code if error, 0 (NO_ERROR) if success
 * str_in(in): string to parse
 * day_num(out): numeric value for found day
 * str_next(out): char pointer to the remaining string after parsing day
 */
static int
str_day_to_int (const char *str_in, int *day_num, const char **str_next)
{
  int i;
  const char *str;
  int err_status = NO_ERROR;

  assert (!IS_EMPTY_STR (str_in));

  str = str_in;

  /* skip leading spaces and tabs */
  while (*str != '\0' && char_isspace (*str))
    {
      str++;
    }

  if (strlen (str) < (int) sizeof (DAY_NAMES_ABBREV[0]) - 1)
    {
      /* not enough characters to work with; exit with error */
      *day_num = -1;
      err_status = TZC_ERR_INVALID_VALUE;
      goto exit;
    }

  for (i = 0; i < TZ_WEEK_DAY_COUNT; i++)
    {
      if (strncasecmp (DAY_NAMES_ABBREV[i], str, sizeof (DAY_NAMES_ABBREV[0]) - 1) == 0)
	{
	  break;
	}
    }
  if (i >= TZ_WEEK_DAY_COUNT)
    {
      /* month abbreviation not valid, or an error occured; set day_num to -1 and exit with -1 */
      *day_num = -1;
      err_status = TZC_ERR_INVALID_VALUE;
      goto exit;
    }

  *day_num = i;
  *str_next = str + sizeof (DAY_NAMES_ABBREV[0]) - 1;

exit:

  return err_status;
}

/*
 * str_read_day_var() - parse the input string as a specification for a day of
 *			the month. The input string may be of the following
 *			forms:
 *			    '21' (e.g. a day of the month)
*			    'lastFri' (e.g. 'lastWEEKDAY')
*			    'Sun>=1' (e.g. WEEKDAY>=NUMBER)
*
 * Returns: 0(NO_ERROR) if success, error code otherwise
 * str(in): input string to parse
 * month(in): month in which this day is
 * type(out): type of bound (see enum TZ_DS_TYPE)
 * day(out): day value as numeric (0 based index value)
 * bound(out): numeric bound for day (0 based index value)
 * str_next(out): pointer to the remaining string after parsing the day rule
 */
static int
str_read_day_var (const char *str, const int month, int *type, int *day, int *bound, const char **str_next)
{
  int err_status = NO_ERROR;
  int day_num;
  char str_last[5] = "last";
  const char *str_cursor;
  const char *str_end;
  const int days_of_month[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

  assert (str != NULL);

  *str_next = str;

  str_end = str + strlen (str);

  /* initialize output parameters */
  *type = -1;
  *day = -1;
  *bound = -1;

  /* try reading a number */
  if (tz_str_read_number (str, str_end, false, false, &day_num, str_next) != NO_ERROR)
    {
      err_status = TZC_ERR_CANT_READ_VALUE;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_CANT_READ_VALUE, "day numeric", str);
      goto exit;
    }
  if (*str_next != str)
    {
      /* This is a fixed day of month, store it as such */
      *type = TZ_DS_TYPE_FIXED;
      *day = day_num - 1;
      goto exit;
    }

  /* no number was read; check if str starts with "last" */
  if (strncmp (str, str_last, strlen (str_last)) == 0)
    {
      str_cursor = str + strlen (str_last);
      if (str_day_to_int (str_cursor, &day_num, str_next) != NO_ERROR)
	{
	  err_status = TZC_ERR_CANT_READ_VALUE;
	  TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_CANT_READ_VALUE, "day string", str_cursor);
	  goto exit;
	}
      if (day_num < TZ_WEEK_DAY_SUN || day_num > TZ_WEEK_DAY_SAT)
	{
	  char temp_msg[TZC_ERR_MSG_MAX_SIZE] = { 0 };

	  sprintf (temp_msg, "%d", day_num);
	  err_status = TZC_ERR_INVALID_VALUE;
	  TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_INVALID_VALUE, "day string value", temp_msg);
	  goto exit;
	}
      *type = TZ_DS_TYPE_VAR_SMALLER;
      *day = day_num;
      /* last valid month day from 0 - 30 */
      *bound = days_of_month[month] - 1;

      goto exit;
    }

  /* string was not a number, nor "last<Weekday>"; therefore it must be something like Sun>=3 */
  str_cursor = str;
  if (str_day_to_int (str_cursor, &day_num, &str_cursor) != NO_ERROR)
    {
      err_status = TZC_ERR_CANT_READ_VALUE;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_CANT_READ_VALUE, "day string", str_cursor);
      goto exit;
    }
  assert (*(str_cursor + 1) == '=');
  if (*str_cursor == '>')
    {
      *type = TZ_DS_TYPE_VAR_GREATER;
    }
  else if (*str_cursor == '<')
    {
      *type = TZ_DS_TYPE_VAR_SMALLER;
    }
  else
    {
      assert (false);
      err_status = TZC_ERR_GENERIC;
      goto exit;
    }

  str_cursor += 2;		/* skip the '>=' operator */

  *day = day_num;
  if (tz_str_read_number (str_cursor, str_end, true, false, &day_num, &str_cursor) != NO_ERROR)
    {
      err_status = TZC_ERR_CANT_READ_VALUE;
      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_CANT_READ_VALUE, "day string", str_cursor);
      goto exit;
    }
  *bound = day_num - 1;

exit:
  return err_status;
}

/*
 * comp_func_raw_countries - comparison function between country entries, used
 *			     when optimizing TZ raw data
 * Returns: -1 if arg1<arg2, 0 if arg1 = arg2, 1 if arg1 > arg2
 * arg1(in): first value to compare
 * arg2(in): second value to compare
 * Note: comparing two TZ_RAW_COUNTRY values means comparing their full_name
 *	 members.
 */
static int
comp_func_raw_countries (const void *arg1, const void *arg2)
{
  TZ_RAW_COUNTRY *c1, *c2;

  assert (arg1 != NULL && arg2 != NULL);

  c1 = (TZ_RAW_COUNTRY *) arg1;
  c2 = (TZ_RAW_COUNTRY *) arg2;

  assert (!IS_EMPTY_STR (c1->full_name) && !IS_EMPTY_STR (c2->full_name));

  if (c1->id == -1 && c2->id == -1)
    {
      return strcmp (c1->full_name, c2->full_name);
    }

  assert (c1->id != c2->id);

  if (c1->id != -1 && c2->id != -1)
    {
      return (c1->id < c2->id) ? -1 : 1;
    }

  if (c1->id != -1)
    {
      return -1;
    }

  return 1;
}

/*
 * comp_func_raw_zones - comparison function between zone entries, used when
 *			 optimizing TZ raw data
 * Returns: -1 if arg1 < arg2, 0 if arg1 = arg2, 1 if arg1 > arg2
 * arg1(in): first value to compare
 * arg2(in): second value to compare
 * Note: comparing two TZ_RAW_ZONE_INFO values means comparing their full_name
 *	 members.
 */
static int
comp_func_raw_zones (const void *arg1, const void *arg2)
{
  TZ_RAW_ZONE_INFO *zone1, *zone2;

  assert (arg1 != NULL && arg2 != NULL);

  zone1 = (TZ_RAW_ZONE_INFO *) arg1;
  zone2 = (TZ_RAW_ZONE_INFO *) arg2;

  assert (!IS_EMPTY_STR (zone1->full_name) && !IS_EMPTY_STR (zone2->full_name));

  if (zone1->id == -1 && zone2->id == -1)
    {
      return strcmp (zone1->full_name, zone2->full_name);
    }

  assert (zone1->id != zone2->id);

  if (zone1->id != -1 && zone2->id != -1)
    {
      return (zone1->id < zone2->id) ? -1 : 1;
    }

  if (zone1->id > -1)
    {
      return -1;
    }

  return 1;
}

/*
 * comp_func_raw_links - comparison function between link entries, used when
 *			 optimizing TZ raw data
 * Returns: -1 if arg1 < arg2, 0 if arg1 = arg2, 1 if arg1 > arg2
 * arg1(in): first value to compare
 * arg2(in): second value to compare
 * Note: comparing two TZ_RAW_ZONE_INFO values means comparing their alias
 *	 members. The TZ links need to be ordered by alias, not by the
 *	 full_name of the corresponding timezone.
 */
static int
comp_func_raw_links (const void *arg1, const void *arg2)
{
  TZ_RAW_LINK *link1, *link2;

  assert (arg1 != NULL && arg2 != NULL);

  link1 = (TZ_RAW_LINK *) arg1;
  link2 = (TZ_RAW_LINK *) arg2;

  assert (!IS_EMPTY_STR (link1->alias) && !IS_EMPTY_STR (link2->alias));

  return strcmp (link1->alias, link2->alias);
}

/*
 * comp_func_raw_offset_rules - comparison function between offset rules, used
 *				when optimizing TZ raw data
 * Returns: -1 if arg1 < arg2, 0 if arg1 = arg2, 1 if arg1 > arg2
 * arg1(in): first value to compare
 * arg2(in): second value to compare
 * Note: comparing two TZ_RAW_OFFSET_RULE values means comparing their ending
 *	 datetime value.
 */
static int
comp_func_raw_offset_rules (const void *arg1, const void *arg2)
{
  TZ_RAW_OFFSET_RULE *rule1, *rule2;
  int r1_until, r2_until;

  assert (arg1 != NULL && arg2 != NULL);

  rule1 = (TZ_RAW_OFFSET_RULE *) arg1;
  rule2 = (TZ_RAW_OFFSET_RULE *) arg2;

  if (rule1->until_flag == UNTIL_INFINITE)
    {
      assert (rule2->until_flag != UNTIL_INFINITE);
      return 1;
    }
  else if (rule2->until_flag == UNTIL_INFINITE)
    {
      return -1;
    }

  r1_until = julian_encode (rule1->until_mon, rule1->until_day, rule1->until_year);
  r2_until = julian_encode (rule2->until_mon, rule2->until_day, rule2->until_year);

  if (r1_until == r2_until)
    {
      /* both dates are equal; compare time (reuse r1_until and r2_until we should not have two offset changes in the
       * same date */
      assert (false);

      r1_until = (rule1->until_hour * 60 + rule1->until_min) * 60 + rule1->until_sec;
      r2_until = (rule2->until_hour * 60 + rule2->until_min) * 60 + rule2->until_sec;
    }

  if (r1_until < r2_until)
    {
      return -1;
    }
  else if (r1_until > r2_until)
    {
      return 1;
    }
  assert (false);		/* can't have two time-overlapping offset rules */

  return 0;
}

/*
 * comp_func_raw_ds_rulesets - comparison function between daylight saving
 *			  rulesets, used when optimizing TZ raw data.
 * Returns: -1 if arg1 < arg2, 0 if arg1 = arg2, 1 if arg1 > arg2
 * arg1(in): first value to compare
 * arg2(in): second value to compare
 * Note: comparing two TZ_RAW_DS_RULE_SET values means comparing their name
 *	 members.
 */
static int
comp_func_raw_ds_rulesets (const void *arg1, const void *arg2)
{
  TZ_RAW_DS_RULESET *rs1, *rs2;

  assert (arg1 != NULL && arg2 != NULL);

  rs1 = (TZ_RAW_DS_RULESET *) arg1;
  rs2 = (TZ_RAW_DS_RULESET *) arg2;

  assert (!IS_EMPTY_STR (rs1->name) && !IS_EMPTY_STR (rs2->name));

  return strcmp (rs1->name, rs2->name);
}

/*
 * get_day_of_week_for_raw_rule - Returns the day in which the ds_rule applies
 *
 * Returns: the day
 * rule(in): daylight saving rule
 * year(in): year in which to apply rule
 */
static int
get_day_of_week_for_raw_rule (const TZ_RAW_DS_RULE * rule, const int year)
{
  int ds_rule_day;
  int ds_rule_month = rule->in_month;

  if (rule->change_on.type == TZ_DS_TYPE_FIXED)
    {
      ds_rule_day = rule->change_on.day_of_month;
    }
  else
    {
      int ds_rule_weekday, day_month_bound;
      bool before = (rule->change_on.type == TZ_DS_TYPE_VAR_SMALLER) ? true : false;

      ds_rule_weekday = rule->change_on.day_of_week;
      day_month_bound = rule->change_on.day_of_month;

      ds_rule_day = tz_get_first_weekday_around_date (year, ds_rule_month, ds_rule_weekday, day_month_bound, before);
    }

  return ds_rule_day;
}

/*
 * comp_func_raw_ds_rules - comparison function between daylight saving
 *			  rules, used when optimizing TZ raw data.
 * Returns: -1 if arg1 < arg2, 0 if arg1 = arg2, 1 if arg1 > arg2
 * arg1(in): first value to compare
 * arg2(in): second value to compare
 * Note: comparing two TZ_RAW_DS_RULE values means comparing their starting
 *	 year.
 */
static int
comp_func_raw_ds_rules (const void *arg1, const void *arg2)
{
  TZ_RAW_DS_RULE *rule1, *rule2;
  int day1, day2;

  assert (arg1 != NULL && arg2 != NULL);

  rule1 = (TZ_RAW_DS_RULE *) arg1;
  rule2 = (TZ_RAW_DS_RULE *) arg2;

  if (rule1->from_year < rule2->from_year)
    {
      return -1;
    }
  else if (rule1->from_year > rule2->from_year)
    {
      return 1;
    }

  if (rule1->in_month != rule2->in_month)
    {
      return rule1->in_month < rule2->in_month ? -1 : 1;
    }

  day1 = get_day_of_week_for_raw_rule (rule1, rule1->from_year);
  day2 = get_day_of_week_for_raw_rule (rule2, rule2->from_year);

  return day1 < day2 ? -1 : 1;
}

/*
 * comp_func_tz_names - comparison function between two TZ_NAME values
 * Returns: -1 if arg1 < arg2, 0 if arg1 = arg2, 1 if arg1 > arg2
 * arg1(in): first value to compare
 * arg2(in): second value to compare
 */
static int
comp_func_tz_names (const void *arg1, const void *arg2)
{
  TZ_NAME *name1, *name2;

  assert (arg1 != NULL && arg2 != NULL);

  name1 = (TZ_NAME *) arg1;
  name2 = (TZ_NAME *) arg2;

  assert (!IS_EMPTY_STR (name1->name) && !IS_EMPTY_STR (name2->name));

  return strcmp (name1->name, name2->name);
}

/*
 * print_seconds_as_time_hms_var () - takes a signed number of seconds and
 *				prints it to stdout in [-]hh:mm[:ss] format
 * Returns:
 * seconds(in): number of seconds to print as hh:mm[:ss]
 */
static void
print_seconds_as_time_hms_var (int seconds)
{
  if (seconds < 0)
    {
      printf ("-");
      seconds = -seconds;
    }
  printf ("%02d", (int) (seconds / 3600));
  seconds %= 3600;
  printf (":%02d", (int) (seconds / 60));
  seconds %= 60;
  if (seconds > 0)
    {
      printf (":%02d", seconds);
    }
}

/*
 * tzc_get_timezones_dot_c_filepath () - get path to timezones.c file in the CUBRID install folder
 *
 * size (in)                       : maximum name size
 * timezones_dot_c_file_path (out) : output file path
 */
static void
tzc_get_timezones_dot_c_filepath (size_t size, char *timezones_dot_c_file_path)
{
  char tz_cub_path[PATH_MAX] = { 0 };

  envvar_cubrid_dir (tz_cub_path, sizeof (tz_cub_path));
  tzc_build_filepath (timezones_dot_c_file_path, size, tz_cub_path, PATH_PARTIAL_TIMEZONES_FILE);
}

/*
 * tzc_export_timezone_dot_c () - saves all timezone data into a C source
 *			      file to be later compiled into a shared library
 * Returns: always NO_ERROR
 * tzd(in): timezone data
 * tz_C_filepath(in): timezones.c file path
 */
static int
tzc_export_timezone_dot_c (const TZ_DATA * tzd, const char *timezones_dot_c_filepath)
{
  int err_status = NO_ERROR;
  TZ_OFFSET_RULE *offrule = NULL;
  int i;
  TZ_DS_RULE *rule = NULL;
  TZ_LEAP_SEC *leap_sec = NULL;
  FILE *fp;

  fp = fopen_ex (timezones_dot_c_filepath, "wt");
  if (fp == NULL)
    {
      err_status = TZC_ERR_GENERIC;
      goto exit;
    }

#if defined(WINDOWS)
  fprintf (fp, "#include <stdio.h>\n");
#else
  fprintf (fp, "#include <stddef.h>\n");
#endif
  fprintf (fp, "#include \"timezone_lib_common.h\"\n\n");

  /* countries */
  fprintf (fp, "%s const int tz_country_count = %d;\n", SHLIB_EXPORT_PREFIX, tzd->country_count);
  fprintf (fp, "%s const TZ_COUNTRY tz_countries[] = {\n", SHLIB_EXPORT_PREFIX);
  for (i = 0; i < tzd->country_count; i++)
    {
      fprintf (fp, "\t{\"%s\", \"%s\"}%s\n", tzd->countries[i].code, tzd->countries[i].full_name,
	       (i == tzd->country_count - 1) ? "" : ",");
    }
  fprintf (fp, "};\n\n");

  /* timezone names */
  fprintf (fp, "%s const char *tz_timezone_names[] = {\n", SHLIB_EXPORT_PREFIX);
  for (i = 0; i < tzd->timezone_count; i++)
    {
      fprintf (fp, "\t\"%s\"%s\n", tzd->timezone_names[i], (i == tzd->timezone_count - 1 ? "" : ","));
    }
  fprintf (fp, "};\n\n");

  /* timezones */
  fprintf (fp, "%s const int timezone_count = %d;\n", SHLIB_EXPORT_PREFIX, tzd->timezone_count);
  fprintf (fp, "%s const TZ_TIMEZONE timezones[] = {\n", SHLIB_EXPORT_PREFIX);
  for (i = 0; i < tzd->timezone_count; i++)
    {
      fprintf (fp, "\t{%d, %d, %d, %d}%s\n", tzd->timezones[i].zone_id, tzd->timezones[i].country_id,
	       tzd->timezones[i].gmt_off_rule_start, tzd->timezones[i].gmt_off_rule_count,
	       (i == tzd->timezone_count - 1) ? "" : ",");
    }
  fprintf (fp, "};\n\n");

  /* NOTE: timezone names are not exported into the shared library, but into a separate C file, to be included into
   * CUBRID */

  /* offset rule array */
  fprintf (fp, "%s const int offset_rule_count = %d;\n", SHLIB_EXPORT_PREFIX, tzd->offset_rule_count);
  fprintf (fp, "%s const TZ_OFFSET_RULE offset_rules[] = {\n", SHLIB_EXPORT_PREFIX);
  for (i = 0; i < tzd->offset_rule_count; i++)
    {
      int julian_date;
      offrule = &(tzd->offset_rules[i]);

      julian_date = julian_encode (1 + offrule->until_mon, 1 + offrule->until_day, offrule->until_year);

      fprintf (fp, "\t{%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, ", offrule->gmt_off, offrule->ds_ruleset,
	       offrule->until_year, offrule->until_mon, offrule->until_day, offrule->until_hour, offrule->until_min,
	       offrule->until_sec, offrule->until_time_type, offrule->until_flag, offrule->ds_type, julian_date);

      if (offrule->std_format == NULL)
	{
	  fprintf (fp, "NULL, ");
	}
      else
	{
	  fprintf (fp, "\"%s\", ", offrule->std_format);
	}

      if (offrule->save_format == NULL)
	{
	  fprintf (fp, "NULL, ");
	}
      else
	{
	  fprintf (fp, "\"%s\", ", offrule->save_format);
	}

      if (offrule->var_format == NULL)
	{
	  fprintf (fp, "NULL }");
	}
      else
	{
	  fprintf (fp, "\"%s\" }", offrule->var_format);
	}

      if (i < tzd->offset_rule_count - 1)
	{
	  fprintf (fp, ",\n");
	}
    }
  fprintf (fp, "};\n\n");

  /* tz names (timezone names and aliases) */
  fprintf (fp, "%s const int tz_name_count = %d;\n", SHLIB_EXPORT_PREFIX, tzd->name_count);
  fprintf (fp, "%s const TZ_NAME tz_names[] = {\n", SHLIB_EXPORT_PREFIX);
  for (i = 0; i < tzd->name_count; i++)
    {
      fprintf (fp, "\t{%d, \"%s\", %d}%s\n", tzd->names[i].zone_id, tzd->names[i].name, tzd->names[i].is_alias,
	       (i == tzd->name_count - 1) ? "" : ",");
    }
  fprintf (fp, "};\n\n");

  /* daylight saving rulesets */
  fprintf (fp, "%s const int ds_ruleset_count = %d;\n", SHLIB_EXPORT_PREFIX, tzd->ds_ruleset_count);
  fprintf (fp, "%s const TZ_DS_RULESET ds_rulesets[] = {\n", SHLIB_EXPORT_PREFIX);
  for (i = 0; i < tzd->ds_ruleset_count; i++)
    {
      fprintf (fp, "\t{%d, %d, \"%s\", %d, \"%s\"}%s\n", tzd->ds_rulesets[i].index_start, tzd->ds_rulesets[i].count,
	       tzd->ds_rulesets[i].ruleset_name, tzd->ds_rulesets[i].to_year_max, tzd->ds_rulesets[i].default_abrev,
	       (i == tzd->ds_ruleset_count - 1) ? "" : ",");
    }
  fprintf (fp, "};\n\n");

  /* daylight saving rules */
  fprintf (fp, "%s const int ds_rule_count = %d;\n", SHLIB_EXPORT_PREFIX, tzd->ds_rule_count);
  fprintf (fp, "%s const TZ_DS_RULE ds_rules[] = {\n", SHLIB_EXPORT_PREFIX);
  for (i = 0; i < tzd->ds_rule_count; i++)
    {
      rule = &(tzd->ds_rules[i]);
      fprintf (fp, "\t{%d, %d, %d, {%d, %d, %d}, %d, %d, %d, \"%s\"}%s\n", rule->from_year, rule->to_year,
	       rule->in_month, rule->change_on.type, rule->change_on.day_of_month, rule->change_on.day_of_week,
	       rule->at_time, rule->at_time_type, rule->save_time, rule->letter_abbrev,
	       (i == tzd->ds_rule_count - 1) ? "" : ",");
    }
  fprintf (fp, "};\n\n");

  /* leap seconds */
  fprintf (fp, "%s const int ds_leap_sec_count = %d;\n", SHLIB_EXPORT_PREFIX, tzd->ds_leap_sec_count);
  fprintf (fp, "%s const TZ_LEAP_SEC ds_leap_sec[] = {\n", SHLIB_EXPORT_PREFIX);
  for (i = 0; i < tzd->ds_leap_sec_count; i++)
    {
      leap_sec = &(tzd->ds_leap_sec[i]);
      fprintf (fp, "\t{%d, %d, %d, %d, %d}%s\n", leap_sec->year, leap_sec->month, leap_sec->day,
	       leap_sec->corr_negative, leap_sec->is_rolling, (i == tzd->ds_leap_sec_count - 1) ? "" : ",");
    }
  fprintf (fp, "};\n\n");

#if defined(WINDOWS)
  /* windows iana map */
  fprintf (fp, "%s const int windows_iana_map_count = %d;\n", SHLIB_EXPORT_PREFIX, tzd->windows_iana_map_count);
  fprintf (fp, "%s const TZ_WINDOWS_IANA_MAP windows_iana_map[] = {\n", SHLIB_EXPORT_PREFIX);

  for (i = 0; i < tzd->windows_iana_map_count; i++)
    {
      fprintf (fp, "\t{\"%s\", \"%s\", %d}%s\n", tzd->windows_iana_map[i].windows_zone,
	       tzd->windows_iana_map[i].territory, tzd->windows_iana_map[i].iana_zone_id,
	       (i == tzd->windows_iana_map_count - 1) ? "" : ",");
    }
  fprintf (fp, "};\n\n");
#endif

  PRINT_STRING_VAR_TO_C_FILE (fp, "tz_timezone_checksum", tzd->checksum);

  if (fp)
    {
      fclose (fp);
    }

exit:
  return err_status;
}

/*
 * tzc_get_ds_ruleset_by_name() - returns the ID/index of a ruleset having the
 *				 specified name
 * Returns: ID of the ruleset with the given name, or -1 if not found
 * tzd(in): time zone data where to search
 * ruleset(in): ruleset name to search for
 */
static int
tzc_get_ds_ruleset_by_name (const TZ_DS_RULESET * ds_rulesets, int ds_ruleset_count, const char *ruleset)
{
  int ruleset_id = -1;
  int index_bot, index_top;
  int cmp_res;

  index_bot = 0;
  index_top = ds_ruleset_count - 1;

  while (index_bot <= index_top)
    {
      ruleset_id = (index_bot + index_top) / 2;
      cmp_res = strcmp (ruleset, ds_rulesets[ruleset_id].ruleset_name);
      if (cmp_res == 0)
	{
	  return ruleset_id;
	}
      else if (cmp_res < 0)
	{
	  index_top = ruleset_id - 1;
	}
      else
	{
	  index_bot = ruleset_id + 1;
	}
    }

  return -1;
}

/*
 * tzc_get_timezone_aliases() - returns the list of names for a given timezone
 * Returns: ID of the ruleset with the given name, or -1 if not found
 * tzd(in): time zone data where to search
 * zone_id(in): timezone ID for which the aliases must be fetched
 * aliases(out): list of indexes of aliases/names for the given timezone ID
 * alias_count(out): number of aliases for the given timezone
 */
static int
tzc_get_timezone_aliases (const TZ_DATA * tzd, const int zone_id, int **aliases, int *alias_count)
{
  int i, err_status = NO_ERROR;
  int *temp_array = NULL;

  assert (*aliases == NULL);

  *aliases = NULL;
  *alias_count = 0;

  for (i = 0; i < tzd->name_count; i++)
    {
      if (zone_id == tzd->names[i].zone_id && tzd->names[i].is_alias == 1)
	{
	  /* name/alias found for the given timezone ID */
	  temp_array = (int *) realloc (*aliases, ((*alias_count) + 1) * sizeof (int));
	  if (temp_array == NULL)
	    {
	      char err_msg[TZC_ERR_MSG_MAX_SIZE];

	      sprintf (err_msg, "%d", (*alias_count) + 1);
	      err_status = TZC_ERR_OUT_OF_MEMORY;
	      TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_OUT_OF_MEMORY, err_msg, "int");
	      goto exit;
	    }
	  *aliases = temp_array;
	  (*aliases)[*alias_count] = i;
	  (*alias_count)++;
	}
    }

exit:
  return err_status;
}

/*
 * tzc_dump_one_offset_rule () - dump a single offset rule
 * Returns:
 * offset_rule(in): the offset rule to dump
 */
static void
tzc_dump_one_offset_rule (const TZ_DATA * tzd, const TZ_OFFSET_RULE * offset_rule)
{
  /* print GMTOFF column */
  print_seconds_as_time_hms_var (offset_rule->gmt_off);

  printf ("\t");

  /* print RULES column */
  if (offset_rule->ds_type == DS_TYPE_RULESET_ID)
    {
      assert (offset_rule->ds_ruleset >= 0 && offset_rule->ds_ruleset < tzd->ds_ruleset_count);
      printf ("%s", tzd->ds_rulesets[offset_rule->ds_ruleset].ruleset_name);
    }
  else
    {
      if (offset_rule->ds_ruleset == 0)
	{
	  printf ("-");
	}
      else
	{
	  print_seconds_as_time_hms_var (offset_rule->ds_ruleset);
	}
    }

  /* print FORMAT column */
  if (offset_rule->var_format != NULL)
    {
      assert (offset_rule->std_format == NULL && offset_rule->save_format == NULL);
      printf ("\t%s", offset_rule->var_format);
    }
  else
    {
      assert (offset_rule->std_format != NULL);
      printf ("\t%s", offset_rule->std_format);
      if (offset_rule->save_format != NULL)
	{
	  printf ("/%s", offset_rule->save_format);
	}
    }

  /* print UNTIL column */
  if (offset_rule->until_year != 0)
    {
      printf ("\t%d", offset_rule->until_year);
      if (offset_rule->until_mon != TZ_MON_COUNT)
	{
	  printf (" %s", MONTH_NAMES_ABBREV[offset_rule->until_mon]);
	  if (offset_rule->until_day != 0)
	    {
	      printf (" %d", offset_rule->until_day + 1);
	    }
	}
    }
  printf ("\t");

  if (offset_rule->until_hour + offset_rule->until_min + offset_rule->until_sec > 0)
    {
      printf ("\t%02d:%02d", offset_rule->until_hour, offset_rule->until_min);
      if (offset_rule->until_sec > 0)
	{
	  printf (":%02d", offset_rule->until_sec);
	}
    }
}

/*
 * tzc_dump_ds_ruleset () - dump daylight saving rules from a specified
 *			    ruleset
 * Returns:
 * tzd(in): loaded timezone data
 * ruleset_id(in): ID of the ruleset to dump
 */
static void
tzc_dump_ds_ruleset (const TZ_DATA * tzd, const int ruleset_id)
{
  TZ_DS_RULESET *ruleset;
  TZ_DS_RULE *rule;
  int i, start_index, end_index;

  assert (ruleset_id >= 0 && ruleset_id < tzd->ds_ruleset_count);

  ruleset = &(tzd->ds_rulesets[ruleset_id]);
  printf ("\nDaylight saving ruleset : %s", ruleset->ruleset_name);

  start_index = ruleset->index_start;
  end_index = start_index + ruleset->count;

  for (i = start_index; i < end_index; i++)
    {
      rule = &(tzd->ds_rules[i]);
      /* print NAME and FROM columns */
      printf ("\nRule\t%s\t%d\t", ruleset->ruleset_name, rule->from_year);
      /* print TO column */
      if (rule->to_year == TZ_MAX_YEAR)
	{
	  printf ("max\t");
	}
      else if (rule->to_year == rule->from_year)
	{
	  printf ("only\t");
	}
      else
	{
	  assert (rule->to_year > rule->from_year);
	  printf ("%d\t", rule->to_year);
	}
      /* NOTE: TYPE column is '-' for all rules at this time, so we just print a '-' */
      printf ("-\t");

      /* print IN column */
      assert (rule->in_month < 12);
      printf ("%s\t", MONTH_NAMES_ABBREV[rule->in_month]);

      /* print ON column */
      switch (rule->change_on.type)
	{
	case TZ_DS_TYPE_FIXED:
	  printf ("%d", rule->change_on.day_of_month + 1);
	  break;
	case TZ_DS_TYPE_VAR_GREATER:
	  printf ("%s>=%d", DAY_NAMES_ABBREV[rule->change_on.day_of_week], rule->change_on.day_of_month + 1);
	  break;
	case TZ_DS_TYPE_VAR_SMALLER:
	  printf ("%s<=%d", DAY_NAMES_ABBREV[rule->change_on.day_of_week], rule->change_on.day_of_month + 1);
	  break;
	default:
	  assert (false);
	  break;
	}
      printf ("\t");

      /* print AT column */
      print_seconds_as_time_hms_var (rule->at_time);
      printf ("\t");

      /* print SAVE column */
      print_seconds_as_time_hms_var (rule->save_time);
      printf ("\t");

      /* print LETTERS column */
      printf ("%s", rule->letter_abbrev);
    }
}

/*
 * tzc_dump_summary () - dump timezone general information
 * Returns:
 * tzd(in): timezone data
 */
void
tzc_dump_summary (const TZ_DATA * tzd)
{
  assert (tzd != NULL);

  printf ("\n Summary");
  printf ("\n No. of countries:    %d", tzd->country_count);
  printf ("\n No. of timezones:    %d", tzd->timezone_count);
  printf ("\n No. of aliases:      %d", tzd->name_count - tzd->timezone_count);
  printf ("\n No. of offset rules: %d", tzd->offset_rule_count);
  printf ("\n No. of daylight saving rulesets: %d", tzd->ds_ruleset_count);
  printf ("\n No. of daylight saving rules:    %d", tzd->ds_rule_count);
  printf ("\n No. of leap seconds: %d", tzd->ds_leap_sec_count);
}

/*
 * tzc_dump_countries () - dump the list of countries
 * Returns:
 * tzd(in): timezone data
 */
void
tzc_dump_countries (const TZ_DATA * tzd)
{
  int i;

  assert (tzd != NULL);

  for (i = 0; i < tzd->country_count; i++)
    {
      printf ("%s     %s\n", tzd->countries[i].code, tzd->countries[i].full_name);
    }
}

/*
 * tzc_dump_timezones () - dump the list of timezones
 * Returns:
 * tzd(in): timezone data
 */
void
tzc_dump_timezones (const TZ_DATA * tzd)
{
  int i;

  assert (tzd != NULL);

  for (i = 0; i < tzd->timezone_count; i++)
    {
      printf ("%5d.    %s\n", i, tzd->timezone_names[i]);
    }
}

/*
 * tzc_dump_one_timezone () - dump all information related to a given timezone
 * Returns:
 * tzd(in): timezone data
 * zone_id(in): ID of the timezone for which to dump information
 */
void
tzc_dump_one_timezone (const TZ_DATA * tzd, const int zone_id)
{
  int err_status = NO_ERROR;
  int i, j;
  int *zone_aliases = NULL;
  int *ds_rulesets_used = NULL;
  int count_ds_rulesets_used = 0;
  int *temp_int_array;
  int alias_count = 0, start_index = 0;
  bool is_first = true, found;
  TZ_TIMEZONE *zone = NULL;
  TZ_OFFSET_RULE *offset_rule = NULL;

  assert (tzd != NULL);

  printf (" Zone name: %s\n", tzd->timezone_names[zone_id]);

  err_status = tzc_get_timezone_aliases (tzd, zone_id, &zone_aliases, &alias_count);

  if (err_status != NO_ERROR)
    {
      goto exit;
    }
  if (alias_count > 0)
    {
      printf (" Aliases (%d): ", alias_count);
    }
  for (j = 0; j < alias_count; j++)
    {
      TZ_NAME *tz_name = &(tzd->names[zone_aliases[j]]);

      if (!is_first)
	{
	  printf (", ");
	}
      else
	{
	  is_first = false;
	}
      printf ("%s", tz_name->name);
    }
  if (alias_count > 0)
    {
      printf ("\n");
    }

  zone = &(tzd->timezones[zone_id]);
  start_index = zone->gmt_off_rule_start;

  /* dump offset rules, and also build the list of DS rulesets to be dumped */
  printf ("\n Offset rule index: %d, count: %d", zone->gmt_off_rule_start, zone->gmt_off_rule_count);
  if (zone->gmt_off_rule_count > 0)
    {
      printf ("\n Offset rules: \n");
    }
  for (i = 0; i < zone->gmt_off_rule_count; i++)
    {
      offset_rule = &(tzd->offset_rules[start_index + i]);
      tzc_dump_one_offset_rule (tzd, offset_rule);
      printf ("\n");

      if (offset_rule->ds_type == DS_TYPE_FIXED)
	{
	  continue;
	}

      /* search for the ruleset id */
      found = false;
      for (j = 0; j < count_ds_rulesets_used && !found; j++)
	{
	  if (ds_rulesets_used[j] == offset_rule->ds_ruleset)
	    {
	      found = true;
	    }
	}

      if (!found)
	{
	  temp_int_array =
	    (int *) realloc (ds_rulesets_used, (count_ds_rulesets_used + 1) * sizeof (ds_rulesets_used[0]));
	  if (temp_int_array == NULL)
	    {
	      printf ("\nOUT OF MEMORY!\n");
	      goto exit;
	    }
	  ds_rulesets_used = temp_int_array;
	  ds_rulesets_used[count_ds_rulesets_used] = offset_rule->ds_ruleset;
	  count_ds_rulesets_used++;
	}
    }

  printf ("\n Found %d daylight saving ruleset(s) used by offset rules\n", count_ds_rulesets_used);
  for (i = 0; i < count_ds_rulesets_used; i++)
    {
      tzc_dump_ds_ruleset (tzd, ds_rulesets_used[i]);
    }

exit:
  if (ds_rulesets_used != NULL)
    {
      free (ds_rulesets_used);
    }
  if (zone_aliases != NULL)
    {
      free (zone_aliases);
    }
}

/*
 * tzc_dump_leap_sec () - dump the list of leap seconds
 * Returns:
 * tzd(in): timezone data
 */
void
tzc_dump_leap_sec (const TZ_DATA * tzd)
{
  int i;
  TZ_LEAP_SEC *leap_sec;

  assert (tzd != NULL);

  printf ("\n# Leap\tYEAR\tMONTH\tDAY\tHH:MM:SS\tCORR\tR/S\n");

  for (i = 0; i < tzd->ds_leap_sec_count; i++)
    {
      leap_sec = &(tzd->ds_leap_sec[i]);
      printf ("Leap\t%d\t%s\t%d\t%d:%d:%d\t%s\t%s\n", leap_sec->year, MONTH_NAMES_ABBREV[leap_sec->month],
	      leap_sec->day, 23, 59, 60, (leap_sec->corr_negative ? "-" : "+"), (leap_sec->is_rolling ? "R" : "S"));
    }
}

/*
 * tzc_log_error () - log timezone compiler error
 * Returns:
 * context(in): timezone compiler error context
 * code(in): error code
 * msg1(in): first string replacement for error message
 * msg2(in): second string replacement for error message
 */
static void
tzc_log_error (const TZ_RAW_CONTEXT * context, const int code, const char *msg1, const char *msg2)
{
  char err_msg[TZC_ERR_MSG_MAX_SIZE];
  char err_msg_temp[TZC_ERR_MSG_MAX_SIZE];

  assert (code <= 0 && -(code) < tzc_Err_message_count);
  *err_msg = '\0';
  *err_msg_temp = '\0';

  if (context != NULL && !IS_EMPTY_STR (context->current_file) && context->current_line != -1)
    {
      snprintf_dots_truncate (err_msg_temp, sizeof (err_msg_temp) - 1, " (file %s, line %d)", context->current_file,
			      context->current_line);
    }
  strcat (err_msg, err_msg_temp);

  *err_msg_temp = '\0';
  snprintf_dots_truncate (err_msg_temp, sizeof (err_msg_temp), tzc_Err_messages[-code], msg1, msg2);
  strcat (err_msg, err_msg_temp);
  strcat (err_msg, "\n");

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TZ_COMPILE_ERROR, 1, err_msg);
}

static void
tzc_summary (TZ_RAW_DATA * tzd_raw, TZ_DATA * tzd)
{
  int i, j;
  int max_len, temp_len;
  int max_len2, temp_len2;
  int max_len3;

  printf ("\n");
  printf (" COUNTRY MAX NAME LEN: ");
  max_len = 0;
  for (i = 0; i < tzd_raw->country_count; i++)
    {
      temp_len = strlen (tzd_raw->countries[i].full_name);
      if (temp_len > max_len)
	{
	  max_len = temp_len;
	}
    }
  printf ("%d\n", max_len);

  printf (" DS RULES & RULESETS\n");
  max_len = 0;
  max_len2 = 0;
  for (i = 0; i < tzd_raw->ruleset_count; i++)
    {
      TZ_RAW_DS_RULESET *ds_ruleset = &(tzd_raw->ds_rulesets[i]);

      temp_len = strlen (ds_ruleset->name);
      if (temp_len > max_len)
	{
	  max_len = temp_len;
	}

      for (j = 0; j < ds_ruleset->rule_count; j++)
	{
	  TZ_RAW_DS_RULE *rule = &(ds_ruleset->rules[j]);
	  temp_len = strlen (rule->letter_abbrev);
	  if (temp_len > max_len2)
	    {
	      max_len2 = temp_len;
	    }
	}
    }
  printf ("   DS RULESET MAX NAME LEN: %d\n", max_len);
  printf ("   DS RULE MAX LETTER_ABBREV LEN: %d\n\n", max_len2);

  printf (" TIMEZONE\n");
  max_len = 0;
  max_len2 = 0;
  max_len3 = 0;
  for (i = 0; i < tzd_raw->zone_count; i++)
    {
      temp_len = strlen (tzd_raw->zones[i].full_name);
      if (temp_len > max_len)
	{
	  max_len = temp_len;
	}
      if (tzd_raw->zones[i].comments != NULL)
	{
	  temp_len = strlen (tzd_raw->zones[i].comments);
	  if (temp_len > max_len2)
	    {
	      max_len2 = temp_len;
	    }
	}
      if (tzd_raw->zones[i].coordinates != NULL)
	{
	  temp_len = strlen (tzd_raw->zones[i].coordinates);
	  if (temp_len > max_len3)
	    {
	      max_len3 = temp_len;
	    }
	}
    }
  printf ("   MAX NAME LEN: %d", max_len);
  printf ("   MAX comments LEN: %d", max_len2);
  printf ("   MAX coordinates LEN: %d\n", max_len3);

  printf (" TZ_NAMES (timezone names and aliases) MAX NAME LEN: ");
  max_len = 0;
  for (i = 0; i < tzd->name_count; i++)
    {
      temp_len = strlen (tzd->names[i].name);
      if (temp_len > max_len)
	{
	  max_len = temp_len;
	}
    }
  printf ("%d\n", max_len);

  printf (" TZ_RW_LINKS : \n");
  max_len = 0;
  max_len2 = 0;
  for (i = 0; i < tzd_raw->link_count; i++)
    {
      temp_len = strlen (tzd_raw->links[i].name);
      if (temp_len > max_len)
	{
	  max_len = temp_len;
	}
      temp_len2 = strlen (tzd_raw->links[i].alias);
      if (temp_len2 > max_len2)
	{
	  max_len2 = temp_len2;
	}
    }
  printf ("   MAX NAME LEN: %d", max_len);
  printf ("   MAX ALIAS LEN: %d\n", max_len2);


  printf (" TZ_RAW_OFFSET_RULES : \n");
  max_len = 0;
  max_len2 = 0;
  for (i = 0; i < tzd_raw->zone_count; i++)
    {
      TZ_RAW_ZONE_INFO *zone = &(tzd_raw->zones[i]);
      for (j = 0; j < zone->offset_rule_count; j++)
	{
	  TZ_RAW_OFFSET_RULE *offrule = &(zone->offset_rules[j]);

	  temp_len = strlen (offrule->ds_ruleset_name);
	  if (temp_len > max_len)
	    {
	      max_len = temp_len;
	    }
	  temp_len = strlen (offrule->format);
	  if (temp_len > max_len2)
	    {
	      max_len2 = temp_len;
	    }
	}
    }
  printf ("   MAX rules LEN: %d", max_len);
  printf ("   MAX format LEN: %d\n", max_len2);
}

#if defined(WINDOWS)
/*
 * comp_func_tz_windows_zones - comparison function between two
 *                              TZ_WINDOWS_IANA_MAP values
 * Returns: -1 if arg1 < arg2, 0 if arg1 = arg2, 1 if arg1 > arg2
 * arg1(in): first value to compare
 * arg2(in): second value to compare
 */
static int
comp_func_tz_windows_zones (const void *arg1, const void *arg2)
{
  TZ_WINDOWS_IANA_MAP *map1, *map2;
  int ret;

  assert (arg1 != NULL && arg2 != NULL);

  map1 = (TZ_WINDOWS_IANA_MAP *) arg1;
  map2 = (TZ_WINDOWS_IANA_MAP *) arg2;

  assert (!IS_EMPTY_STR (map1->windows_zone) && !IS_EMPTY_STR (map2->windows_zone) && !IS_EMPTY_STR (map1->territory)
	  && !IS_EMPTY_STR (map2->territory));

  ret = strcmp (map1->windows_zone, map2->windows_zone);
  if (ret != 0)
    {
      return ret;
    }
  return strcmp (map1->territory, map2->territory);
}

/*
 * xml_start_mapZone() - extracts from a mapZone tag the Windows timezone name
 *			 and IANA timezone name
 *
 * Returns: 0 parser OK, non-zero value if parser NOK
 * data(in): user data
 * attr(in): array of pairs for XML attribute and value (strings) of current
 *	     element
 */
static int
xml_start_mapZone (void *data, const char **attr)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  TZ_DATA *tz = NULL;
  char *windows_zone = NULL;
  char *iana_zone = NULL;
  char *territory = NULL;
  TZ_WINDOWS_IANA_MAP *temp;
  int len_windows_zone;
  int len_territory;
  int iana_zone_id = -1;
  int i;

  assert (data != NULL);
  tz = (TZ_DATA *) XML_USER_DATA (pd);

  if (xml_get_att_value (attr, "other", &windows_zone) == 0 && xml_get_att_value (attr, "territory", &territory) == 0
      && xml_get_att_value (attr, "type", &iana_zone) == 0)
    {
      assert (windows_zone != NULL && territory != NULL && iana_zone != NULL);

      if (tz->windows_iana_map != NULL
	  && strcmp (windows_zone, tz->windows_iana_map[tz->windows_iana_map_count - 1].windows_zone) == 0)
	{
	  return 0;
	}

      temp =
	(TZ_WINDOWS_IANA_MAP *) realloc (tz->windows_iana_map,
					 (tz->windows_iana_map_count + 1) * sizeof (TZ_WINDOWS_IANA_MAP));
      if (temp == NULL)
	{
	  char err_msg[TZC_ERR_MSG_MAX_SIZE];
	  sprintf (err_msg, "%d", tz->windows_iana_map_count + 1);
	  TZC_LOG_ERROR_2ARG (NULL, TZC_ERR_OUT_OF_MEMORY, err_msg, "TZ_WINDOWS_IANA_MAP");
	  return -1;
	}

      len_windows_zone = strlen (windows_zone);
      len_territory = strlen (territory);
      if (len_windows_zone > TZ_WINDOWS_ZONE_NAME_SIZE || len_territory > TZ_COUNTRY_CODE_SIZE)
	{
	  TZC_LOG_ERROR_1ARG (NULL, TZC_ERR_INVALID_VALUE, "TZ_WINDOWS_IANA_MAP");
	  if (temp != nullptr)
	    {
	      free (temp);
	    }
	  return -1;
	}

      memmove (temp[tz->windows_iana_map_count].windows_zone, windows_zone, len_windows_zone);
      temp[tz->windows_iana_map_count].windows_zone[len_windows_zone] = '\0';
      memmove (temp[tz->windows_iana_map_count].territory, territory, len_territory);
      temp[tz->windows_iana_map_count].territory[len_territory] = '\0';

      for (i = 0; i < tz->name_count; i++)
	{
	  if (strcmp (iana_zone, tz->names[i].name) == 0)
	    {
	      iana_zone_id = tz->names[i].zone_id;
	      break;
	    }
	}

      temp[tz->windows_iana_map_count].iana_zone_id = iana_zone_id;
      tz->windows_iana_map = temp;
      tz->windows_iana_map_count++;
      return 0;
    }
  return 1;
}

/*
 * tzc_load_windows_iana_map() - loads the data from the file marked as
 *			        TZF_LIBC_IANA_ZONES_MAP
 *
 * Returns: 0 (NO_ERROR) if success, error code or -1 otherwise
 * tz_data(out): timezone data structure to hold the loaded information
 * input_folder(in): folder containing IANA's timezone database
 *
 */
static int
tzc_load_windows_iana_map (TZ_DATA * tz_data, const char *input_folder)
{
  int err_status = NO_ERROR, i;
  char filepath[PATH_MAX] = { 0 };
  char str[TZ_MAX_LINE_LEN] = { 0 };
  XML_PARSER_DATA windows_zones_parser;

  assert (tz_data != NULL);
  assert (input_folder != NULL);

  for (i = 0; i < tz_File_count; i++)
    {
      if (tz_Files[i].type == TZF_WINDOWS_IANA_ZONES_MAP)
	{
	  break;
	}
    }

  assert (i < tz_File_count);
  tzc_build_filepath (filepath, sizeof (filepath), input_folder, tz_Files[i].name);

  tz_data->windows_iana_map_count = 0;
  tz_data->windows_iana_map = NULL;
  windows_zones_parser.ud = tz_data;

  windows_zones_parser.xml_parser =
    xml_init_parser (&windows_zones_parser, filepath, "UTF-8", windows_zones_elements,
		     sizeof (windows_zones_elements) / sizeof (XML_ELEMENT_DEF *));

  if (windows_zones_parser.xml_parser == NULL)
    {
      err_status = ER_TZ_COMPILE_ERROR;
      goto exit;
    }

  xml_parser_exec (&windows_zones_parser);
  /* sort windows zone names */
  qsort (tz_data->windows_iana_map, tz_data->windows_iana_map_count, sizeof (TZ_WINDOWS_IANA_MAP),
	 comp_func_tz_windows_zones);

exit:
  xml_destroy_parser (&windows_zones_parser);
  return err_status;
}
#endif

/*
 * tzc_find_timezone_names() - returns 1 if the timezone name was found
 *			       or 0 otherwise
 * Returns: 1 if the timezone name was found or 0 otherwise
 * tzd(in): time zone data where to search
 * timezone_name(in): timezone name to search for
 */
static int
tzc_find_timezone_names (const TZ_DATA * tzd, const char *timezone_name)
{
  int index_bot, index_top;
  int cmp_res;

  index_bot = 0;
  index_top = tzd->name_count - 1;

  while (index_bot <= index_top)
    {
      int mid = index_bot + (index_top - index_bot) / 2;
      cmp_res = strcmp (timezone_name, tzd->names[mid].name);
      if (cmp_res == 0)
	{
	  return tzd->names[mid].zone_id;
	}
      else if (cmp_res < 0)
	{
	  index_top = mid - 1;
	}
      else
	{
	  index_bot = mid + 1;
	}
    }

  return -1;
}

/*
 * tzc_find_country_names() - returns 1 if the country name was found
 *			      or 0 otherwise
 * Returns: 1 if the country name was found or 0 otherwise
 * countries(in): vector of countries where to search
 * country_count(in): number of elements of countries vector
 * country_name (in) : name of the country to search for
 */
static int
tzc_find_country_names (const TZ_COUNTRY * countries, const int country_count, const char *country_name)
{
  int index_bot, index_top;
  int cmp_res;

  index_bot = 0;
  index_top = country_count - 1;

  while (index_bot <= index_top)
    {
      int mid = index_bot + (index_top - index_bot) / 2;

      cmp_res = strcmp (country_name, countries[mid].full_name);
      if (cmp_res == 0)
	{
	  return mid;
	}
      else if (cmp_res < 0)
	{
	  index_top = mid - 1;
	}
      else
	{
	  index_bot = mid + 1;
	}
    }

  return -1;
}

/*
 * comp_ds_rules() - equality function for two daylight saving rules
 *
 * Returns: true if the rules are identical or false otherwise
 * rule1(in): first daylight saving rule
 * rule2(in): second daylight saving rule
 */
static bool
comp_ds_rules (const TZ_DS_RULE * rule1, const TZ_DS_RULE * rule2)
{
  if (rule1->at_time != rule2->at_time || rule1->change_on.day_of_month != rule2->change_on.day_of_month
      || rule1->change_on.day_of_week != rule2->change_on.day_of_week || rule1->change_on.type != rule2->change_on.type
      || rule1->from_year != rule2->from_year || rule1->to_year != rule2->to_year || rule1->in_month != rule2->in_month
      || rule1->save_time != rule2->save_time)
    {
      return false;
    }
  return true;
}

/*
 * comp_offset_rules() - equality function for two offset rules
 *
 * Returns: true if the rules are identical or false otherwise
 * rule1(in): first offset rule
 * rule2(in): second offset rule
 */
static bool
comp_offset_rules (const TZ_OFFSET_RULE * rule1, const TZ_OFFSET_RULE * rule2)
{
  if (rule1->gmt_off != rule2->gmt_off || rule1->until_sec != rule2->until_sec || rule1->until_min != rule2->until_min
      || rule1->until_hour != rule2->until_hour || rule1->until_day != rule2->until_day
      || rule1->until_mon != rule2->until_mon || rule1->until_year != rule2->until_year)
    {
      return false;
    }
  return true;
}

/*
 * copy_offset_rule() - copies in dst the offset rule in tzd at position index
 *                      in the offset rule array
 *
 * Returns: error or no error
 * dst(in/out): destination offset rule
 * tzd(in): timezone data
 * index(in): index of the source offset rule in tzd
 */
static int
copy_offset_rule (TZ_OFFSET_RULE * dst, const TZ_DATA * tzd, const int index)
{
  int err_status = NO_ERROR;

  *dst = tzd->offset_rules[index];
  dst->julian_date = julian_encode (1 + tzd->offset_rules[index].until_mon,
				    1 + tzd->offset_rules[index].until_day, tzd->offset_rules[index].until_year);
  if (tzd->offset_rules[index].std_format != NULL)
    {
      DUPLICATE_STR (dst->std_format, tzd->offset_rules[index].std_format);
    }
  else
    {
      dst->std_format = NULL;
    }

  if (tzd->offset_rules[index].save_format != NULL)
    {
      DUPLICATE_STR (dst->save_format, tzd->offset_rules[index].save_format);
    }
  else
    {
      dst->save_format = NULL;
    }

  if (tzd->offset_rules[index].var_format != NULL)
    {
      DUPLICATE_STR (dst->var_format, tzd->offset_rules[index].var_format);
    }
  else
    {
      dst->var_format = NULL;
    }

exit:
  return err_status;
}

/*
 * init_ds_ruleset() - initializes the members of dst_ruleset
 *
 * Returns: error or no error
 * dst_ruleset(in/out): destination ds ruleset
 * tzd(in): timezone data
 * index(in): index of the ds ruleset in the tzd ds_ruleset array
 * start(in): start of the ds ruleset
 */
static int
init_ds_ruleset (TZ_DS_RULESET * dst_ruleset, const TZ_DATA * tzd, const int index, const int start)
{
  int err_status = NO_ERROR;

  dst_ruleset->count = tzd->ds_rulesets[index].count;
  DUPLICATE_STR (dst_ruleset->ruleset_name, tzd->ds_rulesets[index].ruleset_name);
  dst_ruleset->index_start = start;
  dst_ruleset->to_year_max = tzd->ds_rulesets[index].to_year_max;
  DUPLICATE_STR (dst_ruleset->default_abrev, tzd->ds_rulesets[index].default_abrev);

exit:
  return err_status;
}

/*
 * copy_ds_rule() - copies in dst the daylight saving rule in tzd at
 *		    position index in the daylight saving rule array
 *
 * Returns: error or no error
 * dst(in/out): destination daylight saving rule
 * tzd(in): timezone data
 * index(in): index of the source daylight saving rule in tzd
 */
static int
copy_ds_rule (TZ_DS_RULE * dst, const TZ_DATA * tzd, const int index)
{
  int err_status = NO_ERROR;

  *dst = tzd->ds_rules[index];
  if (tzd->ds_rules[index].letter_abbrev != NULL)
    {
      DUPLICATE_STR (dst->letter_abbrev, tzd->ds_rules[index].letter_abbrev);
    }
  else
    {
      dst->letter_abbrev = NULL;
    }

exit:
  return err_status;
}

/*
 * tz_data_partial_clone() - copies timezone data from tzd into
 *                           the three data structures
 *
 * Returns: error or no error
 * timezone_names(in/out): timezone names without aliases
 * timezones(in/out): timezones
 * names(in/out): timezone names including aliases
 * tzd(in): timezone data
 */
static int
tz_data_partial_clone (char **timezone_names, TZ_TIMEZONE * timezones, TZ_NAME * names, const TZ_DATA * tzd)
{
  int i, err_status = NO_ERROR;

  for (i = 0; i < tzd->timezone_count; i++)
    {
      DUPLICATE_STR (timezone_names[i], tzd->timezone_names[i]);
    }

  memcpy (timezones, tzd->timezones, tzd->timezone_count * sizeof (TZ_TIMEZONE));

  memcpy (names, tzd->names, tzd->name_count * sizeof (TZ_NAME));
  for (i = 0; i < tzd->name_count; i++)
    {
      DUPLICATE_STR (names[i].name, tzd->names[i].name);
    }

exit:
  return err_status;
}

/*
 * init_tz_name() - copies the members of src into dst
 *
 * Returns: error or no error
 * dst(in/out): destination tz_name
 * src(in): source tz_name
 */
static int
init_tz_name (TZ_NAME * dst, TZ_NAME * src)
{
  int err_status = NO_ERROR;

  dst->is_alias = src->is_alias;
  DUPLICATE_STR (dst->name, src->name);
  dst->zone_id = src->zone_id;

exit:
  return err_status;
}

#if defined (SA_MODE)
/*
 * tzc_extend() - Does a merge between the new timezone data and
 *                the old timezone data in order to maintain backward
 *                compatibility with the timezone data present in the
 *                database. If the data could not be made backward
 *                compatible a message is printed
 *
 * Returns: error or no error
 * tzd (in/out): new timezone library data that needs to be merged with
 *               the old timezone library data
 */
static int
tzc_extend (TZ_DATA * tzd)
{
  int err_status = NO_ERROR;
  TZ_DATA old_tzd;
  TZ_TIMEZONE *all_timezones = NULL, *timezones, *old_timezones;
  TZ_NAME *names, *all_names = NULL, *old_names;
  int i, j, k, l;
  int all_timezones_count = 0;
  int all_timezones_and_aliases_count = 0;
  char **all_timezone_names = NULL;
  int start_timezones, start_names, start;
  int gmt_off_rule_start, prev_gmt_off_rule_start;
  char *timezone_name;
  int zone_id;
  TZ_OFFSET_RULE *all_offset_rules = NULL;
  int all_offset_rule_count = 0;
  int start_gmt_old, start_gmt_new;
  int all_ds_ruleset_count = 0;
  int ruleset_id;
  char *mark_ruleset = NULL;
  TZ_DS_RULESET *all_ds_rulesets = NULL;
  int prev_start_ds_rule = 0, start_ds_rule = 0;
  TZ_DS_RULE *all_ds_rules = NULL;
  int all_ds_rule_count = 0;
  int comp_res;
  TZ_DATA *tzd_or_old_tzd = NULL;
  const char *ruleset_name;
  bool is_compat = true;
  int start_ds_ruleset_old = 0, start_ds_ruleset_new = 0;
  const TZ_DS_RULE *old_ds_rule = NULL;
  const TZ_DS_RULE *new_ds_rule = NULL;
  int all_country_count = 0;
  TZ_COUNTRY *all_countries = NULL;
  char *country_name = NULL;
  int country_id;
  int temp_zone_id;
  bool found_duplicate = false;
  OFFSET_RULE_INTERVAL *old_tzd_offset_rule_map = NULL;
  OFFSET_RULE_INTERVAL *tzd_offset_rule_map = NULL;
  int old_tzd_map_count = 0, tzd_map_count = 0;
  int find_idx = -1;
  int cnt;
  char timezone_library_path[PATH_MAX] = { 0 };

  /* First load the data structures from the old library and after that do the update */

  envvar_libdir_file (timezone_library_path, PATH_MAX, LIB_TZ_NAME);

  err_status = tz_load_with_library_path (&old_tzd, timezone_library_path);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

  names = tzd->names;
  for (i = 0; i < tzd->name_count; i++)
    {
      if (tzc_find_timezone_names (&old_tzd, names[i].name) == -1)
	{
	  if (names[i].is_alias == 0)
	    {
	      all_timezones_count++;
	    }
	  all_timezones_and_aliases_count++;
	}
    }
  all_timezones_count += old_tzd.timezone_count;
  all_timezones_and_aliases_count += old_tzd.name_count;

  /* Count the number of new added countries */
  for (i = 0; i < tzd->country_count; i++)
    {
      int country_id = tzc_find_country_names (old_tzd.countries, old_tzd.country_count,
					       tzd->countries[i].full_name);

      if (country_id == -1)
	{
	  all_country_count++;
	}
    }
  all_country_count += old_tzd.country_count;

  all_countries = (TZ_COUNTRY *) calloc (all_country_count, sizeof (TZ_COUNTRY));
  if (all_countries == NULL)
    {
      err_status = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_status, 1, all_country_count * sizeof (TZ_COUNTRY));
      goto exit;
    }

  all_timezone_names = (char **) calloc (all_timezones_count, sizeof (char *));
  if (all_timezone_names == NULL)
    {
      err_status = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_status, 1, all_timezones_count * sizeof (char *));
      goto exit;
    }

  all_timezones = (TZ_TIMEZONE *) calloc (all_timezones_count, sizeof (TZ_TIMEZONE));
  if (all_timezones == NULL)
    {
      err_status = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_status, 1, all_timezones_count * sizeof (TZ_TIMEZONE));
      goto exit;
    }

  all_names = (TZ_NAME *) calloc (all_timezones_and_aliases_count, sizeof (TZ_NAME));
  if (all_names == NULL)
    {
      err_status = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_status, 1, all_timezones_and_aliases_count * sizeof (TZ_NAME));
      goto exit;
    }

  old_tzd_offset_rule_map = (OFFSET_RULE_INTERVAL *) calloc (old_tzd.timezone_count, sizeof (OFFSET_RULE_INTERVAL));

  if (old_tzd_offset_rule_map == NULL)
    {
      err_status = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_status, 1, old_tzd.timezone_count * sizeof (OFFSET_RULE_INTERVAL));
      goto exit;
    }

  tzd_offset_rule_map = (OFFSET_RULE_INTERVAL *) calloc (tzd->timezone_count, sizeof (OFFSET_RULE_INTERVAL));

  if (tzd_offset_rule_map == NULL)
    {
      err_status = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_status, 1, tzd->timezone_count * sizeof (OFFSET_RULE_INTERVAL));
      goto exit;
    }

  mark_ruleset = (char *) calloc (old_tzd.ds_ruleset_count, sizeof (char));

  if (mark_ruleset == NULL)
    {
      err_status = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_status, 1, old_tzd.ds_ruleset_count * sizeof (char));
      goto exit;
    }

  err_status = tz_data_partial_clone (all_timezone_names, all_timezones, all_names, &old_tzd);
  if (err_status != NO_ERROR)
    {
      goto exit;
    }

  /* Add the new countries */
  i = 0, j = 0, k = 0;
  while (i < old_tzd.country_count && j < tzd->country_count)
    {
      comp_res = strcmp (old_tzd.countries[i].full_name, tzd->countries[j].full_name);

      if (comp_res == 0)
	{
	  INIT_COUNTRY (&all_countries[k], &tzd->countries[j]);
	  i++;
	  j++;
	}
      else if (comp_res < 0)
	{
	  INIT_COUNTRY (&all_countries[k], &old_tzd.countries[i]);
	  i++;
	}
      else
	{
	  INIT_COUNTRY (&all_countries[k], &tzd->countries[j]);
	  j++;
	}
      k++;
    }

  while (i < old_tzd.country_count)
    {
      INIT_COUNTRY (&all_countries[k], &old_tzd.countries[i]);
      i++;
      k++;
    }

  while (j < tzd->country_count)
    {
      INIT_COUNTRY (&all_countries[k], &tzd->countries[j]);
      j++;
      k++;
    }

  assert (k == all_country_count);

  /* Add the new timezones */
  timezones = tzd->timezones;
  start_timezones = old_tzd.timezone_count;
  start_names = old_tzd.name_count;

  for (i = 0; i < tzd->name_count; i++)
    {
      zone_id = tzc_find_timezone_names (&old_tzd, names[i].name);
      if (zone_id == -1)
	{
	  err_status = init_tz_name (&all_names[start_names], &names[i]);
	  if (err_status != NO_ERROR)
	    {
	      goto exit;
	    }
	  if (names[i].is_alias == 0)
	    {
	      DUPLICATE_STR (all_timezone_names[start_timezones], names[i].name);
	      all_timezones[start_timezones] = timezones[names[i].zone_id];
	      all_timezones[start_timezones].zone_id = start_timezones;
	      all_names[start_names].zone_id = start_timezones;
	      start_timezones++;
	    }
	  start_names++;
	  if (is_compat == true)
	    {
	      is_compat = false;
	    }
	}
    }

  /* Now fix the zone ids for the alias timezones */
  for (i = old_tzd.name_count; i < all_timezones_and_aliases_count; i++)
    {
      if (all_names[i].is_alias == 1)
	{
	  timezone_name = tzd->timezone_names[all_names[i].zone_id];
	  zone_id = tzc_find_timezone_names (&old_tzd, timezone_name);

	  if (zone_id != -1)
	    {
	      all_names[i].zone_id = zone_id;
	    }
	  /* We have an alias pointing to a new timezone */
	  else
	    {
	      for (j = old_tzd.timezone_count; j < all_timezones_count; j++)
		{
		  if (strcmp (timezone_name, all_timezone_names[j]) == 0)
		    {
		      all_names[i].zone_id = j;
		      break;
		    }
		}
	    }
	}
    }

  for (i = 0; i < all_timezones_count; i++)
    {
      found_duplicate = false;
      zone_id = tzc_find_timezone_names (tzd, all_timezone_names[i]);

      if (zone_id == -1)
	{
	  /* Go back and search for duplicate intervals in the old timezone library */
	  for (j = i - 1; j >= 0; j--)
	    {
	      temp_zone_id = tzc_find_timezone_names (tzd, all_timezone_names[j]);
	      if (temp_zone_id == -1 && all_timezones[i].gmt_off_rule_start == all_timezones[j].gmt_off_rule_start
		  && all_timezones[i].gmt_off_rule_count == all_timezones[j].gmt_off_rule_count)
		{
		  found_duplicate = true;
		  break;
		}
	    }
	  if (found_duplicate == true)
	    {
	      continue;
	    }
	  all_offset_rule_count += all_timezones[i].gmt_off_rule_count;
	}
      else
	{
	  /* Go back and search for duplicate intervals in the tzd timezone library */
	  for (j = i - 1; j >= 0; j--)
	    {
	      temp_zone_id = tzc_find_timezone_names (tzd, all_timezone_names[j]);

	      if (temp_zone_id != -1
		  && timezones[zone_id].gmt_off_rule_start == timezones[temp_zone_id].gmt_off_rule_start
		  && timezones[zone_id].gmt_off_rule_count == timezones[temp_zone_id].gmt_off_rule_count)
		{
		  found_duplicate = true;
		  break;
		}
	    }
	  if (found_duplicate == true)
	    {
	      continue;
	    }

	  all_offset_rule_count += timezones[zone_id].gmt_off_rule_count;
	}
    }

  all_offset_rules = (TZ_OFFSET_RULE *) calloc (all_offset_rule_count, sizeof (TZ_OFFSET_RULE));
  if (all_offset_rules == NULL)
    {
      err_status = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_status, 1, all_offset_rule_count * sizeof (TZ_OFFSET_RULE));
      goto exit;
    }

  gmt_off_rule_start = 0;

  /* Add the new offset rules, fix the old ones and do a check for backward compatibility */

  /* Use the old_tzd_offset_rule_map and the tzd_offset_rule_map arrays to filter out duplicate offset rule intervals.
   * For each timezone we check if its offset rule interval was previously found. If it was, we use the mapped start of
   * the interval in the new timezone library. If not, we map the old start of the interval to the new one in the new
   * timezone library. */

  for (i = 0; i < all_timezones_count; i++)
    {
      prev_gmt_off_rule_start = gmt_off_rule_start;
      all_timezones[i].gmt_off_rule_start = gmt_off_rule_start;
      find_idx = -1;
      country_name = NULL;
      zone_id = tzc_find_timezone_names (tzd, all_timezone_names[i]);

      if (zone_id == -1)
	{
	  if (old_tzd.timezones[i].country_id != -1)
	    {
	      country_name = old_tzd.countries[old_tzd.timezones[i].country_id].full_name;
	    }
	  start = old_tzd.timezones[i].gmt_off_rule_start;
	  cnt = old_tzd.timezones[i].gmt_off_rule_count;

	  for (j = 0; j < old_tzd_map_count; j++)
	    {
	      if (old_tzd_offset_rule_map[j].original_offset_rule_start == start
		  && old_tzd_offset_rule_map[j].len == cnt)
		{
		  find_idx = j;
		  break;
		}
	    }

	  if (find_idx == -1)
	    {
	      old_tzd_offset_rule_map[old_tzd_map_count].original_offset_rule_start = start;
	      old_tzd_offset_rule_map[old_tzd_map_count].len = cnt;
	      old_tzd_offset_rule_map[old_tzd_map_count++].final_offset_rule_start = gmt_off_rule_start;
	      gmt_off_rule_start += cnt;
	    }
	  else
	    {
	      all_timezones[i].gmt_off_rule_start = old_tzd_offset_rule_map[find_idx].final_offset_rule_start;
	    }
	}
      else
	{
	  if (tzd->timezones[zone_id].country_id != -1)
	    {
	      country_name = tzd->countries[tzd->timezones[zone_id].country_id].full_name;
	    }
	  all_timezones[i].gmt_off_rule_count = timezones[zone_id].gmt_off_rule_count;
	  start = timezones[zone_id].gmt_off_rule_start;
	  cnt = timezones[zone_id].gmt_off_rule_count;

	  for (j = 0; j < tzd_map_count; j++)
	    {
	      if (tzd_offset_rule_map[j].original_offset_rule_start == start && tzd_offset_rule_map[j].len == cnt)
		{
		  find_idx = j;
		  break;
		}
	    }

	  if (find_idx == -1)
	    {
	      tzd_offset_rule_map[tzd_map_count].original_offset_rule_start = start;
	      tzd_offset_rule_map[tzd_map_count].len = cnt;
	      tzd_offset_rule_map[tzd_map_count++].final_offset_rule_start = gmt_off_rule_start;
	      gmt_off_rule_start += cnt;
	    }
	  else
	    {
	      all_timezones[i].gmt_off_rule_start = tzd_offset_rule_map[find_idx].final_offset_rule_start;
	    }

	  if (i < old_tzd.timezone_count)
	    {
	      if (old_tzd.timezones[i].gmt_off_rule_count != timezones[zone_id].gmt_off_rule_count)
		{
		  is_compat = false;
		}
	      else
		{
		  start_gmt_old = old_tzd.timezones[i].gmt_off_rule_start;
		  start_gmt_new = tzd->timezones[zone_id].gmt_off_rule_start;
		  for (j = start_gmt_old; j < start_gmt_old + old_tzd.timezones[i].gmt_off_rule_count; j++)
		    {
		      int tzd_offset_rule_idx = start_gmt_new + j - start_gmt_old;

		      if (old_tzd.offset_rules[j].ds_type != tzd->offset_rules[tzd_offset_rule_idx].ds_type)
			{
			  is_compat = false;
			  break;
			}

		      if (old_tzd.offset_rules[j].ds_type != DS_TYPE_FIXED)
			{
			  if (strcmp (old_tzd.ds_rulesets[old_tzd.offset_rules[j].ds_ruleset].ruleset_name,
				      tzd->ds_rulesets[tzd->offset_rules[tzd_offset_rule_idx].ds_ruleset].ruleset_name)
			      != 0)
			    {
			      is_compat = false;
			      break;
			    }
			}
		      else
			{
			  if (old_tzd.offset_rules[j].ds_ruleset != tzd->offset_rules[tzd_offset_rule_idx].ds_ruleset)
			    {
			      is_compat = false;
			      break;
			    }
			}
		      if (comp_offset_rules (&old_tzd.offset_rules[j], &tzd->offset_rules[tzd_offset_rule_idx]) ==
			  false)
			{
			  is_compat = false;
			  break;
			}
		    }
		}
	    }
	}

      /* Fix the country ids in the new timezone vector */
      if (country_name != NULL)
	{
	  country_id = tzc_find_country_names (all_countries, all_country_count, country_name);
	}
      else
	{
	  country_id = -1;
	}
      all_timezones[i].country_id = country_id;

      if (find_idx == -1)
	{
	  for (j = prev_gmt_off_rule_start; j < gmt_off_rule_start; j++)
	    {
	      int offset_rule_index = start + j - prev_gmt_off_rule_start;
	      if (zone_id == -1)
		{
		  err_status = copy_offset_rule (&all_offset_rules[j], &old_tzd, offset_rule_index);

		  if (err_status != NO_ERROR)
		    {
		      goto exit;
		    }

		  if (old_tzd.offset_rules[offset_rule_index].ds_type != DS_TYPE_FIXED)
		    {
		      mark_ruleset[old_tzd.offset_rules[offset_rule_index].ds_ruleset] = 1;
		    }
		}
	      else
		{
		  err_status = copy_offset_rule (&all_offset_rules[j], tzd, offset_rule_index);
		  if (err_status != NO_ERROR)
		    {
		      goto exit;
		    }
		}
	    }
	}
    }

  for (i = 0; i < old_tzd.ds_ruleset_count; i++)
    {
      ruleset_id =
	tzc_get_ds_ruleset_by_name (tzd->ds_rulesets, tzd->ds_ruleset_count, old_tzd.ds_rulesets[i].ruleset_name);
      if (ruleset_id == -1 && mark_ruleset[i] == 1)
	{
	  all_ds_ruleset_count++;
	  all_ds_rule_count += old_tzd.ds_rulesets[i].count;
	}
    }
  all_ds_ruleset_count += tzd->ds_ruleset_count;
  all_ds_rule_count += tzd->ds_rule_count;

  all_ds_rulesets = (TZ_DS_RULESET *) calloc (all_ds_ruleset_count, sizeof (TZ_DS_RULESET));
  if (all_ds_rulesets == NULL)
    {
      err_status = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_status, 1, all_ds_ruleset_count * sizeof (TZ_DS_RULESET));
      goto exit;
    }

  all_ds_rules = (TZ_DS_RULE *) calloc (all_ds_rule_count, sizeof (TZ_DS_RULE));
  if (all_ds_rules == NULL)
    {
      err_status = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err_status, 1, all_ds_rule_count * sizeof (TZ_DS_RULE));
      goto exit;
    }

  /* Do a merge between old timezone library ds_rulesets and current timezone library */

  i = 0, j = 0, k = 0;
  while (i < old_tzd.ds_ruleset_count && j < tzd->ds_ruleset_count)
    {
      prev_start_ds_rule = start_ds_rule;
      tzd_or_old_tzd = NULL;
      comp_res = strcmp (old_tzd.ds_rulesets[i].ruleset_name, tzd->ds_rulesets[j].ruleset_name);
      if (comp_res == 0)
	{
	  start_ds_rule += tzd->ds_rulesets[j].count;
	  err_status = init_ds_ruleset (&all_ds_rulesets[k], tzd, j, prev_start_ds_rule);
	  if (err_status != NO_ERROR)
	    {
	      goto exit;
	    }
	  tzd_or_old_tzd = tzd;
	  start = tzd->ds_rulesets[j].index_start;
	  if (old_tzd.ds_rulesets[i].count != tzd->ds_rulesets[j].count)
	    {
	      is_compat = false;
	    }
	  i++;
	  j++;
	}
      else if (comp_res < 0)
	{
	  if (mark_ruleset[i] == 1)
	    {
	      start_ds_rule += old_tzd.ds_rulesets[i].count;
	      err_status = init_ds_ruleset (&all_ds_rulesets[k], &old_tzd, i, prev_start_ds_rule);
	      if (err_status != NO_ERROR)
		{
		  goto exit;
		}
	      tzd_or_old_tzd = &old_tzd;
	      start = old_tzd.ds_rulesets[i].index_start;
	    }
	  i++;
	}
      /* This is a new ruleset, we will set its index start later */
      else
	{
	  err_status = init_ds_ruleset (&all_ds_rulesets[k], tzd, j, -tzd->ds_rulesets[j].index_start);
	  if (err_status != NO_ERROR)
	    {
	      goto exit;
	    }
	  j++;
	}
      if (comp_res >= 0 || (comp_res < 0 && mark_ruleset[i - 1] == 1))
	{
	  k++;
	}

      /* Now copy the daylight saving rules also and do backward compatibility checking */
      if (tzd_or_old_tzd != NULL)
	{
	  if (comp_res == 0 && old_tzd.ds_rulesets[i - 1].count == tzd->ds_rulesets[j - 1].count)
	    {
	      start_ds_ruleset_old = old_tzd.ds_rulesets[i - 1].index_start;
	      start_ds_ruleset_new = tzd->ds_rulesets[j - 1].index_start;
	    }
	  for (l = prev_start_ds_rule; l < start_ds_rule; l++)
	    {
	      err_status = copy_ds_rule (&all_ds_rules[l], tzd_or_old_tzd, start + l - prev_start_ds_rule);
	      if (err_status != NO_ERROR)
		{
		  goto exit;
		}

	      /* Do backward compatibility checking */
	      if (comp_res == 0 && old_tzd.ds_rulesets[i - 1].count == tzd->ds_rulesets[j - 1].count)
		{
		  old_ds_rule = &old_tzd.ds_rules[start_ds_ruleset_old + l - prev_start_ds_rule];
		  new_ds_rule = &tzd->ds_rules[start_ds_ruleset_new + l - prev_start_ds_rule];
		  if (comp_ds_rules (old_ds_rule, new_ds_rule) == false)
		    {
		      is_compat = false;
		    }
		}
	    }
	}
    }

  /* Now copy the remaining rules */
  while (i < old_tzd.ds_ruleset_count)
    {
      prev_start_ds_rule = start_ds_rule;

      if (mark_ruleset[i] == 1)
	{
	  start_ds_rule += old_tzd.ds_rulesets[i].count;
	  err_status = init_ds_ruleset (&all_ds_rulesets[k], &old_tzd, i, prev_start_ds_rule);
	  if (err_status != NO_ERROR)
	    {
	      goto exit;
	    }
	  start = old_tzd.ds_rulesets[i].index_start;
	  k++;
	}

      for (l = prev_start_ds_rule; l < start_ds_rule; l++)
	{
	  err_status = copy_ds_rule (&all_ds_rules[l], &old_tzd, start + l - prev_start_ds_rule);
	  if (err_status != NO_ERROR)
	    {
	      goto exit;
	    }
	}
      i++;
    }

  while (j < tzd->ds_ruleset_count)
    {
      err_status = init_ds_ruleset (&all_ds_rulesets[k], tzd, j, -tzd->ds_rulesets[j].index_start);
      if (err_status != NO_ERROR)
	{
	  goto exit;
	}
      j++;
      k++;
    }

  /* Now copy the new daylight saving rules that were added in the current timezone library */
  assert (k == all_ds_ruleset_count);
  for (i = 0; i < all_ds_ruleset_count; i++)
    {
      if (all_ds_rulesets[i].index_start < 0)
	{
	  prev_start_ds_rule = start_ds_rule;
	  start = -all_ds_rulesets[i].index_start;
	  all_ds_rulesets[i].index_start = prev_start_ds_rule;
	  start_ds_rule += all_ds_rulesets[i].count;

	  /* Now copy the new daylight saving rules to the end */
	  for (j = prev_start_ds_rule; j < start_ds_rule; j++)
	    {
	      err_status = copy_ds_rule (&all_ds_rules[j], tzd, start + j - prev_start_ds_rule);
	      if (err_status != NO_ERROR)
		{
		  goto exit;
		}
	    }
	}
    }

  /* Now we need to fix the ds_ruleset index in the offset rules */
  for (i = 0; i < all_timezones_count; i++)
    {
      zone_id = tzc_find_timezone_names (tzd, all_timezone_names[i]);

      if (zone_id == -1)
	{
	  start = old_tzd.timezones[i].gmt_off_rule_start;
	  tzd_or_old_tzd = &old_tzd;
	}
      else
	{
	  start = timezones[zone_id].gmt_off_rule_start;
	  tzd_or_old_tzd = tzd;
	}

      for (j = 0; j < all_timezones[i].gmt_off_rule_count; j++)
	{
	  int tzd_or_old_tzd_ds_ruleset = tzd_or_old_tzd->offset_rules[start + j].ds_ruleset;

	  if (tzd_or_old_tzd->offset_rules[start + j].ds_type != DS_TYPE_FIXED)
	    {
	      ruleset_name = tzd_or_old_tzd->ds_rulesets[tzd_or_old_tzd_ds_ruleset].ruleset_name;
	    }
	  else
	    {
	      ruleset_name = NULL;
	    }

	  if (ruleset_name != NULL)
	    {
	      ruleset_id = tzc_get_ds_ruleset_by_name (all_ds_rulesets, all_ds_ruleset_count, ruleset_name);
	      all_offset_rules[all_timezones[i].gmt_off_rule_start + j].ds_ruleset = ruleset_id;
	    }
	}
    }

  qsort (all_names, all_timezones_and_aliases_count, sizeof (TZ_NAME), comp_func_tz_names);

exit:
  /* Free all data structures except for the windows zone map data structure and the leap seconds array */

  tzc_free_tz_data (tzd, false);
  if (mark_ruleset != NULL)
    {
      free_and_init (mark_ruleset);
    }
  if (old_tzd_offset_rule_map != NULL)
    {
      free_and_init (old_tzd_offset_rule_map);
    }
  if (tzd_offset_rule_map != NULL)
    {
      free_and_init (tzd_offset_rule_map);
    }

  /* Now use the new data structures */
  tzd->countries = all_countries;
  tzd->country_count = all_country_count;
  tzd->timezone_names = all_timezone_names;
  tzd->timezones = all_timezones;
  tzd->timezone_count = all_timezones_count;
  tzd->names = all_names;
  tzd->name_count = all_timezones_and_aliases_count;
  tzd->offset_rules = all_offset_rules;
  tzd->offset_rule_count = all_offset_rule_count;
  tzd->ds_rulesets = all_ds_rulesets;
  tzd->ds_ruleset_count = all_ds_ruleset_count;
  tzd->ds_rules = all_ds_rules;
  tzd->ds_rule_count = all_ds_rule_count;

  if (err_status == NO_ERROR)
    {
      old_names = old_tzd.names;
      old_timezones = old_tzd.timezones;
      names = tzd->names;
      timezones = tzd->timezones;

      for (i = 0; i < ZONE_MAX; i++)
	{
	  tz_Is_backward_compatible_timezone[i] = true;
	}

      for (i = 0; i < old_tzd.name_count; i++)
	{
	  int tzd_zone_id;
	  int old_start, old_count;
	  int new_start, new_count;

	  tzd_zone_id = tzc_find_timezone_names (tzd, old_names[i].name);

	  /* This should not ever happen */
	  assert (tzd_zone_id != -1);

	  if (old_timezones[old_names[i].zone_id].gmt_off_rule_count != timezones[tzd_zone_id].gmt_off_rule_count)
	    {
	      tz_Is_backward_compatible_timezone[i] = false;
	      continue;
	    }

	  old_start = old_timezones[old_names[i].zone_id].gmt_off_rule_start;
	  new_start = timezones[tzd_zone_id].gmt_off_rule_start;
	  old_count = old_timezones[old_names[i].zone_id].gmt_off_rule_count;
	  new_count = timezones[tzd_zone_id].gmt_off_rule_count;

	  for (j = old_start, k = new_start; j < old_start + old_count && k < new_start + new_count; j++, k++)
	    {
	      TZ_OFFSET_RULE rule1, rule2;

	      rule1 = old_tzd.offset_rules[j];
	      rule2 = tzd->offset_rules[k];

	      if (rule1.ds_type != rule2.ds_type)
		{
		  tz_Is_backward_compatible_timezone[i] = false;
		  break;
		}

	      if (comp_offset_rules (&rule1, &rule2) == false)
		{
		  tz_Is_backward_compatible_timezone[i] = false;
		  break;
		}

	      if (rule1.ds_type != DS_TYPE_FIXED)
		{
		  int ds_rule1_count = old_tzd.ds_rulesets[rule1.ds_ruleset].count;
		  int ds_rule2_count = tzd->ds_rulesets[rule2.ds_ruleset].count;
		  int ds_rule1_start = old_tzd.ds_rulesets[rule1.ds_ruleset].index_start;
		  int ds_rule2_start = tzd->ds_rulesets[rule2.ds_ruleset].index_start;
		  int rule1_index = ds_rule1_start;
		  int rule2_index = ds_rule2_start;

		  if (strcmp (old_tzd.ds_rulesets[rule1.ds_ruleset].ruleset_name,
			      tzd->ds_rulesets[rule2.ds_ruleset].ruleset_name) != 0)
		    {
		      tz_Is_backward_compatible_timezone[i] = false;
		      break;
		    }

		  if (ds_rule1_count != ds_rule2_count)
		    {
		      tz_Is_backward_compatible_timezone[i] = false;
		      break;
		    }

		  while ((rule1_index < ds_rule1_start + ds_rule1_count)
			 && (rule2_index < ds_rule2_start + ds_rule2_count))
		    {
		      TZ_DS_RULE ds_rule1 = old_tzd.ds_rules[rule1_index];
		      TZ_DS_RULE ds_rule2 = tzd->ds_rules[rule2_index];

		      if (comp_ds_rules (&ds_rule1, &ds_rule2) == false)
			{
			  break;
			}
		      rule1_index++, rule2_index++;
		    }

		  if (rule1_index < ds_rule1_start + ds_rule1_count)
		    {
		      tz_Is_backward_compatible_timezone[i] = false;
		      break;
		    }
		}
	      else if (rule1.ds_ruleset != rule2.ds_ruleset)
		{
		  tz_Is_backward_compatible_timezone[i] = false;
		  break;
		}
	    }
	}

      if (is_compat == false)
	{
	  printf ("Updating data in the tables containing timezone data types...\n");
	  err_status = ER_TZ_COMPILE_ERROR;
	}
    }

  return err_status;
}
#endif

/*
 * tzc_compute_timezone_checksum() - Computes an MD5 for the timezone data
 *                                   structures
 * Returns:
 * tzd (in/out): timezone library
 * type(in): tells which make_tz mode was used
 */
static int
tzc_compute_timezone_checksum (TZ_DATA * tzd, TZ_GEN_TYPE type)
{
  char *input_buf;
  int size = 0;
  int i;
  int error = NO_ERROR;
  char *buf;

  for (i = 0; i < tzd->country_count; i++)
    {
      size += sizeof (tzd->countries[i].code);
      size += sizeof (tzd->countries[i].full_name);
    }

  for (i = 0; i < tzd->timezone_count; i++)
    {
      size += sizeof (tzd->timezones[i].country_id);
      size += sizeof (tzd->timezones[i].gmt_off_rule_count);
      size += sizeof (tzd->timezones[i].gmt_off_rule_start);
      size += sizeof (tzd->timezones[i].zone_id);
    }

  for (i = 0; i < tzd->timezone_count; i++)
    {
      size += strlen (tzd->timezone_names[i]);
    }

  for (i = 0; i < tzd->offset_rule_count; i++)
    {
      size += sizeof (tzd->offset_rules[i].gmt_off);
      size += sizeof (tzd->offset_rules[i].ds_ruleset);
      size += sizeof (tzd->offset_rules[i].until_year);
      size += sizeof (tzd->offset_rules[i].until_mon);
      size += sizeof (tzd->offset_rules[i].until_day);
      size += sizeof (tzd->offset_rules[i].until_hour);
      size += sizeof (tzd->offset_rules[i].until_min);
      size += sizeof (tzd->offset_rules[i].until_sec);
      size += sizeof (tzd->offset_rules[i].until_time_type);
      size += sizeof (tzd->offset_rules[i].until_flag);
      size += sizeof (tzd->offset_rules[i].ds_type);
      size += sizeof (tzd->offset_rules[i].julian_date);
      if (tzd->offset_rules[i].std_format != NULL)
	{
	  size += strlen (tzd->offset_rules[i].std_format);
	}
      if (tzd->offset_rules[i].save_format != NULL)
	{
	  size += strlen (tzd->offset_rules[i].save_format);
	}
      if (tzd->offset_rules[i].var_format != NULL)
	{
	  size += strlen (tzd->offset_rules[i].var_format);
	}
    }

  for (i = 0; i < tzd->name_count; i++)
    {
      size += sizeof (tzd->names[i].is_alias);
      size += sizeof (tzd->names[i].zone_id);
      size += strlen (tzd->names[i].name);
    }

  for (i = 0; i < tzd->ds_ruleset_count; i++)
    {
      size += sizeof (tzd->ds_rulesets[i].count);
      size += sizeof (tzd->ds_rulesets[i].index_start);
      size += strlen (tzd->ds_rulesets[i].ruleset_name);
    }

  for (i = 0; i < tzd->ds_rule_count; i++)
    {
      size += sizeof (tzd->ds_rules[i].from_year);
      size += sizeof (tzd->ds_rules[i].to_year);
      size += sizeof (tzd->ds_rules[i].in_month);
      size += sizeof (tzd->ds_rules[i].change_on);
      size += sizeof (tzd->ds_rules[i].at_time);
      size += sizeof (tzd->ds_rules[i].at_time_type);
      size += sizeof (tzd->ds_rules[i].save_time);
      if (tzd->ds_rules[i].letter_abbrev != NULL)
	{
	  size += strlen (tzd->ds_rules[i].letter_abbrev);
	}
    }

  for (i = 0; i < tzd->ds_leap_sec_count; i++)
    {
      size += sizeof (tzd->ds_leap_sec[i].corr_negative);
      size += sizeof (tzd->ds_leap_sec[i].day);
      size += sizeof (tzd->ds_leap_sec[i].is_rolling);
      size += sizeof (tzd->ds_leap_sec[i].month);
      size += sizeof (tzd->ds_leap_sec[i].year);
    }

  input_buf = (char *) calloc (size, sizeof (char));
  if (input_buf == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  buf = input_buf;

  for (i = 0; i < tzd->country_count; i++)
    {
      memcpy (buf, tzd->countries[i].code, sizeof (tzd->countries[i].code));
      buf += sizeof (tzd->countries[i].code);
      memcpy (buf, tzd->countries[i].full_name, sizeof (tzd->countries[i].full_name));
      buf += sizeof (tzd->countries[i].full_name);
    }

  for (i = 0; i < tzd->timezone_count; i++)
    {
      BUF_PUT_INT32 (buf, tzd->timezones[i].country_id);
      BUF_PUT_INT32 (buf, tzd->timezones[i].gmt_off_rule_count);
      BUF_PUT_INT32 (buf, tzd->timezones[i].gmt_off_rule_start);
      BUF_PUT_INT32 (buf, tzd->timezones[i].zone_id);
    }

  for (i = 0; i < tzd->timezone_count; i++)
    {
      memcpy (buf, tzd->timezone_names[i], strlen (tzd->timezone_names[i]));
      buf += strlen (tzd->timezone_names[i]);
    }

  for (i = 0; i < tzd->offset_rule_count; i++)
    {
      BUF_PUT_INT32 (buf, tzd->offset_rules[i].gmt_off);
      BUF_PUT_INT32 (buf, tzd->offset_rules[i].ds_ruleset);
      BUF_PUT_INT16 (buf, tzd->offset_rules[i].until_year);
      memcpy (buf, &tzd->offset_rules[i].until_mon, sizeof (tzd->offset_rules[i].until_mon));
      buf += sizeof (tzd->offset_rules[i].until_mon);
      memcpy (buf, &tzd->offset_rules[i].until_day, sizeof (tzd->offset_rules[i].until_day));
      buf += sizeof (tzd->offset_rules[i].until_day);
      memcpy (buf, &tzd->offset_rules[i].until_hour, sizeof (tzd->offset_rules[i].until_hour));
      buf += sizeof (tzd->offset_rules[i].until_hour);
      memcpy (buf, &tzd->offset_rules[i].until_min, sizeof (tzd->offset_rules[i].until_min));
      buf += sizeof (tzd->offset_rules[i].until_min);
      memcpy (buf, &tzd->offset_rules[i].until_sec, sizeof (tzd->offset_rules[i].until_sec));
      buf += sizeof (tzd->offset_rules[i].until_sec);
      BUF_PUT_INT32 (buf, tzd->offset_rules[i].until_time_type);
      BUF_PUT_INT32 (buf, tzd->offset_rules[i].until_flag);
      BUF_PUT_INT32 (buf, tzd->offset_rules[i].ds_type);
      BUF_PUT_INT32 (buf, tzd->offset_rules[i].julian_date);
      if (tzd->offset_rules[i].std_format != NULL)
	{
	  memcpy (buf, tzd->offset_rules[i].std_format, strlen (tzd->offset_rules[i].std_format));
	  buf += strlen (tzd->offset_rules[i].std_format);
	}
      if (tzd->offset_rules[i].save_format != NULL)
	{
	  memcpy (buf, tzd->offset_rules[i].save_format, strlen (tzd->offset_rules[i].save_format));
	  buf += strlen (tzd->offset_rules[i].save_format);
	}
      if (tzd->offset_rules[i].var_format != NULL)
	{
	  memcpy (buf, tzd->offset_rules[i].var_format, strlen (tzd->offset_rules[i].var_format));
	  buf += strlen (tzd->offset_rules[i].var_format);
	}
    }

  for (i = 0; i < tzd->name_count; i++)
    {
      memcpy (buf, &tzd->names[i].is_alias, sizeof (tzd->names[i].is_alias));
      buf += sizeof (tzd->names[i].is_alias);
      BUF_PUT_INT32 (buf, tzd->names[i].zone_id);
      memcpy (buf, tzd->names[i].name, strlen (tzd->names[i].name));
      buf += strlen (tzd->names[i].name);
    }

  for (i = 0; i < tzd->ds_ruleset_count; i++)
    {
      BUF_PUT_INT32 (buf, tzd->ds_rulesets[i].count);
      BUF_PUT_INT32 (buf, tzd->ds_rulesets[i].index_start);
      memcpy (buf, tzd->ds_rulesets[i].ruleset_name, strlen (tzd->ds_rulesets[i].ruleset_name));
      buf += strlen (tzd->ds_rulesets[i].ruleset_name);
    }

  for (i = 0; i < tzd->ds_rule_count; i++)
    {
      BUF_PUT_INT16 (buf, tzd->ds_rules[i].from_year);
      BUF_PUT_INT16 (buf, tzd->ds_rules[i].to_year);
      memcpy (buf, &tzd->ds_rules[i].in_month, sizeof (tzd->ds_rules[i].in_month));
      buf += sizeof (tzd->ds_rules[i].in_month);
      BUF_PUT_INT32 (buf, tzd->ds_rules[i].change_on.type);
      memcpy (buf, &tzd->ds_rules[i].change_on.day_of_month, sizeof (tzd->ds_rules[i].change_on.day_of_month));
      buf += sizeof (tzd->ds_rules[i].change_on.day_of_month);
      memcpy (buf, &tzd->ds_rules[i].change_on.day_of_week, sizeof (tzd->ds_rules[i].change_on.day_of_week));
      buf += sizeof (tzd->ds_rules[i].change_on.day_of_week);
      BUF_PUT_INT32 (buf, tzd->ds_rules[i].at_time);
      BUF_PUT_INT32 (buf, tzd->ds_rules[i].at_time_type);
      BUF_PUT_INT32 (buf, tzd->ds_rules[i].save_time);
      if (tzd->ds_rules[i].letter_abbrev != NULL)
	{
	  memcpy (buf, tzd->ds_rules[i].letter_abbrev, strlen (tzd->ds_rules[i].letter_abbrev));
	  buf += strlen (tzd->ds_rules[i].letter_abbrev);
	}
    }

  for (i = 0; i < tzd->ds_leap_sec_count; i++)
    {
      memcpy (buf, &tzd->ds_leap_sec[i].corr_negative, sizeof (tzd->ds_leap_sec[i].corr_negative));
      buf += sizeof (tzd->ds_leap_sec[i].corr_negative);
      memcpy (buf, &tzd->ds_leap_sec[i].day, sizeof (tzd->ds_leap_sec[i].day));
      buf += sizeof (tzd->ds_leap_sec[i].day);
      memcpy (buf, &tzd->ds_leap_sec[i].is_rolling, sizeof (tzd->ds_leap_sec[i].is_rolling));
      buf += sizeof (tzd->ds_leap_sec[i].is_rolling);
      memcpy (buf, &tzd->ds_leap_sec[i].month, sizeof (tzd->ds_leap_sec[i].month));
      BUF_PUT_INT16 (buf, tzd->ds_leap_sec[i].year);
    }

  memset (tzd->checksum, 0, sizeof (tzd->checksum));
  error = crypt_md5_buffer_hex (input_buf, size, tzd->checksum);

  free (input_buf);
  return error;
}

#if defined (SA_MODE)
/*
 * execute_query() - Execute the query given by str
 *
 * Return: error or no error
 * str (in): query string
 * result (out): query result
 *
 */
static int
execute_query (const char *str, DB_QUERY_RESULT ** result)
{
  DB_SESSION *session = NULL;
  STATEMENT_ID stmt_id;
  int error = NO_ERROR;

  session = db_open_buffer (str);
  if (session == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto exit;
    }

  stmt_id = db_compile_statement (session);
  if (stmt_id < 0)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto exit;
    }

  error = db_execute_statement_local (session, stmt_id, result);

exit:
  if (session != NULL)
    {
      db_close_session (session);
    }

  return error;
}

/*
 * tzc_update() - Do a data migration in case that tzc_extend fails
 *
 * Returns: error or no error
 * tzd (in): Timezone library used to the data migration
 * database_name(in): Database name for which to do data migration or NULL if data migration should be done
 *		      for all the databases
 */
static int
tzc_update (TZ_DATA * tzd, const char *database_name)
{
  DB_INFO *db_info_list = NULL;
  DB_INFO *db_info = NULL;
  int error = NO_ERROR;

  tz_set_new_timezone_data (tzd);

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);

  tz_Compare_datetimetz_tz_id = true;
  tz_Compare_timestamptz_tz_id = true;

  /* Read the directory with the databases names */
  error = cfg_read_directory (&db_info_list, false);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  /* Iterate through all the databases */
  for (db_info = db_info_list; db_info != NULL; db_info = db_info->next)
    {
      if (database_name != NULL && strcmp (database_name, db_info->name) != 0)
	{
	  continue;
	}

      error = tzc_update_internal (database_name);
      if (error != NO_ERROR)
	{
	  goto exit;
	}
    }

exit:
  cfg_free_directory (db_info_list);

  tz_Compare_datetimetz_tz_id = false;
  tz_Compare_timestamptz_tz_id = false;

  return error;
}

/*
 * tzc_update_internal() - Do a data migration in case that tzc_extend fails
 *
 * Returns: error or no error
 * database_name(in): Database name for which to do data migration
 */
static int
tzc_update_internal (const char *database_name)
{
  const char *program_name = "extend";
  DB_OBJLIST *table_list = NULL, *table = NULL;
  DB_QUERY_RESULT *result = NULL;
  QUERY_BUF update_query;
  QUERY_BUF update_set;
  QUERY_BUF update_where;
  bool is_first_column = true;
  bool has_timezone_column = false;
  int error = NO_ERROR;

  assert (database_name != NULL);

  printf ("Opening database %s\n", database_name);

  /* Open the database */
  error = db_restart (program_name, TRUE, database_name);
  if (error != NO_ERROR)
    {
      printf ("Error while opening database %s\n", database_name);
      goto exit_on_error;
    }

  printf ("Updating database %s...\n", database_name);

  table_list = db_get_all_classes ();
  if (table_list == NULL)
    {
      error = db_error_code ();
      if (error != NO_ERROR)
	{
	  printf ("Error while listing tables for database %s\n", database_name);
	  goto free_resource_1;
	}
    }

  /* First get the names for the tables in the database */
  for (table = table_list; table != NULL; table = table->next)
    {
      const char *table_name = NULL;
      DB_ATTRIBUTE *column_list = NULL, *column = NULL;

      if (db_is_system_class (table->op) == TRUE)
	{
	  continue;
	}

      table_name = db_get_class_name (table->op);	/* owner_name.table_name */

      printf ("Updating table %s...\n", table_name);

      column_list = db_get_attributes (table->op);
      if (column_list == NULL)
	{
	  error = db_error_code ();
	  if (error != NO_ERROR)
	    {
	      printf ("Error while listing columns for table %s\n", table_name);
	      goto free_resource_2;
	    }
	}

      printf ("We will update the following columns:\n");

      is_first_column = true;
      has_timezone_column = false;

      for (column = column_list; column != NULL; column = db_attribute_next (column))
	{
	  const char *column_name = db_attribute_name (column);
	  DB_DOMAIN *column_domain = db_attribute_domain (column);
	  DB_TYPE column_type = db_domain_type (column_domain);

	  /* Now do the update if the datatype is of timezone type */
	  switch (column_type)
	    {
	    case DB_TYPE_DATETIMETZ:
	    case DB_TYPE_DATETIMELTZ:
	    case DB_TYPE_TIMESTAMPTZ:
	    case DB_TYPE_TIMESTAMPLTZ:
	      has_timezone_column = true;

	      /* We are going to make an update query for each table which includes a timezone column like:
	       *  UPDATE [t] SET [tzc1] = CONV_TZ([tzc1]), [tzc2] = CONV_TZ([tzc2]) ...
	       *  WHERE [tzc1] != CONV_TZ([tzc1]) OR [tzc2] != CONV_TZ([tzc2]) ... ;
	       */
	      if (is_first_column == true)
		{
		  memset (&update_set, 0, sizeof (QUERY_BUF));
		  memset (&update_where, 0, sizeof (QUERY_BUF));

		  if (tz_write_query_string
		      (&update_set, "UPDATE [%s] SET [%s] = conv_tz ([%s])", table_name, column_name,
		       column_name) == NULL)
		    {
		      printf ("Failed to create a query to update the timezone of column %s in table %s.\n", table_name,
			      column_name);
		      goto free_resource_3;
		    }

		  if (tz_write_query_string (&update_where, " WHERE [%s] != conv_tz ([%s])", column_name, column_name)
		      == NULL)
		    {
		      printf ("Failed to create a query to update the timezone of column %s in table %s.\n", table_name,
			      column_name);
		      goto free_resource_3;
		    }
		  is_first_column = false;
		}
	      else
		{
		  if (tz_write_query_string (&update_set, ", [%s] = conv_tz ([%s])", column_name, column_name) == NULL)
		    {
		      printf ("Failed to create a query to update the timezone of column %s in table %s.\n", table_name,
			      column_name);
		      goto free_resource_3;
		    }

		  if (tz_write_query_string (&update_where, " OR [%s] != conv_tz ([%s])", column_name, column_name) ==
		      NULL)
		    {
		      printf ("Failed to create a query to update the timezone of column %s in table %s.\n", table_name,
			      column_name);
		      goto free_resource_3;
		    }
		}
	      printf ("%s ", column_name);
	      break;

	    default:
	      continue;
	    }
	}
      printf ("\n");

      /* If we have at least a column that is of timezone data type then execute the query */
      if (has_timezone_column == true)
	{
	  update_query.buf = (char *) malloc ((update_set.len + update_where.len) * sizeof (char));
	  if (update_query.buf == NULL)
	    {
	      printf ("Failed to allocate memory for query buffer.\n");
	      goto free_resource_3;
	    }
	  strcpy (update_query.buf, update_set.buf);
	  strcpy (update_query.buf + update_set.len, update_where.buf);
	  free_and_init (update_set.buf);
	  free_and_init (update_where.buf);

	  error = execute_query (update_query.buf, &result);
	  if (error < 0)
	    {
	      printf ("Error while updating table %s\n", table_name);
	      db_abort_transaction ();
	      goto free_resource_4;
	    }
	  db_query_end (result);
	  free_and_init (update_query.buf);
	}

      printf ("Finished updating table %s\n", table_name);
    }

  db_objlist_free (table_list);
  table_list = NULL;

  db_commit_transaction ();
  printf ("Finished updating database %s\n", database_name);

  printf ("Shutting down database %s...\n", database_name);
  db_shutdown ();

  return NO_ERROR;

free_resource_4:
  free_and_init (update_query.buf);

free_resource_3:
  free_and_init (update_set.buf);
  free_and_init (update_where.buf);

free_resource_2:
  db_objlist_free (table_list);
  table_list = NULL;

free_resource_1:
  db_shutdown ();

exit_on_error:
  return error;
}

static QUERY_BUF *
tz_write_query_string (QUERY_BUF * query, const char *format, ...)
{
#define QUERY_BUF_INIT_SIZE 4096
#define QUERY_BUF_UNIT_SIZE 1024

  char *backup = NULL;
  int len;

  va_list args;

  if (query->buf == NULL)
    {
      query->buf = (char *) malloc (QUERY_BUF_INIT_SIZE * sizeof (char));
      if (query->buf == NULL)
	{
	  printf ("Failed to allocate memory for query buffer.\n");
	  return NULL;
	}
      query->size = QUERY_BUF_INIT_SIZE;

      va_start (args, format);
      len = vsprintf (query->buf, format, args);
      va_end (args);
      query->last = query->buf + len;
      query->len = len;
    }
  else
    {
      va_start (args, format);
      len = vsnprintf (NULL, 0, format, args);
      va_end (args);
      if ((query->len + len) > query->size)
	{
	  backup = query->buf;
	  query->buf = (char *) realloc (query->buf, (query->size + QUERY_BUF_UNIT_SIZE) * sizeof (char));
	  if (query->buf == NULL)
	    {
	      printf ("Failed to allocate memory for query buffer.\n");
	      query->buf = backup;
	      return NULL;
	    }
	  query->size += QUERY_BUF_UNIT_SIZE;
	  query->last = query->buf + query->len;
	}

      va_start (args, format);
      len = vsprintf (query->last, format, args);
      va_end (args);
      query->last += len;
      query->len += len;
    }

  return query;

#undef QUERY_BUF_INIT_SIZE
#undef QUERY_BUF_UNIT_SIZE
}
#endif
