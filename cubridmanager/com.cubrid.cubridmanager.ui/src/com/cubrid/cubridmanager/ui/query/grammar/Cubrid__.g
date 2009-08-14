lexer grammar Cubrid;
options {
  language=Java;

}
@header {package com.cubrid.cubridmanager.ui.query.grammar;}

ACTION : 'ACTION' ;
ADD : 'ADD' ;
ALL : 'ALL' ;
ALTER : 'ALTER' ;
AM : 'AM' ;
AND : 'AND' ;
AS : 'AS' ;
ASC : 'ASC' ;
ATTRIBUTE : 'ATTRIBUTE' ;
AUTOCOMMIT : 'AUTOCOMMIT' ;
AUTO_INCREMENT : 'AUTO_INCREMENT' ;
BETWEEN : 'BETWEEN' ;
BIT : 'BIT' ;
BY : 'BY' ;
CALL : 'CALL' ;
CASE : 'CASE' ;
CACHE : 'CACHE' ;
CASCADE : 'CASCADE' ;
CHANGE : 'CHANGE' ;
CHAR : 'CHAR' ;
CHARACTER : 'CHARACTER' ;
CHECK : 'CHECK' ;
CLASS : 'CLASS' ;
COMMIT : 'COMMIT' ;
CONSTRAINT : 'CONSTRAINT' ;
CREATE : 'CREATE' ;
DATE : 'DATE' ;
DECIMAL : 'DECIMAL' ;
DEFERRED : 'DEFERRED' ;
DESC : 'DESC' ;
DEFAULT : 'DEFAULT' ;
DELETE : 'DELETE' ;
DIFFERENCE : 'DIFFERENCE' ;
DISTINCT : 'DISTINCT' ;
DOUBLE : 'DOUBLE' ;
DROP : 'DROP' ;
ELSE : 'ELSE' ;
END_STRING : 'END' ;
EXCEPT : 'EXCET' ;
EXISTS : 'EXISTS' ;
FILE : 'FILE' ;
FLOAT : 'FLOAT' ;
FOREIGN : 'FOREIGN' ;
FROM : 'FROM' ;
FUNCTION : 'FUNCTION' ;
GROUP : 'GROUP' ;
HAVING : 'HAVING' ;
IN : 'IN' ;
INDEX : 'INDEX' ;
INHERIT : 'INHERIT' ;
INNER : 'INNER' ;
INSERT : 'INSERT' ;
INT : 'INT' ;
INTEGER : 'INTEGER' ;
INTERSECTION : 'INTERSECTION' ;
INTO : 'INTO' ;
IS : 'IS' ;
JOIN : 'JOIN' ;
KEY : 'KEY' ;
LIKE : 'LIKE' ;
LIST : 'LIST' ;
LEFT : 'LEFT' ;
METHOD : 'METHOD' ;
MONETARY : 'MONETARY' ;
MULTISET : 'MULTISET' ;
MULTISET_OF : 'MULTISET_OF' ;
NCHAR : 'NCHAR' ;
NO : 'NO' ;
NOT : 'NOT' ;
NULL : 'NULL' ;
NUMERIC : 'NUMERIC' ;
OBJECT : 'OBJECT' ;
OF : 'OF' ;
OFF : 'OFF' ;
ON : 'ON' ;
ONLY : 'ONLY' ;
OPTION : 'OPTION' ;
OR : 'OR' ;
ORDER : 'ORDER' ;
OUTER : 'OUTER' ;
PM : 'PM' ;
PRECISION : 'PRECISION' ;
PRIMARY : 'PRIMARY' ;
QUERY : 'QUERY' ;
REAL : 'REAL' ;
REFERENCES : 'REFERENCES' ;
RENAME : 'RENAME' ;
RESTRICT : 'RESTRICT' ;
RIGHT : 'RIGHT' ;
ROLLBACK : 'ROLLBACK' ;
SELECT : 'SELECT' ;
SEQUENCE : 'SEQUENCE' ;
SEQUENCE_OF : 'SEQUENCE_OF' ;
SET : 'SET' ;
SHARE : 'SHARE' ;
SMALLINT : 'SMALLINT' ;
REVERSE : 'REVERSE' ;
STRING_STR : 'STRING' ;
SUBCLASS : 'SUBCLASS' ;
SUPERCLASS : 'SUPERCLASS' ;
TABLE : 'TABLE' ;
TIME : 'TIME' ;
TIMESTAMP : 'TIMESTAMP' ;
THEN : 'THEN' ;
TRIGGER : 'TRIGGER' ;
TRIGGERS : 'TRIGGERS' ;
TO : 'TO' ;
VALUES : 'VALUES' ;
UNION : 'UNION' ;
UNIQUE : 'UNIQUE' ;
UPDATE : 'UPDATE' ;
USING : 'USING' ;
VARCHAR : 'VARCHAR' ;
VARYING : 'VARYING' ;
VCLASS : 'VCLASS' ;
VIEW : 'VIEW' ;
WHEN : 'WHEN' ;
WHERE : 'WHERE' ;
WITH : 'WITH' ;
WORK : 'WORK' ;
END : ';' ;
COMMA : ',' ;
STAR : '*' ;
STARTBRACE : '{' ;
ENDBRACE : '}' ;
DOT : '.' ;
QUOTA : '\'' ;
DBQUOTA : '"' ;
EQUAL : '=' ;
CONNECT : '||' ;
DOLLAR : '$' ;
Q_MARK : '\u003F' ;
T156 : '+=' ;
T157 : '-=' ;
T158 : '*=' ;
T159 : '/=' ;
T160 : '&=' ;
T161 : '|=' ;
T162 : '^=' ;
T163 : '%=' ;
T164 : '|' ;
T165 : '&' ;
T166 : '+' ;
T167 : '<>' ;
T168 : '<=' ;
T169 : '>=' ;
T170 : '<' ;
T171 : '>' ;
T172 : '-' ;
T173 : '/' ;
T174 : '%' ;

// $ANTLR src "Cubrid.g" 811
DATE_FORMAT:
	('0'..'9')('0'..'2') '/' ('0'..'9')('0'..'9') ('/' ('0'..'9')('0'..'9')('0'..'9')('0'..'9') )? ;

// $ANTLR src "Cubrid.g" 814
TIME_FORMAT:
	('0'..'9')('0'..'2') ':' ('0'..'9')('0'..'9') (':' ('0'..'9')('0'..'9') )?('am' | 'pm')? ;	
	
	
// $ANTLR src "Cubrid.g" 830
LENGTH:
	STARTBRACKET ( '0'..'9' )+ ENDBRACKET
	;
		
// $ANTLR src "Cubrid.g" 1107
DECIMALLITERAL:
	'0'..'9'+ 
	;

// $ANTLR src "Cubrid.g" 1136
STARTBRACKET: '(';

// $ANTLR src "Cubrid.g" 1138
ENDBRACKET: ')';

// $ANTLR src "Cubrid.g" 1150
STRING : '\'' ('a'..'z'|'A'..'Z'|'_'|'0'..'9'| ' ' | '.'|','|'/'|'\\'| ':' | '-' | MARKS| KOREA | CHINESE | JAPAN)* '\'';

// $ANTLR src "Cubrid.g" 1152
MARKS : '(' | ')' | '['| ']'| '{'| '}';

//CHARS: '\u0020'..'\u007D';

// $ANTLR src "Cubrid.g" 1156
KOREA : '\uAC00'..'\uD7AF' | '\u1100'..'\u11FF' | '\u3130'..'\u318F';

// $ANTLR src "Cubrid.g" 1158
CHINESE : '\u4E00'..'\u9FA5';

// $ANTLR src "Cubrid.g" 1160
JAPAN :  '\u3040'..'\u31FF';

// $ANTLR src "Cubrid.g" 1162
COLUMN : '"' ('a'..'z'|'A'..'Z'|'_'|'0'..'9' | ' ' | '.'|',')* '"';

// $ANTLR src "Cubrid.g" 1164
ID: ('A'..'Z'|'a'..'z'|'_'|'0'..'9')+;

// $ANTLR src "Cubrid.g" 1166
PATH : '"' ('a'..'z'|'A'..'Z'|'_'|'0'..'9' | ' ' | '.'|'/'|':'|'\\')* '"';

// $ANTLR src "Cubrid.g" 1168
WS : (' '|'\n'|'\r'|'\t')+{skip();}; 

// $ANTLR src "Cubrid.g" 1170
ML_COMMENT:
	'/*' ( options {greedy=false;} : . )* '*/' {$channel = HIDDEN;} 
    |	'--' ( options {greedy=false;} : . )* '\n'       //{$channel = HIDDEN;} 
    {
    	$channel = HIDDEN;
    	//skip();
    }
    ;
