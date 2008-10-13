/*
 * syn.h
 *
 * This file includes definitions and macros associated with syntax diagrams
 *
 * Terence Parr
 * Purdue University
 * August 1990
 * $Revision: 1.3 $
 */

#ifndef _SYN_H_
#define _SYN_H_

#define NumNodeTypes	4
#define NumJuncTypes	9

/* List the different node types */
#define nJunction		1
#define nRuleRef		2
#define nToken			3
#define nAction			4

/* Different types of junctions */
#define aSubBlk			1
#define aOptBlk			2
#define aLoopBlk		3
#define EndBlk			4
#define RuleBlk			5
#define Generic			6	/* just a junction--no unusual characteristics */
#define EndRule			7
#define aPlusBlk		8
#define aLoopBegin		9

typedef int NodeType;

#define TreeBlockAllocSize		500
#define JunctionBlockAllocSize	200
#define ActionBlockAllocSize	50
#define RRefBlockAllocSize		100
#define TokenBlockAllocSize		100

/* note that 'right' is used by the tree node allocator as a ptr for linked list */
typedef struct _tree {
			struct _tree *down, *right;
			int token;
			union {
				int rk;	/* if token==EpToken, => how many more tokens req'd */
				struct _tree *tref;	/* if token==TREE_REF */
				set sref;			/* if token==SET */
			} v;
		} Tree;

				/* M e s s a g e  P a s s i n g  T o  N o d e s */

/*
 * assumes a 'Junction *r' exists.  This macro calls a function with
 * the pointer to the node to operate on and a pointer to the rule
 * in which it is enclosed.
 */
#define TRANS(p)	{if ( (p)==NULL ) fatal("TRANS: NULL object");		\
					if ( (p)->ntype == nJunction ) (*(fpJTrans[((Junction *)(p))->jtype]))( p );\
					else (*(fpTrans[(p)->ntype]))( p );}

#define PRINT(p)	{if ( (p)==NULL ) fatal("PRINT: NULL object");\
					(*(fpPrint[(p)->ntype]))( p );}

#define REACH(p,k,rk,a){if ( (p)==NULL ) fatal("REACH: NULL object");\
					(a) = (*(fpReach[(p)->ntype]))( p, k, rk );}

#define TRAV(p,k,rk,a){if ( (p)==NULL ) fatal("TRAV: NULL object");\
					(a) = (*(fpTraverse[(p)->ntype]))( p, k, rk );}

/* All syntax diagram nodes derive from Node -- superclass
 */
typedef struct _node {
			NodeType ntype;
		} Node;

typedef struct _anode {
			NodeType ntype;
			Node *next;
			char *action;
			int file;			/* index in FileStr (name of file with action) */
			int line;			/* line number that action occurs on */
		} ActionNode;

typedef struct _toknode {
			NodeType ntype;
			Node *next;
			char *rname;		/* name of rule it's in */
			int file;			/* index in FileStr (name of file with rule) */
			int line;			/* line number that token occurs on */
			int token;
			int label;			/* token label or expression ? */
			int astnode;		/* leaf/root/excluded (used to build AST's) */
		} TokNode;

typedef struct _rrnode {
			NodeType ntype;
			Node *next;
			char *rname;		/* name of rule it's in */
			int file;			/* index in FileStr (name of file with rule)
								   it's in */
			int line;			/* line number that rule ref occurs on */
			char *text;			/* reference to which rule */
			char *parms;		/* point to parameters of rule invocation
								   (if present) */
			char *assign;		/* point to left-hand-side of assignment
								   (if any) */
			int linked;			/* Has a FoLink already been established? */
			int astnode;		/* excluded? (used to build AST's) */
		} RuleRefNode;

typedef struct _junct {
			NodeType ntype;
			int visited;		/* used by recursive routines to avoid
								   infinite recursion */
			char *lock;			/* used by REACH to track infinite recursion */
			int altnum;			/* used in subblocks. altnum==0 means not an
								   alt of subrule */
			int jtype;			/* annotation for code-gen/FIRST/FOLLOW.
								   Junction type */
			struct _junct *end;	/* pointer to node with EndBlk in it
								   if blk == a block type */
			Node *p1, *p2;
			char *rname;		/* name of rule junction is in */
			int file;			/* index in FileStr (name of file with rule)
								   if blk == RuleBlk */
			int line;			/* line number that rule occurs on */
			int halt;			/* never move past a junction with halt==TRUE */
			char *pdecl;		/* point to declaration of parameters on rule
								   (if present) */
			char *parm;			/* point to parameter of block invocation
								   (if present) */
			char *ret;			/* point to return type of rule (if present) */
			char *erraction;	/* point to error action (if present) */
			set *fset;			/* used for code generation */
			Tree *ftree;		/* used for code generation */
} Junction;

typedef struct { Node *left, *right; } Graph;

#endif
