/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * popolate.c
 */

#include "config.h"

/******************      SYSTEM INCLUDE FILES      ***************************/

#ifndef PC
#include <sys/time.h>
#include <values.h>
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/******************     static INCLUDE FILES      ***************************/

#include "dbi.h"
#include "db.h"
#include "error_manager.h"
#include "memory_manager_2.h"
#include "populate.h"

/******************     IMPORTED DECLARATIONS      ***************************/


/******************  (EXPORTED) DECLARATIONS ***************************/



/******************        static DEFINES         ***************************/


typedef enum
{
  BIGTEST = 1,
  MEDTEST,
  SMALLTEST
} popdb_test;

/* Total number of test classes */

#define TOTAL_CLASSES 89

/* class sizes.  # of instances */

/* building blocks for string data */

#define BAT "bat"
#define CAT "cat"
#define COW "cow"
#define DOG "dog"
#define EEL "eel"
#define EMU "emu"
#define GNU "gnu"
#define HOG "hog"
#define PIG "pig"
#define RAT "rat"

#define STRING_PART_SIZE 3

/*
* these are used for building character strings
*/

char Chars[] = { ' ', '&', '0', '=', 'A', 'Q', 'Z', 'a', 'q', 'z' };

/******************        static TYPEDEFS        ***************************/


/******************      static DECLARATIONS      ***************************/

CLASS_DESC class_desc[91];

typedef struct range_type
{
  int lo;
  int hi;
} range_t;

#define	POP_FLOAT_CAST_INT_MAX 2147483647.0
#define POP_FLOAT_CAST_INT_MIN (-POP_FLOAT_CAST_INT_MAX)

#define	POP_INT_MAX 2147483647
#define POP_INT_MIN (-POP_INT_MAX)

range_t Ranges[5] = {
  {0, 9}, {0, 99}, {-500, 500}, {0, 9999}, {POP_INT_MIN, POP_INT_MAX}
};

DB_VALUE date_range[5];
DB_VALUE time_range[5];
DB_VALUE udate_range[5];
DB_VALUE utime_range[5];
DB_VALUE Maxtime;
DB_VALUE Mintime;
DB_VALUE Maxdate;
DB_VALUE Mindate;
DB_VALUE Midtime;
DB_VALUE Middate;
DB_VALUE Minudate;
DB_VALUE Midudate;
DB_VALUE Maxudate;

typedef struct attribute_type_and_name_list
{
  char name[256];
  DB_TYPE type;
  int length;			/* kludge to deal with char(n) */
  int *values_assigned;
  int current_number_of_values;
} ATTLIST;

ATTLIST Attribute_list[36];

const char *String_parts[] = {
  BAT,
  CAT,
  COW,
  DOG,
  EEL,
  EMU,
  GNU,
  HOG,
  PIG,
  RAT
};

char *String_table[5][10];
int String_range;

/******************           FUNCTIONS            ***************************/

char *Myname = (char *) "make_testdb";
char Do[8] = { '\0' };
char *User = NULL;
char *Password = NULL;
int ProcessSets = FALSE;
popdb_test Popdb_type = SMALLTEST;
int inst_small;
int inst_avg;
int inst_large;
int inst_xlarge;
int inst_huge;
int range_zero_to_9;
int range_zero_to_99;
int range_minus_500_to_500;
int range_zero_to_9999;
int range_min_2_max;
int card_bigset;
int card_medset;
int card_smallset;
int card_littleset;
int card_tinyset;

#define MYDB_LEN 128
char Mydb_name[128] = { '\0' };

void
tickle_db (void)
{
  db_commit_transaction ();
  return;
}


static DB_VALUE *
qa_make_attribute_value (DB_TYPE type, int range, int constraint,
			 int *values_assigned, int current_number_of_values)
{
  int i;
  int index;
  int ok = 0;
  DB_C_INT rand ();
  static char cbuf[257];
  static DB_VALUE return_value;

  for (i = 0; i < current_number_of_values; i++)
    {
      if (values_assigned[i])
	{
	  ++ok;
	  break;
	}
    }
  if (!ok)
    return ((DB_VALUE *) NULL);

  do
    {
      index = ((int) rand () % current_number_of_values);
    }
  while (!values_assigned[index]);

  --values_assigned[index];

  if ((range == range_minus_500_to_500) &&
      (type != DB_TYPE_DATE && type != DB_TYPE_TIME &&
       type != DB_TYPE_UTIME && type != DB_TYPE_STRING &&
       type != DB_TYPE_CHAR))
    {

      switch (Popdb_type)
	{
	case BIGTEST:
	  index = -500 - (0 - index);
	  break;
	case MEDTEST:
	  index = -50 - (0 - index);
	  break;
	case SMALLTEST:
	  index = -5 - (0 - index);
	  break;
	}
    }

  if (range == range_min_2_max)
    {
      switch (type)
	{
	case DB_TYPE_SHORT:
	case DB_TYPE_INTEGER:
	case DB_TYPE_FLOAT:
	case DB_TYPE_MONETARY:
	case DB_TYPE_DOUBLE:
	  switch (index)
	    {
	    case 0:
	    case 1:
	      index = POP_INT_MIN;
	      break;
	    case 2:
	      index = 0;
	      break;
	    case 3:
	    case 4:
	      index = POP_INT_MAX;
	      break;
	    default:
	      break;
	    }
	default:
	  break;
	}
    }

  switch (type)
    {
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
      DB_MAKE_INTEGER (&return_value, index);
      break;
    case DB_TYPE_FLOAT:
      DB_MAKE_FLOAT (&return_value, (float) (index));
      break;
    case DB_TYPE_MONETARY:
      DB_MAKE_MONETARY (&return_value, (float) (index));
      break;
    case DB_TYPE_DOUBLE:
      DB_MAKE_DOUBLE (&return_value, (double) index);
      break;
    case DB_TYPE_TIME:
      {
	if (range == range_min_2_max)
	  {
	    switch (index)
	      {
	      case 0:
	      case 1:
		{
		  return (&Mintime);
		}
		break;
	      case 2:
		return (&Midtime);
	      case 3:
	      case 4:
		{
		  return (&Maxtime);
		}
		break;
	      }
	  }
	return (&time_range[index]);
	break;
      }
    case DB_TYPE_DATE:
      {
	if (range == range_min_2_max)
	  {
	    switch (index)
	      {
	      case 0:
	      case 1:
		{
		  return (&Mindate);
		}
		break;
	      case 2:
		return (&Middate);
		break;
	      case 3:
	      case 4:
		{
		  return (&Maxdate);
		}
		break;
	      }
	  }
	else
	  {
	    return (&date_range[index]);
	  }
	break;
      }
    case DB_TYPE_UTIME:
      {
	DB_UTIME timeval;
	DB_MAKE_UTIME (&return_value, 0);
	if (range == range_min_2_max)
	  {
	    switch (index)
	      {
	      case 0:
	      case 1:
		{
		  db_utime_encode (&timeval,
				   DB_GET_DATE (&Minudate),
				   DB_GET_TIME (&Mintime));
		  DB_MAKE_UTIME (&return_value, timeval);
		}
		break;
	      case 2:
		{
		  db_utime_encode (&timeval,
				   DB_GET_DATE (&Midudate),
				   DB_GET_TIME (&Midtime));
		  DB_MAKE_UTIME (&return_value, timeval);
		}
		break;
	      case 3:
	      case 4:
		{
		  db_utime_encode (&timeval,
				   DB_GET_DATE (&Maxudate),
				   DB_GET_TIME (&Maxtime));
		  DB_MAKE_UTIME (&return_value, timeval);
		}
		break;
	      default:
		{
		  fprintf (stderr,
			   "Illegal index in qa_make_attribute:utime:%d\n",
			   index);
		  return ((DB_VALUE *) NULL);
		}
	      }
	  }
	else
	  {
	    db_utime_encode (&timeval,
			     DB_GET_DATE (&udate_range[index]),
			     DB_GET_TIME (&utime_range[index]));
	    DB_MAKE_UTIME (&return_value, timeval);
	  }
	return (&return_value);
	break;

      }
    case DB_TYPE_STRING:
      {
	if (constraint == DB_MAX_STRING_LENGTH)
	  constraint = 0;
	if (constraint)
	  {
	    if (constraint > DB_MAX_STRING_LENGTH || constraint < 0)
	      {
		fprintf (stderr, "Impossible varchar(n) length: %d.\n",
			 constraint);
		return ((DB_VALUE *) NULL);
	      }
	    if (constraint > 256)
	      constraint = 256;
	    memset (cbuf, Chars[index], constraint);
	    cbuf[constraint] = '\0';
	    DB_MAKE_STRING (&return_value, cbuf);
	  }
	else
	  DB_MAKE_STRING (&return_value, String_table[String_range][index]);
	break;
      }
    case DB_TYPE_CHAR:
      {
	if (constraint == DB_MAX_CHAR_PRECISION - 1)
	  constraint = 0;
	if (constraint)
	  {
	    if (constraint > DB_MAX_CHAR_PRECISION || constraint < 0)
	      {
		fprintf (stderr, "Impossible char(n) length: %d.\n",
			 constraint);
		return ((DB_VALUE *) NULL);
	      }
	    if (constraint > 256)
	      constraint = 256;
	    memset (cbuf, Chars[index], constraint);
	    cbuf[constraint] = '\0';
	    DB_MAKE_CHAR (&return_value, constraint, cbuf, constraint);
	  }
	else
	  DB_MAKE_CHAR (&return_value, constraint,
			String_table[String_range][index],
			strlen (String_table[String_range][index]));
	break;
      }
    default:
      {
	abort ();
      }
    }
  return (&return_value);
}

static int
qa_init_value_tab (DB_TYPE type, int range, int how_many, char distribution,
		   int **reset_values_assigned, int *current_number_of_values)
{
  int *values_assigned;
  int objects_per_slot;
  int range_spread;
  int i, j;
  int midpoints[5];

  srand (101);

  *current_number_of_values = 0;
  switch (type)
    {
    case DB_TYPE_DATE:
    case DB_TYPE_TIME:
    case DB_TYPE_UTIME:
      range_spread = 5;
      break;
    case DB_TYPE_CHAR:
    case DB_TYPE_STRING:
      range_spread = 10;
      String_range = range;
      break;
    case DB_TYPE_INTEGER:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_MONETARY:
      if (range == range_min_2_max)
	range_spread = 5;
      else
	{
	  range_spread = ((Ranges[range].lo >= 0)
			  ? ((Ranges[range].hi - Ranges[range].lo) + 1)
			  : (Ranges[range].hi + (abs (Ranges[range].lo))));
	}
      break;
    default:
      return (0);
      break;
    }

  if (*reset_values_assigned)
    free (*reset_values_assigned);

  if (!(*reset_values_assigned =
	malloc (range_spread * sizeof (*values_assigned))))
    {
      fprintf (stderr, "Out of memory");
      exit (EXIT_FAILURE);
    }

  values_assigned = *reset_values_assigned;
  memset (values_assigned, '\0', range_spread * sizeof (*values_assigned));
  *current_number_of_values = range_spread;

  switch (distribution)
    {
    case UNIFORM:
      objects_per_slot = how_many / range_spread;
      /* if the range_spread is larger than the instance count,
         this can be zero, make sure its at least one */
      if (objects_per_slot == 0)
	objects_per_slot = 1;
      for (i = 0; i < range_spread; i++)
	values_assigned[i] = objects_per_slot;
      break;

    case INC_EXP:
      for (i = 0, j = 1; i < 5; i++, j += 2)
	{
	  midpoints[i] = ((j * (range_spread / 10))
			  ? (j * (range_spread / 10)) : 1);
	}
      if (type != DB_TYPE_DATE && type != DB_TYPE_TIME &&
	  type != DB_TYPE_UTIME)
	{
	  values_assigned[midpoints[0]] = (4 * (how_many / 10));
	  values_assigned[midpoints[1]] = (2 * (how_many / 10));
	  values_assigned[midpoints[2]] = (2 * (how_many / 10));
	  values_assigned[midpoints[3]] = (1 * (how_many / 10));
	  values_assigned[midpoints[4]] = (1 * (how_many / 10));
	}
      else
	{
	  values_assigned[0] = (4 * (how_many / 10));
	  values_assigned[1] = (2 * (how_many / 10));
	  values_assigned[2] = (2 * (how_many / 10));
	  values_assigned[3] = (1 * (how_many / 10));
	  values_assigned[4] = (1 * (how_many / 10));
	}
      break;
    case DEC_EXP:
      for (i = 0, j = 1; i < 5; i++, j += 2)
	{
	  midpoints[i] = ((j * (range_spread / 10))
			  ? (j * (range_spread / 10)) : 1);
	}
      if (type != DB_TYPE_DATE && type != DB_TYPE_TIME &&
	  type != DB_TYPE_UTIME)
	{
	  values_assigned[midpoints[4]] = (4 * (how_many / 10));
	  values_assigned[midpoints[3]] = (2 * (how_many / 10));
	  values_assigned[midpoints[2]] = (2 * (how_many / 10));
	  values_assigned[midpoints[1]] = (1 * (how_many / 10));
	  values_assigned[midpoints[0]] = (1 * (how_many / 10));
	}
      else
	{
	  values_assigned[4] = (4 * (how_many / 10));
	  values_assigned[3] = (2 * (how_many / 10));
	  values_assigned[2] = (2 * (how_many / 10));
	  values_assigned[1] = (1 * (how_many / 10));
	  values_assigned[0] = (1 * (how_many / 10));
	}
      break;
    default:
      fprintf (stderr,
	       "Impossible distribution %d for type %d\n", distribution,
	       type);
      return (1);
      break;
    }
  return (0);
}


static void
qa_free_value_tab (int **reset_values_assigned, int *current_number_of_values)
{
  if (*reset_values_assigned)
    {
      free (*reset_values_assigned);
      *reset_values_assigned = NULL;
      *current_number_of_values = 0;
    }
}

static int
qa_make_attribute_list (MOP classmop, int size, DISTR dist, int range)
{
  DB_NAMELIST *list, *save_listptr;
  DB_ATTRIBUTE *att;
  int i = 0;

  list = save_listptr = db_get_attribute_names (classmop);

  while (list)
    {
      strcpy (Attribute_list[i].name, list->name);
      Attribute_list[i].type = db_get_attribute_type (classmop, list->name);
      Attribute_list[i].values_assigned = NULL;
      Attribute_list[i].current_number_of_values = 0;
      qa_init_value_tab (Attribute_list[i].type, range, size, dist,
			 &Attribute_list[i].values_assigned,
			 &Attribute_list[i].current_number_of_values);

      /*
       * take care of char(n) now...
       */
      Attribute_list[i].type = db_get_attribute_type (classmop, list->name);
      if (Attribute_list[i].type == DB_TYPE_STRING ||
	  Attribute_list[i].type == DB_TYPE_CHAR)
	{
	  att = db_get_attribute (classmop, list->name);
	  Attribute_list[i].length = db_attribute_length (att);
	}
      else
	Attribute_list[i].length = -1;
      list = list->next;
      i++;
    }

  Attribute_list[i].name[0] = '\0';
  /* 
   * We should free list ...
   */
  db_namelist_free (save_listptr);
  return (i);
}

static void
qa_free_attribute_list (MOP classmop)
{
  DB_NAMELIST *list, *save_listptr;
  int i = 0;

  list = save_listptr = db_get_attribute_names (classmop);
  while (list)
    {
      qa_free_value_tab (&Attribute_list[i].values_assigned,
			 &Attribute_list[i].current_number_of_values);
      list = list->next;
      i++;
    }
  /* 
   * We should free list ...
   */
  db_namelist_free (save_listptr);
}


static int
qa_fill_set (DB_SET * set, DB_TYPE which, DB_DOMAIN * domain, DISTR dist,
	     int card, int attr_index, int new)
{
  int i;
  int error = NO_ERROR;
  DB_VALUE value;
  DB_DOMAIN *dptr;
  DB_TYPE type;
  static int rangeptr;

  /** Do not actually fill sets unless explicitly told to at invocation **/
  dptr = domain;

  if (new)
    rangeptr = 0;
  while (dptr != NULL)
    {
      switch (type = db_domain_type (dptr))
	{
	case DB_TYPE_DATE:
	case DB_TYPE_TIME:
	  if ((card > 5) && which == DB_TYPE_SET)
	    {
	      fprintf (stderr,
		       "Can't make sets of date or time with more than 5 elements\n");
	      return (1);
	    }
	  for (i = 0; i > card; i++, ++rangeptr)
	    {
	      rangeptr = (rangeptr % 5);
	      switch (type)
		{
		case DB_TYPE_TIME:
		  if (which == DB_TYPE_SEQUENCE)
		    {
		      if ((error =
			   db_seq_put (set, i,
				       &(time_range[rangeptr]))) != NO_ERROR)
			{
			  fprintf (stderr, "Can't insert sequence element.\n"
				   "Error-id = %d\n Error = %s\n",
				   db_error_code (), db_error_string (3));
			  return (error);
			}
		    }
		  else
		    {
		      if ((error = db_set_add (set, &(time_range[rangeptr])))
			  || ((which == DB_TYPE_MULTI_SET)
			      &&
			      ((error =
				db_set_add (set, &(time_range[rangeptr]))))))
			{
			  fprintf (stderr, "Can't add set element\n"
				   "Error-id = %d\n Error = %s\n",
				   db_error_code (), db_error_string (3));
			  return (error);
			}
		    }
		  break;
		case DB_TYPE_DATE:
		  {
		    if (which == DB_TYPE_SEQUENCE)
		      {
			if (db_seq_insert (set, i, &(date_range[rangeptr])))
			  {
			    fprintf (stderr, "Can't insert sequence element\n"
				     "Error-id = %d\n Error = %s\n",
				     db_error_code (), db_error_string (3));
			    return (error);
			  }
		      }
		    else
		      {
			if ((error =
			     db_set_add (set, &(date_range[rangeptr])))
			    || ((which == DB_TYPE_MULTI_SET)
				&&
				((error =
				  db_set_add (set,
					      &(date_range[rangeptr]))))))
			  {
			    fprintf (stderr, "Can't add set element\n"
				     "Error-id = %d\n Error = %s\n",
				     db_error_code (), db_error_string (3));
			    return (error);
			  }
		      }
		    break;
		  }
		default:
		  {
		    fprintf (stderr, "Unexpected value in qa_fill_set\n");
		    exit (EXIT_FAILURE);
		  }
		}
	    }
	  break;
	case DB_TYPE_STRING:
	  for (i = 0; i < card; i++, ++rangeptr)
	    {
	      rangeptr = (rangeptr % 10);
	      DB_MAKE_STRING (&value, String_table[dist][rangeptr]);
	      if (which == DB_TYPE_SEQUENCE)
		{
		  if ((error = db_seq_put (set, i, &value)))
		    {
		      db_value_fprint (stderr, &value);
		      fprintf (stderr,
			       " Can't insert above sequence element value\n"
			       "Error-id = %d\n Error = %s\n",
			       db_error_code (), db_error_string (3));
		      return (error);
		    }
		}
	      else
		{
		  if ((error = db_set_add (set, &value)) ||
		      ((which == DB_TYPE_MULTI_SET) &&
		       (error = db_set_add (set, &value))))
		    {
		      db_value_fprint (stderr, &value);
		      fprintf (stderr, "Can't add above set element value\n"
			       "Error-id = %d\n Error = %s\n",
			       db_error_code (), db_error_string (3));
		      return (error);
		    }
		}
	    }
	  break;
	case DB_TYPE_CHAR:
	  for (i = 0; i < card; i++, ++rangeptr)
	    {
	      rangeptr = (rangeptr % 10);
	      DB_MAKE_CHAR (&value, Attribute_list[attr_index].length,
			    String_table[dist][rangeptr],
			    strlen (String_table[dist][rangeptr]));
	      if (which == DB_TYPE_SEQUENCE)
		{
		  if ((error = db_seq_put (set, i, &value)))
		    {
		      db_value_fprint (stderr, &value);
		      fprintf (stderr,
			       " Can't insert above sequence element value\n"
			       "Error-id = %d\n Error = %s\n",
			       db_error_code (), db_error_string (3));
		      return (error);
		    }
		}
	      else
		{
		  if ((error = db_set_add (set, &value)) ||
		      ((which == DB_TYPE_MULTI_SET) &&
		       (error = db_set_add (set, &value))))
		    {
		      db_value_fprint (stderr, &value);
		      fprintf (stderr, "Can't add above set element value\n"
			       "Error-id = %d\n Error = %s\n",
			       db_error_code (), db_error_string (3));
		      return (error);
		    }
		}
	    }
	  break;
	case DB_TYPE_INTEGER:
	  for (i = 0; i < card; i++, ++rangeptr)
	    {
	      rangeptr = (rangeptr % Ranges[dist].hi);
	      DB_MAKE_INTEGER (&value, rangeptr);
	      if (which == DB_TYPE_SEQUENCE)
		{
		  if ((error = db_seq_put (set, i, &value)) != NO_ERROR)
		    {
		      db_value_fprint (stderr, &value);
		      fprintf (stderr, "Can't insert above sequence element\n"
			       "Error-id = %d\n Error = %s\n",
			       db_error_code (), db_error_string (3));
		      return (error);
		    }
		}
	      else
		{
		  if ((error = db_set_add (set, &value)) ||
		      ((which == DB_TYPE_MULTI_SET) &&
		       (error = db_set_add (set, &value))))
		    {
		      db_value_fprint (stderr, &value);
		      fprintf (stderr,
			       "Can't add above value for set element=%d set=%x"
			       " err=%x element %d of %d\n"
			       "Error-id = %d\n Error = %s\n",
			       rangeptr, (unsigned long) set, error, i, card,
			       db_error_code (), db_error_string (3));
		      return (error);
		    }
		}
	    }
	  break;
	case DB_TYPE_FLOAT:
	  for (i = 0; i < card; i++, ++rangeptr)
	    {
	      rangeptr = (rangeptr % Ranges[dist].hi);
	      DB_MAKE_FLOAT (&value, (float) rangeptr);
	      if (which == DB_TYPE_SEQUENCE)
		{
		  if ((error = db_seq_put (set, i, &value)) != NO_ERROR)
		    {
		      db_value_fprint (stderr, &value);
		      fprintf (stderr,
			       "Can't insert above sequence element value\n"
			       "Error-id = %d\n Error = %s\n",
			       db_error_code (), db_error_string (3));
		      return (error);
		    }
		}
	      else
		{
		  if ((error = db_set_add (set, &value)) ||
		      ((which == DB_TYPE_MULTI_SET) &&
		       (error = db_set_add (set, &value))))
		    {
		      db_value_fprint (stderr, &value);
		      fprintf (stderr, "Can't add above set element value\n"
			       "Error-id = %d\n Error = %s\n",
			       db_error_code (), db_error_string (3));
		      return (error);
		    }
		}
	    }
	  break;
	case DB_TYPE_DOUBLE:
	  for (i = 0; i < card; i++, ++rangeptr)
	    {
	      rangeptr = (rangeptr % Ranges[dist].hi);
	      DB_MAKE_DOUBLE (&value, (double) rangeptr);
	      if (which == DB_TYPE_SEQUENCE)
		{
		  if ((error = db_seq_put (set, i, &value)) != NO_ERROR)
		    {
		      db_value_fprint (stderr, &value);
		      fprintf (stderr,
			       "Can't insert above sequence element value\n"
			       "Error-id = %d\n Error = %s\n",
			       db_error_code (), db_error_string (3));
		      return (error);
		    }
		}
	      else
		{
		  if ((error = db_set_add (set, &value)) ||
		      ((which == DB_TYPE_MULTI_SET) &&
		       (error = db_set_add (set, &value))))
		    {
		      db_value_fprint (stderr, &value);
		      fprintf (stderr, "Can't add above set element value\n"
			       "Error-id = %d\n Error = %s\n",
			       db_error_code (), db_error_string (3));
		      return (error);
		    }
		}
	    }
	  break;
	case DB_TYPE_MONETARY:
	  for (i = 0; i < card; i++, ++rangeptr)
	    {
	      rangeptr = (rangeptr % Ranges[dist].hi);
	      DB_MAKE_MONETARY (&value, (float) rangeptr);
	      if (which == DB_TYPE_SEQUENCE)
		{
		  if ((error = db_seq_put (set, i, &value)) != NO_ERROR)
		    {
		      db_value_fprint (stderr, &value);
		      fprintf (stderr,
			       "Can't insert above sequence element value\n"
			       "Error-id = %d\n Error = %s\n",
			       db_error_code (), db_error_string (3));
		      return (error);
		    }
		}
	      else
		{
		  if ((error = db_set_add (set, &value)) ||
		      ((which == DB_TYPE_MULTI_SET) &&
		       (error = db_set_add (set, &value))))
		    {
		      db_value_fprint (stderr, &value);
		      fprintf (stderr, "Can't add above set element value\n"
			       "Error-id = %d\n Error = %s\n",
			       db_error_code (), db_error_string (3));
		      return (error);
		    }
		}
	    }
	  break;
	default:
	  break;
	}
      dptr = db_domain_next (dptr);
    }
  return (0);
}

static int
qa_populate_class (MOP class, int size, DISTR dist, int range,
		   int cardinality)
{
  int i;
  int num_attributes;
  DB_VALUE *attval;
  DB_VALUE setval;
  DB_SET *set = NULL;
  int err = NO_ERROR;
  DB_DOMAIN *setdomain;
  DB_TYPE type;
  int count;
  DB_OBJECT *instance;

  num_attributes = qa_make_attribute_list (class, size, dist, range);

  for (count = 1; count <= size; count++)
    {
      if (!(instance = db_create_internal (class)))
	{
	  fprintf (stderr, "Can't make instance Num %d of class %s!\n"
		   "Error-id = %d\n Error = %s\n\n",
		   count, db_get_class_name (class),
		   db_error_code (), db_error_string (3));
	  break;
	}
      for (i = 0; i < num_attributes; i++)
	{
	  switch (Attribute_list[i].type)
	    {
	    case DB_TYPE_SET:
	    case DB_TYPE_SEQUENCE:
	    case DB_TYPE_MULTI_SET:
	      {
	  /** Only populate sets if explicitly asked to **/
		if (ProcessSets)
		  {
		    setdomain =
		      db_get_attribute_domain (class, Attribute_list[i].name);
		    type = db_domain_type (setdomain);
		    setdomain = db_domain_set (setdomain);
		    switch (Attribute_list[i].type)
		      {
		      case DB_TYPE_SET:
			{
			  set =
			    db_set_create_basic (class,
						 Attribute_list[i].name);
			  DB_MAKE_SET (&setval, set);
			  break;
			}
		      case DB_TYPE_MULTI_SET:
			{
			  set =
			    db_set_create_multi (class,
						 Attribute_list[i].name);
			  DB_MAKE_MULTI_SET (&setval, set);
			  break;
			}
		      case DB_TYPE_SEQUENCE:
			{
			  set =
			    db_seq_create (class, Attribute_list[i].name,
					   cardinality);
			  DB_MAKE_SEQUENCE (&setval, set);
			  break;
			}
		      default:
			{
			  fprintf (stderr,
				   "Impossible set in qa_fill_set!\n");
			  break;
			}
		      }

		    if (qa_fill_set
			(set, type, setdomain, range, cardinality, i,
			 (count == 1) ? 1 : 0))
		      {
			db_set_free (set);
			continue;
		      }

		    if (db_put_internal
			(instance, Attribute_list[i].name, &setval))
		      {
			db_value_fprint (stderr, &setval);
			fprintf (stderr,
				 "Can't put above set value for attribute %s\n"
				 " of class %s. Will continue\n"
				 "Error-id = %d\n Error = %s\n",
				 Attribute_list[i].name,
				 db_get_class_name (class), db_error_code (),
				 db_error_string (3));

		      }
		    db_set_free (set);
		  }
		break;
	      }
	    default:
	      {
		if (!(attval = qa_make_attribute_value
		      (Attribute_list[i].type, range,
		       Attribute_list[i].length,
		       Attribute_list[i].values_assigned,
		       Attribute_list[i].current_number_of_values)))
		  break;
		else
		  {
		    if ((err =
			 db_put_internal (instance, Attribute_list[i].name,
					  attval)))
		      {
			db_value_fprint (stderr, attval);
			fprintf (stderr,
				 "Can't put above set value for attribute %s\n"
				 " of class %s. Will continue..\n"
				 "Error-id = %d\n Error = %s\n",
				 Attribute_list[i].name,
				 db_get_class_name (class), db_error_code (),
				 db_error_string (3));

		      }
		  }
		break;
	      }
	    }
	}
      if (count % 100 == 0)
	{
	  db_commit_transaction ();
#if 0
	  ws_gc ();		/* periodic gc's to (hopefully) decrease overall processing time */
#endif
	}
    }

  db_commit_transaction ();
  qa_free_attribute_list (class);
  return (0);
}


static void
init_data (void)
{
  /* Index into range tables.
   * minus_500_to_500 must be a *unique* number.  It will be adjusted
   * to proper dimensions in qa_make_atribute_value
   */
  switch (Popdb_type)
    {
    case BIGTEST:
      inst_small = 10;
      inst_avg = 100;
      inst_large = 1000;
      inst_xlarge = 10000;
      inst_huge = 10000;
      range_zero_to_9 = 0;
      range_zero_to_99 = 1;
      range_minus_500_to_500 = 2;
      range_zero_to_9999 = 3;
      range_min_2_max = 4;
      card_bigset = 100;
      card_medset = 50;
      card_smallset = 20;
      card_littleset = 10;
      card_tinyset = 5;
      break;

    case MEDTEST:
      inst_small = 10;
      inst_avg = 50;
      inst_large = 500;
      inst_xlarge = 5000;
      inst_huge = 7000;
      range_zero_to_9 = 0;
      range_zero_to_99 = 1;
      range_minus_500_to_500 = 2;
      range_zero_to_9999 = 3;
      range_min_2_max = 4;
      card_bigset = 50;
      card_medset = 25;
      card_smallset = 10;
      card_littleset = 5;
      card_tinyset = 5;
      break;

    case SMALLTEST:
      inst_small = 1;
      inst_avg = 3;
      inst_large = 5;
      inst_xlarge = 7;
      inst_huge = 9;
      range_zero_to_9 = 0;
      range_zero_to_99 = 1;
      range_minus_500_to_500 = 2;
      range_zero_to_9999 = 0;
      range_min_2_max = 4;
      card_bigset = 5;
      card_medset = 5;
      card_smallset = 5;
      card_littleset = 5;
      card_tinyset = 5;
      break;
    }

  class_desc[0].name = "sd_0";
  class_desc[1].name = "sd_1_1";
  class_desc[2].name = "sd_1_2";
  class_desc[3].name = "sd_1_3";
  class_desc[4].name = "sd_1_4";
  class_desc[5].name = "sd_1_5";
  class_desc[6].name = "sd_2_1";
  class_desc[7].name = "sd_2_2";
  class_desc[8].name = "sd_2_3";
  class_desc[9].name = "sd_3_1";
  class_desc[10].name = "sd_3_2";
  class_desc[11].name = "sd_3_3";
  class_desc[12].name = "sd_4_2";
  class_desc[13].name = "sd_4_1";
  class_desc[14].name = "sd_5_1";
  class_desc[15].name = "sd_5_2";
  class_desc[16].name = "sd_6_1";
  class_desc[17].name = "sd_7_1";
  class_desc[18].name = "sd_8_1";
  class_desc[19].name = "sd_9_1";
  class_desc[20].name = "sd_10_1";
  class_desc[21].name = "sd_11_1";
  class_desc[22].name = "sd_2_4";
  class_desc[23].name = "sd_3_4";
  class_desc[24].name = "sd_4_3";
  class_desc[25].name = "sd_5_3";
  class_desc[26].name = "sd_6_2";
  class_desc[27].name = "sd_7_2";
  class_desc[28].name = "sd_8_2";
  class_desc[29].name = "sd_9_2";
  class_desc[30].name = "sd_10_2";
  class_desc[31].name = "sd_11_2";
  class_desc[32].name = "co_5_2";
  class_desc[33].name = "co_5_1";
  class_desc[34].name = "co_4_3";
  class_desc[35].name = "co_4_4";
  class_desc[36].name = "co_4_2";
  class_desc[37].name = "co_4_5";
  class_desc[38].name = "co_4_6";
  class_desc[39].name = "co_3_2";
  class_desc[40].name = "co_4_7";
  class_desc[41].name = "co_4_8";
  class_desc[42].name = "co_3_1";
  class_desc[43].name = "co_4_1";
  class_desc[44].name = "co_3_3";
  class_desc[45].name = "co_3_4";
  class_desc[46].name = "co_2_2";
  class_desc[47].name = "co_4_9";
  class_desc[48].name = "co_2_1";
  class_desc[49].name = "co_2_3";
  class_desc[50].name = "co_3_5";
  class_desc[51].name = "co_1_2";
  class_desc[52].name = "co_1_1";
  class_desc[53].name = "co_0";
  class_desc[54].name = "iw_0";
  class_desc[55].name = "iw_1_1";
  class_desc[56].name = "iw_1_2";
  class_desc[57].name = "iw_1_3";
  class_desc[58].name = "iw_1_4";
  class_desc[59].name = "iw_2_1";
  class_desc[60].name = "iw_2_2";
  class_desc[61].name = "iw_2_3";
  class_desc[62].name = "iw_2_4";
  class_desc[63].name = "iw_2_5";
  class_desc[64].name = "iw_2_6";
  class_desc[65].name = "iw_2_7";
  class_desc[66].name = "iw_2_8";
  class_desc[67].name = "iw_2_9";
  class_desc[68].name = "iw_2_10";
  class_desc[69].name = "iw_2_11";
  class_desc[70].name = "iw_2_12";
  class_desc[71].name = "iw_2_13";
  class_desc[72].name = "iw_2_14";
  class_desc[73].name = "iw_2_15";
  class_desc[74].name = "iw_2_16";
  class_desc[75].name = "iw_2_17";
  class_desc[76].name = "iw_2_18";
  class_desc[77].name = "iw_2_19";
  class_desc[78].name = "iw_2_20";
  class_desc[79].name = "iw_2_21";
  class_desc[80].name = "iw_3_1";
  class_desc[81].name = "iw_3_2";
  class_desc[82].name = "iw_3_3";
  class_desc[83].name = "iw_4_1";
  class_desc[84].name = "iw_4_2";
  class_desc[85].name = "iw_4_3";
  class_desc[86].name = "iw_4_4";
  class_desc[87].name = "iw_4_5";
  class_desc[88].name = "iw_4_6";
  class_desc[89].name = "";

  class_desc[0].distribution = UNIFORM;
  class_desc[0].num_instances = inst_avg;
  class_desc[0].range_index = range_zero_to_99;
  class_desc[0].set_cardinality = card_tinyset;

  class_desc[1].distribution = UNIFORM;
  class_desc[1].num_instances = inst_small;
  class_desc[1].range_index = range_zero_to_9;
  class_desc[1].set_cardinality = card_tinyset;

  class_desc[2].distribution = UNIFORM;
  class_desc[2].num_instances = inst_avg;
  class_desc[2].range_index = range_zero_to_99;
  class_desc[2].set_cardinality = card_tinyset;

  class_desc[3].distribution = UNIFORM;
  class_desc[3].num_instances = inst_small;
  class_desc[3].range_index = range_zero_to_9;
  class_desc[3].set_cardinality = card_tinyset;

  class_desc[4].distribution = UNIFORM;
  class_desc[4].num_instances = inst_avg;
  class_desc[4].range_index = range_zero_to_99;
  class_desc[4].set_cardinality = card_tinyset;

  class_desc[5].distribution = DEC_EXP;
  class_desc[5].num_instances = inst_avg;
  class_desc[5].range_index = range_zero_to_99;
  class_desc[5].set_cardinality = card_tinyset;

  class_desc[6].distribution = UNIFORM;
  class_desc[6].num_instances = inst_avg;
  class_desc[6].range_index = range_zero_to_99;
  class_desc[6].set_cardinality = card_tinyset;

  class_desc[7].distribution = INC_EXP;
  class_desc[7].num_instances = inst_large;
  class_desc[7].range_index = range_minus_500_to_500;
  class_desc[7].set_cardinality = card_tinyset;

  class_desc[8].distribution = UNIFORM;
  class_desc[8].num_instances = inst_avg;
  class_desc[8].range_index = range_zero_to_9;
  class_desc[8].set_cardinality = card_tinyset;

  class_desc[9].distribution = DEC_EXP;
  class_desc[9].num_instances = inst_large;
  class_desc[9].range_index = range_zero_to_99;
  class_desc[9].set_cardinality = card_tinyset;

  class_desc[10].distribution = UNIFORM;
  class_desc[10].num_instances = inst_avg;
  class_desc[10].range_index = range_zero_to_99;
  class_desc[10].set_cardinality = card_tinyset;

  class_desc[11].distribution = UNIFORM;
  class_desc[11].num_instances = inst_avg;
  class_desc[11].range_index = range_zero_to_99;
  class_desc[11].set_cardinality = card_bigset;

  class_desc[12].distribution = UNIFORM;
  class_desc[12].num_instances = inst_avg;
  class_desc[12].range_index = range_zero_to_99;
  class_desc[12].set_cardinality = card_tinyset;

  class_desc[13].distribution = UNIFORM;
  class_desc[13].num_instances = inst_avg;
  class_desc[13].range_index = range_zero_to_9;
  class_desc[13].set_cardinality = card_tinyset;

  class_desc[14].distribution = UNIFORM;
  class_desc[14].num_instances = inst_large;
  class_desc[14].range_index = range_zero_to_99;
  class_desc[14].set_cardinality = card_tinyset;

  class_desc[15].distribution = DEC_EXP;
  class_desc[15].num_instances = inst_avg;
  class_desc[15].range_index = range_zero_to_99;
  class_desc[15].set_cardinality = card_tinyset;

  class_desc[16].distribution = UNIFORM;
  class_desc[16].num_instances = inst_avg;
  class_desc[16].range_index = range_min_2_max;
  class_desc[16].set_cardinality = card_tinyset;

  class_desc[17].distribution = UNIFORM;
  class_desc[17].num_instances = inst_avg;
  class_desc[17].range_index = range_zero_to_99;
  class_desc[17].set_cardinality = card_tinyset;

  class_desc[18].distribution = INC_EXP;
  class_desc[18].num_instances = inst_large;
  class_desc[18].range_index = range_minus_500_to_500;
  class_desc[18].set_cardinality = card_tinyset;

  class_desc[19].distribution = UNIFORM;
  class_desc[19].num_instances = inst_avg;
  class_desc[19].range_index = range_zero_to_99;
  class_desc[19].set_cardinality = card_tinyset;

  class_desc[20].distribution = INC_EXP;
  class_desc[20].num_instances = inst_avg;
  class_desc[20].range_index = range_zero_to_99;
  class_desc[20].set_cardinality = card_littleset;

  class_desc[21].distribution = UNIFORM;
  class_desc[21].num_instances = inst_avg;
  class_desc[21].range_index = range_zero_to_99;
  class_desc[21].set_cardinality = card_tinyset;

  class_desc[22].distribution = UNIFORM;
  class_desc[22].num_instances = inst_avg;
  class_desc[22].range_index = range_zero_to_99;
  class_desc[22].set_cardinality = card_tinyset;

  class_desc[23].distribution = UNIFORM;
  class_desc[23].num_instances = inst_avg;
  class_desc[23].range_index = range_zero_to_99;
  class_desc[23].set_cardinality = card_smallset;

  class_desc[24].distribution = UNIFORM;
  class_desc[24].num_instances = inst_large;
  class_desc[24].range_index = range_zero_to_9;
  class_desc[24].set_cardinality = card_tinyset;

  class_desc[25].distribution = DEC_EXP;
  class_desc[25].num_instances = inst_avg;
  class_desc[25].range_index = range_zero_to_99;
  class_desc[25].set_cardinality = card_tinyset;

  class_desc[26].distribution = UNIFORM;
  class_desc[26].num_instances = inst_avg;
  class_desc[26].range_index = range_zero_to_99;
  class_desc[26].set_cardinality = card_tinyset;

  class_desc[27].distribution = UNIFORM;
  class_desc[27].num_instances = inst_avg;
  class_desc[27].range_index = range_min_2_max;
  class_desc[27].set_cardinality = card_tinyset;

  class_desc[28].distribution = UNIFORM;
  class_desc[28].num_instances = inst_avg;
  class_desc[28].range_index = range_zero_to_9;
  class_desc[28].set_cardinality = card_tinyset;

  class_desc[29].distribution = UNIFORM;
  class_desc[29].num_instances = inst_avg;
  class_desc[29].range_index = range_zero_to_99;
  class_desc[29].set_cardinality = card_tinyset;

  class_desc[30].distribution = UNIFORM;
  class_desc[30].num_instances = inst_avg;
  class_desc[30].range_index = range_zero_to_99;
  class_desc[30].set_cardinality = card_medset;

  class_desc[31].distribution = UNIFORM;
  class_desc[31].num_instances = inst_xlarge;
  class_desc[31].range_index = range_zero_to_9999;
  class_desc[31].set_cardinality = card_tinyset;

  class_desc[32].distribution = UNIFORM;
  class_desc[32].num_instances = inst_avg;
  class_desc[32].range_index = range_zero_to_9;
  class_desc[32].set_cardinality = card_tinyset;

  class_desc[33].distribution = DEC_EXP;
  class_desc[33].num_instances = inst_xlarge;
  class_desc[33].range_index = range_minus_500_to_500;
  class_desc[33].set_cardinality = card_tinyset;

  class_desc[34].distribution = UNIFORM;
  class_desc[34].num_instances = inst_small;
  class_desc[34].range_index = range_zero_to_9;
  class_desc[34].set_cardinality = card_tinyset;

  class_desc[35].distribution = UNIFORM;
  class_desc[35].num_instances = inst_large;
  class_desc[35].range_index = range_zero_to_9;
  class_desc[35].set_cardinality = card_tinyset;

  class_desc[36].distribution = UNIFORM;
  class_desc[36].num_instances = inst_large;
  class_desc[36].range_index = range_zero_to_99;
  class_desc[36].set_cardinality = card_tinyset;

  class_desc[37].distribution = UNIFORM;
  class_desc[37].num_instances = inst_small;
  class_desc[37].range_index = range_zero_to_9;
  class_desc[37].set_cardinality = card_tinyset;

  class_desc[38].distribution = UNIFORM;
  class_desc[38].num_instances = inst_large;
  class_desc[38].range_index = range_zero_to_99;
  class_desc[38].set_cardinality = card_tinyset;

  class_desc[39].distribution = UNIFORM;
  class_desc[39].num_instances = inst_large;
  class_desc[39].range_index = range_zero_to_99;
  class_desc[39].set_cardinality = card_tinyset;

  class_desc[40].distribution = DEC_EXP;
  class_desc[40].num_instances = inst_large;
  class_desc[40].range_index = range_min_2_max;
  class_desc[40].set_cardinality = card_tinyset;

  class_desc[41].distribution = UNIFORM;
  class_desc[41].num_instances = inst_small;
  class_desc[41].range_index = range_min_2_max;
  class_desc[41].set_cardinality = card_tinyset;

  class_desc[42].distribution = UNIFORM;
  class_desc[42].num_instances = inst_avg;
  class_desc[42].range_index = range_zero_to_9;
  class_desc[42].set_cardinality = card_tinyset;

  class_desc[43].distribution = INC_EXP;
  class_desc[43].num_instances = inst_large;
  class_desc[43].range_index = range_zero_to_99;
  class_desc[43].set_cardinality = card_tinyset;

  class_desc[44].distribution = UNIFORM;
  class_desc[44].num_instances = inst_large;
  class_desc[44].range_index = range_minus_500_to_500;
  class_desc[44].set_cardinality = card_tinyset;

  class_desc[45].distribution = INC_EXP;
  class_desc[45].num_instances = inst_small;
  class_desc[45].range_index = range_zero_to_9;
  class_desc[45].set_cardinality = card_tinyset;

  class_desc[46].distribution = DEC_EXP;
  class_desc[46].num_instances = inst_large;
  class_desc[46].range_index = range_zero_to_99;
  class_desc[46].set_cardinality = card_tinyset;

  class_desc[47].distribution = UNIFORM;
  class_desc[47].num_instances = inst_avg;
  class_desc[47].range_index = range_zero_to_99;
  class_desc[47].set_cardinality = card_tinyset;

  class_desc[48].distribution = UNIFORM;
  class_desc[48].num_instances = inst_small;
  class_desc[48].range_index = range_zero_to_9;
  class_desc[48].set_cardinality = card_tinyset;

  class_desc[49].distribution = UNIFORM;
  class_desc[49].num_instances = inst_avg;
  class_desc[49].range_index = range_zero_to_99;
  class_desc[49].set_cardinality = card_tinyset;

  class_desc[50].distribution = UNIFORM;
  class_desc[50].num_instances = inst_avg;
  class_desc[50].range_index = range_zero_to_99;
  class_desc[50].set_cardinality = card_tinyset;

  class_desc[51].distribution = DEC_EXP;
  class_desc[51].num_instances = inst_large;
  class_desc[51].range_index = range_zero_to_9;
  class_desc[51].set_cardinality = card_tinyset;

  class_desc[52].distribution = UNIFORM;
  class_desc[52].num_instances = inst_large;
  class_desc[52].range_index = range_zero_to_99;
  class_desc[52].set_cardinality = card_tinyset;

  class_desc[53].distribution = INC_EXP;
  class_desc[53].num_instances = inst_large;
  class_desc[53].range_index = range_zero_to_99;
  class_desc[53].set_cardinality = card_tinyset;

  class_desc[54].distribution = INC_EXP;
  class_desc[54].num_instances = inst_avg;
  class_desc[54].range_index = range_min_2_max;
  class_desc[54].set_cardinality = card_tinyset;

  class_desc[55].distribution = UNIFORM;
  class_desc[55].num_instances = inst_avg;
  class_desc[55].range_index = range_min_2_max;
  class_desc[55].set_cardinality = card_tinyset;

  class_desc[56].distribution = UNIFORM;
  class_desc[56].num_instances = inst_large;
  class_desc[56].range_index = range_zero_to_99;
  class_desc[56].set_cardinality = card_tinyset;

  class_desc[57].distribution = DEC_EXP;
  class_desc[57].num_instances = inst_large;
  class_desc[57].range_index = range_zero_to_99;
  class_desc[57].set_cardinality = card_tinyset;

  class_desc[58].distribution = UNIFORM;
  class_desc[58].num_instances = inst_large;
  class_desc[58].range_index = range_zero_to_99;
  class_desc[58].set_cardinality = card_tinyset;

  class_desc[59].distribution = UNIFORM;
  class_desc[59].num_instances = inst_large;
  class_desc[59].range_index = range_minus_500_to_500;
  class_desc[59].set_cardinality = card_tinyset;

  class_desc[60].distribution = INC_EXP;
  class_desc[60].num_instances = inst_xlarge;
  class_desc[60].range_index = range_minus_500_to_500;
  class_desc[60].set_cardinality = card_tinyset;

  class_desc[61].distribution = UNIFORM;
  class_desc[61].num_instances = inst_large;
  class_desc[61].range_index = range_zero_to_9;
  class_desc[61].set_cardinality = card_tinyset;

  class_desc[62].distribution = INC_EXP;
  class_desc[62].num_instances = inst_avg;
  class_desc[62].range_index = range_zero_to_99;
  class_desc[62].set_cardinality = card_tinyset;

  class_desc[63].distribution = UNIFORM;
  class_desc[63].num_instances = inst_xlarge;
  class_desc[63].range_index = range_minus_500_to_500;
  class_desc[63].set_cardinality = card_tinyset;

  class_desc[64].distribution = UNIFORM;
  class_desc[64].num_instances = inst_small;
  class_desc[64].range_index = range_zero_to_9;
  class_desc[64].set_cardinality = card_tinyset;

  class_desc[65].distribution = UNIFORM;
  class_desc[65].num_instances = inst_avg;
  class_desc[65].range_index = range_zero_to_99;
  class_desc[65].set_cardinality = card_tinyset;

  class_desc[66].distribution = UNIFORM;
  class_desc[66].num_instances = inst_small;
  class_desc[66].range_index = range_min_2_max;
  class_desc[66].set_cardinality = card_tinyset;

  class_desc[67].distribution = UNIFORM;
  class_desc[67].num_instances = inst_large;
  class_desc[67].range_index = range_minus_500_to_500;
  class_desc[67].set_cardinality = card_tinyset;

  class_desc[68].distribution = UNIFORM;
  class_desc[68].num_instances = inst_avg;
  class_desc[68].range_index = range_zero_to_9;
  class_desc[68].set_cardinality = card_tinyset;

  class_desc[69].distribution = UNIFORM;
  class_desc[69].num_instances = inst_large;
  class_desc[69].range_index = range_minus_500_to_500;
  class_desc[69].set_cardinality = card_tinyset;

  class_desc[70].distribution = INC_EXP;
  class_desc[70].num_instances = inst_large;
  class_desc[70].range_index = range_zero_to_9;
  class_desc[70].set_cardinality = card_tinyset;

  class_desc[71].distribution = UNIFORM;
  class_desc[71].num_instances = inst_avg;
  class_desc[71].range_index = range_zero_to_9;
  class_desc[71].set_cardinality = card_tinyset;

  class_desc[72].distribution = UNIFORM;
  class_desc[72].num_instances = inst_large;
  class_desc[72].range_index = range_zero_to_99;
  class_desc[72].set_cardinality = card_tinyset;

  class_desc[73].distribution = UNIFORM;
  class_desc[73].num_instances = inst_avg;
  class_desc[73].range_index = range_zero_to_99;
  class_desc[73].set_cardinality = card_tinyset;

  class_desc[74].distribution = INC_EXP;
  class_desc[74].num_instances = inst_huge;
  class_desc[74].range_index = range_zero_to_9999;
  class_desc[74].set_cardinality = card_tinyset;

  class_desc[75].distribution = UNIFORM;
  class_desc[75].num_instances = inst_large;
  class_desc[75].range_index = range_minus_500_to_500;
  class_desc[75].set_cardinality = card_tinyset;

  class_desc[76].distribution = UNIFORM;
  class_desc[76].num_instances = inst_avg;
  class_desc[76].range_index = range_min_2_max;
  class_desc[76].set_cardinality = card_tinyset;

  class_desc[77].distribution = UNIFORM;
  class_desc[77].num_instances = inst_large;
  class_desc[77].range_index = range_minus_500_to_500;
  class_desc[77].set_cardinality = card_tinyset;

  class_desc[78].distribution = UNIFORM;
  class_desc[78].num_instances = inst_avg;
  class_desc[78].range_index = range_zero_to_99;
  class_desc[78].set_cardinality = card_tinyset;

  class_desc[79].distribution = UNIFORM;
  class_desc[79].num_instances = inst_large;
  class_desc[79].range_index = range_zero_to_99;
  class_desc[79].set_cardinality = card_tinyset;

  class_desc[80].distribution = UNIFORM;
  class_desc[80].num_instances = inst_avg;
  class_desc[80].range_index = range_zero_to_9;
  class_desc[80].set_cardinality = card_tinyset;

  class_desc[81].distribution = UNIFORM;
  class_desc[81].num_instances = inst_avg;
  class_desc[81].range_index = range_zero_to_9;
  class_desc[81].set_cardinality = card_tinyset;

  class_desc[82].distribution = UNIFORM;
  class_desc[82].num_instances = inst_avg;
  class_desc[82].range_index = range_zero_to_9;
  class_desc[82].set_cardinality = card_tinyset;

  class_desc[83].distribution = UNIFORM;
  class_desc[83].num_instances = inst_avg;
  class_desc[83].range_index = range_zero_to_9;
  class_desc[83].set_cardinality = card_tinyset;

  class_desc[84].distribution = UNIFORM;
  class_desc[84].num_instances = inst_avg;
  class_desc[84].range_index = range_zero_to_9;
  class_desc[84].set_cardinality = card_tinyset;

  class_desc[85].distribution = UNIFORM;
  class_desc[85].num_instances = inst_avg;
  class_desc[85].range_index = range_zero_to_99;
  class_desc[85].set_cardinality = card_tinyset;

  class_desc[86].distribution = UNIFORM;
  class_desc[86].num_instances = inst_avg;
  class_desc[86].range_index = range_zero_to_99;
  class_desc[86].set_cardinality = card_tinyset;

  class_desc[87].distribution = UNIFORM;
  class_desc[87].num_instances = inst_avg;
  class_desc[87].range_index = range_zero_to_99;
  class_desc[87].set_cardinality = card_tinyset;

  class_desc[88].distribution = UNIFORM;
  class_desc[88].num_instances = inst_avg;
  class_desc[88].range_index = range_min_2_max;
  class_desc[88].set_cardinality = card_tinyset;

  class_desc[89].distribution = UNIFORM;
  class_desc[89].num_instances = 0;
  class_desc[89].range_index = 0;
  class_desc[89].set_cardinality = 0;
}

static void
init_string_table (void)
{
  int i;
  int j;
  int k;

  for (i = 0; i < 5; i++)
    {
      for (j = 0; j < 10; j++)
	{
	  if (!(String_table[i][j] =
		(char *) calloc (1, (3 * (STRING_PART_SIZE * (i + 1)) + 1))))
	    {
	      fprintf (stderr, "Out of memeory!\n");
	      exit (EXIT_FAILURE);
	    }
	  for (k = 0; k <= i * 3; k++)
	    {
	      strcat (String_table[i][j], String_parts[j]);
	    }
	}
    }
  return;
}

static void
init_date_range (void)
{
  DB_MAKE_DATE (&date_range[0], 10, 22, 1757);
  DB_MAKE_DATE (&date_range[1], 6, 6, 1965);
  DB_MAKE_DATE (&date_range[2], 9, 1, 1991);
  DB_MAKE_DATE (&date_range[3], 12, 25, 1995);
  DB_MAKE_DATE (&date_range[4], 1, 1, 2001);

  DB_MAKE_DATE (&udate_range[0], 1, 1, 1970);
  DB_MAKE_DATE (&udate_range[1], 8, 4, 1976);
  DB_MAKE_DATE (&udate_range[2], 3, 31, 1988);
  DB_MAKE_DATE (&udate_range[3], 10, 16, 1989);
  DB_MAKE_DATE (&udate_range[4], 1, 1, 2001);

  DB_MAKE_DATE (&Maxdate, 12, 31, 9999);
  DB_MAKE_DATE (&Mindate, 1, 1, 1);
  DB_MAKE_DATE (&Middate, 7, 15, 4900);	/* ? */

  /* the following are new */

  DB_MAKE_DATE (&Minudate, 1, 1, 1970);
  DB_MAKE_DATE (&Midudate, 6, 1, 1992);	/* ? */
  DB_MAKE_DATE (&Maxudate, 12, 31, 2020);	/* ? */
}

static void
init_time_range (void)
{
  DB_MAKE_TIME (&time_range[0], 0, 0, 0);
  DB_MAKE_TIME (&time_range[1], 3, 30, 0);
  DB_MAKE_TIME (&time_range[2], 6, 45, 30);
  DB_MAKE_TIME (&time_range[3], 12, 0, 15);
  DB_MAKE_TIME (&time_range[4], 18, 15, 15);

  DB_MAKE_TIME (&utime_range[0], 0, 0, 1);
  DB_MAKE_TIME (&utime_range[1], 3, 30, 0);
  DB_MAKE_TIME (&utime_range[2], 6, 45, 30);
  DB_MAKE_TIME (&utime_range[3], 12, 0, 15);
  DB_MAKE_TIME (&utime_range[4], 18, 15, 15);

  DB_MAKE_TIME (&Maxtime, 24, 60, 60);
  DB_MAKE_TIME (&Maxtime, 0, 0, 0);
  DB_MAKE_TIME (&Midtime, 12, 30, 30);	/* ? */
}

static void
init (void)
{
  /*
   * The following max and min values are used to control the generation of 
   * max and min values..avoid plataform dependent generation as much as 
   * possible. For example, INT_MIN in some machines is -2147483647 and in
   * other machines is -2147483648
   */

  if (POP_FLOAT_CAST_INT_MAX > (float) INT_MAX ||
      POP_FLOAT_CAST_INT_MIN < (float) INT_MIN)
    {
      /* 
       * The max or min values are different. We could end up generating
       * a different database that the expected one.
       */
      fprintf (stderr,
	       "\n\nWARNING: We may end up with a different Database\n");
      fprintf (stderr,
	       " INT_MAX (%d) or INT_MIN (%d) are different on this plataform\n",
	       INT_MAX, INT_MIN);
      fprintf (stderr,
	       " than those values (%f %f) on our default production plataform",
	       POP_FLOAT_CAST_INT_MAX, POP_FLOAT_CAST_INT_MIN);
    }
  init_data ();
  init_string_table ();
  init_date_range ();
  init_time_range ();
}

/******************************************************************************
 * populate_class							      *
 *									      *
 * arguments:								      *
 *	index: 								      *
 *									      *
 * returns/side-effects: int 					      *
 *									      *
 * description: 							      *
 *****************************************************************************/

int
populate_class (int index)
{
  DB_OBJECT *class;
  char *nm_ptr;

  nm_ptr = (char *) class_desc[index].name;

  if ((class = db_find_class (nm_ptr)) == NULL)
    {
      fprintf (stderr, " Class = %s was not found...Will not be populated\n"
	       "Error-id = %d\n Error = %s\n",
	       nm_ptr, db_error_code (), db_error_string (3));
      return db_error_code ();
    }

  return (qa_populate_class (class,
			     class_desc[index].num_instances,
			     class_desc[index].distribution,
			     class_desc[index].range_index,
			     class_desc[index].set_cardinality));


}				/* populate_class */

  /**
   ** Function:
   **    print_usage()
   **
   ** Description:
   **    Print a usage message whenever command is invoked wrong.
   **/
void
print_usage (const char *msg)
{
  fprintf (stdout, "Incorrect usage: %s\n", msg);
  fprintf (stdout,
	   "Usage:  populate [-p passwd] [-u user] [-s classes] [-d db_name] [-t {big|med|small}] [-S]\n");
}


int
main (int argc, char *argv[])
{
  int i;
  void qa_create_composition_hierarchy ();
  int j;
  int c;
  extern char *optarg;
  extern int optind;
  int errcode;

  Myname = argv[0];

  if (argc < 2)
    {
      print_usage ("No data base name.");
      exit (EXIT_FAILURE);
    }

  while ((c = getopt (argc, argv, "p:u:s:d:t:S")) != EOF)
    {
      switch (c)
	{
	case 'u':
	  {
	    User = optarg;
	    break;
	  }
	case 'p':
	  {
	    Password = optarg;
	    break;
	  }
	case 's':
	  {
	    strcat (Do, optarg);
	    break;
	  }
	case 'd':
	  {
	    strncpy (Mydb_name, optarg, MYDB_LEN);
	    break;
	  }
	case 't':
	  {
	    switch (optarg[0])
	      {
	      case 'b':
		Popdb_type = BIGTEST;
		break;
	      case 'm':
		Popdb_type = MEDTEST;
		break;
	      default:
	      case 's':
		Popdb_type = SMALLTEST;
		break;
	      }
	    break;
	  }
	case 'S':
	  {
       /** Due to legacy behavior and time, sets are not really populated by
	** default.  To have the set attributes filled in with non NULL
	** sets, turn this option on.
	**/
	    printf ("** OPTION: SETS will be populated\n");
	    ProcessSets = TRUE;
	    break;
	  }
	default:
	  {
	    char mbuf[1000];

	    sprintf (mbuf, "Unknown option: %c", c);
	    print_usage (mbuf);
	    exit (EXIT_FAILURE);
	    break;
	  }
	}
    }

  if (optind != argc)
    {
      print_usage ("Too many arguments.");
      exit (EXIT_FAILURE);
    }

  if (User == NULL)
    User = (char *) "public";
  if (Do[0] == '\0')
    sprintf (Do, "sdiwco");

  db_login (User, Password);
  if ((errcode = db_restart (argv[0], TRUE, Mydb_name)))
    {
      fprintf (stderr, "Can't start %s, error: %s\n", Mydb_name,
	       db_error_string (3));
      exit (EXIT_FAILURE);
    }

  init ();

  for (i = 0; i < TOTAL_CLASSES; i++)
    {
      for (j = 0; j <= 4; j += 2)
	{
	  if (!(memcmp ((char *) (class_desc[i].name), &(Do[j]), 2)))
	    {
	      populate_class (i);
	      continue;
	    }
	}
      /*
       * Help the workspace by shutting down after every class population 
       */

      db_shutdown ();
      if ((errcode = db_restart (argv[0], TRUE, Mydb_name)))
	{
	  fprintf (stderr, "Can't restart %s, error: %s\n", Mydb_name,
		   db_error_string (3));
	  exit (EXIT_FAILURE);
	}

    }

  qa_create_composition_hierarchy ();
  db_commit_transaction ();
  db_shutdown ();
  return (0);
}
