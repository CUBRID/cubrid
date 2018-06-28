/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * loader_grammar.y - loader grammar file
 */

%skeleton "lalr1.cc"
%require "3.0"
%defines
%define api.namespace { cubloader }
%define parser_class_name { loader_parser }
%locations
%no-lines

%parse-param { loader_scanner &scanner }
%parse-param { loader_driver &driver }
%lex-param { loader_driver &driver }

%define parse.assert

%union {
  int intval;
  LDR_STRING *string;
  LDR_CLASS_COMMAND_SPEC *cmd_spec;
  LDR_CONSTRUCTOR_SPEC *ctor_spec;
  LDR_CONSTANT *constant;
  LDR_OBJECT_REF *obj_ref;
};

%code requires {
#include "loader.h"

namespace cubloader
{
  class loader_driver;
  class loader_scanner;
}
}

%code {
#include "loader_driver.hpp"
#include "memory_alloc.h"

#undef yylex
#define yylex scanner.yylex

/*#define PARSER_DEBUG*/

#ifdef PARSER_DEBUG
#define DBG_PRINT(s) printf("rule: %s\n", (s));
#else
#define DBG_PRINT(s)
#endif
}

%token NL
%token NULL_
%token CLASS
%token SHARED
%token DEFAULT
%token DATE_
%token TIME
%token UTIME
%token TIMESTAMP
%token TIMESTAMPLTZ
%token TIMESTAMPTZ
%token DATETIME
%token DATETIMELTZ
%token DATETIMETZ
%token CMD_ID
%token CMD_CLASS
%token CMD_CONSTRUCTOR
%token REF_ELO_INT
%token REF_ELO_EXT
%token REF_USER
%token REF_CLASS
%token OBJECT_REFERENCE
%token OID_DELIMETER
%token SET_START_BRACE
%token SET_END_BRACE
%token START_PAREN
%token END_PAREN
%token <string> REAL_LIT
%token <string> INT_LIT
%token <intval> OID_
%token <string> TIME_LIT4
%token <string> TIME_LIT42
%token <string> TIME_LIT3
%token <string> TIME_LIT31
%token <string> TIME_LIT2
%token <string> TIME_LIT1
%token <string> DATE_LIT2
%token YEN_SYMBOL
%token WON_SYMBOL
%token BACKSLASH
%token DOLLAR_SYMBOL
%token TURKISH_LIRA_CURRENCY
%token BRITISH_POUND_SYMBOL
%token CAMBODIAN_RIEL_SYMBOL
%token CHINESE_RENMINBI_SYMBOL
%token INDIAN_RUPEE_SYMBOL
%token RUSSIAN_RUBLE_SYMBOL
%token AUSTRALIAN_DOLLAR_SYMBOL
%token CANADIAN_DOLLAR_SYMBOL
%token BRASILIAN_REAL_SYMBOL
%token ROMANIAN_LEU_SYMBOL
%token EURO_SYMBOL
%token SWISS_FRANC_SYMBOL
%token DANISH_KRONE_SYMBOL
%token NORWEGIAN_KRONE_SYMBOL
%token BULGARIAN_LEV_SYMBOL
%token VIETNAMESE_DONG_SYMBOL
%token CZECH_KORUNA_SYMBOL
%token POLISH_ZLOTY_SYMBOL
%token SWEDISH_KRONA_SYMBOL
%token CROATIAN_KUNA_SYMBOL
%token SERBIAN_DINAR_SYMBOL

%token <string> IDENTIFIER
%token Quote
%token DQuote
%token NQuote
%token BQuote
%token XQuote
%token <string> SQS_String_Body
%token <string> DQS_String_Body
%token COMMA

%type <intval> attribute_list_qualifier
%type <cmd_spec> class_commamd_spec
%type <ctor_spec> constructor_spec
%type <string> attribute_name
%type <string> argument_name
%type <string> attribute_names
%type <string> attribute_list
%type <string> argument_names
%type <string> constructor_argument_list
%type <constant> constant
%type <constant> constant_list

%type <constant> ansi_string
%type <constant> dq_string
%type <constant> nchar_string
%type <constant> bit_string
%type <constant> sql2_date
%type <constant> sql2_time
%type <constant> sql2_timestamp
%type <constant> sql2_timestampltz
%type <constant> sql2_timestamptz
%type <constant> sql2_datetime
%type <constant> sql2_datetimeltz
%type <constant> sql2_datetimetz
%type <constant> utime
%type <constant> monetary
%type <constant> object_reference
%type <constant> set_constant
%type <constant> system_object_reference

%type <obj_ref> class_identifier
%type <string> instance_number
%type <intval> ref_type
%type <intval> object_id

%type <constant> set_elements

%start loader_start
%%

loader_start :
  {
    // add here any initialization code
  }
  loader_lines
  {
    ldr_act_finish (ldr_Current_context, 0);
  }
  ;

loader_lines :
  line
  {
    DBG_PRINT ("line");
  }
  |
  loader_lines line
  {
    DBG_PRINT ("line_list line");
  }
  ;

line :
  one_line NL
  {
    DBG_PRINT ("one_line");
    driver.set_in_instance_line (true);
  }
  |
  NL
  {
    driver.set_in_instance_line (true);
  }
  ;

one_line :
  command_line
  {
    DBG_PRINT ("command_line");
    driver.reset_pool_indexes ();
  }
  |
  instance_line
  {
    DBG_PRINT ("instance_line");
    ldr_act_finish_line (ldr_Current_context);
    driver.reset_pool_indexes ();
  }
  ;

command_line :
  class_command
  {
    DBG_PRINT ("class_command");
  }
  |
  id_command
  {
    DBG_PRINT ("id_command");
  }
  ;

id_command :
  CMD_ID IDENTIFIER INT_LIT
  {
    skip_current_class = false;

    ldr_act_start_id (ldr_Current_context, $2->val);
    ldr_act_set_id (ldr_Current_context, atoi ($3->val));

    driver.free_ldr_string (&$2);
    driver.free_ldr_string (&$3);
  }
  ;

class_command :
  CMD_CLASS IDENTIFIER class_commamd_spec
  {
    LDR_CLASS_COMMAND_SPEC *cmd_spec;
    LDR_STRING *class_name;
    LDR_STRING *attr, *save, *args;

    DBG_PRINT ("class_commamd_spec");

    class_name = $2;
    cmd_spec = $3;

    ldr_act_set_skip_current_class (class_name->val, class_name->size);
    ldr_act_init_context (ldr_Current_context, class_name->val, class_name->size);

    if (cmd_spec->qualifier != LDR_ATTRIBUTE_ANY)
      {
	ldr_act_restrict_attributes (ldr_Current_context, (LDR_ATTRIBUTE_TYPE) cmd_spec->qualifier);
      }

    for (attr = cmd_spec->attr_list; attr; attr = attr->next)
      {
	ldr_act_add_attr (ldr_Current_context, attr->val, attr->size);
      }

    ldr_act_check_missing_non_null_attrs (ldr_Current_context);

    if (cmd_spec->ctor_spec)
      {
	ldr_act_set_constructor (ldr_Current_context, cmd_spec->ctor_spec->idname->val);

	for (args = cmd_spec->ctor_spec->arg_list; args; args = args->next)
	  {
	    ldr_act_add_argument (ldr_Current_context, args->val);
	  }

	for (args = cmd_spec->ctor_spec->arg_list; args; args = save)
	  {
	    save = args->next;
	    driver.free_ldr_string (&args);
	  }

	driver.free_ldr_string (&(cmd_spec->ctor_spec->idname));
	free_and_init (cmd_spec->ctor_spec);
      }

    for (attr = cmd_spec->attr_list; attr; attr = save)
      {
	save = attr->next;
	driver.free_ldr_string (&attr);
      }

    driver.free_ldr_string (&class_name);
    free_and_init (cmd_spec);
  }
  ;

class_commamd_spec :
  attribute_list
  {
    DBG_PRINT ("attribute_list");
    $$ = driver.make_class_command_spec (LDR_ATTRIBUTE_ANY, $1, NULL);
  }
  |
  attribute_list constructor_spec
  {
    DBG_PRINT ("attribute_list constructor_spec");
    $$ = driver.make_class_command_spec (LDR_ATTRIBUTE_ANY, $1, $2);
  }
  |
  attribute_list_qualifier attribute_list
  {
    DBG_PRINT ("attribute_list_qualifier attribute_list");
    $$ = driver.make_class_command_spec ($1, $2, NULL);
  }
  |
  attribute_list_qualifier attribute_list constructor_spec
  {
    DBG_PRINT ("attribute_list_qualifier attribute_list constructor_spec");
    $$ = driver.make_class_command_spec ($1, $2, $3);
  }
  ;

attribute_list_qualifier :
  CLASS
  {
    DBG_PRINT ("CLASS");
    $$ = LDR_ATTRIBUTE_CLASS;
  }
  |
  SHARED
  {
    DBG_PRINT ("SHARED");
    $$ = LDR_ATTRIBUTE_SHARED;
  }
  |
  DEFAULT
  {
    DBG_PRINT ("DEFAULT");
    $$ = LDR_ATTRIBUTE_DEFAULT;
  }
  ;

attribute_list :
  START_PAREN END_PAREN
  {
    $$ = NULL;
  }
  |
  START_PAREN attribute_names END_PAREN
  {
    $$ = $2;
  }
  ;

attribute_names :
  attribute_name
  {
    DBG_PRINT ("attribute_name");
    $$ = driver.append_string_list (NULL, $1);
  }
  |
  attribute_names attribute_name
  {
    DBG_PRINT ("attribute_names attribute_name");
    $$ = driver.append_string_list ($1, $2);
  }
  |
  attribute_names COMMA attribute_name
  {
    DBG_PRINT ("attribute_names COMMA attribute_name");
    $$ = driver.append_string_list ($1, $3);
  }
  ;

attribute_name :
  IDENTIFIER
  {
    $$ = $1;
  }
  ;

constructor_spec :
  CMD_CONSTRUCTOR IDENTIFIER constructor_argument_list
  {
    $$ = driver.make_constructor_spec ($2, $3);
  }
  ;

constructor_argument_list :
  START_PAREN END_PAREN
  {
    $$ = NULL;
  }
  |
  START_PAREN argument_names END_PAREN
  {
    $$ = $2;
  }
  ;

argument_names :
  argument_name
  {
    DBG_PRINT ("argument_name");
    $$ = driver.append_string_list (NULL, $1);
  }
  |
  argument_names argument_name
  {
    DBG_PRINT ("argument_names argument_name");
    $$ = driver.append_string_list ($1, $2);
  }
  |
  argument_names COMMA argument_name
  {
    DBG_PRINT ("argument_names COMMA argument_name");
    $$ = driver.append_string_list ($1, $3);
  }
  ;

argument_name :
  IDENTIFIER
  {
    $$ = $1;
  };
  ;

instance_line :
  object_id
  {
    skip_current_instance = false;
    ldr_act_start_instance (ldr_Current_context, $1, NULL);
  }
  |
  object_id constant_list
  {
    skip_current_instance = false;
    ldr_act_start_instance (ldr_Current_context, $1, $2);
    ldr_process_constants ($2);
  }
  |
  constant_list
  {
    skip_current_instance = false;
    ldr_act_start_instance (ldr_Current_context, -1, $1);
    ldr_process_constants ($1);
  }
  ;

object_id :
  OID_
  {
    $$ = $1;
  }
  ;

constant_list :
  constant
  {
    DBG_PRINT ("constant");
    $$ = driver.append_constant_list (NULL, $1);
  }
  |
  constant_list constant
  {
    DBG_PRINT ("constant_list constant");
    $$ = driver.append_constant_list ($1, $2);
  }
  ;

constant :
  ansi_string 		{ $$ = $1; }
  | dq_string		{ $$ = $1; }
  | nchar_string 	{ $$ = $1; }
  | bit_string 		{ $$ = $1; }
  | sql2_date 		{ $$ = $1; }
  | sql2_time 		{ $$ = $1; }
  | sql2_timestamp 	{ $$ = $1; }
  | sql2_timestampltz 	{ $$ = $1; }
  | sql2_timestamptz 	{ $$ = $1; }
  | utime 		{ $$ = $1; }
  | sql2_datetime 	{ $$ = $1; }
  | sql2_datetimeltz 	{ $$ = $1; }
  | sql2_datetimetz 	{ $$ = $1; }
  | NULL_  		{ $$ = driver.make_constant (LDR_NULL, NULL); }
  | TIME_LIT4 		{ $$ = driver.make_constant (LDR_TIME, $1); }
  | TIME_LIT42 		{ $$ = driver.make_constant (LDR_TIME, $1); }
  | TIME_LIT3 		{ $$ = driver.make_constant (LDR_TIME, $1); }
  | TIME_LIT31 		{ $$ = driver.make_constant (LDR_TIME, $1); }
  | TIME_LIT2 		{ $$ = driver.make_constant (LDR_TIME, $1); }
  | TIME_LIT1 		{ $$ = driver.make_constant (LDR_TIME, $1); }
  | INT_LIT 		{ $$ = driver.make_constant (LDR_INT, $1); }
  | REAL_LIT
  {
    if (strchr ($1->val, 'F') != NULL || strchr ($1->val, 'f') != NULL)
      {
	$$ = driver.make_constant (LDR_FLOAT, $1);
      }
    else if (strchr ($1->val, 'E') != NULL || strchr ($1->val, 'e') != NULL)
      {
	$$ = driver.make_constant (LDR_DOUBLE, $1);
      }
    else
      {
	$$ = driver.make_constant (LDR_NUMERIC, $1);
      }
  }
  | DATE_LIT2			{ $$ = driver.make_constant (LDR_DATE, $1); }
  | monetary			{ $$ = $1; }
  | object_reference		{ $$ = $1; }
  | set_constant		{ $$ = $1; }
  | system_object_reference	{ $$ = $1; }
  ;

ansi_string :
  Quote SQS_String_Body
  {
    $$ = driver.make_constant (LDR_STR, $2);
  }
  ;

nchar_string :
  NQuote SQS_String_Body
  {
    $$ = driver.make_constant (LDR_NSTR, $2);
  }
  ;

dq_string
  :DQuote DQS_String_Body
  {
    $$ = driver.make_constant (LDR_STR, $2);
  }
  ;

sql2_date :
  DATE_ Quote SQS_String_Body
  {
    $$ = driver.make_constant (LDR_DATE, $3);
  }
  ;

sql2_time :
  TIME Quote SQS_String_Body
  {
    $$ = driver.make_constant (LDR_TIME, $3);
  }
  ;

sql2_timestamp :
  TIMESTAMP Quote SQS_String_Body
  {
    $$ = driver.make_constant (LDR_TIMESTAMP, $3);
  }
  ;

sql2_timestampltz :
  TIMESTAMPLTZ Quote SQS_String_Body
  {
    $$ = driver.make_constant (LDR_TIMESTAMPLTZ, $3);
  }
  ;

sql2_timestamptz :
  TIMESTAMPTZ Quote SQS_String_Body
  {
    $$ = driver.make_constant (LDR_TIMESTAMPTZ, $3);
  }
  ;

utime :
  UTIME Quote SQS_String_Body
  {
    $$ = driver.make_constant (LDR_TIMESTAMP, $3);
  }
  ;

sql2_datetime :
  DATETIME Quote SQS_String_Body
  {
    $$ = driver.make_constant (LDR_DATETIME, $3);
  }
  ;

sql2_datetimeltz :
  DATETIMELTZ Quote SQS_String_Body
  {
    $$ = driver.make_constant (LDR_DATETIMELTZ, $3);
  }
  ;

sql2_datetimetz :
  DATETIMETZ Quote SQS_String_Body
  {
    $$ = driver.make_constant (LDR_DATETIMETZ, $3);
  }
  ;

bit_string :
  BQuote SQS_String_Body
  {
    $$ = driver.make_constant (LDR_BSTR, $2);
  }
  |
  XQuote SQS_String_Body
  {
    $$ = driver.make_constant (LDR_XSTR, $2);
  }
  ;

object_reference :
  OBJECT_REFERENCE class_identifier
  {
    $$ = driver.make_constant (LDR_CLASS_OID, $2);
  }
  |
  OBJECT_REFERENCE class_identifier instance_number
  {
    $2->instance_number = $3;
    $$ = driver.make_constant (LDR_OID, $2);
  }
  ;

class_identifier:
  INT_LIT
  {
    $$ = driver.make_object_ref ($1);
  }
  |
  IDENTIFIER
  {
    $$ = driver.make_object_ref ($1);
  }
  ;

instance_number :
  OID_DELIMETER INT_LIT
  {
    $$ = $2;
  }
  ;

set_constant :
  SET_START_BRACE SET_END_BRACE
  {
    $$ = driver.make_constant (LDR_COLLECTION, NULL);
  }
  |
  SET_START_BRACE set_elements SET_END_BRACE
  {
    $$ = driver.make_constant (LDR_COLLECTION, $2);
  }
  ;

set_elements:
  constant
  {
    DBG_PRINT ("constant");
    $$ = driver.append_constant_list (NULL, $1);
  }
  |
  set_elements constant
  {
    DBG_PRINT ("set_elements constant");
    $$ = driver.append_constant_list ($1, $2);
  }
  |
  set_elements COMMA constant
  {
    DBG_PRINT ("set_elements COMMA constant");
    $$ = driver.append_constant_list ($1, $3);
  }
  |
  set_elements NL constant
  {
    DBG_PRINT ("set_elements NL constant");
    $$ = driver.append_constant_list ($1, $3);
  }
  |
  set_elements COMMA NL constant
  {
    DBG_PRINT ("set_elements COMMA NL constant");
    $$ = driver.append_constant_list ($1, $4);
  }
  ;

system_object_reference :
  ref_type Quote SQS_String_Body
  {
    $$ = driver.make_constant ($1, $3);
  }
  ;

ref_type :
  REF_ELO_INT { $$ = LDR_ELO_INT; }
  |
  REF_ELO_EXT { $$ = LDR_ELO_EXT; }
  |
  REF_USER { $$ = LDR_SYS_USER; }
  |
  REF_CLASS { $$ = LDR_SYS_CLASS; }
  ;

monetary :
  DOLLAR_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_DOLLAR, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  YEN_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_YEN, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  WON_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_WON, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  TURKISH_LIRA_CURRENCY REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_TL, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  BACKSLASH REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_WON, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  BRITISH_POUND_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_BRITISH_POUND, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  CAMBODIAN_RIEL_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_CAMBODIAN_RIEL, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  CHINESE_RENMINBI_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_CHINESE_RENMINBI, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  INDIAN_RUPEE_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_INDIAN_RUPEE, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  RUSSIAN_RUBLE_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_RUSSIAN_RUBLE, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  AUSTRALIAN_DOLLAR_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_AUSTRALIAN_DOLLAR, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  CANADIAN_DOLLAR_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_CANADIAN_DOLLAR, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  BRASILIAN_REAL_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_BRASILIAN_REAL, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  ROMANIAN_LEU_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_ROMANIAN_LEU, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  EURO_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_EURO, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  SWISS_FRANC_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_SWISS_FRANC, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  DANISH_KRONE_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_DANISH_KRONE, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  NORWEGIAN_KRONE_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_NORWEGIAN_KRONE, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  BULGARIAN_LEV_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_BULGARIAN_LEV, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  VIETNAMESE_DONG_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_VIETNAMESE_DONG, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  CZECH_KORUNA_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_CZECH_KORUNA, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  POLISH_ZLOTY_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_POLISH_ZLOTY, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  SWEDISH_KRONA_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_SWEDISH_KRONA, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  CROATIAN_KUNA_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_CROATIAN_KUNA, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  |
  SERBIAN_DINAR_SYMBOL REAL_LIT
  {
    LDR_MONETARY_VALUE *mon_value = driver.make_monetary_value (DB_CURRENCY_SERBIAN_DINAR, $2);

    $$ = driver.make_constant (LDR_MONETARY, mon_value);
  }
  ;
%%

/*** Additional Code ***/

void
cubloader::loader_parser::error (const loader_parser::location_type& l, const std::string& m)
{
  driver.error (l, m);
}
