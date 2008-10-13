/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * evnthand.c
 */

#include <stdio.h>
#include <string.h>

/* Functions within this module */

int getEventMessage (void);
void outputMessage (FILE * fp);

/* Local defines */

#define LOGFILENAME "evnthand.log"
#define NUM_OF_FIELDS  6
#define MAX_LEN_FIELD 256

/* Global constants */

const char fieldSeparator = 0;
const char messageSeparator[] = "\0\n";


char fields[NUM_OF_FIELDS][MAX_LEN_FIELD];
const char *errorName = (char *) fields[0];	/* Generic Error Name.            */
const char *errorId = (char *) fields[1];	/* Event/Error id number.         */
const char *errorMesg = (char *) fields[2];	/* Standard message for error id. */
const char *dbName = (char *) fields[3];	/* Server's Database              */
const char *hostName = (char *) fields[4];	/* Host were Server resides.      */
const char *userInfo = (char *) fields[5];	/* User supplied information      */


/******************************************************************************
 * main                                                                       *
 *                                                                            *
 * description: This module is the main calling routine for the event_handler *
 *****************************************************************************/

int
main (int argc, char *argv[])
{
  FILE *logFile_fp;
  int field_num = 0;

  if (argc > 1)
    {
      fprintf (stderr,
	       "Usage : %s. (NOTE should be invoked by CUBRID server)\n",
	       argv[0]);
      exit (1);
    }

  if ((logFile_fp = fopen (LOGFILENAME, "a")) == NULL)
    {
      fprintf (stderr, "Cannot open logfile %s\n", LOGFILENAME);
      exit (1);
    }

  fprintf (logFile_fp, "CUBRID server has started\n");
  fflush (logFile_fp);

  /* read stdin until EOF */
  while (1)
    {
      /* Clear field array */
      for (field_num = 0; field_num < NUM_OF_FIELDS; field_num++)
	fields[field_num][0] = '\0';

      /* Get message and break up into fields */
      if (getEventMessage () == EOF)
	break;

      outputMessage (logFile_fp);
    }

  /* create special server stopped message */

  sprintf (fields[0], "ER_EV_SERVER_STOPPED");
  sprintf (fields[1], "%d", 0);
  sprintf (fields[2], "CUBRID Server has terminated");
  fields[5][0] = '\0';

  outputMessage (logFile_fp);

  fclose (logFile_fp);

  exit (0);
}				/* main */

/* getEventMessage : Reads in stdin and parses messages read.
 *                 : Each message should contain 6 fields separated by nul
 *                 : A nul followed by a CR indicates a new message.
 *                 : Message are placed in global array, fields[][].
 *                 : When end of message is found the function returns, and the
 *                 : array fields contains any messages acquired.
 *                 : When EOF and no messages is read, EOF is returned.
 */
int
getEventMessage (void)
{
  int currentField = 0;
  int currentPosition = 0;
  int currentChar = 0;

  currentChar = getc (stdin);

  while (currentChar != EOF)
    {
      if (currentChar == fieldSeparator)
	{
	  fields[currentField][currentPosition] = '\0';
	  currentPosition = 0;
	  currentField++;
	  currentChar = getc (stdin);
	  if (currentChar == messageSeparator[1])
	    break;
	}
      else
	{
	  if (currentField < NUM_OF_FIELDS && currentPosition < MAX_LEN_FIELD)
	    fields[currentField][currentPosition] = currentChar;
	  currentPosition++;
	  currentChar = getc (stdin);
	}
    }
  if (currentChar == EOF && currentField == 0 && currentPosition == 0)
    return (EOF);
  else
    return (1);
}				/* getEventMessage() */

/* outputMessage : Outputs a formated event message, using global variables,
 *               : pointing to the fields array.
 */

void
outputMessage (FILE * fp)
{
  static int message_num = 0;

  message_num++;

  fprintf (fp, "----------------< Message %d >----------------\n",
	   message_num);

  /* output fields to log file */
  fprintf (fp, "Error name      = '%s'\n", errorName);
  fprintf (fp, "Error id        = '%s'\n", errorId);
  fprintf (fp, "Error message   = '%s'\n", errorMesg);
  fprintf (fp, "Database name   = '%s'\n", dbName);
  fprintf (fp, "Host     name   = '%s'\n", hostName);
  fprintf (fp, "User Info       = '%s'\n", userInfo);
  fflush (fp);

}				/* outputMessage() */
