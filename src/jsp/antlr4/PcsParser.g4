parser grammar PcsParser;
import SqlParser;

@header {
package com.cubrid.plcsql.transpiler.antlrgen;
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
    : non_reserved_keywords_pre12c
    | non_reserved_keywords_in_12c
    | REGULAR_ID
    | CURRENT_VALUE
    | NEXT_VALUE
    | RAISE_APPLICATION_ERROR
    | A_LETTER
    | AGENT
    | AGGREGATE
    | ANALYZE
    | AUTONOMOUS_TRANSACTION
    | BATCH
    | BINARY_INTEGER
    | BOOLEAN
    | C_LETTER
    | CHAR
    | CLUSTER
    | CONSTRUCTOR
    | CUSTOMDATUM
    | DECIMAL
    | DELETE
    | DETERMINISTIC
    | DSINTERVAL_UNCONSTRAINED
    | ERR
    | EXCEPTION
    | EXCEPTION_INIT
    | EXCEPTIONS
    | EXISTS
    | EXIT
    | FLOAT
    | FORALL
    | INDICES
    | INOUT
    | INTEGER
    | LANGUAGE
    | LONG
    | LOOP
    | NUMBER
    | ORADATA
    | OSERROR
    | OUT
    | OVERRIDING
    | PARALLEL_ENABLE
    | PIPELINED
    | PLS_INTEGER
    | POSITIVE
    | POSITIVEN
    | PRAGMA
    | RAISE
    | RAW
    | RECORD
    | REF
    | RENAME
    | RESTRICT_REFERENCES
    | RESULT
    | SELF
    | SERIALLY_REUSABLE
    | SET
    | SIGNTYPE
    | SIMPLE_INTEGER
    | SMALLINT
    | SQLDATA
    | SQLERROR
    | SUBTYPE
    | TIMESTAMP_LTZ_UNCONSTRAINED
    | TIMESTAMP_TZ_UNCONSTRAINED
    | TIMESTAMP_UNCONSTRAINED
    | TRIGGER
    | VARCHAR
    | VARCHAR2
    | VARIABLE
    | WARNING
    | WHILE
    | XMLAGG
    | YMINTERVAL_UNCONSTRAINED
    | REGR_
    | VAR_
    | COVAR_
    ;

non_reserved_keywords_in_12c
    : ACL
    | ACTION
    | ACTIONS
    | ACTIVE
    | ACTIVE_DATA
    | ACTIVITY
    | ADAPTIVE_PLAN
    | ADVANCED
    | AFD_DISKSTRING
    | ANOMALY
    | ANSI_REARCH
    | APPLICATION
    | APPROX_COUNT_DISTINCT
    | ARCHIVAL
    | ARCHIVED
    | ASIS
    | ASSIGN
    | AUTO_LOGIN
    | AUTO_REOPTIMIZE
    | AVRO
    | BACKGROUND
    | BATCHSIZE
    | BATCH_TABLE_ACCESS_BY_ROWID
    | BEGINNING
    | BEQUEATH
    | BITMAP_AND
    | BSON
    | CACHING
    | CALCULATED
    | CALLBACK
    | CAPACITY
    | CDBDEFAULT
    | CLASSIFIER
    | CLEANUP
    | CLIENT
    | CLUSTER_DETAILS
    | CLUSTER_DISTANCE
    | CLUSTERING
    | COMMON_DATA
    | COMPONENT
    | COMPONENTS
    | CON_DBID_TO_ID
    | CONDITION
    | CONDITIONAL
    | CON_GUID_TO_ID
    | CON_ID
    | CON_NAME_TO_ID
    | CONTAINER_DATA
    | CONTAINERS
    | CON_UID_TO_ID
    | COOKIE
    | COPY
    | CREATE_FILE_DEST
    | CREDENTIAL
    | CRITICAL
    | CUBE_AJ
    | CUBE_SJ
    | DATAMOVEMENT
    | DATAOBJ_TO_MAT_PARTITION
    | DATAPUMP
    | DATA_SECURITY_REWRITE_LIMIT
    | DAYS
    | DB_UNIQUE_NAME
    | DECORRELATE
    | DEFINE
    | DELEGATE
    | DELETE_ALL
    | DESTROY
    | DIMENSIONS
    | DISABLE_ALL
    | DISABLE_PARALLEL_DML
    | DISCARD
    | DISTRIBUTE
    | DUPLICATE
    | DV
    | EDITIONABLE
    | ELIM_GROUPBY
    | EM
    | ENABLE_ALL
    | ENABLE_PARALLEL_DML
    | EQUIPART
    | EVAL
    | EVALUATE
    | EXISTING
    | EXPRESS
    | EXTRACTCLOBXML
    | FACTOR
    | FAILOVER
    | FAILURE
    | FAMILY
    | FAR
    | FASTSTART
    | FEATURE_DETAILS
    | FETCH
    | FILE_NAME_CONVERT
    | FIXED_VIEW_DATA
    | FORMAT
    | GATHER_OPTIMIZER_STATISTICS
    | GET
    | ILM
    | INACTIVE
    | INDEXING
    | INHERIT
    | INMEMORY
    | INMEMORY_PRUNING
    | INPLACE
    | INTERLEAVED
    | JSON
    | JSON_ARRAY
    | JSON_ARRAYAGG
    | JSON_EQUAL
    | JSON_EXISTS
    | JSON_EXISTS2
    | JSONGET
    | JSON_OBJECT
    | JSON_OBJECTAGG
    | JSONPARSE
    | JSON_QUERY
    | JSON_SERIALIZE
    | JSON_TABLE
    | JSON_TEXTCONTAINS
    | JSON_TEXTCONTAINS2
    | JSON_VALUE
    | KEYSTORE
    | LABEL
    | LAX
    | LIFECYCLE
    | LINEAR
    | LOCKING
    | LOGMINING
    | MAP
    | MATCH
    | MATCHES
    | MATCH_NUMBER
    | MATCH_RECOGNIZE
    | MAX_SHARED_TEMP_SIZE
    | MEMCOMPRESS
    | METADATA
    | MODEL_NB
    | MODEL_SV
    | MODIFICATION
    | MODULE
    | MONTHS
    | MULTIDIMENSIONAL
    | NEG
    | NO_ADAPTIVE_PLAN
    | NO_ANSI_REARCH
    | NO_AUTO_REOPTIMIZE
    | NO_BATCH_TABLE_ACCESS_BY_ROWID
    | NO_CLUSTERING
    | NO_COMMON_DATA
    | NOCOPY
    | NO_DATA_SECURITY_REWRITE
    | NO_DECORRELATE
    | NO_ELIM_GROUPBY
    | NO_GATHER_OPTIMIZER_STATISTICS
    | NO_INMEMORY
    | NO_INMEMORY_PRUNING
    | NOKEEP
    | NONEDITIONABLE
    | NO_OBJECT_LINK
    | NO_PARTIAL_JOIN
    | NO_PARTIAL_ROLLUP_PUSHDOWN
    | NOPARTITION
    | NO_PQ_CONCURRENT_UNION
    | NO_PQ_REPLICATE
    | NO_PQ_SKEW
    | NO_PX_FAULT_TOLERANCE
    | NORELOCATE
    | NOREPLAY
    | NO_ROOT_SW_FOR_LOCAL
    | NO_SQL_TRANSLATION
    | NO_USE_CUBE
    | NO_USE_VECTOR_AGGREGATION
    | NO_VECTOR_TRANSFORM
    | NO_VECTOR_TRANSFORM_DIMS
    | NO_VECTOR_TRANSFORM_FACT
    | NO_ZONEMAP
    | OBJ_ID
    | OFFSET
    | OLS
    | OMIT
    | ONE
    | ORA_CHECK_ACL
    | ORA_CHECK_PRIVILEGE
    | ORA_CLUSTERING
    | ORA_INVOKING_USER
    | ORA_INVOKING_USERID
    | ORA_INVOKING_XS_USER
    | ORA_INVOKING_XS_USER_GUID
    | ORA_RAWCOMPARE
    | ORA_RAWCONCAT
    | ORA_WRITE_TIME
    | PARTIAL
    | PARTIAL_JOIN
    | PARTIAL_ROLLUP_PUSHDOWN
    | PAST
    | PATCH
    | PATH_PREFIX
    | PATTERN
    | PER
    | PERIOD
    | PERIOD_KEYWORD
    | PERMUTE
    | PLUGGABLE
    | POOL_16K
    | POOL_2K
    | POOL_32K
    | POOL_4K
    | POOL_8K
    | PQ_CONCURRENT_UNION
    | PQ_DISTRIBUTE_WINDOW
    | PQ_FILTER
    | PQ_REPLICATE
    | PQ_SKEW
    | PRELOAD
    | PRETTY
    | PREV
    | PRINTBLOBTOCLOB
    | PRIORITY
    | PRIVILEGED
    | PROXY
    | PRUNING
    | PX_FAULT_TOLERANCE
    | REALM
    | REDEFINE
    | RELOCATE
    | RESTART
    | ROLESET
    | ROWID_MAPPING_TABLE
    | RUNNING
    | SAVE
    | SCRUB
    | SDO_GEOM_MBR
    | SECRET
    | SERIAL
    | SERVICE_NAME_CONVERT
    | SERVICES
    | SHARING
    | SHELFLIFE
    | SOURCE_FILE_DIRECTORY
    | SOURCE_FILE_NAME_CONVERT
    | SQL_TRANSLATION_PROFILE
    | STANDARD_HASH
    | STANDBYS
    | STATE
    | STATEMENT
    | STREAM
    | SUBSCRIBE
    | SUBSET
    | SUCCESS
    | SYSBACKUP
    | SYS_CHECK_PRIVILEGE
    | SYSDG
    | SYS_GET_COL_ACLIDS
    | SYSGUID
    | SYSKM
    | SYS_MKXTI
    | SYSOBJ
    | SYS_OP_CYCLED_SEQ
    | SYS_OP_HASH
    | SYS_OP_KEY_VECTOR_CREATE
    | SYS_OP_KEY_VECTOR_FILTER
    | SYS_OP_KEY_VECTOR_FILTER_LIST
    | SYS_OP_KEY_VECTOR_SUCCEEDED
    | SYS_OP_KEY_VECTOR_USE
    | SYS_OP_PART_ID
    | SYS_OP_ZONE_ID
    | SYS_RAW_TO_XSID
    | SYS_XSID_TO_RAW
    | SYS_ZMAP_FILTER
    | SYS_ZMAP_REFRESH
    | TAG
    | TEXT
    | TIER
    | TIES
    | TO_ACLID
    | TRANSLATION
    | TRUST
    | UCS2
    | UNCONDITIONAL
    | UNMATCHED
    | UNPLUG
    | UNSUBSCRIBE
    | USABLE
    | USE_CUBE
    | USE_HIDDEN_PARTITIONS
    | USER_DATA
    | USER_TABLESPACES
    | USE_VECTOR_AGGREGATION
    | USING_NO_EXPAND
    | UTF16BE
    | UTF16LE
    | UTF32
    | UTF8
    | V1
    | V2
    | VALID_TIME_END
    | VECTOR_TRANSFORM
    | VECTOR_TRANSFORM_DIMS
    | VECTOR_TRANSFORM_FACT
    | VERIFIER
    | VIOLATION
    | VISIBILITY
    | WEEK
    | WEEKS
    | WITH_PLSQL
    | WRAPPER
    | XS
    | YEARS
    | ZONEMAP
    ;

non_reserved_keywords_pre12c
    : ABORT
    | ABS
    | ACCESSED
    | ACCESS
    | ACCOUNT
    | ACOS
    | ACTIVATE
    | ACTIVE_COMPONENT
    | ACTIVE_FUNCTION
    | ACTIVE_TAG
    | ADD_COLUMN
    | ADD_GROUP
    | ADD_MONTHS
    | ADD
    | ADJ_DATE
    | ADMINISTER
    | ADMINISTRATOR
    | ADMIN
    | ADVISE
    | ADVISOR
    | AFTER
    | ALIAS
    | ALLOCATE
    | ALLOW
    | ALL_ROWS
    | ALWAYS
    | ANALYZE
    | ANCILLARY
    | AND_EQUAL
    | ANTIJOIN
    | ANYSCHEMA
    | APPENDCHILDXML
    | APPEND
    | APPEND_VALUES
    | APPLY
    | ARCHIVELOG
    | ARCHIVE
    | ARRAY
    | ASCII
    | ASCIISTR
    | ASIN
    | ASSEMBLY
    | ASSOCIATE
    | ASYNCHRONOUS
    | ASYNC
    | ATAN2
    | ATAN
    | AT
    | ATTRIBUTE
    | ATTRIBUTES
    | AUTHENTICATED
    | AUTHENTICATION
    | AUTHID
    | AUTHORIZATION
    | AUTOALLOCATE
    | AUTOEXTEND
    | AUTOMATIC
    | AUTO
    | AVAILABILITY
    | AVG
    | BACKUP
    | BASICFILE
    | BASIC
    | BATCH
    | BECOME
    | BEFORE
    | BEGIN
    | BEGIN_OUTLINE_DATA
    | BEHALF
    | BFILE
    | BFILENAME
    | BIGFILE
    | BINARY_DOUBLE_INFINITY
    | BINARY_DOUBLE
    | BINARY_DOUBLE_NAN
    | BINARY_FLOAT_INFINITY
    | BINARY_FLOAT
    | BINARY_FLOAT_NAN
    | BINARY
    | BIND_AWARE
    | BINDING
    | BIN_TO_NUM
    | BITAND
    | BITMAP
    | BITMAPS
    | BITMAP_TREE
    | BITS
    | BLOB
    | BLOCK
    | BLOCK_RANGE
    | BLOCKSIZE
    | BLOCKS
    | BODY
    | BOTH
    | BOUND
    | BRANCH
    | BREADTH
    | BROADCAST
    | BUFFER_CACHE
    | BUFFER
    | BUFFER_POOL
    | BUILD
    | BULK
    | BYPASS_RECURSIVE_CHECK
    | BYPASS_UJVC
    | BYTE
    | CACHE_CB
    | CACHE_INSTANCES
    | CACHE
    | CACHE_TEMP_TABLE
    | CALL
    | CANCEL
    | CARDINALITY
    | CASCADE
    | CASE
    | CAST
    | CATEGORY
    | CEIL
    | CELL_FLASH_CACHE
    | CERTIFICATE
    | CFILE
    | CHAINED
    | CHANGE_DUPKEY_ERROR_INDEX
    | CHANGE
    | CHARACTER
    | CHAR_CS
    | CHARTOROWID
    | CHECK_ACL_REWRITE
    | CHECKPOINT
    | CHILD
    | CHOOSE
    | CHR
    | CHUNK
    | CLASS
    | CLEAR
    | CLOB
    | CLONE
    | CLOSE_CACHED_OPEN_CURSORS
    | CLOSE
    | CLUSTER_BY_ROWID
    | CLUSTER_ID
    | CLUSTERING_FACTOR
    | CLUSTER_PROBABILITY
    | CLUSTER_SET
    | COALESCE
    | COALESCE_SQ
    | COARSE
    | CO_AUTH_IND
    | COLD
    | COLLECT
    | COLUMNAR
    | COLUMN_AUTH_INDICATOR
    | COLUMN
    | COLUMNS
    | COLUMN_STATS
    | COLUMN_VALUE
    | COMMENT
    | COMMIT
    | COMMITTED
    | COMPACT
    | COMPATIBILITY
    | COMPILE
    | COMPLETE
    | COMPLIANCE
    | COMPOSE
    | COMPOSITE_LIMIT
    | COMPOSITE
    | COMPOUND
    | COMPUTE
    | CONCAT
    | CONFIRM
    | CONFORMING
    | CONNECT_BY_CB_WHR_ONLY
    | CONNECT_BY_COMBINE_SW
    | CONNECT_BY_COST_BASED
    | CONNECT_BY_ELIM_DUPS
    | CONNECT_BY_FILTERING
    | CONNECT_BY_ISCYCLE
    | CONNECT_BY_ISLEAF
    | CONNECT_BY_ROOT
    | CONNECT_TIME
    | CONSIDER
    | CONSISTENT
    | CONSTANT
    | CONST
    | CONSTRAINT
    | CONSTRAINTS
    | CONTAINER
    | CONTENT
    | CONTENTS
    | CONTEXT
    | CONTINUE
    | CONTROLFILE
    | CONVERT
    | CORR_K
    | CORR
    | CORR_S
    | CORRUPTION
    | CORRUPT_XID_ALL
    | CORRUPT_XID
    | COSH
    | COS
    | COST
    | COST_XML_QUERY_REWRITE
    | COUNT
    | COVAR_POP
    | COVAR_SAMP
    | CPU_COSTING
    | CPU_PER_CALL
    | CPU_PER_SESSION
    | CRASH
    | CREATE_STORED_OUTLINES
    | CREATION
    | CROSSEDITION
    | CROSS
    | CSCONVERT
    | CUBE_GB
    | CUBE
    | CUME_DISTM
    | CUME_DIST
    | CURRENT_DATE
    | CURRENT
    | CURRENT_SCHEMA
    | CURRENT_TIME
    | CURRENT_TIMESTAMP
    | CURRENT_USER
    | CURRENTV
    | CURSOR
    | CURSOR_SHARING_EXACT
    | CURSOR_SPECIFIC_SEGMENT
    | CV
    | CYCLE
    | DANGLING
    | DATABASE
    | DATAFILE
    | DATAFILES
    | DATA
    | DATAOBJNO
    | DATAOBJ_TO_PARTITION
    | DATE_MODE
    | DAY
    | DBA
    | DBA_RECYCLEBIN
    | DBMS_STATS
    | DB_ROLE_CHANGE
    | DBTIMEZONE
    | DB_VERSION
    | DDL
    | DEALLOCATE
    | DEBUGGER
    | DEBUG
    | DECLARE
    | DEC
    | DECOMPOSE
    | DECREMENT
    | DECR
    | DECRYPT
    | DEDUPLICATE
    | DEFAULTS
    | DEFERRABLE
    | DEFERRED
    | DEFINED
    | DEFINER
    | DEGREE
    | DELAY
    | DELETEXML
    | DEMAND
    | DENSE_RANKM
    | DENSE_RANK
    | DEPENDENT
    | DEPTH
    | DEQUEUE
    | DEREF
    | DEREF_NO_REWRITE
    | DETACHED
    | DETERMINES
    | DICTIONARY
    | DIMENSION
    | DIRECT_LOAD
    | DIRECTORY
    | DIRECT_PATH
    | DISABLE
    | DISABLE_PRESET
    | DISABLE_RPKE
    | DISALLOW
    | DISASSOCIATE
    | DISCONNECT
    | DISKGROUP
    | DISK
    | DISKS
    | DISMOUNT
    | DISTINGUISHED
    | DISTRIBUTED
    | DML
    | DML_UPDATE
    | DOCFIDELITY
    | DOCUMENT
    | DOMAIN_INDEX_FILTER
    | DOMAIN_INDEX_NO_SORT
    | DOMAIN_INDEX_SORT
    | DOUBLE
    | DOWNGRADE
    | DRIVING_SITE
    | DROP_COLUMN
    | DROP_GROUP
    | DST_UPGRADE_INSERT_CONV
    | DUMP
    | DYNAMIC
    | DYNAMIC_SAMPLING_EST_CDN
    | DYNAMIC_SAMPLING
    | EACH
    | EDITIONING
    | EDITION
    | EDITIONS
    | ELEMENT
    | ELIMINATE_JOIN
    | ELIMINATE_OBY
    | ELIMINATE_OUTER_JOIN
    | EMPTY_BLOB
    | EMPTY_CLOB
    | EMPTY
    | ENABLE
    | ENABLE_PRESET
    | ENCODING
    | ENCRYPTION
    | ENCRYPT
    | END_OUTLINE_DATA
    | ENFORCED
    | ENFORCE
    | ENQUEUE
    | ENTERPRISE
    | ENTITYESCAPING
    | ENTRY
    | ERROR_ARGUMENT
    | ERROR
    | ERROR_ON_OVERLAP_TIME
    | ERRORS
    | ESCAPE
    | ESTIMATE
    | EVALNAME
    | EVALUATION
    | EVENTS
    | EVERY
    | EXCEPTIONS
    | EXCEPT
    | EXCHANGE
    | EXCLUDE
    | EXCLUDING
    | EXECUTE
    | EXEMPT
    | EXISTSNODE
    | EXPAND_GSET_TO_UNION
    | EXPAND_TABLE
    | EXPIRE
    | EXPLAIN
    | EXPLOSION
    | EXP
    | EXPORT
    | EXPR_CORR_CHECK
    | EXTENDS
    | EXTENT
    | EXTENTS
    | EXTERNALLY
    | EXTERNAL
    | EXTRACT
    | EXTRACTVALUE
    | EXTRA
    | FACILITY
    | FACT
    | FACTORIZE_JOIN
    | FAILED_LOGIN_ATTEMPTS
    | FAILED
    | FAILGROUP
    | FALSE
    | FAST
    | FBTSCAN
    | FEATURE_ID
    | FEATURE_SET
    | FEATURE_VALUE
    | FILE
    | FILESYSTEM_LIKE_LOGGING
    | FILTER
    | FINAL
    | FINE
    | FINISH
    | FIRSTM
    | FIRST
    | FIRST_ROWS
    | FIRST_VALUE
    | FLAGGER
    | FLASHBACK
    | FLASH_CACHE
    | FLOB
    | FLOOR
    | FLUSH
    | FOLDER
    | FOLLOWING
    | FOLLOWS
    | FORCE
    | FORCE_XML_QUERY_REWRITE
    | FOREIGN
    | FOREVER
    | FORWARD
    | FRAGMENT_NUMBER
    | FREELIST
    | FREELISTS
    | FREEPOOLS
    | FRESH
    | FROM_TZ
    | FULL
    | FULL_OUTER_JOIN_TO_OUTER
    | FUNCTION
    | FUNCTIONS
    | GATHER_PLAN_STATISTICS
    | GBY_CONC_ROLLUP
    | GBY_PUSHDOWN
    | GENERATED
    | GLOBALLY
    | GLOBAL
    | GLOBAL_NAME
    | GLOBAL_TOPIC_ENABLED
    | GREATEST
    | GROUP_BY
    | GROUP_ID
    | GROUPING_ID
    | GROUPING
    | GROUPS
    | GUARANTEED
    | GUARANTEE
    | GUARD
    | HASH_AJ
    | HASHKEYS
    | HASH
    | HASH_SJ
    | HEADER
    | HEAP
    | HELP
    | HEXTORAW
    | HEXTOREF
    | HIDDEN_KEYWORD
    | HIDE
    | HIERARCHY
    | HIGH
    | HINTSET_BEGIN
    | HINTSET_END
    | HOT
    | HOUR
    | HWM_BROKERED
    | HYBRID
    | IDENTIFIER
    | IDENTITY
    | IDGENERATORS
    | IDLE_TIME
    | ID
    | IF
    | IGNORE
    | IGNORE_OPTIM_EMBEDDED_HINTS
    | IGNORE_ROW_ON_DUPKEY_INDEX
    | IGNORE_WHERE_CLAUSE
    | IMMEDIATE
    | IMPACT
    | IMPORT
    | INCLUDE
    | INCLUDE_VERSION
    | INCLUDING
    | INCREMENTAL
    | INCREMENT
    | INCR
    | INDENT
    | INDEX_ASC
    | INDEX_COMBINE
    | INDEX_DESC
    | INDEXED
    | INDEXES
    | INDEX_FFS
    | INDEX_FILTER
    | INDEX_JOIN
    | INDEX_ROWS
    | INDEX_RRS
    | INDEX_RS_ASC
    | INDEX_RS_DESC
    | INDEX_RS
    | INDEX_SCAN
    | INDEX_SKIP_SCAN
    | INDEX_SS_ASC
    | INDEX_SS_DESC
    | INDEX_SS
    | INDEX_STATS
    | INDEXTYPE
    | INDEXTYPES
    | INDICATOR
    | INFINITE
    | INFORMATIONAL
    | INITCAP
    | INITIALIZED
    | INITIALLY
    | INITIAL
    | INITRANS
    | INLINE
    | INLINE_XMLTYPE_NT
    | IN_MEMORY_METADATA
    | INNER
    | INSERTCHILDXMLAFTER
    | INSERTCHILDXMLBEFORE
    | INSERTCHILDXML
    | INSERTXMLAFTER
    | INSERTXMLBEFORE
    | INSTANCE
    | INSTANCES
    | INSTANTIABLE
    | INSTANTLY
    | INSTEAD
    | INSTR2
    | INSTR4
    | INSTRB
    | INSTRC
    | INSTR
    | INTERMEDIATE
    | INTERNAL_CONVERT
    | INTERNAL_USE
    | INTERPRETED
    | INTERVAL
    | INT
    | INVALIDATE
    | INVISIBLE
    | IN_XQUERY
    | ISOLATION_LEVEL
    | ISOLATION
    | ITERATE
    | ITERATION_NUMBER
    | JAVA
    | JOB
    | JOIN
    | KEEP_DUPLICATES
    | KEEP
    | KERBEROS
    | KEY_LENGTH
    | KEY
    | KEYSIZE
    | KEYS
    | KILL
    | LAG
    | LAST_DAY
    | LAST
    | LAST_VALUE
    | LATERAL
    | LAYER
    | LDAP_REGISTRATION_ENABLED
    | LDAP_REGISTRATION
    | LDAP_REG_SYNC_INTERVAL
    | LEADING
    | LEAD
    | LEAST
    | LEFT
    | LENGTH2
    | LENGTH4
    | LENGTHB
    | LENGTHC
    | LENGTH
    | LESS
    | LEVEL
    | LEVELS
    | LIBRARY
    | LIFE
    | LIFETIME
    | LIKE2
    | LIKE4
    | LIKEC
    | LIKE_EXPAND
    | LIMIT
    | LINK
    | LISTAGG
    | LIST
    | LN
    | LNNVL
    | LOAD
    | LOB
    | LOBNVL
    | LOBS
    | LOCAL_INDEXES
    | LOCAL
    | LOCALTIME
    | LOCALTIMESTAMP
    | LOCATION
    | LOCATOR
    | LOCKED
    | LOGFILE
    | LOGFILES
    | LOGGING
    | LOGICAL
    | LOGICAL_READS_PER_CALL
    | LOGICAL_READS_PER_SESSION
    | LOG
    | LOGOFF
    | LOGON
    | LOG_READ_ONLY_VIOLATIONS
    | LOWER
    | LOW
    | LPAD
    | LTRIM
    | MAIN
    | MAKE_REF
    | MANAGED
    | MANAGEMENT
    | MANAGE
    | MANAGER
    | MANUAL
    | MAPPING
    | MASTER
    | MATCHED
    | MATERIALIZED
    | MATERIALIZE
    | MAXARCHLOGS
    | MAXDATAFILES
    | MAXEXTENTS
    | MAXIMIZE
    | MAXINSTANCES
    | MAXLOGFILES
    | MAXLOGHISTORY
    | MAXLOGMEMBERS
    | MAX
    | MAXSIZE
    | MAXTRANS
    | MAXVALUE
    | MEASURE
    | MEASURES
    | MEDIAN
    | MEDIUM
    | MEMBER
    | MEMORY
    | MERGEACTIONS
    | MERGE_AJ
    | MERGE_CONST_ON
    | MERGE
    | MERGE_SJ
    | METHOD
    | MIGRATE
    | MIGRATION
    | MINEXTENTS
    | MINIMIZE
    | MINIMUM
    | MINING
    | MIN
    | MINUS_NULL
    | MINUTE
    | MINVALUE
    | MIRRORCOLD
    | MIRRORHOT
    | MIRROR
    | MLSLABEL
    | MODEL_COMPILE_SUBQUERY
    | MODEL_DONTVERIFY_UNIQUENESS
    | MODEL_DYNAMIC_SUBQUERY
    | MODEL_MIN_ANALYSIS
    | MODEL
    | MODEL_NO_ANALYSIS
    | MODEL_PBY
    | MODEL_PUSH_REF
    | MODIFY_COLUMN_TYPE
    | MODIFY
    | MOD
    | MONITORING
    | MONITOR
    | MONTH
    | MONTHS_BETWEEN
    | MOUNT
    | MOUNTPATH
    | MOVEMENT
    | MOVE
    | MULTISET
    | MV_MERGE
    | NAMED
    | NAME
    | NAMESPACE
    | NAN
    | NANVL
    | NATIONAL
    | NATIVE_FULL_OUTER_JOIN
    | NATIVE
    | NATURAL
    | NAV
    | NCHAR_CS
    | NCHAR
    | NCHR
    | NCLOB
    | NEEDED
    | NESTED
    | NESTED_TABLE_FAST_INSERT
    | NESTED_TABLE_GET_REFS
    | NESTED_TABLE_ID
    | NESTED_TABLE_SET_REFS
    | NESTED_TABLE_SET_SETID
    | NETWORK
    | NEVER
    | NEW
    | NEW_TIME
    | NEXT_DAY
    | NEXT
    | NL_AJ
    | NLJ_BATCHING
    | NLJ_INDEX_FILTER
    | NLJ_INDEX_SCAN
    | NLJ_PREFETCH
    | NLS_CALENDAR
    | NLS_CHARACTERSET
    | NLS_CHARSET_DECL_LEN
    | NLS_CHARSET_ID
    | NLS_CHARSET_NAME
    | NLS_COMP
    | NLS_CURRENCY
    | NLS_DATE_FORMAT
    | NLS_DATE_LANGUAGE
    | NLS_INITCAP
    | NLS_ISO_CURRENCY
    | NL_SJ
    | NLS_LANG
    | NLS_LANGUAGE
    | NLS_LENGTH_SEMANTICS
    | NLS_LOWER
    | NLS_NCHAR_CONV_EXCP
    | NLS_NUMERIC_CHARACTERS
    | NLS_SORT
    | NLSSORT
    | NLS_SPECIAL_CHARS
    | NLS_TERRITORY
    | NLS_UPPER
    | NO_ACCESS
    | NOAPPEND
    | NOARCHIVELOG
    | NOAUDIT
    | NO_BASETABLE_MULTIMV_REWRITE
    | NO_BIND_AWARE
    | NO_BUFFER
    | NOCACHE
    | NO_CARTESIAN
    | NO_CHECK_ACL_REWRITE
    | NO_CLUSTER_BY_ROWID
    | NO_COALESCE_SQ
    | NO_CONNECT_BY_CB_WHR_ONLY
    | NO_CONNECT_BY_COMBINE_SW
    | NO_CONNECT_BY_COST_BASED
    | NO_CONNECT_BY_ELIM_DUPS
    | NO_CONNECT_BY_FILTERING
    | NO_COST_XML_QUERY_REWRITE
    | NO_CPU_COSTING
    | NOCPU_COSTING
    | NOCYCLE
    | NODELAY
    | NO_DOMAIN_INDEX_FILTER
    | NO_DST_UPGRADE_INSERT_CONV
    | NO_ELIMINATE_JOIN
    | NO_ELIMINATE_OBY
    | NO_ELIMINATE_OUTER_JOIN
    | NOENTITYESCAPING
    | NO_EXPAND_GSET_TO_UNION
    | NO_EXPAND
    | NO_EXPAND_TABLE
    | NO_FACT
    | NO_FACTORIZE_JOIN
    | NO_FILTERING
    | NOFORCE
    | NO_FULL_OUTER_JOIN_TO_OUTER
    | NO_GBY_PUSHDOWN
    | NOGUARANTEE
    | NO_INDEX_FFS
    | NO_INDEX
    | NO_INDEX_SS
    | NO_LOAD
    | NOLOCAL
    | NOLOGGING
    | NOMAPPING
    | NOMAXVALUE
    | NO_MERGE
    | NOMINIMIZE
    | NOMINVALUE
    | NO_MODEL_PUSH_REF
    | NO_MONITORING
    | NOMONITORING
    | NO_MONITOR
    | NO_MULTIMV_REWRITE
    | NO
    | NO_NATIVE_FULL_OUTER_JOIN
    | NONBLOCKING
    | NONE
    | NO_NLJ_BATCHING
    | NO_NLJ_PREFETCH
    | NONSCHEMA
    | NOORDER
    | NO_ORDER_ROLLUPS
    | NO_OUTER_JOIN_TO_ANTI
    | NO_OUTER_JOIN_TO_INNER
    | NOOVERRIDE
    | NO_PARALLEL_INDEX
    | NOPARALLEL_INDEX
    | NO_PARALLEL
    | NOPARALLEL
    | NO_PARTIAL_COMMIT
    | NO_PLACE_DISTINCT
    | NO_PLACE_GROUP_BY
    | NO_PQ_MAP
    | NO_PRUNE_GSETS
    | NO_PULL_PRED
    | NO_PUSH_PRED
    | NO_PUSH_SUBQ
    | NO_PX_JOIN_FILTER
    | NO_QKN_BUFF
    | NO_QUERY_TRANSFORMATION
    | NO_REF_CASCADE
    | NORELY
    | NOREPAIR
    | NORESETLOGS
    | NO_RESULT_CACHE
    | NOREVERSE
    | NO_REWRITE
    | NOREWRITE
    | NORMAL
    | NOROWDEPENDENCIES
    | NOSCHEMACHECK
    | NOSEGMENT
    | NO_SEMIJOIN
    | NO_SEMI_TO_INNER
    | NO_SET_TO_JOIN
    | NOSORT
    | NO_SQL_TUNE
    | NO_STAR_TRANSFORMATION
    | NO_STATEMENT_QUEUING
    | NO_STATS_GSETS
    | NOSTRICT
    | NO_SUBQUERY_PRUNING
    | NO_SUBSTRB_PAD
    | NO_SWAP_JOIN_INPUTS
    | NOSWITCH
    | NO_TABLE_LOOKUP_BY_NL
    | NO_TEMP_TABLE
    | NOTHING
    | NOTIFICATION
    | NO_TRANSFORM_DISTINCT_AGG
    | NO_UNNEST
    | NO_USE_HASH_AGGREGATION
    | NO_USE_HASH_GBY_FOR_PUSHDOWN
    | NO_USE_HASH
    | NO_USE_INVISIBLE_INDEXES
    | NO_USE_MERGE
    | NO_USE_NL
    | NOVALIDATE
    | NO_XDB_FASTPATH_INSERT
    | NO_XML_DML_REWRITE
    | NO_XMLINDEX_REWRITE_IN_SELECT
    | NO_XMLINDEX_REWRITE
    | NO_XML_QUERY_REWRITE
    | NTH_VALUE
    | NTILE
    | NULLIF
    | NULLS
    | NUMERIC
    | NUM_INDEX_KEYS
    | NUMTODSINTERVAL
    | NUMTOYMINTERVAL
    | NVARCHAR2
    | NVL2
    | NVL
    | OBJECT2XML
    | OBJECT
    | OBJNO
    | OBJNO_REUSE
    | OCCURENCES
    | OFFLINE
    | OFF
    | OIDINDEX
    | OID
    | OLAP
    | OLD
    | OLD_PUSH_PRED
    | OLTP
    | ONLINE
    | ONLY
    | OPAQUE
    | OPAQUE_TRANSFORM
    | OPAQUE_XCANONICAL
    | OPCODE
    | OPEN
    | OPERATIONS
    | OPERATOR
    | OPT_ESTIMATE
    | OPTIMAL
    | OPTIMIZE
    | OPTIMIZER_FEATURES_ENABLE
    | OPTIMIZER_GOAL
    | OPT_PARAM
    | ORA_BRANCH
    | ORADEBUG
    | ORA_DST_AFFECTED
    | ORA_DST_CONVERT
    | ORA_DST_ERROR
    | ORA_GET_ACLIDS
    | ORA_GET_PRIVILEGES
    | ORA_HASH
    | ORA_ROWSCN
    | ORA_ROWSCN_RAW
    | ORA_ROWVERSION
    | ORA_TABVERSION
    | ORDERED
    | ORDERED_PREDICATES
    | ORDINALITY
    | OR_EXPAND
    | ORGANIZATION
    | OR_PREDICATES
    | OTHER
    | OUTER_JOIN_TO_ANTI
    | OUTER_JOIN_TO_INNER
    | OUTER
    | OUTLINE_LEAF
    | OUTLINE
    | OUT_OF_LINE
    | OVERFLOW
    | OVERFLOW_NOMOVE
    | OVERLAPS
    | OVER
    | OWNER
    | OWNERSHIP
    | OWN
    | PACKAGE
    | PACKAGES
    | PARALLEL_INDEX
    | PARALLEL
    | PARAMETERS
    | PARAM
    | PARENT
    | PARITY
    | PARTIALLY
    | PARTITION_HASH
    | PARTITION_LIST
    | PARTITION
    | PARTITION_RANGE
    | PARTITIONS
    | PARTNUMINST
    | PASSING
    | PASSWORD_GRACE_TIME
    | PASSWORD_LIFE_TIME
    | PASSWORD_LOCK_TIME
    | PASSWORD
    | PASSWORD_REUSE_MAX
    | PASSWORD_REUSE_TIME
    | PASSWORD_VERIFY_FUNCTION
    | PATH
    | PATHS
    | PBL_HS_BEGIN
    | PBL_HS_END
    | PCTINCREASE
    | PCTTHRESHOLD
    | PCTUSED
    | PCTVERSION
    | PENDING
    | PERCENTILE_CONT
    | PERCENTILE_DISC
    | PERCENT_KEYWORD
    | PERCENT_RANKM
    | PERCENT_RANK
    | PERFORMANCE
    | PERMANENT
    | PERMISSION
    | PFILE
    | PHYSICAL
    | PIKEY
    | PIV_GB
    | PIVOT
    | PIV_SSF
    | PLACE_DISTINCT
    | PLACE_GROUP_BY
    | PLAN
    | PLSCOPE_SETTINGS
    | PLSQL_CCFLAGS
    | PLSQL_CODE_TYPE
    | PLSQL_DEBUG
    | PLSQL_OPTIMIZE_LEVEL
    | PLSQL_WARNINGS
    | POINT
    | POLICY
    | POST_TRANSACTION
    | POWERMULTISET_BY_CARDINALITY
    | POWERMULTISET
    | POWER
    | PQ_DISTRIBUTE
    | PQ_MAP
    | PQ_NOMAP
    | PREBUILT
    | PRECEDES
    | PRECEDING
    | PRECISION
    | PRECOMPUTE_SUBQUERY
    | PREDICATE_REORDERS
    | PREDICTION_BOUNDS
    | PREDICTION_COST
    | PREDICTION_DETAILS
    | PREDICTION
    | PREDICTION_PROBABILITY
    | PREDICTION_SET
    | PREPARE
    | PRESENT
    | PRESENTNNV
    | PRESENTV
    | PRESERVE
    | PRESERVE_OID
    | PREVIOUS
    | PRIMARY
    | PRIVATE
    | PRIVATE_SGA
    | PRIVILEGE
    | PRIVILEGES
    | PROCEDURAL
    | PROCEDURE
    | PROCESS
    | PROFILE
    | PROGRAM
    | PROJECT
    | PROPAGATE
    | PROTECTED
    | PROTECTION
    | PULL_PRED
    | PURGE
    | PUSH_PRED
    | PUSH_SUBQ
    | PX_GRANULE
    | PX_JOIN_FILTER
    | QB_NAME
    | QUERY_BLOCK
    | QUERY
    | QUEUE_CURR
    | QUEUE
    | QUEUE_ROWP
    | QUIESCE
    | QUORUM
    | QUOTA
    | RANDOM_LOCAL
    | RANDOM
    | RANGE
    | RANKM
    | RANK
    | RAPIDLY
    | RATIO_TO_REPORT
    | RAWTOHEX
    | RAWTONHEX
    | RBA
    | RBO_OUTLINE
    | RDBA
    | READ
    | READS
    | REAL
    | REBALANCE
    | REBUILD
    | RECORDS_PER_BLOCK
    | RECOVERABLE
    | RECOVER
    | RECOVERY
    | RECYCLEBIN
    | RECYCLE
    | REDACTION
    | REDO
    | REDUCED
    | REDUNDANCY
    | REF_CASCADE_CURSOR
    | REFERENCED
    | REFERENCE
    | REFERENCES
    | REFERENCING
    | REF
    | REFRESH
    | REFTOHEX
    | REGEXP_COUNT
    | REGEXP_INSTR
    | REGEXP_LIKE
    | REGEXP_REPLACE
    | REGEXP_SUBSTR
    | REGISTER
    | REGR_AVGX
    | REGR_AVGY
    | REGR_COUNT
    | REGR_INTERCEPT
    | REGR_R2
    | REGR_SLOPE
    | REGR_SXX
    | REGR_SXY
    | REGR_SYY
    | REGULAR
    | REJECT
    | REKEY
    | RELATIONAL
    | RELY
    | REMAINDER
    | REMOTE_MAPPED
    | REMOVE
    | REPAIR
    | REPEAT
    | REPLACE
    | REPLICATION
    | REQUIRED
    | RESETLOGS
    | RESET
    | RESIZE
    | RESOLVE
    | RESOLVER
    | RESPECT
    | RESTORE_AS_INTERVALS
    | RESTORE
    | RESTRICT_ALL_REF_CONS
    | RESTRICTED
    | RESTRICT
    | RESULT_CACHE
    | RESUMABLE
    | RESUME
    | RETENTION
    | RETRY_ON_ROW_CHANGE
    | RETURNING
    | RETURN
    | REUSE
    | REVERSE
    | REWRITE
    | REWRITE_OR_ERROR
    | RIGHT
    | ROLE
    | ROLES
    | ROLLBACK
    | ROLLING
    | ROLLUP
    | ROUND
    | ROWDEPENDENCIES
    | ROWID
    | ROWIDTOCHAR
    | ROWIDTONCHAR
    | ROW_LENGTH
    | ROW
    | ROW_NUMBER
    | ROWNUM
    | ROWS
    | RPAD
    | RTRIM
    | RULE
    | RULES
    | SALT
    | SAMPLE
    | SAVE_AS_INTERVALS
    | SAVEPOINT
    | SB4
    | SCALE
    | SCALE_ROWS
    | SCAN_INSTANCES
    | SCAN
    | SCHEDULER
    | SCHEMACHECK
    | SCHEMA
    | SCN_ASCENDING
    | SCN
    | SCOPE
    | SD_ALL
    | SD_INHIBIT
    | SD_SHOW
    | SEARCH
    | SECOND
    | SECUREFILE_DBA
    | SECUREFILE
    | SECURITY
    | SEED
    | SEG_BLOCK
    | SEG_FILE
    | SEGMENT
    | SELECTIVITY
    | SEMIJOIN_DRIVER
    | SEMIJOIN
    | SEMI_TO_INNER
    | SEQUENCED
    | SEQUENCE
    | SEQUENTIAL
    | SERIALIZABLE
    | SERVERERROR
    | SESSION_CACHED_CURSORS
    | SESSION
    | SESSIONS_PER_USER
    | SESSIONTIMEZONE
    | SESSIONTZNAME
    | SETS
    | SETTINGS
    | SET_TO_JOIN
    | SEVERE
    | SHARED
    | SHARED_POOL
    | SHOW
    | SHRINK
    | SHUTDOWN
    | SIBLINGS
    | SID
    | SIGNAL_COMPONENT
    | SIGNAL_FUNCTION
    | SIGN
    | SIMPLE
    | SINGLE
    | SINGLETASK
    | SINH
    | SIN
    | SKIP_EXT_OPTIMIZER
    | SKIP_
    | SKIP_UNQ_UNUSABLE_IDX
    | SKIP_UNUSABLE_INDEXES
    | SMALLFILE
    | SNAPSHOT
    | SOME
    | SORT
    | SOUNDEX
    | SOURCE
    | SPACE_KEYWORD
    | SPECIFICATION
    | SPFILE
    | SPLIT
    | SPREADSHEET
    | SQLLDR
    | SQL
    | SQL_TRACE
    | SQRT
    | STALE
    | STANDALONE
    | STANDBY_MAX_DATA_DELAY
    | STANDBY
    | STAR
    | STAR_TRANSFORMATION
    | STARTUP
    | STATEMENT_ID
    | STATEMENT_QUEUING
    | STATEMENTS
    | STATIC
    | STATISTICS
    | STATS_BINOMIAL_TEST
    | STATS_CROSSTAB
    | STATS_F_TEST
    | STATS_KS_TEST
    | STATS_MODE
    | STATS_MW_TEST
    | STATS_ONE_WAY_ANOVA
    | STATS_T_TEST_INDEP
    | STATS_T_TEST_INDEPU
    | STATS_T_TEST_ONE
    | STATS_T_TEST_PAIRED
    | STATS_WSR_TEST
    | STDDEV
    | STDDEV_POP
    | STDDEV_SAMP
    | STOP
    | STORAGE
    | STORE
    | STREAMS
    | STRICT
    | STRING
    | STRIPE_COLUMNS
    | STRIPE_WIDTH
    | STRIP
    | STRUCTURE
    | SUBMULTISET
    | SUBPARTITION
    | SUBPARTITION_REL
    | SUBPARTITIONS
    | SUBQUERIES
    | SUBQUERY_PRUNING
    | SUBSTITUTABLE
    | SUBSTR2
    | SUBSTR4
    | SUBSTRB
    | SUBSTRC
    | SUBSTR
    | SUCCESSFUL
    | SUMMARY
    | SUM
    | SUPPLEMENTAL
    | SUSPEND
    | SWAP_JOIN_INPUTS
    | SWITCH
    | SWITCHOVER
    | SYNCHRONOUS
    | SYNC
    | SYSASM
    | SYS_AUDIT
    | SYSAUX
    | SYS_CHECKACL
    | SYS_CONNECT_BY_PATH
    | SYS_CONTEXT
    | SYSDATE
    | SYSDBA
    | SYS_DBURIGEN
    | SYS_DL_CURSOR
    | SYS_DM_RXFORM_CHR
    | SYS_DM_RXFORM_NUM
    | SYS_DOM_COMPARE
    | SYS_DST_PRIM2SEC
    | SYS_DST_SEC2PRIM
    | SYS_ET_BFILE_TO_RAW
    | SYS_ET_BLOB_TO_IMAGE
    | SYS_ET_IMAGE_TO_BLOB
    | SYS_ET_RAW_TO_BFILE
    | SYS_EXTPDTXT
    | SYS_EXTRACT_UTC
    | SYS_FBT_INSDEL
    | SYS_FILTER_ACLS
    | SYS_FNMATCHES
    | SYS_FNREPLACE
    | SYS_GET_ACLIDS
    | SYS_GET_PRIVILEGES
    | SYS_GETTOKENID
    | SYS_GETXTIVAL
    | SYS_GUID
    | SYS_MAKEXML
    | SYS_MAKE_XMLNODEID
    | SYS_MKXMLATTR
    | SYS_OP_ADT2BIN
    | SYS_OP_ADTCONS
    | SYS_OP_ALSCRVAL
    | SYS_OP_ATG
    | SYS_OP_BIN2ADT
    | SYS_OP_BITVEC
    | SYS_OP_BL2R
    | SYS_OP_BLOOM_FILTER_LIST
    | SYS_OP_BLOOM_FILTER
    | SYS_OP_C2C
    | SYS_OP_CAST
    | SYS_OP_CEG
    | SYS_OP_CL2C
    | SYS_OP_COMBINED_HASH
    | SYS_OP_COMP
    | SYS_OP_CONVERT
    | SYS_OP_COUNTCHG
    | SYS_OP_CSCONV
    | SYS_OP_CSCONVTEST
    | SYS_OP_CSR
    | SYS_OP_CSX_PATCH
    | SYS_OP_DECOMP
    | SYS_OP_DESCEND
    | SYS_OP_DISTINCT
    | SYS_OP_DRA
    | SYS_OP_DUMP
    | SYS_OP_DV_CHECK
    | SYS_OP_ENFORCE_NOT_NULL
    | SYSOPER
    | SYS_OP_EXTRACT
    | SYS_OP_GROUPING
    | SYS_OP_GUID
    | SYS_OP_IIX
    | SYS_OP_ITR
    | SYS_OP_LBID
    | SYS_OP_LOBLOC2BLOB
    | SYS_OP_LOBLOC2CLOB
    | SYS_OP_LOBLOC2ID
    | SYS_OP_LOBLOC2NCLOB
    | SYS_OP_LOBLOC2TYP
    | SYS_OP_LSVI
    | SYS_OP_LVL
    | SYS_OP_MAKEOID
    | SYS_OP_MAP_NONNULL
    | SYS_OP_MSR
    | SYS_OP_NICOMBINE
    | SYS_OP_NIEXTRACT
    | SYS_OP_NII
    | SYS_OP_NIX
    | SYS_OP_NOEXPAND
    | SYS_OP_NTCIMG
    | SYS_OP_NUMTORAW
    | SYS_OP_OIDVALUE
    | SYS_OP_OPNSIZE
    | SYS_OP_PAR_1
    | SYS_OP_PARGID_1
    | SYS_OP_PARGID
    | SYS_OP_PAR
    | SYS_OP_PIVOT
    | SYS_OP_R2O
    | SYS_OP_RAWTONUM
    | SYS_OP_RDTM
    | SYS_OP_REF
    | SYS_OP_RMTD
    | SYS_OP_ROWIDTOOBJ
    | SYS_OP_RPB
    | SYS_OPTLOBPRBSC
    | SYS_OP_TOSETID
    | SYS_OP_TPR
    | SYS_OP_TRTB
    | SYS_OPTXICMP
    | SYS_OPTXQCASTASNQ
    | SYS_OP_UNDESCEND
    | SYS_OP_VECAND
    | SYS_OP_VECBIT
    | SYS_OP_VECOR
    | SYS_OP_VECXOR
    | SYS_OP_VERSION
    | SYS_OP_VREF
    | SYS_OP_VVD
    | SYS_OP_XMLCONS_FOR_CSX
    | SYS_OP_XPTHATG
    | SYS_OP_XPTHIDX
    | SYS_OP_XPTHOP
    | SYS_OP_XTXT2SQLT
    | SYS_ORDERKEY_DEPTH
    | SYS_ORDERKEY_MAXCHILD
    | SYS_ORDERKEY_PARENT
    | SYS_PARALLEL_TXN
    | SYS_PATHID_IS_ATTR
    | SYS_PATHID_IS_NMSPC
    | SYS_PATHID_LASTNAME
    | SYS_PATHID_LASTNMSPC
    | SYS_PATH_REVERSE
    | SYS_PXQEXTRACT
    | SYS_RID_ORDER
    | SYS_ROW_DELTA
    | SYS_SC_2_XMLT
    | SYS_SYNRCIREDO
    | SYSTEM_DEFINED
    | SYSTEM
    | SYSTIMESTAMP
    | SYS_TYPEID
    | SYS_UMAKEXML
    | SYS_XMLANALYZE
    | SYS_XMLCONTAINS
    | SYS_XMLCONV
    | SYS_XMLEXNSURI
    | SYS_XMLGEN
    | SYS_XMLI_LOC_ISNODE
    | SYS_XMLI_LOC_ISTEXT
    | SYS_XMLINSTR
    | SYS_XMLLOCATOR_GETSVAL
    | SYS_XMLNODEID_GETCID
    | SYS_XMLNODEID_GETLOCATOR
    | SYS_XMLNODEID_GETOKEY
    | SYS_XMLNODEID_GETPATHID
    | SYS_XMLNODEID_GETPTRID
    | SYS_XMLNODEID_GETRID
    | SYS_XMLNODEID_GETSVAL
    | SYS_XMLNODEID_GETTID
    | SYS_XMLNODEID
    | SYS_XMLT_2_SC
    | SYS_XMLTRANSLATE
    | SYS_XMLTYPE2SQL
    | SYS_XQ_ASQLCNV
    | SYS_XQ_ATOMCNVCHK
    | SYS_XQBASEURI
    | SYS_XQCASTABLEERRH
    | SYS_XQCODEP2STR
    | SYS_XQCODEPEQ
    | SYS_XQCON2SEQ
    | SYS_XQCONCAT
    | SYS_XQDELETE
    | SYS_XQDFLTCOLATION
    | SYS_XQDOC
    | SYS_XQDOCURI
    | SYS_XQDURDIV
    | SYS_XQED4URI
    | SYS_XQENDSWITH
    | SYS_XQERRH
    | SYS_XQERR
    | SYS_XQESHTMLURI
    | SYS_XQEXLOBVAL
    | SYS_XQEXSTWRP
    | SYS_XQEXTRACT
    | SYS_XQEXTRREF
    | SYS_XQEXVAL
    | SYS_XQFB2STR
    | SYS_XQFNBOOL
    | SYS_XQFNCMP
    | SYS_XQFNDATIM
    | SYS_XQFNLNAME
    | SYS_XQFNNM
    | SYS_XQFNNSURI
    | SYS_XQFNPREDTRUTH
    | SYS_XQFNQNM
    | SYS_XQFNROOT
    | SYS_XQFORMATNUM
    | SYS_XQFTCONTAIN
    | SYS_XQFUNCR
    | SYS_XQGETCONTENT
    | SYS_XQINDXOF
    | SYS_XQINSERT
    | SYS_XQINSPFX
    | SYS_XQIRI2URI
    | SYS_XQLANG
    | SYS_XQLLNMFRMQNM
    | SYS_XQMKNODEREF
    | SYS_XQNILLED
    | SYS_XQNODENAME
    | SYS_XQNORMSPACE
    | SYS_XQNORMUCODE
    | SYS_XQ_NRNG
    | SYS_XQNSP4PFX
    | SYS_XQNSPFRMQNM
    | SYS_XQPFXFRMQNM
    | SYS_XQ_PKSQL2XML
    | SYS_XQPOLYABS
    | SYS_XQPOLYADD
    | SYS_XQPOLYCEL
    | SYS_XQPOLYCSTBL
    | SYS_XQPOLYCST
    | SYS_XQPOLYDIV
    | SYS_XQPOLYFLR
    | SYS_XQPOLYMOD
    | SYS_XQPOLYMUL
    | SYS_XQPOLYRND
    | SYS_XQPOLYSQRT
    | SYS_XQPOLYSUB
    | SYS_XQPOLYUMUS
    | SYS_XQPOLYUPLS
    | SYS_XQPOLYVEQ
    | SYS_XQPOLYVGE
    | SYS_XQPOLYVGT
    | SYS_XQPOLYVLE
    | SYS_XQPOLYVLT
    | SYS_XQPOLYVNE
    | SYS_XQREF2VAL
    | SYS_XQRENAME
    | SYS_XQREPLACE
    | SYS_XQRESVURI
    | SYS_XQRNDHALF2EVN
    | SYS_XQRSLVQNM
    | SYS_XQRYENVPGET
    | SYS_XQRYVARGET
    | SYS_XQRYWRP
    | SYS_XQSEQ2CON4XC
    | SYS_XQSEQ2CON
    | SYS_XQSEQDEEPEQ
    | SYS_XQSEQINSB
    | SYS_XQSEQRM
    | SYS_XQSEQRVS
    | SYS_XQSEQSUB
    | SYS_XQSEQTYPMATCH
    | SYS_XQSTARTSWITH
    | SYS_XQSTATBURI
    | SYS_XQSTR2CODEP
    | SYS_XQSTRJOIN
    | SYS_XQSUBSTRAFT
    | SYS_XQSUBSTRBEF
    | SYS_XQTOKENIZE
    | SYS_XQTREATAS
    | SYS_XQ_UPKXML2SQL
    | SYS_XQXFORM
    | TABLE
    | TABLE_LOOKUP_BY_NL
    | TABLES
    | TABLESPACE
    | TABLESPACE_NO
    | TABLE_STATS
    | TABNO
    | TANH
    | TAN
    | TBLORIDXPARTNUM
    | TEMPFILE
    | TEMPLATE
    | TEMPORARY
    | TEMP_TABLE
    | TEST
    | THAN
    | THE
    | THEN
    | THREAD
    | THROUGH
    | TIME
    | TIMEOUT
    | TIMES
    | TIMESTAMP
    | TIMEZONE_ABBR
    | TIMEZONE_HOUR
    | TIMEZONE_MINUTE
    | TIME_ZONE
    | TIMEZONE_OFFSET
    | TIMEZONE_REGION
    | TIV_GB
    | TIV_SSF
    | TO_BINARY_DOUBLE
    | TO_BINARY_FLOAT
    | TO_BLOB
    | TO_CHAR
    | TO_CLOB
    | TO_DATE
    | TO_DSINTERVAL
    | TO_LOB
    | TO_MULTI_BYTE
    | TO_NCHAR
    | TO_NCLOB
    | TO_NUMBER
    | TOPLEVEL
    | TO_SINGLE_BYTE
    | TO_TIME
    | TO_TIMESTAMP
    | TO_TIMESTAMP_TZ
    | TO_TIME_TZ
    | TO_YMINTERVAL
    | TRACE
    | TRACING
    | TRACKING
    | TRAILING
    | TRANSACTION
    | TRANSFORM_DISTINCT_AGG
    | TRANSITIONAL
    | TRANSITION
    | TRANSLATE
    | TREAT
    | TRIGGERS
    | TRIM
    | TRUE
    | TRUNCATE
    | TRUNC
    | TRUSTED
    | TUNING
    | TX
    | TYPE
    | TYPES
    | TZ_OFFSET
    | UB2
    | UBA
    | UID
    | UNARCHIVED
    | UNBOUNDED
    | UNBOUND
    | UNDER
    | UNDO
    | UNDROP
    | UNIFORM
    | UNISTR
    | UNLIMITED
    | UNLOAD
    | UNLOCK
    | UNNEST_INNERJ_DISTINCT_VIEW
    | UNNEST
    | UNNEST_NOSEMIJ_NODISTINCTVIEW
    | UNNEST_SEMIJ_VIEW
    | UNPACKED
    | UNPIVOT
    | UNPROTECTED
    | UNQUIESCE
    | UNRECOVERABLE
    | UNRESTRICTED
    | UNTIL
    | UNUSABLE
    | UNUSED
    | UPDATABLE
    | UPDATED
    | UPDATEXML
    | UPD_INDEXES
    | UPD_JOININDEX
    | UPGRADE
    | UPPER
    | UPSERT
    | UROWID
    | USAGE
    | USE_ANTI
    | USE_CONCAT
    | USE_HASH_AGGREGATION
    | USE_HASH_GBY_FOR_PUSHDOWN
    | USE_HASH
    | USE_INVISIBLE_INDEXES
    | USE_MERGE_CARTESIAN
    | USE_MERGE
    | USE
    | USE_NL
    | USE_NL_WITH_INDEX
    | USE_PRIVATE_OUTLINES
    | USER_DEFINED
    | USERENV
    | USERGROUP
    | USER
    | USER_RECYCLEBIN
    | USERS
    | USE_SEMI
    | USE_STORED_OUTLINES
    | USE_TTT_FOR_GSETS
    | USE_WEAK_NAME_RESL
    | USING
    | VALIDATE
    | VALIDATION
    | VALUE
    | VARIANCE
    | VAR_POP
    | VARRAY
    | VARRAYS
    | VAR_SAMP
    | VARYING
    | VECTOR_READ
    | VECTOR_READ_TRACE
    | VERIFY
    | VERSIONING
    | VERSION
    | VERSIONS_ENDSCN
    | VERSIONS_ENDTIME
    | VERSIONS
    | VERSIONS_OPERATION
    | VERSIONS_STARTSCN
    | VERSIONS_STARTTIME
    | VERSIONS_XID
    | VIRTUAL
    | VISIBLE
    | VOLUME
    | VSIZE
    | WAIT
    | WALLET
    | WELLFORMED
    | WHENEVER
    | WHEN
    | WHITESPACE
    | WIDTH_BUCKET
    | WITHIN
    | WITHOUT
    | WORK
    | WRAPPED
    | WRITE
    | XDB_FASTPATH_INSERT
    | X_DYN_PRUNE
    | XID
    | XML2OBJECT
    | XMLATTRIBUTES
    | XMLCAST
    | XMLCDATA
    | XMLCOLATTVAL
    | XMLCOMMENT
    | XMLCONCAT
    | XMLDIFF
    | XML_DML_RWT_STMT
    | XMLELEMENT
    | XMLEXISTS2
    | XMLEXISTS
    | XMLFOREST
    | XMLINDEX_REWRITE_IN_SELECT
    | XMLINDEX_REWRITE
    | XMLINDEX_SEL_IDX_TBL
    | XMLISNODE
    | XMLISVALID
    | XML
    | XMLNAMESPACES
    | XMLPARSE
    | XMLPATCH
    | XMLPI
    | XMLQUERY
    | XMLQUERYVAL
    | XMLROOT
    | XMLSCHEMA
    | XMLSERIALIZE
    | XMLTABLE
    | XMLTRANSFORMBLOB
    | XMLTRANSFORM
    | XMLTYPE
    | XPATHTABLE
    | XS_SYS_CONTEXT
    | YEAR
    | YES
    | ZONE
    ;

