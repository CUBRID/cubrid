/* Abstract syntax tree
 *
 * Manipulation functions
 * ANTLR Version 1.00
 *	Revision History: $Log: ast.c,v $
 *	Revision History: Revision 1.3  2005/10/27 05:39:24  beatrice
 *	Revision History: bug fix. gcc version.
 *	Revision History:
 *	Revision 1.3  1994/07/12  22:25:00  xsql
 *	unsigned to unsigned long
 *
 *	Revision 1.2  1992/07/09  17:33:31  treyes
 *	This is the original pccts version 1.0 of this file.
 *
 */
#ifdef __STDC__
#include <stdarg.h>
#else
#include <varargs.h>
#endif

/* ensure that tree manipulation variables are current after a rule
 * reference
 */
void
zzlink (_root, _sibling, _tail)
     AST **_root, **_sibling, **_tail;
{
  if (*_sibling == NULL)
    return;
  if (*_root == NULL)
    *_root = *_sibling;
  else if (*_root != *_sibling)
    (*_root)->down = *_sibling;
  if (*_tail == NULL)
    *_tail = *_sibling;
  while ((*_tail)->right != NULL)
    *_tail = (*_tail)->right;
}

AST *
zzastnew ()
{
#ifdef __STDC__
  extern char *calloc (unsigned long, unsigned long);
#else
  extern char *calloc ();
#endif

  AST *p = (AST *) calloc (1, sizeof (AST));
  if (p == NULL)
    fprintf (stderr, "%s(%d): cannot allocate AST node\n", __FILE__,
	     __LINE__);
  return p;
}

/* add a child node to the current sibling list */
void
zzsubchild (_root, _sibling, _tail)
     AST **_root, **_sibling, **_tail;
{
  AST *n = zzastnew ();
  zzcr_ast (n, &(zzaCur), LA (1), LATEXT (1));
  zzastPush (n);
  if (*_tail != NULL)
    (*_tail)->right = n;
  else
    {
      *_sibling = n;
      if (*_root != NULL)
	(*_root)->down = *_sibling;
    }
  *_tail = n;
  if (*_root == NULL)
    *_root = *_sibling;
}

/* make a new AST node.  Make the newly-created
 * node the root for the current sibling list.  If a root node already
 * exists, make the newly-created node the root of the current root.
 */
void
zzsubroot (_root, _sibling, _tail)
     AST **_root, **_sibling, **_tail;
{
  AST *n = zzastnew ();
  zzcr_ast (n, &(zzaCur), LA (1), LATEXT (1));
  zzastPush (n);
  if (*_root != NULL)
    if ((*_root)->down == *_sibling)
      *_sibling = *_tail = *_root;
  *_root = n;
  (*_root)->down = *_sibling;
}

/* Apply function to root then each sibling
 * example: print tree in child-sibling LISP-format (AST has token field)
 *
 *	void show(tree)
 *	AST *tree;
 *	{
 *		if ( tree == NULL ) return;
 *		printf(" %s", zztokens[tree->token]);
 *	}
 *
 *	void before() { printf(" ("); }
 *	void after()  { printf(" )"); }
 *
 *	LISPdump() { zzpre_ast(tree, show, before, after); }
 *
 */
void
zzpre_ast (tree, func, before, after)
     AST *tree;
     void (*func) (),		/* apply this to each tree node */
  (*before) (),			/* apply this to root of subtree before preordering it */
  (*after) ();			/* apply this to root of subtree after preordering it */
{
  while (tree != NULL)
    {
      if (tree->down != NULL)
	(*before) (tree);
      (*func) (tree);
      zzpre_ast (tree->down, func, before, after);
      if (tree->down != NULL)
	(*after) (tree);
      tree = tree->right;
    }
}

/* free all AST nodes in tree; apply func to each before freeing */
void
zzfree_ast (tree)
     AST *tree;
{
  if (tree == NULL)
    return;
  zzfree_ast (tree->down);
  zzfree_ast (tree->right);
  zztfree (tree);
}

/* build a tree (root child1 child2 ... NULL)
 * If root is NULL, simply make the children siblings and return ptr
 * to 1st sibling (child1).  If root is not single node, return NULL.
 *
 * Siblings that are actually siblins lists themselves are handled
 * correctly.  For example #( NULL, #( NULL, A, B, C), D) results
 * in the tree ( NULL A B C D ).
 *
 * Requires at least two parameters with the last one being NULL.  If
 * both are NULL, return NULL.
 */
#ifdef __STDC__
AST *
zztmake (AST * rt, ...)
#else
AST *
zztmake (va_alist)
     va_dcl
#endif
{
  va_list ap;
  register AST *child, *sibling = NULL, *tail, *w;
  AST *root;

#ifdef __STDC__
  va_start (ap, rt);
  root = rt;
#else
  va_start (ap);
  root = va_arg (ap, AST *);
#endif

  if (root != NULL)
    if (root->down != NULL)
      return NULL;
  child = va_arg (ap, AST *);
  while (child != NULL)
    {
      for (w = child; w->right != NULL; w = w->right)
	{;
	}			/* find end of child */
      if (sibling == NULL)
	{
	  sibling = child;
	  tail = w;
	}
      else
	{
	  tail->right = child;
	  tail = w;
	}
      child = va_arg (ap, AST *);
    }
  if (root == NULL)
    root = sibling;
  else
    root->down = sibling;
  va_end (ap);
  return root;
}

/* tree duplicate */
AST *
zzdup_ast (t)
     AST *t;
{
  AST *u;

  if (t == NULL)
    return NULL;
  u = zzastnew ();
  *u = *t;
  u->right = zzdup_ast (t->right);
  u->down = zzdup_ast (t->down);
  return u;
}

void
zztfree (t)
     AST *t;
{
#ifdef zzd_ast
  zzd_ast (t);
#endif
  free (t);
}
