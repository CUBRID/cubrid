/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * popolate.c
 */


#ifndef POPULATE_HEADER_
#define POPULATE_HEADER_

/******************      SYSTEM INCLUDE FILES      ***************************/


/******************     PRIVATE INCLUDE FILES      ***************************/


/******************     IMPORTED DECLARATIONS      ***************************/

extern char *get_next_stmt (FILE * fp);
extern void end_stmts (void);

/****************** PUBLIC (EXPORTED) DECLARATIONS ***************************/


/******************        PRIVATE DEFINES         ***************************/

/* These defines aren't available unless __STDC__ is defined */
#if defined(SOLARIS)
#define MAXDOUBLE       1.79769313486231570e+308
#define MAXFLOAT        ((float)3.40282346638528860e+38)
#define MINDOUBLE       4.94065645841246544e-324
#define MINFLOAT        ((float)1.40129846432481707e-45)
#endif

#define MAX_CLASS_NAME 50

#define CO_0 0
#define CO_11 1
#define CO_12 2
#define CO_21 3
#define CO_22 4
#define CO_23 5
#define CO_31 6
#define CO_32 7
#define CO_33 8
#define CO_34 9
#define CO_41 10
#define CO_42 11
#define CO_43 12
#define CO_44 13
#define CO_45 14
#define CO_46 15
#define CO_47 16
#define CO_48 17
#define CO_51 18
#define CO_52 19

#define COMPCLASSES 20

#define DESC_CO_12 51
#define DESC_CO_44 35
#define DESC_CO_48 41
#define DESC_CO_52 32

/******************        PRIVATE TYPEDEFS        ***************************/

typedef enum
{
  UNIFORM, INC_EXP, DEC_EXP
} DISTR;

typedef struct
{
  const char *name;
  DISTR distribution;
  int num_instances;
  int range_index;
  int set_cardinality;
} CLASS_DESC;

typedef struct comph_struct
{
  const char *name;
  DB_OBJECT *class;
} comph_t;

/******************      PRIVATE DECLARATIONS      ***************************/

void qa_create_composition_hierarchy ();
void do_co0 ();
void do_co12 ();
void do_co22 ();
void do_31 ();
void do_co32 ();
void do_co43 ();
void do_co44 ();
void do_co46 ();
void do_co47 ();
void do_co48 ();
void do_co52 ();
void do_co_classes ();


#endif
