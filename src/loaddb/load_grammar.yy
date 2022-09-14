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
 * load_grammar.yy - loader grammar file
 */

%skeleton "lalr1.cc"
%require "3.0"
%defines
%define api.namespace { cubload }
%define parser_class_name { parser }
%locations
%no-lines

%parse-param { driver &m_driver }

%define parse.assert

%union {
  int int_val;
  string_type *string;
  constant_type *constant;
  object_ref_type *obj_ref;
  constructor_spec_type *ctor_spec;
  class_command_spec_type *cmd_spec;
};

%code requires {
// This code will be copied into loader grammar header file
#include "load_common.hpp"

namespace cubload
{
  // forward declaration
  class driver;
}
}

%code {
// This code will be copied into loader grammar source file
#include "dbtype.h"
#include "load_driver.hpp"

#undef yylex
#define yylex m_driver.get_scanner ().yylex

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
%token <int_val> OID_
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
%token DOT

%type <int_val> attribute_list_type
%type <cmd_spec> class_command_spec
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
%type <int_val> ref_type
%type <int_val> object_id

%type <constant> set_elements

%start loader_start
%%

loader_start :
  {
    // add here any initialization code
  }
  loader_lines
  {
    m_driver.get_object_loader ().flush_records ();
    m_driver.get_object_loader ().destroy ();
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
  {
    m_driver.update_start_line ();
  }
  one_line NL
  {
    DBG_PRINT ("one_line");
    m_driver.get_semantic_helper ().set_in_instance_line (true);
  }
  |
  NL
  {
    m_driver.get_semantic_helper ().set_in_instance_line (true);
  }
  ;

one_line :
  command_line
  {
    DBG_PRINT ("command_line");
    m_driver.get_semantic_helper ().reset_after_line ();
  }
  |
  instance_line
  {
    DBG_PRINT ("instance_line");
    m_driver.get_object_loader ().finish_line ();
    m_driver.get_semantic_helper ().reset_after_line ();
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
  CMD_ID IDENTIFIER DOT IDENTIFIER INT_LIT
  {
    DBG_PRINT ("CMD_ID IDENTIFIER DOT IDENTIFIER INT_LIT");
    std::string name;
    name.reserve($2->size + sizeof (".") + $4->size);
    name.append ($2->val).append (".").append ($4->val);
    m_driver.get_class_installer ().check_class (name.c_str (), atoi ($5->val));
  }
  |
  CMD_ID IDENTIFIER INT_LIT
  {
    DBG_PRINT ("CMD_ID IDENTIFIER INT_LIT");
    m_driver.get_class_installer ().check_class ($2->val, atoi ($3->val));
  }
  ;

class_command :
  CMD_CLASS IDENTIFIER DOT IDENTIFIER class_command_spec
  {
    DBG_PRINT ("CMD_CLASS IDENTIFIER DOT IDENTIFIER class_command_spec");
    std::string name;
    name.reserve($2->size + sizeof (".") + $4->size);
    name.append ($2->val).append (".").append ($4->val);
    string_type name_buf (const_cast<char *> (name.c_str ()), name.size (), false);
    m_driver.get_class_installer ().install_class (&name_buf, $5);

    delete $5;
    $5 = NULL;
  }
  |
  CMD_CLASS IDENTIFIER class_command_spec
  {
    DBG_PRINT ("CMD_CLASS IDENTIFIER class_command_spec");
    m_driver.get_class_installer ().install_class ($2, $3);

    delete $3;
    $3 = NULL;
  }
  ;

class_command_spec :
  attribute_list
  {
    DBG_PRINT ("attribute_list");
    $$ = new class_command_spec_type (LDR_ATTRIBUTE_ANY, $1, NULL);
  }
  |
  attribute_list constructor_spec
  {
    DBG_PRINT ("attribute_list constructor_spec");
    $$ = new class_command_spec_type (LDR_ATTRIBUTE_ANY, $1, $2);
  }
  |
  attribute_list_type attribute_list
  {
    DBG_PRINT ("attribute_list_type attribute_list");
    $$ = new class_command_spec_type ($1, $2, NULL);
  }
  |
  attribute_list_type attribute_list constructor_spec
  {
    DBG_PRINT ("attribute_list_type attribute_list constructor_spec");
    $$ = new class_command_spec_type ($1, $2, $3);
  }
  ;

attribute_list_type :
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
    $$ = m_driver.get_semantic_helper ().append_string_list (NULL, $1);
  }
  |
  attribute_names attribute_name
  {
    DBG_PRINT ("attribute_names attribute_name");
    $$ = m_driver.get_semantic_helper ().append_string_list ($1, $2);
  }
  |
  attribute_names COMMA attribute_name
  {
    DBG_PRINT ("attribute_names COMMA attribute_name");
    $$ = m_driver.get_semantic_helper ().append_string_list ($1, $3);
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
    $$ = new constructor_spec_type ($2, $3);
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
    $$ = m_driver.get_semantic_helper ().append_string_list (NULL, $1);
  }
  |
  argument_names argument_name
  {
    DBG_PRINT ("argument_names argument_name");
    $$ = m_driver.get_semantic_helper ().append_string_list ($1, $2);
  }
  |
  argument_names COMMA argument_name
  {
    DBG_PRINT ("argument_names COMMA argument_name");
    $$ = m_driver.get_semantic_helper ().append_string_list ($1, $3);
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
    m_driver.get_object_loader ().start_line ($1);
  }
  |
  object_id constant_list
  {
    m_driver.get_object_loader ().start_line ($1);
    m_driver.get_object_loader ().process_line ($2);
  }
  |
  constant_list
  {
    m_driver.get_object_loader ().start_line (-1);
    m_driver.get_object_loader ().process_line ($1);
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
    $$ = m_driver.get_semantic_helper ().append_constant_list (NULL, $1);
  }
  |
  constant_list constant
  {
    DBG_PRINT ("constant_list constant");
    $$ = m_driver.get_semantic_helper ().append_constant_list ($1, $2);
  }
  ;

constant :
  ansi_string                { $$ = $1; }
  | dq_string                { $$ = $1; }
  | nchar_string             { $$ = $1; }
  | bit_string               { $$ = $1; }
  | sql2_date                { $$ = $1; }
  | sql2_time                { $$ = $1; }
  | sql2_timestamp           { $$ = $1; }
  | sql2_timestampltz        { $$ = $1; }
  | sql2_timestamptz         { $$ = $1; }
  | utime                    { $$ = $1; }
  | sql2_datetime            { $$ = $1; }
  | sql2_datetimeltz         { $$ = $1; }
  | sql2_datetimetz          { $$ = $1; }
  | NULL_                    { $$ = m_driver.get_semantic_helper ().make_constant (LDR_NULL, NULL); }
  | TIME_LIT4                { $$ = m_driver.get_semantic_helper ().make_constant (LDR_TIME, $1); }
  | TIME_LIT42               { $$ = m_driver.get_semantic_helper ().make_constant (LDR_TIME, $1); }
  | TIME_LIT3                { $$ = m_driver.get_semantic_helper ().make_constant (LDR_TIME, $1); }
  | TIME_LIT31               { $$ = m_driver.get_semantic_helper ().make_constant (LDR_TIME, $1); }
  | TIME_LIT2                { $$ = m_driver.get_semantic_helper ().make_constant (LDR_TIME, $1); }
  | TIME_LIT1                { $$ = m_driver.get_semantic_helper ().make_constant (LDR_TIME, $1); }
  | INT_LIT                  { $$ = m_driver.get_semantic_helper ().make_constant (LDR_INT, $1); }
  | REAL_LIT                 { $$ = m_driver.get_semantic_helper ().make_real ($1); }
  | DATE_LIT2                { $$ = m_driver.get_semantic_helper ().make_constant (LDR_DATE, $1); }
  | monetary                 { $$ = $1; }
  | object_reference         { $$ = $1; }
  | set_constant             { $$ = $1; }
  | system_object_reference  { $$ = $1; }
  ;

ansi_string :
  Quote SQS_String_Body
  {
    $$ = m_driver.get_semantic_helper ().make_constant (LDR_STR, $2);
  }
  ;

nchar_string :
  NQuote SQS_String_Body
  {
    $$ = m_driver.get_semantic_helper ().make_constant (LDR_NSTR, $2);
  }
  ;

dq_string
  :DQuote DQS_String_Body
  {
    $$ = m_driver.get_semantic_helper ().make_constant (LDR_STR, $2);
  }
  ;

sql2_date :
  DATE_ Quote SQS_String_Body
  {
    $$ = m_driver.get_semantic_helper ().make_constant (LDR_DATE, $3);
  }
  ;

sql2_time :
  TIME Quote SQS_String_Body
  {
    $$ = m_driver.get_semantic_helper ().make_constant (LDR_TIME, $3);
  }
  ;

sql2_timestamp :
  TIMESTAMP Quote SQS_String_Body
  {
    $$ = m_driver.get_semantic_helper ().make_constant (LDR_TIMESTAMP, $3);
  }
  ;

sql2_timestampltz :
  TIMESTAMPLTZ Quote SQS_String_Body
  {
    $$ = m_driver.get_semantic_helper ().make_constant (LDR_TIMESTAMPLTZ, $3);
  }
  ;

sql2_timestamptz :
  TIMESTAMPTZ Quote SQS_String_Body
  {
    $$ = m_driver.get_semantic_helper ().make_constant (LDR_TIMESTAMPTZ, $3);
  }
  ;

utime :
  UTIME Quote SQS_String_Body
  {
    $$ = m_driver.get_semantic_helper ().make_constant (LDR_TIMESTAMP, $3);
  }
  ;

sql2_datetime :
  DATETIME Quote SQS_String_Body
  {
    $$ = m_driver.get_semantic_helper ().make_constant (LDR_DATETIME, $3);
  }
  ;

sql2_datetimeltz :
  DATETIMELTZ Quote SQS_String_Body
  {
    $$ = m_driver.get_semantic_helper ().make_constant (LDR_DATETIMELTZ, $3);
  }
  ;

sql2_datetimetz :
  DATETIMETZ Quote SQS_String_Body
  {
    $$ = m_driver.get_semantic_helper ().make_constant (LDR_DATETIMETZ, $3);
  }
  ;

bit_string :
  BQuote SQS_String_Body
  {
    $$ = m_driver.get_semantic_helper ().make_constant (LDR_BSTR, $2);
  }
  |
  XQuote SQS_String_Body
  {
    $$ = m_driver.get_semantic_helper ().make_constant (LDR_XSTR, $2);
  }
  ;

object_reference :
  OBJECT_REFERENCE class_identifier
  {
    $$ = m_driver.get_semantic_helper ().make_constant (LDR_CLASS_OID, $2);
  }
  |
  OBJECT_REFERENCE class_identifier instance_number
  {
    $2->instance_number = $3;
    $$ = m_driver.get_semantic_helper ().make_constant (LDR_OID, $2);
  }
  ;

class_identifier:
  INT_LIT
  {
    $$ = new object_ref_type ($1, NULL);
  }
  |
  IDENTIFIER
  {
    $$ = new object_ref_type (NULL, $1);
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
    $$ = m_driver.get_semantic_helper ().make_constant (LDR_COLLECTION, NULL);
  }
  |
  SET_START_BRACE set_elements SET_END_BRACE
  {
    $$ = m_driver.get_semantic_helper ().make_constant (LDR_COLLECTION, $2);
  }
  ;

set_elements:
  constant
  {
    DBG_PRINT ("constant");
    $$ = m_driver.get_semantic_helper ().append_constant_list (NULL, $1);
  }
  |
  set_elements constant
  {
    DBG_PRINT ("set_elements constant");
    $$ = m_driver.get_semantic_helper ().append_constant_list ($1, $2);
  }
  |
  set_elements COMMA constant
  {
    DBG_PRINT ("set_elements COMMA constant");
    $$ = m_driver.get_semantic_helper ().append_constant_list ($1, $3);
  }
  |
  set_elements NL constant
  {
    DBG_PRINT ("set_elements NL constant");
    $$ = m_driver.get_semantic_helper ().append_constant_list ($1, $3);
  }
  |
  set_elements COMMA NL constant
  {
    DBG_PRINT ("set_elements COMMA NL constant");
    $$ = m_driver.get_semantic_helper ().append_constant_list ($1, $4);
  }
  ;

system_object_reference :
  ref_type Quote SQS_String_Body
  {
    $$ = m_driver.get_semantic_helper ().make_constant ($1, $3);
  }
  ;

ref_type :
  REF_ELO_INT   { $$ = LDR_ELO_INT; }
  | REF_ELO_EXT { $$ = LDR_ELO_EXT; }
  | REF_USER    { $$ = LDR_SYS_USER; }
  | REF_CLASS   { $$ = LDR_SYS_CLASS; }
  ;

monetary :
  DOLLAR_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_DOLLAR, $2);
  }
  |
  YEN_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_YEN, $2);
  }
  |
  WON_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_WON, $2);
  }
  |
  TURKISH_LIRA_CURRENCY REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_TL, $2);
  }
  |
  BACKSLASH REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_WON, $2);
  }
  |
  BRITISH_POUND_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_BRITISH_POUND, $2);
  }
  |
  CAMBODIAN_RIEL_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_CAMBODIAN_RIEL, $2);
  }
  |
  CHINESE_RENMINBI_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_CHINESE_RENMINBI, $2);
  }
  |
  INDIAN_RUPEE_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_INDIAN_RUPEE, $2);
  }
  |
  RUSSIAN_RUBLE_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_RUSSIAN_RUBLE, $2);
  }
  |
  AUSTRALIAN_DOLLAR_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_AUSTRALIAN_DOLLAR, $2);
  }
  |
  CANADIAN_DOLLAR_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_CANADIAN_DOLLAR, $2);
  }
  |
  BRASILIAN_REAL_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_BRASILIAN_REAL, $2);
  }
  |
  ROMANIAN_LEU_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_ROMANIAN_LEU, $2);
  }
  |
  EURO_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_EURO, $2);
  }
  |
  SWISS_FRANC_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_SWISS_FRANC, $2);
  }
  |
  DANISH_KRONE_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_DANISH_KRONE, $2);
  }
  |
  NORWEGIAN_KRONE_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_NORWEGIAN_KRONE, $2);
  }
  |
  BULGARIAN_LEV_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_BULGARIAN_LEV, $2);
  }
  |
  VIETNAMESE_DONG_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_VIETNAMESE_DONG, $2);
  }
  |
  CZECH_KORUNA_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_CZECH_KORUNA, $2);
  }
  |
  POLISH_ZLOTY_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_POLISH_ZLOTY, $2);
  }
  |
  SWEDISH_KRONA_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_SWEDISH_KRONA, $2);
  }
  |
  CROATIAN_KUNA_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_CROATIAN_KUNA, $2);
  }
  |
  SERBIAN_DINAR_SYMBOL REAL_LIT
  {
    $$ = m_driver.get_semantic_helper ().make_monetary_constant (DB_CURRENCY_SERBIAN_DINAR, $2);
  }
  ;
%%

/*** Additional Code ***/

void
cubload::parser::error (const parser::location_type& l, const std::string& m)
{
  m_driver.get_error_handler ().on_error (LOADDB_MSG_SYNTAX_ERR, m_driver.get_scanner ().lineno (),
					  m_driver.get_scanner ().YYText ());
}
