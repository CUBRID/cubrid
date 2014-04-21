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
 * dynamic_array.h
 */

#ifndef DYNAMIC_ARRAY_H_
#define DYNAMIC_ARRAY_H_

typedef struct dynamic_array_t dynamic_array;
struct dynamic_array_t
{
  int count;
  int len;
  int max;
  unsigned char *array;
};

extern dynamic_array *da_create (int count, size_t len);
extern int da_add (dynamic_array * da, void *data);
extern int da_put (dynamic_array * da, int pos, void *data);
extern int da_get (dynamic_array * da, int pos, void *data);
extern int da_size (dynamic_array * da);
extern int da_destroy (dynamic_array * da);

#endif /* DYNAMIC_ARRAY_H_ */
