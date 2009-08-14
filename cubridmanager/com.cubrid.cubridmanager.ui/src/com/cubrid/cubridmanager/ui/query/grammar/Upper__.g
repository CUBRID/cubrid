lexer grammar Upper;
options {
  language=Java;

}
@header {package com.cubrid.cubridmanager.ui.query.grammar;}

T15 : '<' ;
T16 : '>' ;
T17 : '+' ;
T18 : '|' ;
T19 : '-' ;
T20 : '=' ;
T21 : '*' ;
T22 : ';' ;
T23 : ':' ;
T24 : '.' ;
T25 : ',' ;
T26 : '\n' ;
T27 : '\r' ;
T28 : '\t' ;
T29 : ' ' ;
T30 : '$' ;
T31 : '?' ;
T32 : '/' ;

// $ANTLR src "Upper.g" 62
SELECT : 'select';
// $ANTLR src "Upper.g" 63
FROM : 'from';
	


	
// $ANTLR src "Upper.g" 68
DECIMALLITERAL:
	'0'..'9'+ 
	;
	
// $ANTLR src "Upper.g" 72
MARKS : '(' | ')' | '['| ']'| '{'| '}';

// $ANTLR src "Upper.g" 74
STRING : QUOTA ('a'..'z'|'A'..'Z'|'_'|'0'..'9' | ' ' | '.'|','|'/'|'\\'| '-' | ':'| MARKS| KOREA | CHINESE | JAPAN)* QUOTA;


// $ANTLR src "Upper.g" 77
KOREA : '\uAC00'..'\uD7AF' | '\u1100'..'\u11FF' | '\u3130'..'\u318F';

// $ANTLR src "Upper.g" 79
CHINESE : '\u4E00'..'\u9FA5';

// $ANTLR src "Upper.g" 81
JAPAN :  '\u3040'..'\u31FF';

// $ANTLR src "Upper.g" 83
QUOTA: '\''| '"';

// $ANTLR src "Upper.g" 85
ID: ('a'..'z'|'A'..'Z'|'_'|'0'..'9')+;

// $ANTLR src "Upper.g" 87
ML_COMMENT:
	'/*' ( options {greedy=false;} : . )* '*/' 
    |	'--' ( options {greedy=false;} : . )* '\n' 
    ;

