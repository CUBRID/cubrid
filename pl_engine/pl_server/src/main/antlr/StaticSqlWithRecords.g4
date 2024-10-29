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

grammar StaticSqlWithRecords;

@header {
package com.cubrid.plcsql.compiler.antlrgen;
}

// =====================================
// Lexer Rules
// =====================================

INSERT:                       I N S E R T ;
REPLACE:                      R E P L A C E ;
UPDATE:                       U P D A T E ;

DUPLICATE:                  D U P L I C A T E;
KEY:                        K E Y;
ON:                         O N;
ROW:                        R O W;
SET:                        S E T;
VALUE:                      V A L U E ;
VALUES:                     V A L U E S ;

REGULAR_ID: (SIMPLE_LETTER | '_') (SIMPLE_LETTER | '_' | [0-9])*;
CHAR_STRING: '\''  (~('\'' | '\r' | '\n') | '\'' '\'' | NEWLINE)* '\'';

SINGLE_LINE_COMMENT:    '--' ~('\r' | '\n')* NEWLINE_EOF    -> channel(HIDDEN);
SINGLE_LINE_COMMENT2:   '//' ~('\r' | '\n')* NEWLINE_EOF    -> channel(HIDDEN);
MULTI_LINE_COMMENT:     '/*' .*? '*/'                       -> channel(HIDDEN);
SPACES: [ \t\r\n]+                                          -> channel(HIDDEN);

// sequence of letters which are not delimiters and spaces
OPAQUE: ~( '=' | ',' | ';' | '\'' | ' ' | '\t' | '\r' | '\n' )+;

fragment NEWLINE_EOF    : NEWLINE | EOF;
fragment SIMPLE_LETTER  : [A-Za-z] | [\uAC00-\uD7A3];   // English letters and Korean letters
fragment NEWLINE        : '\r'? '\n';

fragment A : [aA]; // match either an 'a' or 'A'
fragment B : [bB];
fragment C : [cC];
fragment D : [dD];
fragment E : [eE];
fragment F : [fF];
fragment G : [gG];
fragment H : [hH];
fragment I : [iI];
fragment J : [jJ];
fragment K : [kK];
fragment L : [lL];
fragment M : [mM];
fragment N : [nN];
fragment O : [oO];
fragment P : [pP];
fragment Q : [qQ];
fragment R : [rR];
fragment S : [sS];
fragment T : [tT];
fragment U : [uU];
fragment V : [vV];
fragment W : [wW];
fragment X : [xX];
fragment Y : [yY];
fragment Z : [zZ];

// =====================================
// Parser Rules
// =====================================

stmt_w_record_values
    : INSERT any+ (VALUES | VALUE) record_list (ON DUPLICATE KEY UPDATE any+)? EOF
    | REPLACE any+ (VALUES | VALUE) record_list EOF
    ;

stmt_w_record_set
    : UPDATE any* table_spec SET row_set any+ EOF
    | INSERT any* table_spec SET row_set (ON DUPLICATE KEY UPDATE any+)? EOF
    | REPLACE any* table_spec SET row_set EOF
    ;

table_spec
    : REGULAR_ID
    | OPAQUE
    ;

row_set
    : ROW '=' record
    ;

record
    : REGULAR_ID
    ;

record_list
    : record (',' record)*
    ;

any
    : REGULAR_ID
    | CHAR_STRING
    | OPAQUE
    | '=' | ','
    ;
