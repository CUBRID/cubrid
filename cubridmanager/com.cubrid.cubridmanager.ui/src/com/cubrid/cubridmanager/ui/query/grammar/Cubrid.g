grammar Cubrid;
options{
	language = Java;
	output=AST;
	rewrite = true;
	//backtrack = true;
}

tokens{
	ACTION = 'ACTION';
	ADD = 'ADD';
	ALL = 'ALL';
	ALTER = 'ALTER';
	AM = 'AM';
	AND = 'AND';
	AS = 'AS';
	ASC = 'ASC';
	ATTRIBUTE = 'ATTRIBUTE';
	AUTOCOMMIT = 'AUTOCOMMIT';
	AUTO_INCREMENT = 'AUTO_INCREMENT';
	BETWEEN = 'BETWEEN';
	BIT = 'BIT';
	BY = 'BY';
	CALL = 'CALL';
	CASE = 'CASE';
	CACHE = 'CACHE';
	CASCADE = 'CASCADE';
	CHANGE = 'CHANGE';
	CHAR = 'CHAR';
	CHARACTER = 'CHARACTER';
	CHECK = 'CHECK';
	CLASS = 'CLASS';
	COMMIT = 'COMMIT';
	CONSTRAINT = 'CONSTRAINT';
	CREATE = 'CREATE';
	DATE = 'DATE';
	DECIMAL = 'DECIMAL';
	DEFERRED = 'DEFERRED';
	DESC = 'DESC';
	DEFAULT = 'DEFAULT';
	DELETE = 'DELETE';
	DIFFERENCE = 'DIFFERENCE';
	DISTINCT = 'DISTINCT';
	DOUBLE = 'DOUBLE';
	DROP = 'DROP';
	ELSE = 'ELSE';
	END_STRING = 'END';
	EXCEPT = 'EXCET';
	EXISTS = 'EXISTS';
	FILE = 'FILE';
	FLOAT = 'FLOAT';
	FOREIGN = 'FOREIGN';
	FROM = 'FROM';
	FUNCTION = 'FUNCTION';
	GROUP = 'GROUP';
	HAVING = 'HAVING';
	IN = 'IN';
	INDEX = 'INDEX';
	INHERIT = 'INHERIT';
	INNER = 'INNER';
	INSERT = 'INSERT';
	INT = 'INT';
	INTEGER = 'INTEGER';
	INTERSECTION = 'INTERSECTION';
	INTO = 'INTO';
	IS = 'IS';
	JOIN = 'JOIN';
	KEY = 'KEY';
	LIKE = 'LIKE';
	LIST = 'LIST';
	LEFT = 'LEFT';
	METHOD = 'METHOD';
	MONETARY = 'MONETARY';
	MULTISET = 'MULTISET';
	MULTISET_OF = 'MULTISET_OF';
	NCHAR = 'NCHAR';
	NO = 'NO';
	NOT = 'NOT';
	NULL = 'NULL';
	NUMERIC = 'NUMERIC';
	OBJECT = 'OBJECT';
	OF = 'OF';
	OFF = 'OFF';
	ON = 'ON';
	ONLY = 'ONLY';
	OPTION = 'OPTION';
	OR = 'OR';
	ORDER = 'ORDER';
	OUTER = 'OUTER';
	PM = 'PM';
	PRECISION = 'PRECISION';
	PRIMARY = 'PRIMARY';
	QUERY = 'QUERY';
	REAL = 'REAL';
	REFERENCES = 'REFERENCES';
	RENAME = 'RENAME';
	RESTRICT = 'RESTRICT';
	RIGHT = 'RIGHT';
	ROLLBACK = 'ROLLBACK';
	SELECT = 'SELECT';
	SEQUENCE = 'SEQUENCE';
	SEQUENCE_OF = 'SEQUENCE_OF';
	SET = 'SET';
	SHARE = 'SHARE';
	SMALLINT = 'SMALLINT';
	REVERSE = 'REVERSE';
	STRING_STR = 'STRING';
	SUBCLASS = 'SUBCLASS';
	SUPERCLASS = 'SUPERCLASS'; 
	TABLE = 'TABLE';
	TIME = 'TIME';
	TIMESTAMP = 'TIMESTAMP';
	THEN = 'THEN';
	TRIGGER = 'TRIGGER';
	TRIGGERS = 'TRIGGERS';
	TO = 'TO';
	VALUES = 'VALUES';
	UNION = 'UNION';
	UNIQUE = 'UNIQUE';
	UPDATE = 'UPDATE';
	USING = 'USING';
	VARCHAR = 'VARCHAR';
	VARYING = 'VARYING';
	VCLASS = 'VCLASS';
	VIEW = 'VIEW';
	WHEN = 'WHEN';
	WHERE = 'WHERE';
	WITH = 'WITH';
	WORK = 'WORK';

	ENTER;
	TAB;
	UNTAB;
	CLEAR;

	END = ';';
	COMMA = ',';
	STAR = '*';
	STARTBRACE = '{';
	ENDBRACE = '}';
	DOT = '.';
	QUOTA = '\'';
	DBQUOTA = '"';
	EQUAL = '=';
	CONNECT = '||';
	DOLLAR = '$';
	Q_MARK = '\u003F';
	
	
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
@rulecatch {
catch (RecognitionException e) {
    throw e;
}

}

execute : 
	((PATH |select_statement | insert |  update | delete | create | create_virtual_class | alter | drop | call | autocommit |rollback | commit) end)* 
	;

autocommit : 
	AUTOCOMMIT (OFF | ON)
	;
rollback:
	ROLLBACK 
	;
	
commit:
	COMMIT WORK
	;	

select_statement:
	query_specification (UNION ALL? query_specification)*
	(ORDER BY sort_specification_comma_list)?
	;
	
query_specification:
	select (qualifier?) select_expressions	
	( (TO | INTO) variable_comma_list)?
	from table_specification_comma_list
	where_clause 
	(USING INDEX index_comma_list)?
	(group_by path_expression_comma_list)?
	(HAVING search_condition)?
	;
	
qualifier:
	ALL | DISTINCT | UNIQUE
	;
	
select_expressions:
	expression_comma_list
	//|STAR
	;	

expression_comma_list:
	expression_co (COMMA expression_co)* -> expression_co (COMMA ENTER expression_co)* ENTER 
	;

expression_co :
	expression (correlation?)
	;
	

	
attribute_name:	
	ID (DOT  (ID | COLUMN))?
	| COLUMN
	;
		
attribute_comma_list:
	attribute_name	(COMMA attribute_name)*	-> attribute_name (COMMA ENTER attribute_name)*  
	;
	
attribute_comma_list_part:
	STARTBRACKET attribute_comma_list ENDBRACKET
	-> ENTER STARTBRACKET ENTER TAB attribute_comma_list ENTER UNTAB ENDBRACKET
	;	
	
left_expression:
	(ID (DOT ID)?)
	;
	
right_expression:
	expression
	;
	
set:
	STARTBRACKET value_comma_list ENDBRACKET;	
	
variable_comma_list:
	variable (COMMA variable)*
	;
	
variable:
	ID
	|value
	;
	
class_name:
	ID
	|COLUMN;	
	
table_specification_comma_list:
	table_specification (comma_join_spec)* -> table_specification (comma_join_spec)* ENTER
	
	// table_specification (COMMA table_specification )* -> table_specification (COMMA ENTER table_specification)* ENTER
	// | table_specification qualified_join_specification* -> table_specification (ENTER qualified_join_specification)* ENTER
	;
	
comma_join_spec:	
	COMMA table_specification ->  COMMA ENTER table_specification
	| qualified_join_specification 
	;

qualified_join_specification:
	(LEFT | RIGHT) OUTER? JOIN table_specification join_condition;	
	
join_condition:
	ON search_condition;	


table_specification:
		class_specification (correlation?)
		| metaclass_specification (correlation?)
		| subquery (correlation?) 
		| TABLE expression (correlation?)
	;

correlation: 
	AS? ID (STARTBRACKET id_comma_list ENDBRACKET)?
	;
	
id_comma_list:
	ID (COMMA ID)*
	;	
		
class_specification:
	class_hierarchy
	| STARTBRACKET	class_hierarchy_comma_list ENDBRACKET
	;
	
class_hierarchy:
	ONLY? class_name
	|ALL class_name (EXCEPT class_specification)?
	;	
	
class_hierarchy_comma_list:
	class_hierarchy (COMMA class_hierarchy)*	
	;

metaclass_specification:
	CLASS class_name
	;

query_statement:
	query_expression
	(ORDER BY sort_specification_comma_list)?
	;	
	
sort_specification_comma_list:
	sort_specification (COMMA sort_specification)*
	;
	
sort_specification:
	(path_expression	(ASC | DESC)?)
	| (unsigned_integer_literal  (ASC | DESC)?)
	;

path_expression:
	attribute_name 
	;
	
query_expression:
	query_term	(table_connect query_term)*
	-> query_term 	(table_connect ENTER query_term)* ENTER
	;	
table_connect:
	table_operator (qualifier?)
	;	
query_term:
	query_specification
	;
	
subquery:
	STARTBRACKET query_statement ENDBRACKET
	-> STARTBRACKET ENTER TAB query_statement ENTER UNTAB ENDBRACKET ENTER
	;

path_expression_comma_list:
	path_expression (COMMA path_expression)* -> path_expression (COMMA ENTER path_expression)* ENTER
	;

table_operator:
	UNION | DIFFERENCE | INTERSECTION
	;	
	
unsigned_integer_literal:
	DECIMALLITERAL
	;	

search_condition:
	condition -> condition ENTER UNTAB
	;


	
condition:
	expression
	;

parExpression:
	STARTBRACKET expression ENDBRACKET
	;

expression:   
	conditionalOrExpression (assignmentOperator expression)? 
	;
	
assignmentOperator:   
   '+='
    |   '-='
    |   '*='
    |   '/='
    |   '&='
    |   '|='
    |   '^='
    |   '%='
    ;	

conditionalOrExpression
    :   conditionalAndExpression ( OR conditionalAndExpression )*
    ->  conditionalAndExpression ( ENTER OR conditionalAndExpression )*
    ;

conditionalAndExpression
    :   inclusiveOrExpression ( AND inclusiveOrExpression )*
    ->   inclusiveOrExpression ( ENTER AND inclusiveOrExpression )*
    ;
    

inclusiveOrExpression
    :   connectExpression ( '|' connectExpression )*
    ;

connectExpression
    :   andExpression ( CONNECT andExpression )*
    ;

andExpression
    :   equalityExpression ( '&' equalityExpression )*
    ;

equalityExpression
    :   additiveExpression (relationalOp additiveExpression )*
    ;    

outer_join:
	STARTBRACE '+' ENDBRACKET
	;

relationalOp:
	'='
	| IS
	| LIKE
	| (NOT? IN)
	| '<>'
	| ('<=')
    |   ('>=')
    |   '<' 
    |   '>' 
	;


additiveExpression
    :   multiplicativeExpression ( ('+' | '-') multiplicativeExpression )*
    ;

multiplicativeExpression
    :   between_expression ( ( '*' | '/' | '%' ) between_expression )*
    ;
    
between_expression:
	unaryExpression ( (NOT?) BETWEEN unaryExpression AND unaryExpression)* 
	;    

unaryExpression:
	NOT? EXISTS? primary
	;

primary:
	parExpression
	|attribute_name (outer_join?)
	| NULL
	| value
	| STAR
	| set
	| function
	| subquery -> ENTER subquery
	//| STARTBRACKET CASE WHEN expression THEN expression (ELSE expression)? END_STRING ENDBRACKET
	| CASE (when_expression+) (else_expression?) END_STRING ->
	  CASE ENTER TAB when_expression+ else_expression? UNTAB END_STRING	
	;
	    		
when_expression:	
	WHEN expression THEN expression ->
	WHEN expression THEN expression ENTER
	;
	
else_expression:
	ELSE expression -> ELSE expression ENTER
	;	
	
index_comma_list:
	index (COMMA index)*
	;
	
index:
	ID (DOT ID)?
	;	

/* ----------------------------INSERT--------------------------------- */	

insert:
	INSERT INTO class_name insert_spec -> INSERT INTO class_name ENTER insert_spec;
	
insert_spec:
	(attributes)? value_clause -> (attributes)?  value_clause 
	|(attributes)? DEFAULT VALUES ->(attributes)? DEFAULT VALUES
	;	

attributes:
	STARTBRACKET attribute (COMMA attribute)* ENDBRACKET 
	-> STARTBRACKET ENTER TAB attribute (COMMA ENTER attribute)* ENTER UNTAB ENDBRACKET ENTER
	;

attribute:
	ID
	;
	
value_clause:
	VALUES
	STARTBRACKET insert_item_comma_list ENDBRACKET
	(TO variable)?
	->
	VALUES ENTER
	STARTBRACKET ENTER TAB insert_item_comma_list UNTAB ENDBRACKET 
	(TO variable)?
	;	
	
insert_item_comma_list:
	insert_item (COMMA insert_item)* -> insert_item (COMMA ENTER insert_item)* ENTER
	;	
	
insert_item:
	expression -> expression
	;
	
/* ----------------------------UPDATE--------------------------------- */
update :
	UPDATE 
	class_all_spec
	SET assignment_comma_list
	where_clause
	->
	UPDATE 
	class_all_spec ENTER 
	SET ENTER TAB assignment_comma_list UNTAB
	where_clause
	;
	
class_all_spec:	
	class_specification 
	| metaclass_specification
	;
	
assignment_comma_list:
	assignment (COMMA assignment)*	
	-> assignment (COMMA ENTER assignment)* ENTER
	;
	
assignment:
	attribute_name EQUAL expression
	;	

/* ----------------------------DELETE-------------------------- */

delete:
	DELETE FROM class_specification
	where_clause
	->
	DELETE FROM ENTER TAB class_specification ENTER UNTAB
	where_clause
	;

/* ----------------------------CREATE-------------------------- */

create:
	CREATE class_or_table class_name
	subclass_definition?
	class_element_definition_part?
	(CLASS ATTRIBUTE  attribute_definition_comma_list)?
	(METHOD method_definition_comma_list)?
	(FILE method_file_comma_list)?
	(INHERIT resolution_comma_list)?
	;
	
create_virtual_class:
	CREATE vclass_or_view class_name	
	subclass_definition?
	view_attribute_definition_part?
	(CLASS ATTRIBUTE  attribute_definition_comma_list)?
	(METHOD method_definition_comma_list)?
	(FILE method_file_comma_list)?
	(INHERIT resolution_comma_list)?
	(AS query_statement)?
	(WITH CHECK OPTION)?
	;

class_or_table:
	CLASS 
	| TABLE
	;

vclass_or_view:
	VCLASS
	| VIEW
	;
	
subclass_definition:
	AS SUBCLASS OF class_name_comma_list
	;

class_name_comma_list:
	class_name (COMMA class_name)*
	->ENTER TAB class_name (COMMA ENTER class_name)* ENTER UNTAB
	;
	
class_element_definition_part:
	STARTBRACKET 
	class_element_comma_list	
	ENDBRACKET
	->
	STARTBRACKET 
	class_element_comma_list	
	ENDBRACKET
	ENTER
	;

class_element_comma_list:
	class_element (COMMA class_element)*
	->
	ENTER TAB class_element (COMMA ENTER class_element)* ENTER UNTAB
	;
	
class_element:
	attribute_definition 
	| class_constraint
	;
	
class_constraint:
	( CONSTRAINT constraint_name)?
	UNIQUE attribute_comma_list_part
	|(PRIMARY KEY attribute_comma_list_part)?
	| referential_constraint 
	;	

constraint_name:
	ID
	;

referential_constraint:
	FOREIGN KEY
	constraint_name?
	attribute_comma_list_part
	REFERENCES
	referenced_table_name?
	attribute_comma_list_part
	referential_triggered_action?
	;

referenced_table_name:
	ID
	;
	
referential_triggered_action:
	update_rule
	(delete_rule cache_object_rule?)?
	;	

update_rule:
	ON UPDATE referential_action
	;

delete_rule:
	ON DELETE referential_action
	;
	
referential_action:
	CASCADE 
	| RESTRICT 
	| NO ACTION
	;

cache_object_rule:
	ON CACHE OBJECT cache_object_column_name
	;	

cache_object_column_name:
	attribute_name
	;
		
view_attribute_definition_part:
	STARTBRACKET 
	view_attribute_def_comma_list	
	ENDBRACKET
	;
	
view_attribute_def_comma_list:
	view_attribute_definition (COMMA view_attribute_definition)*
	->
	ENTER TAB view_attribute_definition (COMMA ENTER view_attribute_definition)* ENTER UNTAB
	;	

view_attribute_definition:
	attribute_definition
	| attribute_name
	;

attribute_definition:
	(general_attribute_name attribute_type
	default_or_shared?
	auto_increment?
	attribute_constraint_list?)
	| function
	;

auto_increment:
	AUTO_INCREMENT (STARTBRACKET DECIMALLITERAL COMMA DECIMALLITERAL ENDBRACKET)?
	;

general_attribute_name:
	CLASS? attribute_name
	;
	
attribute_type:
	domain
	;	
	
domain:
	privative_type
	| collections;
	
domain_comma_list:
	domain (COMMA domain)*
	;	

collections:
	SET domain
	| SET STARTBRACKET domain_comma_list ENDBRACKET
	| LIST OR SEQUENCE domain
	| LIST OR SEQUENCE STARTBRACKET domain_comma_list ENDBRACKET
	| MULTISET domain
	| MULTISET STARTBRACKET domain_comma_list ENDBRACKET
	;

privative_type:
	char_
	| varchar
	| nchar
	| ncharvarying
	| bit
	| bitvarying
	| numeric 
	| integer_
	| smallint
	| monetary
	| float_
	| doubleprecision
	| date_
	| time_
	| timestamp
	| string
	| class_name
	| OBJECT
	;

string:
	STRING_STR
	;
		
char_:
	CHAR LENGTH?
	;	

nchar:
	NCHAR LENGTH?
	;
	
varchar:
	VARCHAR LENGTH
	;
		
ncharvarying:
	NCHAR VARYING LENGTH
	;	
	
bit:
	BIT LENGTH
	;
	
bitvarying:
	BIT VARYING LENGTH
	;
	
numeric:
 	(NUMERIC | DECIMAL) 
 	( STARTBRACKET DECIMALLITERAL (COMMA DECIMALLITERAL )? ENDBRACKET)?
 	;
	 	
integer_:
	INTEGER | INT
	;
	
smallint:
	SMALLINT
	;
	
monetary:
	MONETARY
	;
	
float_:
	(FLOAT | REAL)
	(STARTBRACKET DECIMALLITERAL ENDBRACKET)?
	;
	
doubleprecision:
	DOUBLE PRECISION
	;
	
date_:
	DATE 
	(QUOTA DATE_FORMAT QUOTA)?
	;
	
DATE_FORMAT:
	('0'..'9')('0'..'2') '/' ('0'..'9')('0'..'9') ('/' ('0'..'9')('0'..'9')('0'..'9')('0'..'9') )? ;

TIME_FORMAT:
	('0'..'9')('0'..'2') ':' ('0'..'9')('0'..'9') (':' ('0'..'9')('0'..'9') )?('am' | 'pm')? ;	
	
	
time_:
	TIME 
	(QUOTA TIME_FORMAT QUOTA)?
	;
	
timestamp:
	TIMESTAMP
	(QUOTA
	 ( (DATE_FORMAT TIME_FORMAT) | (TIME_FORMAT DATE_FORMAT) )
	QUOTA)?
	;

LENGTH:
	STARTBRACKET ( '0'..'9' )+ ENDBRACKET
	;
		
default_or_shared:
	SHARE value_specification?
	|DEFAULT value_specification
	;
	
attribute_constraint_list:
	attribute_constraint+
	;
	
attribute_constraint:
	NOT NULL
	| UNIQUE
	| PRIMARY KEY 
	;
	
value_specification:
	value
	;	

attribute_definition_comma_list:
	attribute_definition (COMMA attribute_definition)*
	->
	ENTER TAB attribute_definition (COMMA ENTER attribute_definition)* ENTER UNTAB
	;	
	
method_definition_comma_list:
	method_definition (COMMA method_definition)*
	->
	ENTER TAB method_definition (COMMA ENTER method_definition)* ENTER UNTAB
	;	
	
method_definition:
	general_method_name
	argument_type_part?
	result_type?
	(FUNCTION function_name)?
	;	

general_method_name:
	ID
	;
	
argument_type_part:
	STARTBRACKET 
	argument_type_comma_list?	
	ENDBRACKET
	;	
argument_type_comma_list:
	argument_type (COMMA argument_type)*
	->
	ENTER TAB argument_type (COMMA ENTER argument_type)* ENTER UNTAB
	;	

argument_type:
	domain
	;

result_type:
	domain
	;
	
function_name:
	ID
	;	
	
method_file_comma_list:
	PATH (COMMA PATH)*
	->
	ENTER TAB PATH (COMMA ENTER PATH)* ENTER UNTAB
	;	

resolution_comma_list:
	resolution (COMMA resolution)*
	->
	ENTER TAB resolution (COMMA ENTER resolution)* ENTER UNTAB
	;
	
resolution:
	general_attribute_name OF class_name
	(AS attribute_name)?
	;

/*----------------------------DROP----------------------------------------*/

drop:
	drop_class
	| drop_index
	| drop_trigger
	| drop_deferred
	;

drop_class:
	DROP class_type? class_specification_comma_list
	;

class_type:
	CLASS
	| TABLE
	| VCLASS
	| VIEW
	;
	
class_specification_comma_list:
	class_specification (COMMA class_specification)*	
	;	
	
drop_index:
	DROP REVERSE? UNIQUE? INDEX index_name? ON class_name attribute_comma_list_part?
	-> DROP REVERSE? UNIQUE? INDEX index_name? ON class_name attribute_comma_list_part?
	;
	
index_name:
	ID
	;	
	
drop_trigger:
	DROP TRIGGER trigger_name_comma_list
	;
	
trigger_name_comma_list:
	trigger_name (COMMA trigger_name)*
	;
	
trigger_name:
	ID
	;	
		
drop_deferred:
	DROP DEFERRED TRIGGER trigger_spec 
	;
	
trigger_spec:
	trigger_name_comma_list
	| ALL TRIGGERS
	;	
	
/*------------------------------ALTER------------------------------------------*/

alter:
	ALTER class_type? class_name alter_clause
	-> ALTER class_type? class_name ENTER alter_clause
	;
	
alter_clause:
	ADD alter_add (INHERIT resolution_comma_list)?
	| DROP alter_drop (INHERIT resolution_comma_list)?
	| RENAME alter_rename (INHERIT resolution_comma_list)?
	| CHANGE alter_change
	| INHERIT resolution_comma_list
	;
	
alter_add:
	( ATTRIBUTE | COLUMN )? class_element_comma_list
	| CLASS ATTRIBUTE class_element_comma_list
	| FILE file_name_comma_list
	| METHOD method_definition_comma_list
	| QUERY select_statement
	| SUPERCLASS class_name_comma_list
	;

file_name_comma_list:
	file_path_name (COMMA file_path_name)*
	;
		
alter_drop:
	(ATTRIBUTE | COLUMN | METHOD )? general_attribute_name_comma_list
	| FILE file_name_comma_list
	| QUERY unsigned_integer_literal? 
	| SUPERCLASS class_name_comma_list
	| CONSTRAINT constraint_name
	;	

general_attribute_name_comma_list:
	general_attribute_name (COMMA general_attribute_name)*
	;
	
alter_change:
	FILE file_path_name AS file_path_name 
	| METHOD method_definition_comma_list
	| QUERY unsigned_integer_literal? select_statement
	| general_attribute_name DEFAULT value_specifiation
	;
	
alter_rename:
	(ATTRIBUTE | COLUMN | METHOD)?
	general_attribute_name AS attribute_name
	| FUNCTION OF general_attribute_name AS function_name
	FILE file_path_name AS file_path_name 
	;
		
value_specifiation:
	value
	;	

file_path_name:
	PATH
	;

/*--------------------------CALL---------------------------------------*/

call:
	CALL method_name STARTBRACKET 
	argument_comma_list?
	ENDBRACKET
	ON call_target (to_variable)?
	;

method_name:
	ID
	;
	
call_target:
	variable_name | metaclass_specification
	;
	
variable_name:
	ID
	;
	
to_variable:
	TO variable;		
		
/*------------------------------------------------------------------------*/

where_clause:
	(WHERE search_condition)?
	->
	(UNTAB WHERE ENTER TAB search_condition)? ENTER
	;
	
function: 
	function_name 
	STARTBRACKET 
	argument_comma_list?
	ENDBRACKET;
	
argument_comma_list:
	argument (COMMA argument)*
	;
	
argument:
	expression (AS privative_type)?
/*
	(attribute_name
	|STARTBRACKET query_specification ENDBRACKET
	| STAR
	| value
	| function
	)
	(express argument)?
*/	
	;	
		


operation:	
	AND | OR 
	;

value:
	STRING
	|(QUOTA number QUOTA )	
	| number
	| currency
	| Q_MARK
	;

value_comma_list:
	value (COMMA value)*
	;	


	
DECIMALLITERAL:
	'0'..'9'+ 
	;

currency:
	DOLLAR number
	;
	
express: 
	'=' 
	| '<' 
	| '>' 
	| '<=' 
	| '>=' 
	| '<>'
	| '+'
	| '-'
	| STAR
	| '/'
	| (NOT?) EXISTS
	| (NOT?) IN
	| CONNECT
	| LIKE;	
	
number:  
	 ('-')?DECIMALLITERAL ( DOT DECIMALLITERAL) ?
	;	
	
 
STARTBRACKET: '(';

ENDBRACKET: ')';

end : END -> END CLEAR ENTER;

select : SELECT -> SELECT ENTER TAB;

from : FROM -> UNTAB FROM ENTER TAB;

where : WHERE -> UNTAB WHERE ENTER TAB;

group_by : GROUP BY -> ENTER UNTAB GROUP BY ENTER TAB ;

STRING : '\'' ('a'..'z'|'A'..'Z'|'_'|'0'..'9'| ' ' | '.'|','|'/'|'\\'| ':' | '-' | MARKS| KOREA | CHINESE | JAPAN)* '\'';

MARKS : '(' | ')' | '['| ']'| '{'| '}';

//CHARS: '\u0020'..'\u007D';

KOREA : '\uAC00'..'\uD7AF' | '\u1100'..'\u11FF' | '\u3130'..'\u318F';

CHINESE : '\u4E00'..'\u9FA5';

JAPAN :  '\u3040'..'\u31FF';

COLUMN : '"' ('a'..'z'|'A'..'Z'|'_'|'0'..'9' | ' ' | '.'|',')* '"';

ID: ('A'..'Z'|'a'..'z'|'_'|'0'..'9')+;

PATH : '"' ('a'..'z'|'A'..'Z'|'_'|'0'..'9' | ' ' | '.'|'/'|':'|'\\')* '"';

WS : (' '|'\n'|'\r'|'\t')+{skip();}; 

ML_COMMENT:
	'/*' ( options {greedy=false;} : . )* '*/' {$channel = HIDDEN;} 
    |	'--' ( options {greedy=false;} : . )* '\n'       //{$channel = HIDDEN;} 
    {
    	$channel = HIDDEN;
    	//skip();
    }
    ;
