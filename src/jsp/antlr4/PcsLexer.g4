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

lexer grammar PcsLexer;

@header {
package com.cubrid.plcsql.compiler.antlrgen;
}

@members {
private int staticSqlParenMatch = -1;
}

// keywords that starts Static SQL
WITH:                         W I T H           { staticSqlParenMatch++; mode(STATIC_SQL); };
SELECT:                       S E L E C T       { staticSqlParenMatch++; mode(STATIC_SQL); };
INSERT:                       I N S E R T       { staticSqlParenMatch++; mode(STATIC_SQL); };
UPDATE:                       U P D A T E       { staticSqlParenMatch++; mode(STATIC_SQL); };
DELETE:                       D E L E T E       { staticSqlParenMatch++; mode(STATIC_SQL); };
REPLACE:                      R E P L A C E     { staticSqlParenMatch++; mode(STATIC_SQL); };
MERGE:                        M E R G E         { staticSqlParenMatch++; mode(STATIC_SQL); };
TRUNCATE:                     T R U N C A T E   { staticSqlParenMatch++; mode(STATIC_SQL); };

// other keywords
AND:                          A N D ;
AS:                           A S ;
AUTONOMOUS_TRANSACTION:       A U T O N O M O U S '_' T R A N S A C T I O N ;
BEGIN:                        B E G I N ;
BETWEEN:                      B E T W E E N ;
BIGINT :                      B I G I N T ;
BOOLEAN:                      B O O L E A N ;
BY:                           B Y ;
CASE:                         C A S E ;
CHARACTER:                    C H A R A C T E R ;
CHAR:                         C H A R ;
CLOSE:                        C L O S E ;
COMMIT:                       C O M M I T ;
CONSTANT:                     C O N S T A N T ;
CONTINUE:                     C O N T I N U E ;
CREATE:                       C R E A T E;
CURSOR:                       C U R S O R ;
DATE:                         D A T E ;
DATETIME:                     D A T E T I M E ;
DATETIMELTZ:                  D A T E T I M E L T Z ;
DATETIMETZ:                   D A T E T I M E T Z ;
DEC:                          D E C ;
DECIMAL:                      D E C I M A L ;
DECLARE:                      D E C L A R E ;
DEFAULT:                      D E F A U L T ;
DIV:                          D I V ;
DOUBLE:                       D O U B L E ;
ELSE:                         E L S E ;
ELSIF:                        E L S I F ;
END:                          E N D ;
ESCAPE:                       E S C A P E ;
EXCEPTION:                    E X C E P T I O N ;
EXECUTE:                      E X E C U T E ;
EXIT:                         E X I T ;
FALSE:                        F A L S E ;
FETCH:                        F E T C H ;
FLOAT:                        F L O A T ;
FOR:                          F O R ;
FUNCTION:                     F U N C T I O N ;
IF:                           I F ;
IMMEDIATE:                    I M M E D I A T E ;
IN:                           I N ;
INTEGER:                      I N T E G E R ;
INT:                          I N T ;
INTO:                         I N T O ;
IS:                           I S ;
LIKE:                         L I K E ;
LIST:                         L I S T ;
LOOP:                         L O O P ;
MOD:                          M O D ;
MULTISET:                     M U L T I S E T ;
NOT:                          N O T ;
NULL_:                        N U L L ;
NUMERIC:                      N U M E R I C ;
OF:                           O F ;
OPEN:                         O P E N ;
OR_REPLACE:                   O R SPACE+ R E P L A C E ;
OR:                           O R;
OUT:                          O U T ;
PERCENT_FOUND:                '%' SPACE* F O U N D ;
PERCENT_ISOPEN:               '%' SPACE* I S O P E N ;
PERCENT_NOTFOUND:             '%' SPACE* N O T F O U N D ;
PERCENT_ROWCOUNT:             '%' SPACE* R O W C O U N T ;
PERCENT_TYPE:                 '%' SPACE* T Y P E ;
PRAGMA:                       P R A G M A ;
PRECISION:                    P R E C I S I O N ;
PROCEDURE:                    P R O C E D U R E ;
RAISE:                        R A I S E ;
RAISE_APPLICATION_ERROR:      R A I S E '_' A P P L I C A T I O N '_' E R R O R ;
REAL:                         R E A L ;
RETURN:                       R E T U R N ;
REVERSE:                      R E V E R S E ;
ROLLBACK:                     R O L L B A C K ;
SEQUENCE:                     S E Q U E N C E ;
SET:                          S E T ;
SETEQ:                        S E T E Q;
SETNEQ:                       S E T N E Q;
SHORT:                        S H O R T ;
SMALLINT:                     S M A L L I N T ;
SQL:                          S Q L ;
STRING:                       S T R I N G ;
SUBSET:                       S U B S E T ;
SUBSETEQ:                     S U B S E T E Q;
SUPERSET:                     S U P E R S E T;
SUPERSETEQ:                   S U P E R S E T E Q ;
SYS_REFCURSOR:                S Y S '_' R E F C U R S O R ;
THEN:                         T H E N ;
TIMESTAMP:                    T I M E S T A M P ;
TIMESTAMPLTZ:                 T I M E S T A M P L T Z ;
TIMESTAMPTZ:                  T I M E S T A M P T Z ;
TIME:                         T I M E ;
TRUE:                         T R U E ;
USING:                        U S I N G ;
VARCHAR:                      V A R C H A R ;
WHEN:                         W H E N ;
WHILE:                        W H I L E ;
WORK:                         W O R K ;
XOR:                          X O R ;
VARYING:                      V A R Y I N G ;

PERIOD2:  '..';
PERIOD:   '.';

FLOATING_POINT_NUM: BASIC_UINT? '.' [0-9]+ ([eE] ('+'|'-')? BASIC_UINT)? [fF]?;
UNSIGNED_INTEGER:    BASIC_UINT ([eE] ('+'|'-')? BASIC_UINT)?;

DELIMITED_ID: ('"' | '[' | '`') REGULAR_ID ('"' | ']' | '`') ;
CHAR_STRING: '\''  (~('\'' | '\r' | '\n') | '\'' '\'' | NEWLINE)* '\'';

NULL_SAFE_EQUALS_OP:          '<=>';

GE:             '>=';
LE:             '<=';
CONCAT_OP:      '||';
LT2:            '<<';
GT2:            '>>';
ASTERISK2:      '**';

LPAREN:                    '(';
RPAREN:                    ')';
ASTERISK:                  '*';
PLUS_SIGN:                 '+';
MINUS_SIGN:                '-';
BIT_COMPLI:                '~';
COMMA:                     ',';
SOLIDUS:                   '/';
AT_SIGN:                   '@';
ASSIGN_OP:                 ':=';

NOT_EQUAL_OP:              '!='
            |              '<>'
            ;

AMPERSAND:          '&';
CARRET_OP:          '^';
EXCLAMATION_OP:     '!';
GT:                 '>';
LT:                 '<';
COLON:              ':';
SEMICOLON:          ';';

BAR:                '|';
EQUALS_OP:          '=';

LEFT_BRACKET:       '[';
RIGHT_BRACKET:      ']';

LEFT_BRACE:         '{';
RIGHT_BRACE:        '}';

INTRODUCER:         '_';

SINGLE_LINE_COMMENT:    '--' ~('\r' | '\n')* NEWLINE_EOF                 -> channel(HIDDEN);
SINGLE_LINE_COMMENT2:   '//' ~('\r' | '\n')* NEWLINE_EOF                 -> channel(HIDDEN);
MULTI_LINE_COMMENT:     '/*' .*? '*/'                                    -> channel(HIDDEN);

REGULAR_ID: SIMPLE_LETTER (SIMPLE_LETTER | '_' | [0-9])*;

SPACES: [ \t\r\n]+ -> channel(HIDDEN);

// ************************
mode STATIC_SQL;
// ************************

SS_SEMICOLON :  ';' {
        setType(PcsParser.SEMICOLON);
        staticSqlParenMatch = -1;
        mode(DEFAULT_MODE);
    };
SS_STR :        '\''  (~('\'' | '\r' | '\n') | '\'' '\'' | NEWLINE)* '\'' ;
SS_WS :         [ \t\r\n]+ ;
SS_LPAREN :     '(' {
        staticSqlParenMatch++;
        setType(PcsParser.SS_NON_STR);
    };
SS_RPAREN :     ')' {
        staticSqlParenMatch--;
        if (staticSqlParenMatch == -1) {
            mode(DEFAULT_MODE);
            setType(PcsParser.RPAREN);
        } else {
            setType(PcsParser.SS_NON_STR);
        }
    };
SS_NON_STR:     ~( ';' | '\'' | ' ' | '\t' | '\r' | '\n' | '(' | ')' )+ ;

// ************************
// Fragment rules
// ************************

fragment BASIC_UINT     : '0'|[1-9][0-9]*;
fragment NEWLINE_EOF    : NEWLINE | EOF;
fragment SIMPLE_LETTER  : [A-Za-z] | [\uAC00-\uD7A3];   // English letters and Korean letters
fragment FLOAT_FRAGMENT : UNSIGNED_INTEGER* '.'? UNSIGNED_INTEGER+;
fragment NEWLINE        : '\r'? '\n';
fragment SPACE          : [ \t];

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


