/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * cm_defines.h - 
 */

#ifndef _CM_DEFINES_H_
#define _CM_DEFINES_H_

#define CUBRID_DATABASE_TXT       "databases.txt"

#define CUBRID_ENV "CUBRID"
#define CUBRID_DATABASES_ENV "CUBRID_DATABASES"
#define CUBRID_LANG_ENV "CUBRID_LANG"



#define FREE_MEM(PTR)		\
	do {			\
	  if (PTR) {		\
	    free(PTR);		\
	    PTR = 0;	\
	  }			\
	} while (0)

#define REALLOC(PTR, SIZE)	\
	(PTR == NULL) ? malloc(SIZE) : realloc(PTR, SIZE)


#endif /* _CM_DEFINES_H_ */
