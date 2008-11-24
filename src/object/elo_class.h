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
 * elo_class.h - External interface for ELO objects
 *
 */

#ifndef _ELO_CLASS_H_
#define _ELO_CLASS_H_

#ident "$Id$"

#include <stdio.h>
#include "object_representation.h"

#define END_OF_ELO (-1)

#if !defined(SEEK_SET)
#define SEEK_SET       (0)
#define SEEK_CUR       (1)
#define SEEK_END       (2)
#endif

#define ELO_READ_ONLY (0)
#define ELO_READ_WRITE (1)
#define ELO_MODE_ERROR (2)

#define ELO_OPEN_READ_ONLY      ("r")
#define ELO_OPEN_WRITE_ONLY     ("w")
#define ELO_OPEN_APPEND         ("a")
#define ELO_OPEN_UPDATE         ("r+")
#define ELO_OPEN_WRITE_UPDATE   ("w+")
#define ELO_OPEN_APPEND_UPDATE  ("a+")

/*
 * ELO_STREAM - A wrapper around ELO objects to give them a stream interface.
 */

typedef struct elo_stream ELO_STREAM;
typedef struct elo_stream DB_STREAM;
typedef struct elo_stream DB_ELO_STREAM;

struct elo_stream
{
  char *buffer;			/* buffered data */
  char mode;			/* ELO_READ_ONLY or READ_READ_WRITE */
  long offset;			/* offset within the elo stream */
  int buffer_pos;		/* position within the data buffer */
  int buffer_size;		/* size of the data buffer */
  int bytes_in_buffer;		/* amount of "good" data in buffer */
  bool buffer_current;		/* is the buffer data up to date */
  bool buffer_modified;		/* has the buffer been dirtied */
  DB_OBJECT *glo;		/* contains the Glo object */
  ELO *elo;			/* pointer to actual elo */
};

/* 
 * elo interface functions
 */

/* Constructors */
extern DB_ELO *elo_copy (DB_ELO * src);
extern DB_ELO *elo_create (const char *pathname);
extern DB_ELO *elo_destroy (DB_ELO * elo, DB_OBJECT * glo);
extern DB_ELO *elo_new_elo (void);
extern DB_ELO *elo_free (DB_ELO * elo);
extern const char *elo_get_pathname (DB_ELO * elo);
extern DB_ELO *elo_set_pathname (DB_ELO * elo, const char *pathname);

/* Primary interface */
extern int elo_read_from (DB_ELO * elo, const long offset, const int length,
			  char *buffer, DB_OBJECT * glo);
extern int elo_insert_into (DB_ELO * elo, long offset, int length,
			    char *buffer, DB_OBJECT * glo);
extern long elo_get_size (DB_ELO * elo, DB_OBJECT * glo);
extern long elo_delete_from (DB_ELO * elo, long offset, int length,
			     DB_OBJECT * glo);
extern int elo_append_to (DB_ELO * elo, int length, char *buffer,
			  DB_OBJECT * glo);
extern long elo_truncate (DB_ELO * elo, long offset, DB_OBJECT * glo);
extern int elo_write_to (DB_ELO * elo, long offset, int length, char *buffer,
			 DB_OBJECT * glo);
extern int elo_compress (DB_ELO * elo);

/* Stream interface */

extern ELO_STREAM *elo_open (DB_OBJECT * glo, const char *mode);
extern char *elo_gets (char *s, int n, ELO_STREAM * elo_stream);
extern int elo_close (ELO_STREAM * elo_stream);
extern int elo_puts (const char *s, ELO_STREAM * elo_stream);
extern int elo_seek (ELO_STREAM * elo_stream, long offset, int origin);
extern int elo_putc (int c, ELO_STREAM * elo_stream);
extern int elo_getc (ELO_STREAM * elo_stream);
extern int elo_setvbuf (ELO_STREAM * elo_stream, char *buf, int buf_size);

#endif /* _ELO_CLASS_H_ */
