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

parser grammar SqlParser;

options {
    tokenVocab=PcsLexer;
}

s_select_only_statement // ???
    : s_subquery_factoring_clause? s_subquery
    ;

s_select_statement // ???
    : s_select_only_statement (s_for_update_clause | s_order_by_clause | s_offset_clause | s_fetch_clause)*
    ;

s_subquery_factoring_clause
    : WITH s_factoring_element (',' s_factoring_element)*
    ;

s_factoring_element
    : s_query_name s_paren_column_list? AS '(' s_subquery s_order_by_clause? ')'
      s_search_clause?
    ;

s_search_clause
    : SEARCH (DEPTH | BREADTH) FIRST BY s_column_name ASC? DESC? (NULLS FIRST)? (NULLS LAST)?
      (',' s_column_name ASC? DESC? (NULLS FIRST)? (NULLS LAST)?)* SET s_column_name
    ;

s_subquery // ???
    : s_subquery_basic_elements s_subquery_operation_part*
    ;

s_subquery_basic_elements
    : s_query_block
    | '(' s_subquery ')'
    ;

s_subquery_operation_part // ???
    : (UNION ALL? | INTERSECT | MINUS) s_subquery_basic_elements
    ;

s_query_block
    : SELECT (DISTINCT | UNIQUE | ALL)? s_selected_list
      s_into_clause? s_from_clause s_where_clause? s_hierarchical_query_clause?
      s_group_by_clause? s_order_by_clause? s_fetch_clause?
    ;

s_selected_list
    : '*'
    | s_select_list_elements (',' s_select_list_elements)*
    ;

s_from_clause
    : FROM s_table_ref_list
    ;

s_select_list_elements
    : s_tableview_name '.' '*'
    | s_expression s_column_alias?
    ;

s_table_ref_list
    : s_table_ref (',' s_table_ref)*
    ;

s_table_ref
    : s_table_ref_aux s_join_clause*
    ;

s_table_ref_aux
    : s_table_ref_aux_internal s_table_alias?
    ;

s_table_ref_aux_internal
    : s_dml_table_expression_clause                  # table_ref_aux_internal_one
    | '(' s_table_ref s_subquery_operation_part* ')'   # table_ref_aux_internal_two
    | ONLY '(' s_dml_table_expression_clause ')'     # table_ref_aux_internal_three
    ;

s_join_clause
    : s_query_partition_clause? (CROSS | NATURAL)? (INNER | s_outer_join_type)?
      JOIN s_table_ref_aux s_query_partition_clause? (s_join_on_part | s_join_using_part)*
    ;

s_join_on_part
    : ON s_condition
    ;

s_join_using_part
    : USING s_paren_column_list
    ;

s_outer_join_type
    : (FULL | LEFT | RIGHT) OUTER?
    ;

s_query_partition_clause
    : PARTITION BY (('(' (s_subquery | s_expressions)? ')') | s_expressions)
    ;

s_hierarchical_query_clause
    : CONNECT BY NOCYCLE? s_condition s_start_part?
    | s_start_part CONNECT BY NOCYCLE? s_condition
    ;

s_start_part
    : START WITH s_condition
    ;

s_group_by_clause
    : GROUP BY s_group_by_elements (',' s_group_by_elements)* s_having_clause?
    | s_having_clause (GROUP BY s_group_by_elements (',' s_group_by_elements)*)?
    ;

s_group_by_elements
    : s_grouping_sets_clause
    | s_rollup_cube_clause
    | s_expression
    ;

s_rollup_cube_clause
    : (ROLLUP | CUBE) '(' s_grouping_sets_elements (',' s_grouping_sets_elements)* ')'
    ;

s_grouping_sets_clause
    : GROUPING SETS '(' s_grouping_sets_elements (',' s_grouping_sets_elements)* ')'
    ;

s_grouping_sets_elements
    : s_rollup_cube_clause
    | '(' s_expressions? ')'
    | s_expression
    ;

s_having_clause
    : HAVING s_condition
    ;

s_order_by_clause
    : ORDER SIBLINGS? BY s_order_by_elements (',' s_order_by_elements)*
    ;

s_order_by_elements
    : s_expression (ASC | DESC)? (NULLS (FIRST | LAST))?
    ;

s_offset_clause
    : OFFSET s_expression (ROW | ROWS)
    ;

s_fetch_clause
    : FETCH (FIRST | NEXT) (s_expression PERCENT_KEYWORD?)? (ROW | ROWS) (ONLY | WITH TIES)
    ;

s_for_update_clause
    : FOR UPDATE s_for_update_of_part? s_for_update_options?
    ;

s_for_update_of_part
    : OF s_column_list
    ;

s_for_update_options
    : SKIP_ LOCKED
    | NOWAIT
    | WAIT s_expression
    ;

s_update_statement // ???
    : UPDATE s_general_table_ref s_update_set_clause s_where_clause?
    ;

s_update_set_clause
    : SET (s_column_based_update_set_clause
      (',' s_column_based_update_set_clause)* | VALUE '(' s_identifier ')' '=' s_expression)
    ;

s_column_based_update_set_clause
    : s_column_name '=' s_expression
    | s_paren_column_list '=' s_subquery
    ;

s_delete_statement // ???
    : DELETE FROM? s_general_table_ref s_where_clause?
    ;

s_insert_statement // ???
    : INSERT s_single_table_insert
    ;

s_single_table_insert
    : s_insert_into_clause (s_values_clause | s_select_statement)
    ;

s_insert_into_clause
    : INTO s_general_table_ref s_paren_column_list?
    ;

s_values_clause
    : VALUES (REGULAR_ID | '(' s_expressions ')')
    ;

s_merge_statement // ???
    : MERGE INTO s_tableview_name s_table_alias? USING s_selected_tableview ON '(' s_condition ')'
      (s_merge_update_clause s_merge_insert_clause? | s_merge_insert_clause s_merge_update_clause?)?
    ;

s_merge_update_clause
    : WHEN MATCHED THEN UPDATE SET s_merge_element (',' s_merge_element)* s_where_clause? s_merge_update_delete_part?
    ;

s_merge_element
    : s_column_name '=' s_expression
    ;

s_merge_update_delete_part
    : DELETE s_where_clause
    ;

s_merge_insert_clause
    : WHEN NOT MATCHED THEN INSERT s_paren_column_list?
      s_values_clause s_where_clause?
    ;

s_truncate_statement
    : TRUNCATE TABLE? s_table_ref CASCADE?
    ;

s_selected_tableview
    : (s_tableview_name | '(' s_select_statement ')') s_table_alias?
    ;

s_general_table_ref
    : (s_dml_table_expression_clause | ONLY '(' s_dml_table_expression_clause ')') s_table_alias?
    ;

s_dml_table_expression_clause
    : s_table_collection_expression
    | '(' s_select_statement ')'
    | s_tableview_name s_sample_clause?
    ;

s_table_collection_expression
    : (TABLE | THE) ('(' s_subquery ')' | '(' s_expression ')' s_outer_join_sign?)
    ;

s_sample_clause
    : SAMPLE BLOCK? '(' s_expression (',' s_expression)? ')' s_seed_part?
    ;

s_seed_part
    : SEED '(' s_expression ')'
    ;

s_condition // !!!
    : s_expression
    ;

s_expressions // !!!
    : s_expression (',' s_expression)*
    ;

s_expression // !!!
    : s_logical_expression
    ;

s_logical_expression // !!!
    : s_unary_logical_expression
    | s_logical_expression AND s_logical_expression
    | s_logical_expression OR s_logical_expression
    ;

s_unary_logical_expression // !!!
    : NOT? s_multiset_expression (IS NOT? s_logical_operation)*
    ;

s_logical_operation // !!!
    : (NULL_
    | NAN | PRESENT
    | INFINITE | A_LETTER SET | EMPTY
    | OF TYPE? '(' ONLY? s_type_spec (',' s_type_spec)* ')')
    ;

s_multiset_expression // !!!
    : s_relational_expression ((MEMBER | SUBMULTISET) OF? s_concatenation)?
    ;

s_relational_expression // !!!
    : s_relational_expression s_relational_operator s_relational_expression
    | s_compound_expression
    ;

s_compound_expression // !!!
    : s_concatenation
      (NOT? ( IN s_in_elements
            | BETWEEN s_between_elements
            | (LIKE | LIKEC | LIKE2 | LIKE4) s_concatenation (ESCAPE s_concatenation)?))?
    ;

s_relational_operator // !!!
    : '='
    | NOT_EQUAL_OP
    | '<='
    | '>='
    | '<'
    | '>'
    ;

s_in_elements // !!!
    : '(' s_subquery ')'
    | '(' s_concatenation (',' s_concatenation)* ')'
    | s_constant
    | s_general_element
    ;

s_between_elements // !!!
    : s_concatenation AND s_concatenation
    ;

s_concatenation // !!!
    : s_model_expression
        (AT (LOCAL | TIME ZONE s_concatenation) | s_interval_expression)?
        (ON OVERFLOW (TRUNCATE | ERROR))?
    | s_concatenation ('*' | '/') s_concatenation
    | s_concatenation ('+' | '-') s_concatenation
    | s_concatenation '||' s_concatenation
    ;

s_interval_expression // !!!
    : DAY ('(' s_concatenation ')')? TO SECOND ('(' s_concatenation ')')?
    | YEAR ('(' s_concatenation ')')? TO MONTH
    ;

s_model_expression // !!!
    : s_unary_expression ('[' s_model_expression_element ']')?
    ;

s_model_expression_element // !!!
    : (ANY | s_expression) (',' (ANY | s_expression))*
    | s_single_column_for_loop (',' s_single_column_for_loop)*
    | s_multi_column_for_loop
    ;

s_single_column_for_loop // !!!
    : FOR s_column_name
       ( IN '(' s_expressions? ')'
       | (LIKE s_expression)? FROM s_expression TO s_expression
         (INCREMENT | DECREMENT) s_expression)
    ;

s_multi_column_for_loop // !!!
    : FOR s_paren_column_list
      IN  '(' (s_subquery | '(' s_expressions? ')') ')'
    ;

s_unary_expression // !!!
    : ('-' | '+') s_unary_expression
    | PRIOR s_unary_expression
    | CONNECT_BY_ROOT s_unary_expression
    | NEW s_unary_expression
    | DISTINCT s_unary_expression
    | ALL s_unary_expression
    | s_case_expression
    | s_quantified_expression
    | s_standard_function
    | s_cursor_name ( PERCENT_ISOPEN | PERCENT_FOUND | PERCENT_NOTFOUND | PERCENT_ROWCOUNT )
    | s_atom
    ;

s_case_expression // !!!
    : s_searched_case_expression
    | s_simple_case_expression
    ;

s_simple_case_expression // !!!
    : CASE s_expression s_simple_case_when_part+  s_case_else_part? END
    ;

s_simple_case_when_part // !!!
    : WHEN s_expression THEN s_expression
    ;

s_searched_case_expression // !!!
    : CASE s_searched_case_when_part+ s_case_else_part? END
    ;

s_searched_case_when_part // !!!
    : WHEN s_expression THEN s_expression
    ;

s_case_else_part // !!!
    : ELSE s_expression
    ;

s_atom // !!!
    : s_table_element s_outer_join_sign
    | s_constant
    | s_general_element
    | '(' s_subquery ')' s_subquery_operation_part*
    | '(' s_expressions ')'
    ;

s_quantified_expression // !!!
    : (SOME | EXISTS | ALL | ANY) ('(' s_select_only_statement ')' | '(' s_expression ')')
    ;

s_string_function // !!!
    : SUBSTR '(' s_expression ',' s_expression (',' s_expression)? ')'
    | TO_CHAR '(' (s_table_element | s_standard_function | s_expression)
                  (',' s_quoted_string)? (',' s_quoted_string)? ')'
    | DECODE '(' s_expressions  ')'
    | CHR '(' s_concatenation USING NCHAR_CS ')'
    | NVL '(' s_expression ',' s_expression ')'
    | TRIM '(' ((LEADING | TRAILING | BOTH)? s_quoted_string? FROM)? s_concatenation ')'
    | TO_DATE '(' (s_table_element | s_standard_function | s_expression) (',' s_quoted_string)? ')'
    ;

s_standard_function // !!!
    : s_string_function
    | s_numeric_function_wrapper
    | s_other_function
    ;

s_numeric_function_wrapper // !!!
    : s_numeric_function (s_single_column_for_loop | s_multi_column_for_loop)?
    ;

s_numeric_function // !!!
    : SUM '(' (DISTINCT | ALL)? s_expression ')'
    | COUNT '(' ( '*' | ((DISTINCT | UNIQUE | ALL)? s_concatenation)? ) ')' s_over_clause?
    | ROUND '(' s_expression (',' UNSIGNED_INTEGER)?  ')'
    | AVG '(' (DISTINCT | ALL)? s_expression ')'
    | MAX '(' (DISTINCT | ALL)? s_expression ')'
    | LEAST '(' s_expressions ')'
    | GREATEST '(' s_expressions ')'
    ;

s_other_function // !!!
    : s_over_clause_keyword s_function_argument_analytic s_over_clause?
    | s_within_or_over_clause_keyword s_function_argument s_within_or_over_part+
    ;

s_over_clause_keyword // !!!
    : AVG
    | CORR
    | LAG
    | LEAD
    | MAX
    | MEDIAN
    | MIN
    | NTILE
    | RATIO_TO_REPORT
    | ROW_NUMBER
    | SUM
    | VARIANCE
    | REGR_
    | STDDEV
    | VAR_
    | COVAR_
    ;

s_within_or_over_clause_keyword // !!!
    : CUME_DIST
    | DENSE_RANK
    | LISTAGG
    | PERCENT_RANK
    | PERCENTILE_CONT
    | PERCENTILE_DISC
    | RANK
    ;

s_over_clause // ???
    : OVER '(' s_query_partition_clause? (s_order_by_clause s_windowing_clause?)? ')'
    ;

s_windowing_clause
    : s_windowing_type
      (BETWEEN s_windowing_elements AND s_windowing_elements | s_windowing_elements)
    ;

s_windowing_type
    : ROWS
    | RANGE
    ;

s_windowing_elements
    : UNBOUNDED PRECEDING
    | CURRENT ROW
    | s_concatenation (PRECEDING | FOLLOWING)
    ;

s_within_or_over_part
    : WITHIN GROUP '(' s_order_by_clause ')'
    | s_over_clause
    ;

s_partition_extension_clause
    : (SUBPARTITION | PARTITION) FOR? '(' s_expressions? ')'
    ;

s_column_alias
    : AS? (s_identifier | s_quoted_string)
    ;

s_table_alias
    : s_identifier
    | s_quoted_string
    ;

s_where_clause
    : WHERE (CURRENT OF s_cursor_name | s_expression)
    ;

s_into_clause
    : INTO s_identifier (',' s_identifier)*
    ;

s_query_name
    : s_identifier
    ;

s_type_name // !!!
    : s_identifier ('.' s_identifier)*
    ;

s_variable_name // !!!
    : s_identifier ('.' s_identifier)?
    ;

s_cursor_name // !!!
    : s_general_element
    ;

s_link_name // ???
    : s_identifier
    ;

s_column_name // !!!
    : s_identifier ('.' s_identifier)*
    ;

s_tableview_name
    : s_identifier ('.' s_identifier)? ('@' s_link_name ('.' s_link_name)? | s_partition_extension_clause)?
    ;

s_char_set_name // !!!
    : s_identifier ('.' s_identifier)*
    ;

s_column_list // !!!
    : s_column_name (',' s_column_name)*
    ;

s_paren_column_list // !!!
    : '(' s_column_list ')'
    ;

s_keep_clause // ???
    : KEEP '(' DENSE_RANK (FIRST | LAST) s_order_by_clause ')' s_over_clause?
    ;

s_function_argument // !!!
    : '(' (s_argument (',' s_argument)*)? ')' s_keep_clause?
    ;

s_function_argument_analytic // !!!
    : '(' (s_argument s_respect_or_ignore_nulls? (',' s_argument s_respect_or_ignore_nulls?)*)? ')' s_keep_clause?
    ;

s_respect_or_ignore_nulls // !!!
    : (RESPECT | IGNORE) NULLS
    ;

s_argument // !!!
    : s_expression
    ;

s_type_spec // !!!
    : s_datatype
    | REF? s_type_name (PERCENT_ROWTYPE | PERCENT_TYPE)?
    ;

s_datatype // !!!
    : s_native_datatype_element s_precision_part? (WITH LOCAL? TIME ZONE | CHARACTER SET s_char_set_name)?
    | INTERVAL (YEAR | DAY) ('(' s_expression ')')? TO (MONTH | SECOND) ('(' s_expression ')')?
    ;

s_precision_part // !!!
    : '(' (s_numeric | '*') (',' (s_numeric | s_numeric_negative))? (CHAR | BYTE)? ')'
    ;

s_native_datatype_element // !!!
    : DEC
    | INTEGER
    | INT
    | NUMERIC
    | SMALLINT
    | DECIMAL
    | DOUBLE PRECISION?
    | FLOAT
    | REAL
    | CHAR
    | CHARACTER
    | VARCHAR
    | STRING
    | DATE
    | TIMESTAMP
    ;

s_general_element // !!!
    : s_general_element_part ('.' s_general_element_part)*
    ;

s_general_element_part // !!!
    : s_identifier ('.' s_identifier)* ('@' s_link_name)? s_function_argument?
    ;

s_table_element // !!!
    : s_identifier ('.' s_identifier)*
    ;

s_constant // !!!
    : TIMESTAMP s_quoted_string (AT TIME ZONE s_quoted_string)?
    | INTERVAL (s_quoted_string | s_general_element_part)
      (YEAR | MONTH | DAY | HOUR | MINUTE | SECOND)
      ('(' UNSIGNED_INTEGER (',' UNSIGNED_INTEGER)? ')')?
      (TO ( DAY | HOUR | MINUTE | SECOND ('(' UNSIGNED_INTEGER ')')?))?
    | s_numeric
    | DATE s_quoted_string
    | TIME s_quoted_string
    | DATETIME s_quoted_string
    | TIMESTAMP s_quoted_string
    | s_quoted_string
    | NULL_
    | TRUE
    | FALSE
    | DEFAULT
    ;

s_numeric // !!!
    : UNSIGNED_INTEGER
    | FLOATING_POINT_NUM
    ;

s_numeric_negative // !!!
    : '-' s_numeric
    ;

s_quoted_string // !!!
    : s_variable_name
    | CHAR_STRING
    ;

s_identifier // !!!
    : s_regular_id
    | DELIMITED_ID
    ;

s_outer_join_sign // !!!
    : '(' '+' ')'
    ;

s_regular_id // !!!
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

s_non_reserved_keywords_in_12c // !!!
    : FETCH
    | OFFSET
    | PERIOD
    | SUPERSET
    | SUBSET
    | SUPERSETEQ
    | SUBSETEQ
    | TIES
    ;

s_non_reserved_keywords_pre12c // !!!
    : AT
    | AVG
    | BEGIN
    | BLOCK
    | BOTH
    | BREADTH
    | BYTE
    | CASE
    | CASCADE
    | CHARACTER
    | CHR
    | CONNECT_BY_ROOT
    | CONSTANT
    | CONTINUE
    | CORR
    | COUNT
    | CROSS
    | CUBE
    | CUME_DIST
    | CURRENT
    | CURSOR
    | DAY
    | DECLARE
    | DEC
    | DECREMENT
    | DENSE_RANK
    | DEPTH
    | DOUBLE
    | EMPTY
    | ERROR
    | ESCAPE
    | EXECUTE
    | FALSE
    | FIRST
    | FOLLOWING
    | FULL
    | FUNCTION
    | GREATEST
    | GROUPING
    | HOUR
    | IF
    | IGNORE
    | IMMEDIATE
    | INCREMENT
    | INFINITE
    | INNER
    | INTERVAL
    | INT
    | JOIN
    | KEEP
    | LAG
    | LAST
    | LEADING
    | LEAD
    | LEAST
    | LEFT
    | LIKE2
    | LIKE4
    | LIKEC
    | LISTAGG
    | LIST
    | LOCAL
    | LOCKED
    | MATCHED
    | MAX
    | MEDIAN
    | MEMBER
    | MERGE
    | MIN
    | MINUTE
    | MOD
    | MONTH
    | NAN
    | NATURAL
    | NCHAR_CS
    | NEW
    | NEXT
    | NOCYCLE
    | NTILE
    | NULLS
    | NUMERIC
    | NVL
    | ONLY
    | OPEN
    | OUTER
    | OVERFLOW
    | OVER
    | PARTITION
    | PERCENTILE_CONT
    | PERCENTILE_DISC
    | PERCENT_KEYWORD
    | PERCENT_RANK
    | PRECEDING
    | PRECISION
    | PRESENT
    | PROCEDURE
    | RANGE
    | RANK
    | RATIO_TO_REPORT
    | REAL
    | REF
    | REPLACE
    | RESPECT
    | RETURN
    | REVERSE
    | RIGHT
    | ROLLBACK
    | ROLLUP
    | ROUND
    | ROW
    | ROW_NUMBER
    | ROWS
    | SAMPLE
    | SEARCH
    | SECOND
    | SEED
    | SETS
    | SIBLINGS
    | SKIP_
    | SOME
    | SQL
    | STDDEV
    | STRING
    | SUBMULTISET
    | SUBPARTITION
    | SUBSTR
    | SUM
    | TABLE
    | THE
    | THEN
    | TIME
    | TIMESTAMP
    | TO_CHAR
    | TO_DATE
    | TRAILING
    | TRIM
    | TRUE
    | TRUNCATE
    | TYPE
    | UNBOUNDED
    | USING
    | VALUE
    | VARIANCE
    | WAIT
    | WHEN
    | WITHIN
    | WORK
    | YEAR
    | ZONE
    ;
