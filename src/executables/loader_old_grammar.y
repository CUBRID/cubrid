/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/*
 * loader_grammar.y - loader grammar file
 */

%{
#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utility.h"
#include "dbtype.h"
#include "language_support.h"
#include "message_catalog.h"
#include "memory_alloc.h"
#include "loader.h"
#include "loader_action.h"

/*#define PARSER_DEBUG*/
#ifdef PARSER_DEBUG
#define DBG_PRINT(s) printf("rule: %s\n", (s));
#else
#define DBG_PRINT(s)
#endif

extern int in_instance_line;
extern FILE *loader_yyin;
extern int loader_yylex(void);
extern void loader_yyerror(char* s);

extern void do_loader_parse(FILE *fp);

static STRING_LIST *append_string_list(STRING_LIST *list, char *str);
static CLASS_COMMAND_SPEC *make_class_command_spec(int qualifier, STRING_LIST *attr_list, CONSTRUCTOR_SPEC *ctor_spec);
static CONSTANT* make_constant(int type, void *val);
static CONSTANT_LIST *append_constant_list(CONSTANT_LIST *list, CONSTANT *cons);
static void process_constant(CONSTANT *c);
static void process_constant_list(CONSTANT_LIST *cons);
%}

%error_verbose

%union {
	int 	intval;
	char	*cptr;
	STRING_LIST	*strlist;
	CLASS_COMMAND_SPEC *cmd_spec;
	CONSTRUCTOR_SPEC *ctor_spec;
	CONSTANT *constant;
	CONSTANT_LIST *const_list;
	OBJECT_REF *obj_ref;	
}

%token NL
%token NULL_
%token CLASS
%token SHARED
%token DEFAULT
%token DATE
%token TIME
%token UTIME
%token TIMESTAMP
%token DATETIME
%token CMD_ID
%token CMD_CLASS
%token CMD_CONSTRUCTOR
%token REF_ELO_INT
%token REF_ELO_EXT
%token REF_USER
%token REF_CLASS
%token OBJECT_REFERENCE
%token OID_DELIMETER
%token SET_START_BRACE
%token SET_END_BRACE
%token START_PAREN
%token END_PAREN
%token <cptr> REAL_LIT
%token <cptr> INT_LIT
%token <intval> OID_
%token <cptr> TIME_LIT4
%token <cptr> TIME_LIT42
%token <cptr> TIME_LIT3
%token <cptr> TIME_LIT31
%token <cptr> TIME_LIT2
%token <cptr> TIME_LIT1
%token <cptr> DATE_LIT2
%token YEN_SYMBOL
%token WON_SYMBOL
%token BACKSLASH
%token DOLLAR_SYMBOL
%token <cptr> IDENTIFIER
%token Quote
%token DQuote
%token NQuote
%token BQuote
%token XQuote
%token <cptr> SQS_String_Body
%token <cptr> DQS_String_Body
%token COMMA

%type <intval> attribute_list_qualifier
%type <cmd_spec> class_commamd_spec
%type <ctor_spec> constructor_spec
%type <cptr> attribute_name
%type <cptr> argument_name
%type <strlist> attribute_names
%type <strlist> attribute_list
%type <strlist> argument_names
%type <strlist> constructor_argument_list
%type <constant> constant
%type <const_list> constant_list

%type <constant> ansi_string
%type <constant> dq_string
%type <constant> nchar_string
%type <constant> bit_string
%type <constant> sql2_date
%type <constant> sql2_time
%type <constant> sql2_timestamp
%type <constant> utime
%type <constant> sql2_datetime
%type <constant> monetary
%type <constant> object_reference
%type <constant> set_constant
%type <constant> system_object_reference

%type <obj_ref> class_identifier
%type <cptr> instance_number
%type <intval> ref_type
%type <intval> object_id

%type <const_list> set_elements

%start loader_start
%%

loader_start :
	{ act_init(); } 
	loader_lines
	{ act_finish(0); act_finish(1); }
	;
	
loader_lines :
	line { DBG_PRINT("line"); }
	|
	loader_lines line { DBG_PRINT("line_list line"); }
	;
line : 
	one_line { DBG_PRINT("one_line"); } NL { in_instance_line = 1; }  
	| 
	NL { in_instance_line = 1; } 
	;

one_line :
	command_line { DBG_PRINT("command_line"); } 
	| 
	instance_line { DBG_PRINT("instance_line"); }
	;

command_line :
	class_command { DBG_PRINT("class_command"); }
	| 
	id_command { DBG_PRINT("id_command"); }
	;

id_command : 
	CMD_ID IDENTIFIER INT_LIT 
	{
		act_start_id($2);
		act_set_id(atoi($3));
		
		free($2);
		free($3);
	}
        ;

class_command : 
	CMD_CLASS IDENTIFIER class_commamd_spec  
	{ 
		CLASS_COMMAND_SPEC *cmd_spec;
		char *class_name;
		STRING_LIST *attr, *save, *args;
		
		DBG_PRINT("class_commamd_spec");
		
		class_name = $2;
		cmd_spec = $3;
		
		act_set_class (class_name);
		
		if (cmd_spec->qualifier == LDR_ATTRIBUTE_CLASS)
		{
			act_class_attributes();
		}
		else if (cmd_spec->qualifier == LDR_ATTRIBUTE_SHARED)
		{
			act_shared_attributes();
		}
		else if (cmd_spec->qualifier == LDR_ATTRIBUTE_DEFAULT)
		{
			act_default_attributes();
		}
		
		for(attr = cmd_spec->attr_list; attr; attr = attr->next)
		{
			act_add_attribute(attr->val);
		}
		
		if (cmd_spec->ctor_spec)
		{
			act_set_constructor(cmd_spec->ctor_spec->idname);
			
			for(args = cmd_spec->ctor_spec->arg_list; args; args = args->next)
			{
				act_add_argument(args->val);
			}
			
			for(args = cmd_spec->ctor_spec->arg_list; args; args = save)
			{
				save = args->next;
				free(args->val);
				free(args);
			}

			free(cmd_spec->ctor_spec->idname);
			free(cmd_spec->ctor_spec);
		}

		for(attr = cmd_spec->attr_list; attr; attr = save)
		{
			save = attr->next;
			free(attr->val);
			free(attr);
		}
		
		free(class_name);
		free(cmd_spec);
	}
        ;
        
class_commamd_spec :
        attribute_list 
        {
        	DBG_PRINT("attribute_list");
        	$$ = make_class_command_spec(LDR_ATTRIBUTE_ANY, $1, NULL);
        }
        |
        attribute_list constructor_spec
        {
        	DBG_PRINT("attribute_list constructor_spec");
        	$$ = make_class_command_spec(LDR_ATTRIBUTE_ANY, $1, $2);
        }
        |
        attribute_list_qualifier attribute_list
        {
        	DBG_PRINT("attribute_list_qualifier attribute_list");
        	$$ = make_class_command_spec($1, $2, NULL);
        }
        |
        attribute_list_qualifier attribute_list constructor_spec
        {
        	DBG_PRINT("attribute_list_qualifier attribute_list constructor_spec");
        	$$ = make_class_command_spec($1, $2, $3);
        }
        ;

attribute_list_qualifier : 
	CLASS { DBG_PRINT("CLASS"); $$ = LDR_ATTRIBUTE_CLASS; }
	|
	SHARED { DBG_PRINT("SHARED"); $$ = LDR_ATTRIBUTE_SHARED; }
	|
	DEFAULT { DBG_PRINT("DEFAULT"); $$ = LDR_ATTRIBUTE_DEFAULT; }
	;

attribute_list :
	START_PAREN END_PAREN 
	{
		$$ = NULL;
	}
	|
	START_PAREN attribute_names END_PAREN 
	{
		$$ = $2; 
	}
	;

attribute_names :
	attribute_name 
	{
		DBG_PRINT("attribute_name");
		$$ = append_string_list(NULL, $1);
	}
	|
	attribute_names attribute_name
	{
		DBG_PRINT("attribute_names attribute_name");
		$$ = append_string_list($1, $2);
	}
	|
	attribute_names COMMA attribute_name
	{
		DBG_PRINT("attribute_names COMMA attribute_name");
		$$ = append_string_list($1, $3);
	}
	;

attribute_name : 
	IDENTIFIER { $$ = $1; };

constructor_spec :
	CMD_CONSTRUCTOR IDENTIFIER constructor_argument_list
	{
		CONSTRUCTOR_SPEC *spec;
		
		spec = (CONSTRUCTOR_SPEC *) malloc(sizeof(CONSTRUCTOR_SPEC));
		spec->idname = $2;
		spec->arg_list =  $3;
		
		$$ = spec;
	} 
	;

constructor_argument_list :
	START_PAREN END_PAREN
	{
		$$ = NULL;
	}
	|
	START_PAREN argument_names END_PAREN
	{
		$$ = $2;
	}
	;

argument_names :
	argument_name
	{
		DBG_PRINT("argument_name");
		$$ = append_string_list(NULL, $1);
	}
	|
	argument_names argument_name
	{
		DBG_PRINT("argument_names argument_name");
		$$ = append_string_list($1, $2);
	}
	|
	argument_names COMMA argument_name
	{
		DBG_PRINT("argument_names COMMA argument_name");
		$$ = append_string_list($1, $3);
	}
	;

argument_name : 
	IDENTIFIER { $$ = $1; };
	;

instance_line :
	object_id
	{
		act_add_instance($1);
	}
	| 
	object_id constant_list 
	{
		act_add_instance($1);
		process_constant_list($2);
	}
	|
	constant_list
	{
		act_add_instance(-1);
		process_constant_list($1);
	}
	;

object_id : 
	OID_ { $$ =  $1; }
	;

constant_list :
	constant
	{
		DBG_PRINT("constant");
		$$ = append_constant_list(NULL, $1);
	}
	|
	constant_list constant 
	{
		DBG_PRINT("constant_list constant");
		$$ = append_constant_list($1, $2);
	}
	;

constant :
	ansi_string 		{ $$ = $1; }
        | dq_string		{ $$ = $1; }
        | nchar_string 		{ $$ = $1; }
        | bit_string 		{ $$ = $1; }
        | sql2_date 		{ $$ = $1; }
        | sql2_time 		{ $$ = $1; }
        | sql2_timestamp 	{ $$ = $1; }
        | utime 		{ $$ = $1; }
        | sql2_datetime		{ $$ = $1; }
        | NULL_         	{ $$ = make_constant(LDR_OLD_NULL, NULL); }
        | TIME_LIT4     	{ $$ = make_constant(LDR_OLD_TIME4, $1); }
        | TIME_LIT42    	{ $$ = make_constant(LDR_OLD_TIME42, $1); }
        | TIME_LIT3     	{ $$ = make_constant(LDR_OLD_TIME3, $1); }
        | TIME_LIT31    	{ $$ = make_constant(LDR_OLD_TIME31, $1); }
        | TIME_LIT2     	{ $$ = make_constant(LDR_OLD_TIME2, $1); }
        | TIME_LIT1     	{ $$ = make_constant(LDR_OLD_TIME1, $1); }
        | INT_LIT       	{ $$ = make_constant(LDR_OLD_INT, $1); }
        | REAL_LIT 		{ $$ = make_constant(LDR_OLD_REAL, $1); }
        | DATE_LIT2     	{ $$ = make_constant(LDR_OLD_DATE2, $1); }
        | monetary		{ $$ = $1; }
        | object_reference	{ $$ = $1; }
        | set_constant		{ $$ = $1; }
        | system_object_reference	{ $$ = $1; }
        ;

ansi_string :
	Quote SQS_String_Body
	{
		$$ = make_constant(LDR_OLD_STR, $2);
	}
	;
	
nchar_string :
	NQuote SQS_String_Body
	{
		$$ = make_constant(LDR_OLD_NSTR, $2);
	}
	;

dq_string 
	: DQuote DQS_String_Body
	{
		$$ = make_constant(LDR_OLD_STR, $2);
	}
	;

sql2_date :
	DATE Quote SQS_String_Body
	{
		$$ = make_constant(LDR_OLD_DATE, $3);
	}
	;
	
sql2_time : 
	TIME Quote SQS_String_Body
	{
		$$ = make_constant(LDR_OLD_TIME, $3);
	}
	;
	
sql2_timestamp : 
	TIMESTAMP Quote SQS_String_Body
	{
		$$ = make_constant(LDR_OLD_TIMESTAMP, $3);
	}
	;
	
utime : 
	UTIME Quote SQS_String_Body
	{
		$$ = make_constant(LDR_OLD_TIMESTAMP, $3);
	}
	;

sql2_datetime : 
	DATETIME Quote SQS_String_Body
	{
		$$ = make_constant(LDR_OLD_DATETIME, $3);
	}
	;

bit_string : 
	BQuote SQS_String_Body
	{
		$$ = make_constant(LDR_OLD_BSTR, $2);
	}
	| 
	XQuote SQS_String_Body
	{
		$$ = make_constant(LDR_OLD_XSTR, $2);
	}
	;

object_reference : 
	OBJECT_REFERENCE class_identifier
	{
		$$ = make_constant(LDR_OLD_CLASS_OID, $2);
	}
	|
	OBJECT_REFERENCE class_identifier instance_number
	{
		$2->instance_number = $3;
		$$ = make_constant(LDR_OLD_OID, $2);
	}
	;
	
class_identifier: 
	INT_LIT 
	{
		OBJECT_REF *ref;
		
		ref = (OBJECT_REF *) malloc(sizeof(OBJECT_REF));
		ref->class_id = $1;
		ref->class_name = NULL;
		ref->instance_number = NULL;
		
		$$ = ref;
	}
	|
	IDENTIFIER
	{
		OBJECT_REF *ref;
		
		ref = (OBJECT_REF *) malloc(sizeof(OBJECT_REF));
		ref->class_id = NULL;
		ref->class_name = $1;
		ref->instance_number = NULL;
		
		$$ = ref;
	}
	;

instance_number : 
	OID_DELIMETER INT_LIT { $$ = $2; }
	;

set_constant : 
	SET_START_BRACE	SET_END_BRACE
	{
		$$ = make_constant(LDR_OLD_COLLECTION, NULL);
	}
	|
	SET_START_BRACE	set_elements SET_END_BRACE
	{
		$$ = make_constant(LDR_OLD_COLLECTION, $2);
	}
	;

set_elements: 
	constant 
	{
		DBG_PRINT("constant");
		$$ = append_constant_list(NULL, $1);
	}
	|
	set_elements constant 
	{
		DBG_PRINT("set_elements constant");
		$$ = append_constant_list($1, $2);
	}
	|
	set_elements COMMA constant
	{
		DBG_PRINT("set_elements COMMA constant");
		$$ = append_constant_list($1, $3);
	}
	|
	set_elements NL constant
	{
		DBG_PRINT("set_elements NL constant");
		$$ = append_constant_list($1, $3);
	}
	|
	set_elements COMMA NL constant
	{
		DBG_PRINT("set_elements COMMA NL constant");
		$$ = append_constant_list($1, $4);
	}
	;

system_object_reference : 
	ref_type Quote SQS_String_Body 
	{
		 $$ = make_constant($1, $3);
	}

ref_type :	
	REF_ELO_INT { $$ = LDR_OLD_SYS_ELO_INTERNAL; } 
	| 
	REF_ELO_EXT { $$ = LDR_OLD_SYS_ELO_EXTERNAL; }
	| 
	REF_USER { $$ = LDR_OLD_SYS_USER; }
	| 
	REF_CLASS { $$ = LDR_OLD_SYS_CLASS; }
	;

currency : 
	DOLLAR_SYMBOL | YEN_SYMBOL | WON_SYMBOL | BACKSLASH;
	
monetary : 
	currency REAL_LIT 
	{
		$$ = make_constant(LDR_OLD_MONETARY, $2);
	}
	;
%%

static STRING_LIST *append_string_list(STRING_LIST *list, char *str)
{
	STRING_LIST *item, *last, *tmp;
	 
	item = (STRING_LIST *) malloc(sizeof(STRING_LIST));
	item->val = str;
	item->next = NULL;

	if (list)
	{	
		for (tmp = list; tmp; tmp = tmp->next)
		{
		 	last = tmp;
		}
		
		last->next = item;
	}
	else
	{
		list = item;
	}
	
	return list;
}

static CLASS_COMMAND_SPEC *make_class_command_spec(int qualifier, STRING_LIST *attr_list, CONSTRUCTOR_SPEC *ctor_spec)
{
	CLASS_COMMAND_SPEC *spec;
	 
	spec = (CLASS_COMMAND_SPEC *) malloc(sizeof(CLASS_COMMAND_SPEC));
	spec->qualifier = qualifier;
	spec->attr_list = attr_list;
	spec->ctor_spec = ctor_spec; 
	
	return spec;
}

static CONSTANT *make_constant(int type, void *val)
{
	CONSTANT *con;
		
	con = (CONSTANT *) malloc(sizeof(CONSTANT));
	con->type = type;
	con->val = val;
		
	return con;
}

static CONSTANT_LIST *append_constant_list(CONSTANT_LIST *list, CONSTANT *cons)
{
	CONSTANT_LIST *item, *last, *tmp;
	 
	item = (CONSTANT_LIST *) malloc(sizeof(CONSTANT_LIST));
	item->val = cons;
	item->next = NULL;
	
	if (list)
	{	
		for (tmp = list; tmp; tmp = tmp->next)
		{
		 	last = tmp;
		}
		
		last->next = item;
	}
	else
	{
		list = item;
	}
	
	return list;
}

static void process_constant(CONSTANT *c)
{
	switch (c->type)
	{
	case LDR_OLD_NULL:
		act_null();
		break;
		
	case LDR_OLD_INT:
		act_int((char *) c->val);
		free(c->val);
		break;
		
	case LDR_OLD_REAL:
		act_real((char *) c->val);
		free(c->val);
		break;
		
	case LDR_OLD_MONETARY:
		act_monetary((char *) c->val);
		free(c->val);
		break;
		
	case LDR_OLD_DATE2:
		act_date((char *) c->val);
		free(c->val);
		break;
		
	case LDR_OLD_TIME4:
		act_time((char *) c->val, 4);
		free(c->val);
		break;
		
	case LDR_OLD_TIME42:
		act_time((char *) c->val, 2);
		free(c->val);
		break;
		
	case LDR_OLD_TIME3:
		act_time((char *) c->val, 3);
		free(c->val);
		break;
		
	case LDR_OLD_TIME31:
		act_time((char *) c->val, 1);
		free(c->val);
		break;
		
	case LDR_OLD_TIME2:
		act_time((char *) c->val, 2);
		free(c->val);
		break;
		
	case LDR_OLD_TIME1:
		act_time((char *) c->val, 1);
		free(c->val);
		break;
		
	case LDR_OLD_DATE:
	case LDR_OLD_TIME:
	case LDR_OLD_TIMESTAMP:
	case LDR_OLD_DATETIME:
	case LDR_OLD_STR:
		act_string((char *) c->val, strlen((char *) c->val), DB_TYPE_CHAR);
		free(c->val);
		break;
		
	case LDR_OLD_NSTR:
		act_string((char *) c->val, strlen((char *) c->val), DB_TYPE_NCHAR);
		free(c->val);
		break;
		
	case LDR_OLD_BSTR:
		act_bstring((char *) c->val, 1);
		free(c->val);
		break;
		
	case LDR_OLD_XSTR:
		act_bstring((char *) c->val, 2);
		free(c->val);
		break;
		
	case LDR_OLD_SYS_ELO_INTERNAL:
	case LDR_OLD_SYS_ELO_EXTERNAL:
	case LDR_OLD_SYS_USER:
	case LDR_OLD_SYS_CLASS:
		act_system((char *) c->val, c->type);
		free(c->val);
		break;
		
	case LDR_OLD_OID:
	case LDR_OLD_CLASS_OID:
		{
			OBJECT_REF *ref;
			
			ref = (OBJECT_REF *) c->val;
			
			if (ref->class_id)
			{
				act_set_ref_class_id(atoi(ref->class_id));
			}
			else
			{
				act_set_ref_class(ref->class_name);
			}
			
			if (ref->instance_number)
			{
				act_reference(atoi(ref->instance_number));
			}
			else
			{
				act_reference_class();
			}
			
			if (ref->class_id)
			{
				free(ref->class_id);
			}
			
			if (ref->class_name)
			{
				free(ref->class_name);
			}
			
			if (ref->instance_number)
			{
				free(ref->instance_number);
			}
			    
			free(ref);
		}
		break;
	
	case LDR_OLD_COLLECTION:
		act_start_set();
		process_constant_list((CONSTANT_LIST *) c->val);
		act_end_set();
		break;
	
	default:
		break;	
	}
	
	free(c);
}

static void process_constant_list(CONSTANT_LIST *cons)
{
	CONSTANT_LIST *c, *save;
	
	for(c = cons; c; c = save)
	{
		save = c->next;
		process_constant(c->val);
		free(c);
	}
}

void do_loader_parse(FILE *fp)
{
  in_instance_line = 1;

  loader_yyin = fp;
  loader_yyparse();
}

#ifdef PARSER_DEBUG
/*int main(int argc, char *argv[])
{
	loader_yyparse();
	return 0;
}
*/
#endif        
