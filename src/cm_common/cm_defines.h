/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
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
