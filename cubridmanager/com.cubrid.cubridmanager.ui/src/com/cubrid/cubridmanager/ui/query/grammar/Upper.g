grammar Upper;

options{
	language = Java;
	output=AST;
}

@header {package com.cubrid.cubridmanager.ui.query.grammar;} 
@lexer::header{package com.cubrid.cubridmanager.ui.query.grammar;}

@members {

	String temp = "";

protected void mismatch(IntStream input, int ttype, BitSet follow) throws RecognitionException {
		throw new RecognitionException();
	}

public void recoverFromMismatchedSet(IntStream input,
                                     RecognitionException e,
                                     BitSet follow)
    throws RecognitionException
{
    throw e;
}

}
// Alter code generation so catch-clauses get replace with
// this action.
divide:
	(
		ID
		|STRING
		|DECIMALLITERAL
		|'<'
		|'>'
		|'+'
		|'|'
		|'-'
		|'='
		| MARKS
		|'*'
		|';'
		|':'
		|ID '.' ID
		|','
		|'\n'
		|'\r'
		|'\t'
		|' '
		|'$'
		|ML_COMMENT
		|'?'
		|'/'
	)*
	;
	
select :
	DECIMALLITERAL
	;	

SELECT : 'select';
FROM : 'from';
	


	
DECIMALLITERAL:
	'0'..'9'+ 
	;
	
MARKS : '(' | ')' | '['| ']'| '{'| '}';

STRING : QUOTA ('a'..'z'|'A'..'Z'|'_'|'0'..'9' | ' ' | '.'|','|'/'|'\\'| '-' | ':'| MARKS| KOREA | CHINESE | JAPAN)* QUOTA;


KOREA : '\uAC00'..'\uD7AF' | '\u1100'..'\u11FF' | '\u3130'..'\u318F';

CHINESE : '\u4E00'..'\u9FA5';

JAPAN :  '\u3040'..'\u31FF';

QUOTA: '\''| '"';

ID: ('a'..'z'|'A'..'Z'|'_'|'0'..'9')+;

ML_COMMENT:
	'/*' ( options {greedy=false;} : . )* '*/' 
    |	'--' ( options {greedy=false;} : . )* '\n' 
    ;

