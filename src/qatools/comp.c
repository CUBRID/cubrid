/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * comp.c
 */


#include "config.h"

/******************      SYSTEM INCLUDE FILES      ***************************/

#ifndef PC
#include <sys/time.h>
#include <values.h>
#endif

#include <stdlib.h>
#include <string.h>
/******************     PRIVATE INCLUDE FILES      ***************************/

#include "dbi.h"
#include "db.h"
#include "error_manager.h"
#include "memory_manager_2.h"
#include "populate.h"

/******************     IMPORTED DECLARATIONS      ***************************/


/****************** PUBLIC (EXPORTED) DECLARATIONS ***************************/


/******************        PRIVATE DEFINES         ***************************/

								     /*#define KLUDGE *//* work around gc bugs */

comph_t Comp_items[21] = {
  {"co_0", NULL}
  ,
  {"co_1_1", NULL}
  ,
  {"co_1_2", NULL}
  ,
  {"co_2_1", NULL}
  ,
  {"co_2_2", NULL}
  ,
  {"co_2_3", NULL}
  ,
  {"co_3_1", NULL}
  ,
  {"co_3_2", NULL}
  ,
  {"co_3_3", NULL}
  ,
  {"co_3_4", NULL}
  ,
  {"co_4_1", NULL}
  ,
  {"co_4_2", NULL}
  ,
  {"co_4_3", NULL}
  ,
  {"co_4_4", NULL}
  ,
  {"co_4_5", NULL}
  ,
  {"co_4_6", NULL}
  ,
  {"co_4_7", NULL}
  ,
  {"co_4_8", NULL}
  ,
  {"co_5_1", NULL}
  ,
  {"co_5_2", NULL}
};

void
build_class_tables ()
{
  int index;
  DB_OBJECT *class;
  char *nm_ptr;

  printf ("Building class object tables.");
  for (index = 0; index < COMPCLASSES; index++)
    {
      nm_ptr = (char *) Comp_items[index].name;
      if (!(class = (db_find_class (nm_ptr))))
	{
	  fprintf (stderr, "Can't find class %s\n", nm_ptr);
	  exit (1);
	}
      Comp_items[index].class = class;
      printf (".");
    }
  printf ("\n");
}

void
qa_create_composition_hierarchy ()
{
  build_class_tables ();
  do_co_classes ();
  printf ("\n");
}

void
do_co0 ()
{
  DB_OBJLIST *optr1, *optr2;
  DB_OBJLIST *head1, *head2;
  DB_VALUE obval;
  void tickle_db ();


  head1 = optr1 = db_get_all_objects (Comp_items[CO_0].class);
  head2 = optr2 = db_get_all_objects (Comp_items[CO_11].class);

  if (head1 == NULL || head2 == NULL)
    {
      fprintf (stderr, "get all objects returns 0 for co_0 or co_1_1\n");
      exit (1);
    }
  printf ("Linking co_0");
  while ((optr2 != NULL) && (optr1 != NULL))
    {
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_0_ref1", &obval))
	{
	  fprintf (stderr, "Can't put object in c0_0_ref1\n");
	  return;
	}

      optr2 = optr2->next;
      optr1 = optr1->next;
    }

  db_objlist_free (head2);
  optr1 = head1;
  head2 = db_get_all_objects (Comp_items[CO_12].class);
  while (optr1)
    {
      optr2 = head2;
      while ((optr1 != NULL) && (optr2 != NULL))
	{
	  DB_MAKE_OBJECT (&obval, optr2->op);
	  if (db_put_internal (optr1->op, "co_0_ref2", &obval))
	    {
	      fprintf (stderr, "Can't put object in %s \n", "co_0_ref2");
	      return;
	    }
	  optr2 = optr2->next;
	  optr1 = optr1->next;
	}
    }
  db_objlist_free (head1);
  db_objlist_free (head2);
  tickle_db ();
}

void
do_co12 ()
{

  DB_OBJLIST *head1, *head2;
  DB_OBJLIST *optr1, *optr2;
  DB_VALUE obval;
  DB_VALUE setval;
  DB_OBJLIST *setptr;
  DB_SET *objset;
  void tickle_db ();
  int i;
  extern CLASS_DESC class_desc[];

  printf (" co_1_2");
  head1 = optr1 = db_get_all_objects (Comp_items[CO_12].class);
  head2 = db_get_all_objects (Comp_items[CO_21].class);

  if (head2 == NULL || head1 == NULL)
    {
      fprintf (stderr, "0 for CO_1_2 or CO_2_1\n");
      exit (1);
    }
  optr2 = NULL;
  while (optr1)
    {
      if ((optr2 == NULL) || (optr2->next == NULL))
	optr2 = head2;
      else
	optr2 = optr2->next;
      setptr = optr2;
      if (!(objset = (db_set_create (optr1->op, "co_1_2_ref1"))))
	{
	  fprintf (stderr, "Can't make set for co-1_2_ref1\n");
	  return;
	}
      DB_MAKE_SET (&setval, objset);
      for (i = 0; i < class_desc[DESC_CO_12].set_cardinality; i++)
	{
	  DB_MAKE_OBJECT (&obval, setptr->op);
	  if (db_set_add (objset, &obval))
	    {
	      fprintf (stderr, "Can't put set in co_1_2_ref1\n");
	      return;
	    }
	  if (setptr->next)
	    setptr = setptr->next;
	  else
	    setptr = head2;
	}
      if (db_put_internal (optr1->op, "co_1_2_ref1", &setval))
	{
	  fprintf (stderr, "Can't create set for co_1_2\n");
	  return;
	}
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_1_2_ref2", &obval))
	{
	  fprintf (stderr, "Can't put object in co_1_2_ref2\n");
	  return;
	}
      optr1 = optr1->next;
    }

  db_objlist_free (head2);

  optr1 = head1;
  head2 = db_get_all_objects (Comp_items[CO_34].class);

  if (head2 == NULL || head1 == NULL)
    {
      fprintf (stderr, "0 for CO_1_2 or CO_3_4\n");
      exit (1);
    }
  optr2 = NULL;
  while (optr1)
    {
      if ((optr2 == NULL) || (optr2->next == NULL))
	optr2 = head2;
      else
	optr2 = optr2->next;
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_1_2_ref4", &obval))
	{
	  fprintf (stderr, "Can't put object in co_1_2_ref4\n");
	  return;
	}
      optr1 = optr1->next;
    }
  db_objlist_free (head2);

  optr1 = head1;
  head2 = optr2 = db_get_all_objects (Comp_items[CO_22].class);
  while ((optr1 != NULL) && (optr2 != NULL))
    {
      setptr = optr2;
      if (!(objset = (db_set_create (optr1->op, "co_1_2_ref3"))))
	{
	  fprintf (stderr, "Can't make set for co-1_2_ref3\n");
	  return;
	}
      DB_MAKE_SET (&setval, objset);
      for (i = 0; i < class_desc[DESC_CO_12].set_cardinality; i++)
	{
	  DB_MAKE_OBJECT (&obval, setptr->op);
	  if (db_set_add (objset, &obval))
	    {
	      fprintf (stderr, "Can't put object in %x \n",
		       (unsigned long) optr1->op);
	      return;
	    }
	  if (setptr->next)
	    setptr = setptr->next;
	  else
	    setptr = head2;
	}
      if (db_put_internal (optr1->op, "co_1_2_ref3", &setval))
	{
	  fprintf (stderr, "Can't create set for co_1_2\n");
	  return;
	}
/*    DB_MAKE_OBJECT(&obval,optr2->op);
    if (db_put_internal(optr1->op,"co_1_2_ref3",&obval)){
      fprintf(stderr,"Can't put object in %x \n",optr1->op);
      return;

  }*/
      optr1 = optr1->next;
      optr2 = optr2->next;
    }

  db_objlist_free (head2);
  optr1 = head1;
  head2 = db_get_all_objects (Comp_items[CO_23].class);
  optr2 = NULL;
  while (optr1)
    {
      if ((optr2 == NULL) || (optr2->next == NULL))
	optr2 = head2;
      else
	optr2 = optr2->next;
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_1_2_ref5", &obval))
	{
	  fprintf (stderr, "Can't put object in co_1_2_ref5\n");
	  return;
	}
      if (db_put_internal (optr1->op, "co_1_2_ref6", &obval))
	{
	  fprintf (stderr, "Can't put object in co_1_2_ref6\n");
	  return;
	}
      if (db_put_internal (optr1->op, "co_1_2_ref7", &obval))
	{
	  fprintf (stderr, "Can't put object in co_1_2_ref7\n");
	  return;
	}
      optr1 = optr1->next;
    }
/*  db_objlist_free(head2);
  optr1=head1;
  head2=optr2=db_get_all_objects(Comp_items[CO_34].class);
  while ((optr1!=NULL) && (optr2!=NULL)){
    setptr=optr2;
    if (!(objset=(db_set_create(optr1->op,"co_1_2_ref8")))){
      fprintf(stderr,"Can't make set for co-1_2_ref8\n");
      return;
    }*/
  db_objlist_free (head1);
  db_objlist_free (head2);
  tickle_db ();
}

void
do_co22 ()
{

  DB_VALUE obval;
  DB_OBJLIST *head1, *head2;
  DB_OBJLIST *optr1, *optr2;
  int err;
  void tickle_db ();


  printf (" co_2_2");
  head1 = optr1 = db_get_all_objects (Comp_items[CO_22].class);
  head2 = db_get_all_objects (Comp_items[CO_31].class);
  optr2 = NULL;
  while (optr1)
    {
      if ((optr2 == NULL) || (optr2->next == NULL))
	optr2 = head2;
      else
	optr2 = optr2->next;
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_2_2_ref1", &obval))
	{
	  fprintf (stderr, "Can't put object in co_2_2_ref1\n");
	  return;
	}
      optr1 = optr1->next;
    }

  db_objlist_free (head2);
  optr1 = head1;
  head2 = optr2 = db_get_all_objects (Comp_items[CO_32].class);
  while ((optr1 != NULL) && (optr2 != NULL))
    {
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_2_2_ref2", &obval))
	{
	  fprintf (stderr, "Can't put object in co_2_2_ref2\n");
	  return;
	}
      optr2 = optr2->next;
      optr1 = optr1->next;
    }

  db_objlist_free (head2);
  optr1 = head1;
  head2 = optr2 = db_get_all_objects (Comp_items[CO_33].class);
  while ((optr1 != NULL) && (optr2 != NULL))
    {
      DB_MAKE_OBJECT (&obval, optr2->op);
      if ((err = db_put_internal (optr1->op, "co_2_2_ref3", &obval)))
	{
	  fprintf (stderr, "Can't put object in co_2_2_ref3 err=%d\n", err);
	  fflush (stderr);
	  continue;
	}
      optr1 = optr1->next;
      /*for(i=0;i<10;i++) if (optr2->next) optr2=optr2->next;else optr2=head2; */
      optr2 = optr2->next;
    }

  db_objlist_free (head2);
  optr1 = head1;
  head2 = db_get_all_objects (Comp_items[CO_34].class);
  optr2 = NULL;
  while (optr1)
    {
      if ((optr2 == NULL) || (optr2->next == NULL))
	optr2 = head2;
      else
	optr2 = optr2->next;
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_2_2_ref4", &obval))
	{
	  fprintf (stderr, "Can't put object in co_2_2_ref4\n");
	  return;
	}
      optr1 = optr1->next;
    }
  db_objlist_free (head1);
  db_objlist_free (head2);
  tickle_db ();
}


void
do_co31 ()
{

  DB_VALUE obval;
  DB_OBJLIST *head1;
  DB_OBJLIST *optr1;
  void tickle_db ();


  printf (" co_3_1");
  head1 = optr1 = db_get_all_objects (Comp_items[CO_31].class);
  while (optr1)
    {
      if (optr1->next)
	DB_MAKE_OBJECT (&obval, optr1->next->op);
      else
	DB_MAKE_OBJECT (&obval, head1->op);
      if (db_put_internal (optr1->op, "co_3_1_ref1", &obval))
	{
	  fprintf (stderr, "Can't put object in co_3_1_ref1\n");
	  return;
	}
      optr1 = optr1->next;
    }
  db_objlist_free (head1);
  tickle_db ();
}

void
do_co32 ()
{
  DB_OBJLIST *head1, *head2;
  DB_OBJLIST *optr1, *optr2;
  DB_VALUE obval;
  void tickle_db ();
  int i;

  printf (" co_3-2");
  head1 = optr1 = db_get_all_objects (Comp_items[CO_32].class);
  head2 = optr2 = db_get_all_objects (Comp_items[CO_42].class);
  while ((optr2 != NULL) && (optr1 != NULL))
    {
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_3_2_ref1", &obval))
	{
	  fprintf (stderr, "Can't put object in co_3_2_ref1\n");
	  return;
	}
      optr1 = optr1->next;
      optr2 = optr2->next;
    }

  db_objlist_free (head2);
  optr1 = head1;
  head2 = db_get_all_objects (Comp_items[CO_43].class);
  optr2 = NULL;
  while (optr1)
    {
      if ((optr2 == NULL) || (optr2->next == NULL))
	optr2 = head2;
      else
	optr2 = optr2->next;
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_3_2_ref2", &obval))
	{
	  fprintf (stderr, "Can't put object in co_3_2_ref2\n");
	  return;
	}
      optr1 = optr1->next;
    }

  db_objlist_free (head2);
  optr1 = head1;
  head2 = optr2 = db_get_all_objects (Comp_items[CO_44].class);
  while ((optr2 != NULL) && (optr1 != NULL))
    {
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_3_2_ref3", &obval))
	{
	  fprintf (stderr, "Can't put object in co_3_2_ref3\n");
	  return;
	}
      optr1 = optr1->next;
      optr2 = optr2->next;
    }

  db_objlist_free (head2);
  optr1 = head1;
  head2 = db_get_all_objects (Comp_items[CO_45].class);
  optr2 = NULL;
  while (optr1)
    {
      if ((optr2 == NULL) || (optr2->next == NULL))
	optr2 = head2;
      else
	optr2 = optr2->next;
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_3_2_ref4", &obval))
	{
	  fprintf (stderr, "Can't put object in co_3_2_ref4\n");
	  return;
	}
      optr1 = optr1->next;
    }

  db_objlist_free (head2);
  optr1 = head1;
  head2 = optr2 = db_get_all_objects (Comp_items[CO_46].class);
  while (optr1)
    {
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_3_2_ref5", &obval))
	{
	  fprintf (stderr, "Can't put object in co_3_2_ref5\n");
	  return;
	}
      for (i = 0; i < 10; i++)
	{
	  if ((optr2 == NULL) || (optr2->next == NULL))
	    optr2 = head2;
	  else
	    optr2 = optr2->next;
	}
      optr1 = optr1->next;
    }
  db_objlist_free (head2);
  db_objlist_free (head1);
  tickle_db ();
}

void
do_co41 ()
{

  DB_VALUE obval;
  DB_OBJLIST *head1;
  DB_OBJLIST *optr1;
  void tickle_db ();


  printf (" co_4_1");
  head1 = optr1 = db_get_all_objects (Comp_items[CO_41].class);
  while (optr1)
    {
      if (optr1->next)
	DB_MAKE_OBJECT (&obval, optr1->next->op);
      else
	DB_MAKE_OBJECT (&obval, head1->op);
      if (db_put_internal (optr1->op, "co_3_1_ref1", &obval))
	{
	  fprintf (stderr, "Can't put object in co_3_1_ref1\n");
	  return;
	}
      optr1 = optr1->next;
    }
  db_objlist_free (head1);
  tickle_db ();
}


void
do_co42 ()
{

  DB_VALUE obval;
  DB_OBJLIST *head1, *head2;
  DB_OBJLIST *optr1, *optr2;
  void tickle_db ();


  printf (" co_2_2");
  head1 = optr1 = db_get_all_objects (Comp_items[CO_42].class);
  head2 = db_get_all_objects (Comp_items[CO_22].class);
  optr2 = NULL;
  while (optr1)
    {
      if ((optr2 == NULL) || (optr2->next == NULL))
	optr2 = head2;
      else
	optr2 = optr2->next;
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_4_2_ref1", &obval))
	{
	  fprintf (stderr, "Can't put object in %x \n",
		   (unsigned long) optr1->op);
	  return;
	}
      optr1 = optr1->next;
    }
  db_objlist_free (head1);
  db_objlist_free (head2);
  tickle_db ();
}


void
do_co43 ()
{
  DB_OBJLIST *head1, *head2;
  DB_OBJLIST *optr1, *optr2;
  DB_VALUE obval;
  void tickle_db ();
  int i;

  printf (" co_4_3");
  head1 = optr1 = db_get_all_objects (Comp_items[CO_43].class);
  head2 = optr2 = db_get_all_objects (Comp_items[CO_51].class);
  while (optr1)
    {
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_4_3_ref1", &obval))
	{
	  fprintf (stderr, "Can't put object in %x \n",
		   (unsigned long) optr1->op);
	  return;
	}
      for (i = 0; i < 1000; i++)
	{
	  if ((optr2 != NULL) && (optr2->next != NULL))
	    optr2 = optr2->next;
	  else
	    optr2 = head2;
	}
      optr1 = optr1->next;
    }
  db_objlist_free (head1);
  db_objlist_free (head2);
}

void
do_co44 ()
{
  DB_OBJLIST *head1, *head2;
  DB_OBJLIST *optr1, *optr2;
  DB_VALUE obval;
  void tickle_db ();
  DB_VALUE setval;
  DB_OBJLIST *setptr;
  DB_SET *objset;
  int i;
  extern CLASS_DESC class_desc[];

  printf (" co_4_4");
  head1 = optr1 = db_get_all_objects (Comp_items[CO_44].class);
  head2 = optr2 = db_get_all_objects (Comp_items[CO_51].class);
  while (optr1)
    {
      if (!(objset = (db_set_create (optr1->op, "co_4_4_ref1"))))
	{
	  fprintf (stderr, "Can't make set for co_4_4_ref1\n");
	  return;
	}
      setptr = optr2;
      DB_MAKE_SET (&setval, objset);
      for (i = 0; i < class_desc[DESC_CO_44].set_cardinality; i++)
	{
	  DB_MAKE_OBJECT (&obval, setptr->op);
	  if (db_set_add (objset, &obval))
	    {
	      fprintf (stderr, "Can't put object in %x \n",
		       (unsigned long) optr1->op);
	      return;
	    }
	  if (setptr->next)
	    setptr = setptr->next;
	  else
	    setptr = head2;
	}
      if (db_put_internal (optr1->op, "co_4_4_ref1", &setval))
	{
	  fprintf (stderr, "Can't put object in %x \n",
		   (unsigned long) optr1->op);
	  return;
	}
/*    DB_MAKE_OBJECT(&obval,optr2->op);
    if (db_put_internal(optr1->op,"co_4_4_ref1",&obval)){
      fprintf(stderr,"Can't put object in %x \n",optr1->op);
      return;
    }*/
      for (i = 0; i < 10; i++)
	{
	  if ((optr2 != NULL) && (optr2->next != NULL))
	    optr2 = optr2->next;
	  else
	    optr2 = head2;
	}
      optr1 = optr1->next;
    }
  db_objlist_free (head1);
  db_objlist_free (head2);
  tickle_db ();
  return;
}

void
do_co46 ()
{
  DB_OBJLIST *head1, *head2;
  DB_OBJLIST *optr1, *optr2;
  DB_VALUE obval;
  void tickle_db ();

  printf (" co_4_6");
  head1 = optr1 = db_get_all_objects (Comp_items[CO_46].class);
  head2 = optr2 = db_get_all_objects (Comp_items[CO_12].class);
  while (optr1)
    {
      if ((optr2 != NULL) && (optr2->next != NULL))
	optr2 = optr2->next;
      else
	optr2 = head2;
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_4_6_ref1", &obval))
	{
	  fprintf (stderr, "Can't put object in %x \n",
		   (unsigned long) optr1->op);
	  return;
	}
      optr1 = optr1->next;
    }
  db_objlist_free (head1);
  db_objlist_free (head2);
  tickle_db ();
  return;
}


void
do_co47 ()
{
  DB_OBJLIST *head1, *head2;
  DB_OBJLIST *optr1, *optr2;
  DB_VALUE obval;
  void tickle_db ();
  int i;

  printf (" co_4_7");
  head1 = optr1 = db_get_all_objects (Comp_items[CO_47].class);
  head2 = optr2 = db_get_all_objects (Comp_items[CO_42].class);
  while ((optr2 != NULL) && (optr1 != NULL))
    {
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_3_2_ref1", &obval))
	{
	  fprintf (stderr, "Can't put object in %x \n",
		   (unsigned long) optr1->op);
	  return;
	}
      optr1 = optr1->next;
      optr2 = optr2->next;
    }

  db_objlist_free (head2);
  optr1 = head1;
  head2 = db_get_all_objects (Comp_items[CO_43].class);
  optr2 = NULL;
  while (optr1)
    {
      if ((optr2 == NULL) || (optr2->next == NULL))
	optr2 = head2;
      else
	optr2 = optr2->next;
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_3_2_ref2", &obval))
	{
	  fprintf (stderr, "Can't put object in %x \n",
		   (unsigned long) optr1->op);
	  return;
	}
      optr1 = optr1->next;
    }

  db_objlist_free (head2);
  optr1 = head1;
  head2 = optr2 = db_get_all_objects (Comp_items[CO_44].class);
  while ((optr2 != NULL) && (optr1 != NULL))
    {
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_3_2_ref3", &obval))
	{
	  fprintf (stderr, "Can't put object in %x \n",
		   (unsigned long) optr1->op);
	  return;
	}
      optr1 = optr1->next;
      optr2 = optr2->next;
    }

  db_objlist_free (head2);
  optr1 = head1;
  head2 = db_get_all_objects (Comp_items[CO_45].class);
  optr2 = NULL;
  while (optr1)
    {
      if ((optr2 == NULL) || (optr2->next == NULL))
	optr2 = head2;
      else
	optr2 = optr2->next;
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_3_2_ref4", &obval))
	{
	  fprintf (stderr, "Can't put object in %x \n",
		   (unsigned long) optr1->op);
	  return;
	}
      optr1 = optr1->next;
    }

  db_objlist_free (head2);
  optr1 = head1;
  head2 = optr2 = db_get_all_objects (Comp_items[CO_46].class);
  while (optr1)
    {
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_3_2_ref5", &obval))
	{
	  fprintf (stderr, "Can't put object in %x \n",
		   (unsigned long) optr1->op);
	  return;
	}
      for (i = 0; i < 10; i++)
	{
	  if ((optr2 == NULL) || (optr2->next == NULL))
	    optr2 = head2;
	  else
	    optr2 = optr2->next;
	}
      optr1 = optr1->next;
    }
  db_objlist_free (head2);
  db_objlist_free (head1);
  tickle_db ();
}

void
do_co48 ()
{
  DB_OBJLIST *head1, *head2;
  DB_OBJLIST *optr1, *optr2;
  DB_VALUE obval;
  void tickle_db ();
  DB_VALUE setval;
  DB_OBJLIST *setptr;
  DB_SET *objset;
  int i;
  extern CLASS_DESC class_desc[];
  int err;

  printf (" co_4_8");
  head1 = optr1 = db_get_all_objects (Comp_items[CO_48].class);
  head2 = optr2 = db_get_all_objects (Comp_items[CO_42].class);
  while ((optr2 != NULL) && (optr1 != NULL))
    {
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_3_2_ref1", &obval))
	{
	  fprintf (stderr, "Can't put object in %x \n",
		   (unsigned long) optr1->op);
	  return;
	}
      optr1 = optr1->next;
      optr2 = optr2->next;
    }

  db_objlist_free (head2);
  optr1 = head1;
  head2 = db_get_all_objects (Comp_items[CO_43].class);
  optr2 = NULL;
  while (optr1)
    {
      if ((optr2 == NULL) || (optr2->next == NULL))
	optr2 = head2;
      else
	optr2 = optr2->next;
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_3_2_ref2", &obval))
	{
	  fprintf (stderr, "Can't put object in %x \n",
		   (unsigned long) optr1->op);
	  return;
	}
      optr1 = optr1->next;
    }

  db_objlist_free (head2);
  optr1 = head1;
  head2 = optr2 = db_get_all_objects (Comp_items[CO_44].class);
  while ((optr2 != NULL) && (optr1 != NULL))
    {
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_3_2_ref3", &obval))
	{
	  fprintf (stderr, "Can't put object in %x \n",
		   (unsigned long) optr1->op);
	  return;
	}
      optr1 = optr1->next;
      optr2 = optr2->next;
    }

  db_objlist_free (head2);
  optr1 = head1;
  head2 = db_get_all_objects (Comp_items[CO_45].class);
  optr2 = NULL;
  while (optr1)
    {
      if ((optr2 == NULL) || (optr2->next == NULL))
	optr2 = head2;
      else
	optr2 = optr2->next;
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_3_2_ref4", &obval))
	{
	  fprintf (stderr, "Can't put object in %x \n",
		   (unsigned long) optr1->op);
	  return;
	}
      optr1 = optr1->next;
    }

  db_objlist_free (head2);
  optr1 = head1;
  head2 = optr2 = db_get_all_objects (Comp_items[CO_46].class);
  while (optr1)
    {
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_3_2_ref5", &obval))
	{
	  fprintf (stderr, "Can't put object in %x \n",
		   (unsigned long) optr1->op);
	  return;
	}
      for (i = 0; i < 10; i++)
	{
	  if ((optr2 == NULL) || (optr2->next == NULL))
	    optr2 = head2;
	  else
	    optr2 = optr2->next;
	}
      optr1 = optr1->next;
    }
  optr1 = head1;
  head2 = optr2 = db_get_all_objects (Comp_items[CO_52].class);
  while (optr1)
    {
      setptr = optr2;
      if (!(objset = (db_set_create (optr1->op, "co_4_8_ref1"))))
	{
	  fprintf (stderr, "Can't make set for co_4_8_ref1\n");
	  return;
	}
      DB_MAKE_SET (&setval, objset);
      for (i = 0; i < class_desc[DESC_CO_48].set_cardinality; i++)
	{
	  DB_MAKE_OBJECT (&obval, setptr->op);
	  if ((err = db_set_add (objset, &obval)))
	    {
	      fprintf (stderr, "Can't put set in co_4_8_ref1, err=%d\n", err);
	      continue;
	    }
	  if (setptr->next)
	    setptr = setptr->next;
	  else
	    setptr = head2;
	}
      if ((err = db_put_internal (optr1->op, "co_4_8_ref1", &setval)))
	{
	  fprintf (stderr, "Can't put object in %x \n",
		   (unsigned long) optr1->op);
	  continue;
	}
      if (optr2->next)
	optr2 = optr2->next;
      else
	optr2 = head2;
      optr1 = optr1->next;
    }
  db_objlist_free (head2);
  db_objlist_free (head1);
  tickle_db ();
}

void
do_co52 ()
{
  DB_OBJLIST *head1, *head2;
  DB_OBJLIST *optr1, *optr2;
  DB_VALUE obval;
  void tickle_db ();
  DB_VALUE setval;
  DB_OBJLIST *setptr;
  DB_SET *objset;
  int i;
  int err;
  extern CLASS_DESC class_desc[];

  printf (" co_5_2");
  head1 = optr1 = db_get_all_objects (Comp_items[CO_52].class);
  head2 = optr2 = db_get_all_objects (Comp_items[CO_42].class);
  while ((optr2 != NULL) && (optr1 != NULL))
    {
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_3_2_ref1", &obval))
	{
	  fprintf (stderr, "Can't put object in %x \n",
		   (unsigned long) optr1->op);
	  return;
	}
      optr1 = optr1->next;
      optr2 = optr2->next;
    }

  db_objlist_free (head2);
  optr1 = head1;
  head2 = db_get_all_objects (Comp_items[CO_43].class);
  optr2 = NULL;
  while (optr1)
    {
      if ((optr2 == NULL) || (optr2->next == NULL))
	optr2 = head2;
      else
	optr2 = optr2->next;
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_3_2_ref2", &obval))
	{
	  fprintf (stderr, "Can't put object in %x \n",
		   (unsigned long) optr1->op);
	  return;
	}
      optr1 = optr1->next;
    }

  db_objlist_free (head2);
  optr1 = head1;
  head2 = optr2 = db_get_all_objects (Comp_items[CO_44].class);
  while ((optr2 != NULL) && (optr1 != NULL))
    {
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_3_2_ref3", &obval))
	{
	  fprintf (stderr, "Can't put object in %x \n",
		   (unsigned long) optr1->op);
	  return;
	}
      optr1 = optr1->next;
      optr2 = optr2->next;
    }

  db_objlist_free (head2);
  optr1 = head1;
  head2 = db_get_all_objects (Comp_items[CO_45].class);
  optr2 = NULL;
  while (optr1)
    {
      if ((optr2 == NULL) || (optr2->next == NULL))
	optr2 = head2;
      else
	optr2 = optr2->next;
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_3_2_ref4", &obval))
	{
	  fprintf (stderr, "Can't put object in %x \n",
		   (unsigned long) optr1->op);
	  return;
	}
      optr1 = optr1->next;
    }

  db_objlist_free (head2);
  optr1 = head1;
  head2 = optr2 = db_get_all_objects (Comp_items[CO_46].class);
  while (optr1)
    {
      DB_MAKE_OBJECT (&obval, optr2->op);
      if (db_put_internal (optr1->op, "co_3_2_ref5", &obval))
	{
	  fprintf (stderr, "Can't put object in %x \n",
		   (unsigned long) optr1->op);
	  return;
	}
      for (i = 0; i < 10; i++)
	{
	  if ((optr2 == NULL) || (optr2->next == NULL))
	    optr2 = head2;
	  else
	    optr2 = optr2->next;
	}
      optr1 = optr1->next;
    }
  optr1 = head1;
  head2 = optr2 = db_get_all_objects (Comp_items[CO_52].class);
  while (optr1)
    {
      setptr = optr2;
      if (!(objset = (db_set_create (optr1->op, "co_4_8_ref1"))))
	{
	  fprintf (stderr, "Can't make set for co_4_8_ref1\n");
	  return;
	}
      DB_MAKE_SET (&setval, objset);
      for (i = 0; i < class_desc[DESC_CO_52].set_cardinality; i++)
	{
	  DB_MAKE_OBJECT (&obval, setptr->op);
	  if ((err = db_set_add (objset, &obval)))
	    {
	      fprintf (stderr, "Can't put set in co_4_8_ref1, err=%d\n", err);
	      continue;
	    }
	  if (setptr->next)
	    setptr = setptr->next;
	  else
	    setptr = head2;
	}
      if ((err = db_put_internal (optr1->op, "co_4_8_ref1", &setval)))
	{
	  fprintf (stderr, "Can't put object in %x , err= %d\n",
		   (unsigned long) optr1->op, err);
	  continue;
	}
      if (optr2->next)
	optr2 = optr2->next;
      else
	optr2 = head2;
      optr1 = optr1->next;
    }
  db_objlist_free (head2);

  optr1 = head1;
  head2 = optr2 = db_get_all_objects (Comp_items[CO_48].class);

  while (optr1)
    {
      DB_MAKE_OBJECT (&obval, optr2->op);
      if ((err = db_put_internal (optr1->op, "co_5_2_ref1", &obval)))
	{
	  fprintf (stderr, "Can't put value to co_5_2_ref1,err=%d\n", err);
	  continue;
	}
      if (optr2->next)
	optr2 = optr2->next;
      else
	optr2 = head2;
      optr1 = optr1->next;
    }
  db_objlist_free (head2);
  db_objlist_free (head1);
  tickle_db ();
}

/*
* assign objects to classes sd_8_1 and sd_8_2.  sd_8_2 inherits sd_8_1 from
* sd_8_1
*/

void
do_sd_81 ()
{
  DB_OBJLIST *head1, *head1a, *head2, *head3, *optr1, *optr1a, *optr2, *optr3;
  DB_OBJECT *class1, *class2, *class3, *class1a;
  DB_VALUE oval1, oval2;
  void tickle_db ();

  class1 = db_find_class ("sd_8_2");
  class1a = db_find_class ("sd_8_1");
  class2 = db_find_class ("sd_1_2");
  class3 = db_find_class ("sd_1_3");

  head1 = optr1 = db_get_all_objects (class1);
  head1a = optr1a = db_get_all_objects (class1a);
  head2 = optr2 = db_get_all_objects (class2);
  head3 = optr3 = db_get_all_objects (class3);

  while (optr1 && optr1a && optr2 && optr3)
    {
      DB_MAKE_OBJECT (&oval1, optr2->op);
      DB_MAKE_OBJECT (&oval2, optr3->op);
      if (db_put_internal (optr1->op, "sd_8_1_ref1", &oval1) ||
	  db_put_internal (optr1->op, "sd_8_2_ref1", &oval2) ||
	  db_put_internal (optr1a->op, "sd_8_1_ref1", &oval1))
	{
	  fprintf (stderr, "Can't put object for class sd_8_1 || sd_8_2\n");
	  fflush (stderr);
	  break;
	}
      optr1 = optr1->next;
      optr1a = optr1a->next;
      optr2 = optr2->next;
      optr3 = optr3->next;
    }
  db_objlist_free (head1);
  db_objlist_free (head1a);
  db_objlist_free (head2);
  db_objlist_free (head3);
  tickle_db ();
  return;
}

void
do_co_classes ()
{
  extern char Do[];

  if (strstr (Do, "co"))
    {
      do_co0 ();
      do_co12 ();
      do_co22 ();
      do_co31 ();
      do_co32 ();
      do_co41 ();
      do_co42 ();
      do_co43 ();
      do_co44 ();
      do_co46 ();
      do_co47 ();
      do_co48 ();
      do_co52 ();
    }
  if (strstr (Do, "sd"))
    {
      do_sd_81 ();
    }
}

int
squirrel (DB_OBJECT ** here, DB_OBJLIST * this, int len)
{
  int i;
  DB_OBJLIST *tmp;

  tmp = this;

  for (i = 0; i < len && tmp; i++)
    {
      here[i] = tmp->op;
      tmp = tmp->next;
    }
  return (0);
}
