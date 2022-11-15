/*
 * Copyright (C) 2008 Search Solution Corporation.
 * Copyright (c) 2016 CUBRID Corporation.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

parser grammar PcsParser;
import SqlParser;

@header {
package com.cubrid.plcsql.compiler.antlrgen;
}

options {
    tokenVocab=PcsLexer;
}

sql_script
    : unit_statement ';'? EOF
    ;

unit_statement
    : create_function
    | create_procedure
    ;

create_function
    : CREATE (OR REPLACE)? FUNCTION identifier ('(' parameter_list ')')? RETURN type_spec
      (IS | AS) seq_of_declare_specs? body ';'
    ;

function_body
    : FUNCTION identifier ('(' parameter_list ')')? RETURN type_spec
      (IS | AS) seq_of_declare_specs? body ';'
    ;

procedure_body
    : PROCEDURE identifier ('(' parameter_list ')')?
      (IS | AS) seq_of_declare_specs? body ';'
    ;

create_procedure
    : CREATE (OR REPLACE)? PROCEDURE identifier ('(' parameter_list ')')?
      (IS | AS) seq_of_declare_specs? body ';'
    ;

parameter_list
    : parameter (',' parameter)*
    ;

parameter
    : parameter_name IN? type_spec          # parameter_in
    | parameter_name IN? OUT type_spec      # parameter_out
    ;

default_value_part
    : (':=' | DEFAULT) expression
    ;

seq_of_declare_specs
    : declare_spec+
    ;

declare_spec
    : pragma_declaration
    | item_declaration
    | cursor_definition
    | procedure_body
    | function_body
    ;

item_declaration
    : constant_declaration
    | exception_declaration
    | variable_declaration
    ;

variable_declaration
    : identifier type_spec ((NOT NULL_)? default_value_part)? ';'
    ;

constant_declaration
    : identifier CONSTANT type_spec (NOT NULL_)? default_value_part ';'
    ;

cursor_definition
    : CURSOR identifier ('(' parameter_list ')' )? IS s_select_statement ';'
    ;

exception_declaration
    : identifier EXCEPTION ';'
    ;

pragma_declaration
    : PRAGMA AUTONOMOUS_TRANSACTION ';'
    ;

seq_of_statements
    : (statement ';')+
    ;

label_declaration
    : '<<' label_name '>>'
    ;

statement
    : block                                 # stmt_block
    | sql_statement                         # stmt_sql              // must go before procedure_call
    | raise_application_error_statement     # stmt_raise_app_err    // must go before procedure_call
    | execute_immediate                     # stmt_exec_imme
    | assignment_statement                  # stmt_assign
    | continue_statement                    # stmt_continue
    | exit_statement                        # stmt_exit
    | null_statement                        # stmt_null
    | raise_statement                       # stmt_raise
    | return_statement                      # stmt_return
    | procedure_call                        # stmt_proc_call
    | if_statement                          # stmt_if
    | loop_statement                        # stmt_loop
    | case_statement                        # stmt_case
    ;

execute_immediate
    : EXECUTE IMMEDIATE dyn_sql (into_clause? using_clause? | using_clause into_clause)
    ;

dyn_sql
    : expression
    ;

into_clause
    : INTO identifier (',' identifier)*
    ;

assignment_statement
    : assignment_target ':=' expression
    ;

continue_statement
    : CONTINUE label_name? (WHEN expression)?
    ;

exit_statement
    : EXIT label_name? (WHEN expression)?
    ;

if_statement
    : IF expression THEN seq_of_statements elsif_part* else_part? END IF
    ;

elsif_part
    : ELSIF expression THEN seq_of_statements
    ;

else_part
    : ELSE seq_of_statements
    ;

loop_statement
    : label_declaration? LOOP seq_of_statements END LOOP                        # stmt_basic_loop
    | label_declaration? WHILE expression LOOP seq_of_statements END LOOP       # stmt_while_loop
    | label_declaration? FOR iterator LOOP seq_of_statements END LOOP           # stmt_for_iter_loop
    | label_declaration? FOR for_cursor LOOP seq_of_statements END LOOP         # stmt_for_cursor_loop
    | label_declaration? FOR for_static_sql LOOP seq_of_statements END LOOP     # stmt_for_static_sql_loop
    | label_declaration? FOR for_dynamic_sql LOOP seq_of_statements END LOOP    # stmt_for_dynamic_sql_loop
    ;

 // actually far more complicated according to the Spec.
iterator
    : index_name IN REVERSE? lower_bound '..' upper_bound (BY step)?
    ;

for_cursor
    : record_name IN cursor_exp ('(' expressions? ')')?
    ;

for_static_sql
    : record_name IN '(' s_select_statement ')'
    ;

for_dynamic_sql
    : record_name IN '(' EXECUTE IMMEDIATE dyn_sql restricted_using_clause? ')'
    ;

lower_bound
    : concatenation
    ;

upper_bound
    : concatenation
    ;

step
    : concatenation
    ;

null_statement
    : NULL_
    ;

raise_statement
    : RAISE exception_name?
    ;

return_statement
    : RETURN expression?
    ;

procedure_call
    : routine_name function_argument?
    ;

body
    : BEGIN seq_of_statements (EXCEPTION exception_handler+)? END label_name?
    ;

exception_handler
    : WHEN exception_name (OR exception_name)* THEN seq_of_statements
    ;

block
    : (DECLARE seq_of_declare_specs)? body
    ;

sql_statement
    : data_manipulation_language_statements
    | cursor_manipulation_statements
    | transaction_control_statements
    ;

data_manipulation_language_statements
    : s_merge_statement
    | s_select_statement
    | s_update_statement
    | s_delete_statement
    | s_insert_statement
    ;

cursor_manipulation_statements
    : close_statement
    | open_statement
    | fetch_statement
    | open_for_statement
    ;

close_statement
    : CLOSE cursor_exp
    ;

open_statement
    : OPEN cursor_exp ('(' expressions? ')')?
    ;

fetch_statement
    : FETCH cursor_exp INTO variable_name (',' variable_name)*
    ;

open_for_statement
    : OPEN variable_name FOR s_select_statement
    ;

transaction_control_statements
    : commit_statement
    | rollback_statement
    ;

commit_statement
    : COMMIT WORK?
    ;

rollback_statement
    : ROLLBACK WORK?
    ;

expressions
    : expression (',' expression)*
    ;

expression
    : unary_logical_expression      # expression_prime
    | expression AND expression     # and_exp
    | expression XOR expression     # xor_exp
    | expression OR expression      # or_exp
    ;

unary_logical_expression
    : relational_expression         # unary_logical_expression_prime
    | NOT unary_logical_expression  # not_exp
    ;

relational_expression
    : is_null_expression                                                # relational_expression_prime
    | relational_expression relational_operator relational_expression   # rel_exp
    ;

is_null_expression
    : between_expression                    # is_null_expression_prime
    | is_null_expression IS NOT? NULL_      # is_null_exp
    ;

between_expression
    : in_expression                                         # between_expression_prime
    | between_expression NOT? BETWEEN between_elements      # between_exp
    ;

in_expression
    : like_expression                       # in_expression_prime
    | in_expression NOT? IN in_elements     # in_exp
    ;

like_expression
    : concatenation                                                         # like_expression_prime
    | like_expression NOT? LIKE like_expression (ESCAPE like_expression)?   # like_exp
    ;

concatenation
    : unary_expression                                          # concatenation_prime
    | concatenation ('*' | '/' | DIV | MOD) concatenation       # mult_exp
    | concatenation ('+' | '-' | '||') concatenation            # add_exp
    | concatenation ('<<' | '>>') concatenation                 # bit_shift_exp
    | concatenation ('&') concatenation                         # bit_and_exp
    | concatenation ('^') concatenation                         # bit_xor_exp
    | concatenation ('|') concatenation                         # bit_or_exp
    ;

unary_expression
    : atom                                      # unary_expression_prime
    | unary_expression '**' unary_expression    # power_exp
    | ('-' | '+') unary_expression              # neg_exp
    ;

atom
    : literal                                   # literal_exp
    | record=identifier '.' field=identifier    # field_exp
    | function_call                             # call_exp
    | identifier                                # id_exp
    | case_expression                           # case_exp
    | SQL PERCENT_ROWCOUNT                      # sql_rowcount_exp  // this must go before the cursor_attr_exp line
    | cursor_exp ( PERCENT_ISOPEN | PERCENT_FOUND | PERCENT_NOTFOUND | PERCENT_ROWCOUNT )   # cursor_attr_exp
    | '(' expression ')'                        # paren_exp
    | '{' expressions '}'                       # list_exp
    ;

function_call
    : identifier function_argument
    ;

relational_operator
    : '='
    | EQUALS_WITH_NULL_OP
    | NOT_EQUAL_OP
    | '<='
    | '>='
    | '<'
    | '>'
    | SETEQ
    | SETNEQ
    | SUPERSET
    | SUBSET
    | SUPERSETEQ
    | SUBSETEQ
    ;

in_elements
    : in_expression (',' in_expression)*
    ;

between_elements
    : between_expression AND between_expression
    ;

case_expression
    : searched_case_expression
    | simple_case_expression
    ;

simple_case_expression
    : CASE expression simple_case_expression_when_part+ case_expression_else_part? END CASE?
    ;

simple_case_expression_when_part
    : WHEN expression THEN expression
    ;

searched_case_expression
    : CASE searched_case_expression_when_part+ case_expression_else_part? END CASE?
    ;

searched_case_expression_when_part
    : WHEN expression THEN expression
    ;

case_expression_else_part
    : ELSE expression
    ;

case_statement
    : searched_case_statement
    | simple_case_statement
    ;

raise_application_error_statement
    : RAISE_APPLICATION_ERROR '(' err_code ',' err_msg ')'
    ;

err_code
    : concatenation
    ;

err_msg
    : concatenation
    ;

simple_case_statement
    : CASE expression simple_case_statement_when_part+  case_statement_else_part? END CASE?
    ;

simple_case_statement_when_part
    : WHEN expression THEN seq_of_statements
    ;

searched_case_statement
    : CASE searched_case_statement_when_part+ case_statement_else_part? END CASE?
    ;

searched_case_statement_when_part
    : WHEN expression THEN seq_of_statements
    ;

case_statement_else_part
    : ELSE seq_of_statements
    ;

restricted_using_clause
    : USING expression (',' expression)*
    ;

using_clause
    : USING using_element (',' using_element)*
    ;

using_element
    : (IN OUT? | OUT)? expression
    ;

routine_name
    : identifier
    ;

parameter_name
    : identifier
    ;

label_name
    : identifier
    ;

exception_name
    : identifier
    ;

variable_name
    : identifier
    ;

index_name
    : identifier
    ;

assignment_target
    : identifier
    ;

cursor_exp
    //: function_call   TODO
    : identifier
    ;

record_name
    : identifier
    ;

table_name
    : identifier
    ;

column_name
    : identifier
    | table_name '.' identifier
    ;

column_list
    : column_name (',' column_name)*
    ;

function_argument
    : '(' (argument (',' argument)*)? ')'
    ;

argument
    : expression
    ;

type_spec
    : native_datatype precision_part?
    ;

precision_part
    : '(' numeric (',' numeric)? ')'
    ;

native_datatype
    : BOOLEAN
    | CHAR | VARCHAR | STRING
    | NUMERIC | DECIMAL | DEC
    | SHORT | SMALLINT
    | INT | INTEGER
    | BIGINT
    | FLOAT | REAL
    | DOUBLE PRECISION?
    | DATE
    | TIME
    | TIMESTAMP
    | DATETIME
    | TIMESTAMPLTZ
    | TIMESTAMPTZ
    | DATETIMELTZ
    | DATETIMETZ
    | SET
    | MULTISET
    | LIST
    | SEQUENCE
    | SYS_REFCURSOR
    ;

literal
    : DATE quoted_string            # date_exp
    | TIME quoted_string            # time_exp
    | TIMESTAMP quoted_string       # timestamp_exp
    | DATETIME quoted_string        # datetime_exp
    | TIMESTAMPTZ quoted_string     # timestamptz_exp
    | TIMESTAMPLTZ quoted_string    # timestampltz_exp
    | DATETIMETZ quoted_string      # datetimetz_exp
    | DATETIMELTZ quoted_string     # datetimeltz_exp
    | numeric                       # num_exp
    | quoted_string                 # str_exp
    | NULL_                         # null_exp
    | TRUE                          # true_exp
    | FALSE                         # false_exp
    ;

numeric
    : UNSIGNED_INTEGER      # uint_exp
    | FLOATING_POINT_NUM    # fp_num_exp
    ;

numeric_negative
    : '-' numeric
    ;

quoted_string
    : CHAR_STRING
    ;

identifier
    : regular_id
    | DELIMITED_ID
    ;

regular_id
    : s_non_reserved_keywords_pre12c
    | s_non_reserved_keywords_in_12c
    | REGULAR_ID
    | RAISE_APPLICATION_ERROR
    | A_LETTER
    | AUTONOMOUS_TRANSACTION
    | CHAR
    | DECIMAL
    | DELETE
    | EXCEPTION
    | EXISTS
    | EXIT
    | FLOAT
    | INTEGER
    | LONG
    | LOOP
    | OUT
    | PRAGMA
    | RAISE
    | RAW
    | REF
    | SET
    | SMALLINT
    | VARCHAR
    | WHILE
    | REGR_
    | VAR_
    | COVAR_
    ;
