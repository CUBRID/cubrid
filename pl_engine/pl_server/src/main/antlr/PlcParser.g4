/**
 * CUBRID PL/CSQL Parser grammar based on and updated from
 *  Oracle(c) PL/SQL 11g Parser (https://github.com/antlr/grammars-v4/tree/master/sql/plsql)
 *
 * Copyright (c) 2009-2011 Alexandre Porcelli <alexandre.porcelli@gmail.com>
 * Copyright (c) 2015-2019 Ivan Kochurkin (KvanTTT, kvanttt@gmail.com, Positive Technologies).
 * Copyright (c) 2017 Mark Adams <madams51703@gmail.com>
 * Copyright (c) 2016 CUBRID Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

parser grammar PlcParser;

@header {
package com.cubrid.plcsql.compiler.antlrgen;
}

options {
    tokenVocab=PlcLexer;
}

sql_script
    : create_routine EOF
    ;

create_routine
    : CREATE (OR_REPLACE)? routine_definition (COMMENT CHAR_STRING)?
    ;

routine_definition
    : (PROCEDURE | FUNCTION) identifier ( (LPAREN parameter_list RPAREN)? | LPAREN RPAREN ) (RETURN type_spec)?
      (IS | AS) (LANGUAGE PLCSQL)? seq_of_declare_specs? body (SEMICOLON)?
    ;

parameter_list
    : parameter (',' parameter)*
    ;

parameter
    : parameter_name IN? type_spec                      # parameter_in
    | parameter_name ( IN? OUT | INOUT ) type_spec      # parameter_out
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
    | routine_definition
    ;

item_declaration
    : constant_declaration
    | exception_declaration
    | variable_declaration
    ;

variable_declaration
    : identifier type_spec ((NOT NULL_)? default_value_part)? SEMICOLON
    ;

constant_declaration
    : identifier CONSTANT type_spec (NOT NULL_)? default_value_part SEMICOLON
    ;

cursor_definition
    : CURSOR identifier ( (LPAREN parameter_list RPAREN)? | LPAREN RPAREN ) IS static_sql SEMICOLON
    ;

exception_declaration
    : identifier EXCEPTION SEMICOLON
    ;

pragma_declaration
    : PRAGMA AUTONOMOUS_TRANSACTION SEMICOLON
    ;

seq_of_statements
    : (statement SEMICOLON)+
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
    : EXECUTE IMMEDIATE dyn_sql (into_clause? restricted_using_clause? | restricted_using_clause into_clause)
    ;

dyn_sql
    : expression
    ;

into_clause
    : INTO identifier (',' identifier)*
    ;

assignment_statement
    : identifier ':=' expression
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
    : label_declaration? LOOP seq_of_statements END LOOP label_name?                       # stmt_basic_loop
    | label_declaration? WHILE expression LOOP seq_of_statements END LOOP label_name?      # stmt_while_loop
    | label_declaration? FOR iterator LOOP seq_of_statements END LOOP label_name?          # stmt_for_iter_loop
    | label_declaration? FOR for_cursor LOOP seq_of_statements END LOOP label_name?        # stmt_for_cursor_loop
    | label_declaration? FOR for_static_sql LOOP seq_of_statements END LOOP label_name?    # stmt_for_static_sql_loop
    | label_declaration? FOR for_dynamic_sql LOOP seq_of_statements END LOOP label_name?   # stmt_for_dynamic_sql_loop
    ;

 // actually far more complicated according to the Spec.
iterator
    : index_name IN REVERSE? lower_bound '..' upper_bound (BY step)?
    ;

for_cursor
    : record_name IN cursor_exp (LPAREN expressions? RPAREN)?
    ;

for_static_sql
    : record_name IN LPAREN static_sql RPAREN
    ;

for_dynamic_sql
    : record_name IN LPAREN EXECUTE IMMEDIATE dyn_sql restricted_using_clause? RPAREN
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
    : (DBMS_OUTPUT '.')? routine_name function_argument?
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
    : static_sql
    | cursor_manipulation_statement
    | transaction_control_statement
    ;

static_sql
    : static_sql_begin (SS_STR | SS_WS | SS_NON_STR)+
    ;

static_sql_begin
    : WITH
    | SELECT
    | INSERT
    | UPDATE
    | DELETE
    | REPLACE
    | MERGE
    | TRUNCATE
    ;

cursor_manipulation_statement
    : close_statement
    | open_statement
    | fetch_statement
    | open_for_statement
    ;

close_statement
    : CLOSE cursor_exp
    ;

open_statement
    : OPEN cursor_exp (LPAREN expressions? RPAREN)?
    ;

fetch_statement
    : FETCH cursor_exp INTO identifier (',' identifier)*
    ;

open_for_statement
    : OPEN identifier FOR static_sql
    ;

transaction_control_statement
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
    : between_expression                                                # relational_expression_prime
    | relational_expression relational_operator relational_expression   # rel_exp
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
    : is_null_expression                                                                # like_expression_prime
    | like_expression NOT? LIKE pattern=concatenation (ESCAPE escape=quoted_string)?    # like_exp
    ;

is_null_expression
    : concatenation                         # is_null_expression_prime
    | is_null_expression IS NOT? NULL_      # is_null_exp
    ;

concatenation
    : unary_expression                                          # concatenation_prime
    | concatenation ('*' | '/' | DIV | MOD) concatenation       # mult_exp
    | concatenation ('+' | '-' ) concatenation                  # add_exp
    | concatenation '||' concatenation                          # str_concat_exp
    | concatenation ('<<' | '>>') concatenation                 # bit_shift_exp
    | concatenation ('&') concatenation                         # bit_and_exp
    | concatenation ('^') concatenation                         # bit_xor_exp
    | concatenation ('|') concatenation                         # bit_or_exp
    ;

unary_expression
    : atom                                      # unary_expression_prime
    | ('-' | '+') unary_expression              # sign_exp
    | '~' unary_expression                      # bit_compli_exp
    ;

atom
    : literal                                   # literal_exp
    | record=identifier '.' field=identifier    # field_exp
    | function_call                             # call_exp
    | identifier                                # id_exp
    | case_expression                           # case_exp
    | SQL PERCENT_ROWCOUNT                      # sql_rowcount_exp  // this must go before the cursor_attr_exp line
    | cursor_exp ( PERCENT_ISOPEN | PERCENT_FOUND | PERCENT_NOTFOUND | PERCENT_ROWCOUNT )   # cursor_attr_exp
    | LPAREN expression RPAREN                  # paren_exp
    | SQLCODE                                   # sqlcode_exp
    | SQLERRM                                   # sqlerrm_exp
    ;

function_call
    : function_name function_argument
    ;

relational_operator
    : '='
    | NULL_SAFE_EQUALS_OP
    | NOT_EQUAL_OP
    | '<='
    | '>='
    | '<'
    | '>'
    ;

in_elements
    : LPAREN in_expression (',' in_expression)* RPAREN
    ;

between_elements
    : between_expression AND between_expression
    ;

case_expression
    : searched_case_expression
    | simple_case_expression
    ;

simple_case_expression
    : CASE expression simple_case_expression_when_part+ case_expression_else_part? END
    ;

simple_case_expression_when_part
    : WHEN expression THEN expression
    ;

searched_case_expression
    : CASE searched_case_expression_when_part+ case_expression_else_part? END
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
    : RAISE_APPLICATION_ERROR LPAREN err_code ',' err_msg RPAREN
    ;

err_code
    : concatenation
    ;

err_msg
    : concatenation
    ;

simple_case_statement
    : CASE expression simple_case_statement_when_part+  case_statement_else_part? END CASE label_name?
    ;

simple_case_statement_when_part
    : WHEN expression THEN seq_of_statements
    ;

searched_case_statement
    : CASE searched_case_statement_when_part+ case_statement_else_part? END CASE label_name?
    ;

searched_case_statement_when_part
    : WHEN expression THEN seq_of_statements
    ;

case_statement_else_part
    : ELSE seq_of_statements
    ;

restricted_using_clause
    : USING restricted_using_element (',' restricted_using_element)*
    ;

restricted_using_element
    : (IN)? expression
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

index_name
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
    : (identifier '.')? identifier
    ;

column_name
    : identifier
    ;

function_argument
    : LPAREN (argument (',' argument)*)? RPAREN
    ;

argument
    : expression
    ;

type_spec
    : native_datatype                               # native_type_spec
    | (table_name '.')? identifier PERCENT_TYPE     # percent_type_spec
    ;

native_datatype
    : numeric_type
    | char_type
    | varchar_type
    | simple_type
    ;

numeric_type
    : (NUMERIC | DECIMAL | DEC) (LPAREN precision=UNSIGNED_INTEGER (',' scale=UNSIGNED_INTEGER)? RPAREN)?
    ;

char_type
    : (CHAR | CHARACTER) ( LPAREN length=UNSIGNED_INTEGER RPAREN )?
    ;

varchar_type
    : (VARCHAR | CHAR VARYING | CHARACTER VARYING) ( LPAREN length=UNSIGNED_INTEGER RPAREN )?
    | STRING
    ;

simple_type
    : BOOLEAN
    | SHORT | SMALLINT
    | INT | INTEGER
    | BIGINT
    | FLOAT | REAL
    | DOUBLE PRECISION?
    | DATE
    | TIME
    | TIMESTAMP
    | DATETIME
    | SYS_REFCURSOR
    ;

literal
    : DATE quoted_string            # date_exp
    | TIME quoted_string            # time_exp
    | TIMESTAMP quoted_string       # timestamp_exp
    | DATETIME quoted_string        # datetime_exp
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
    : REGULAR_ID
    | DELIMITED_ID
    ;

function_name
    : identifier
    | DATE
    | DEFAULT
    | IF
    | INSERT
    | MOD
    | REPLACE
    | REVERSE
    | TIME
    | TIMESTAMP
    | TRUNCATE
    ;


