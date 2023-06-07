/* 
 * Date last modified: Jan 2001
 * Modifications by Dan Libby (dan@libby.com), including:
 *  - various fixes, null checks, etc
 *  - addition of Q_Iter funcs, macros
 */

/*
 *  File : q.h
 *
 *  Peter Yard  02 Jan 1993.
 *
 *  Disclaimer: This code is released to the public domain.
 */

#ifndef Q__H
#define Q__H

#ifndef False_
   #define False_ 0
#endif

#ifndef True_
   #define True_ 1
#endif

typedef struct nodeptr datanode;

typedef struct nodeptr {
   void        *data ;
   datanode    *prev, *next ;
} node ;

/* For external use with Q_Iter* funcs */
typedef struct nodeptr* q_iter;

typedef struct {
   node        *head, *tail, *cursor;
   int         size, sorted, item_deleted;
} xmlrpc_queue;

typedef  struct {
   void        *dataptr;
   node        *loc ;
} index_elt ;


int    Q_Init(xmlrpc_queue  *q);
void   Q_Destroy(xmlrpc_queue *q);
int    Q_IsEmpty(xmlrpc_queue *q);
int    Q_Size(xmlrpc_queue *q);
int    Q_AtHead(xmlrpc_queue *q);
int    Q_AtTail(xmlrpc_queue *q);
int    Q_PushHead(xmlrpc_queue *q, void *d);
int    Q_PushTail(xmlrpc_queue *q, void *d);
void  *Q_Head(xmlrpc_queue *q);
void  *Q_Tail(xmlrpc_queue *q);
void  *Q_PopHead(xmlrpc_queue *q);
void  *Q_PopTail(xmlrpc_queue *q);
void  *Q_Next(xmlrpc_queue *q);
void  *Q_Previous(xmlrpc_queue *q);
void  *Q_DelCur(xmlrpc_queue *q);
void  *Q_Get(xmlrpc_queue *q);
int    Q_Put(xmlrpc_queue *q, void *data);
int    Q_Sort(xmlrpc_queue *q, int (*Comp)(const void *, const void *));
int    Q_Find(xmlrpc_queue *q, void *data,
              int (*Comp)(const void *, const void *));
void  *Q_Seek(xmlrpc_queue *q, void *data,
              int (*Comp)(const void *, const void *));
int    Q_Insert(xmlrpc_queue *q, void *data,
                int (*Comp)(const void *, const void *));

/* read only funcs for iterating through xmlrpc_queue. above funcs modify xmlrpc_queue */
q_iter Q_Iter_Head(xmlrpc_queue *q);
q_iter Q_Iter_Tail(xmlrpc_queue *q);
q_iter Q_Iter_Next(q_iter qi);
q_iter Q_Iter_Prev(q_iter qi);
void*  Q_Iter_Get(q_iter qi);
int    Q_Iter_Put(q_iter qi, void* data); // not read only! here for completeness.
void*  Q_Iter_Del(xmlrpc_queue *q, q_iter iter); // not read only! here for completeness.

/* Fast (macro'd) versions of above */
#define Q_Iter_Head_F(q) (q ? (q_iter)((xmlrpc_queue*)q)->head : NULL)
#define Q_Iter_Tail_F(q) (q ? (q_iter)((xmlrpc_queue*)q)->tail : NULL)
#define Q_Iter_Next_F(qi) (qi ? (q_iter)((node*)qi)->next : NULL)
#define Q_Iter_Prev_F(qi) (qi ? (q_iter)((node*)qi)->prev : NULL)
#define Q_Iter_Get_F(qi) (qi ? ((node*)qi)->data : NULL)

#endif /* Q__H */
