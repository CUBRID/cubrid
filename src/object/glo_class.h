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
 * glo_class.h - Attribute names for the Glo class
 *
 */

#ifndef _GLO_CLASS_H_
#define _GLO_CLASS_H_

#ident "$Id$"

#include "dbi.h"
/* Class names */

#define GLO_CLASS_NAME                    "glo"

/* Glo attribute names */

#define GLO_CLASS_HOLDER_NAME             "holder_obj"
#define GLO_CLASS_UNIT_SIZE_NAME          "unit_size"
#define GLO_CLASS_HEADER_SIZE_NAME        "header_size"

/* Glo instance method names */

#define GLO_METHOD_READ                   "read_data"
#define GLO_METHOD_PRINT_READ             "print_data"
#define GLO_METHOD_WRITE                  "write_data"
#define GLO_METHOD_SEEK                   "data_seek"
#define GLO_METHOD_INSERT                 "insert_data"
#define GLO_METHOD_DELETE                 "delete_data"
#define GLO_METHOD_PATHNAME               "glo_pathname"
#define GLO_METHOD_FULL_PATHNAME          "glo_full_pathname"
#define GLO_METHOD_TRUNCATE               "truncate_data"
#define GLO_METHOD_APPEND                 "append_data"
#define GLO_METHOD_SIZE                   "data_size"
#define GLO_METHOD_COMPRESS               "compress_data"
#define GLO_METHOD_DESTROY                "destroy_data"
#define GLO_METHOD_INITIALIZE             "initialize_data"
#define GLO_METHOD_COPY_TO                "copy_to"
#define GLO_METHOD_COPY_FROM              "copy_from"
#define GLO_METHOD_POSITION               "data_pos"
#define GLO_METHOD_LIKE_SEARCH            "like_search"
#define GLO_METHOD_REG_SEARCH             "reg_search"
#define GLO_METHOD_BINARY_SEARCH          "binary_search"
#define GLO_METHOD_GET_ERROR              "get_error"
#define GLO_METHOD_SET_ERROR              "set_error"

/* Glo class method names */
#define GLO_CLASS_METHOD_NEW "new"	/* for backwards compatibility */

/* These will become the official class methods used to create Glos */
#define GLO_CLASS_METHOD_NEW_LO           "new_lo"
#define GLO_CLASS_METHOD_NEW_LO_IMPORT    "new_lo_import"
#define GLO_CLASS_METHOD_NEW_FBO          "new_fbo"

/* X11Bitmap attribute names */
/* none for now */

#define SIZE_METHOD  "data_size"


/* These are the "official" errors returned from the multimedia methods. */
/* These errors are returned from the get_error method (defined for the  */
/* glo class and the agent class                                         */

/* These are the Glo errors */

#define INVALID_STRING_INPUT_ARGUMENT  -2	/* bad input argument */
#define INVALID_INTEGER_INPUT_ARGUMENT -3	/* bad input argument */
#define INVALID_STRING_OR_OBJ_ARGUMENT -4	/* bad input argument */
#define INVALID_OBJECT_INPUT_ARGUMENT  -5	/* bad input argument */
#define UNABLE_TO_FIND_GLO_STRUCTURE   -6	/* internal error     */
#define COULD_NOT_ACQUIRE_WRITE_LOCK   -7	/* object already locked */
#define ERROR_DURING_TRUNCATION        -8	/* truncation error   */
#define ERROR_DURING_DELETE            -9	/* delete error       */
#define ERROR_DURING_INSERT           -10	/* insert error       */
#define ERROR_DURING_WRITE            -11	/* write error        */
#define ERROR_DURING_READ             -12	/* read error         */
#define ERROR_DURING_SEEK             -13	/* seek error         */
#define ERROR_DURING_APPEND           -14	/* append error       */
#define ERROR_DURING_MIGRATE          -15	/* migrate error      */
#define COPY_TO_ERROR                 -16	/* copy to error      */
#define COPY_FROM_ERROR               -17	/* copy from error    */
#define COULD_NOT_ALLOCATE_SEARCH_BUFFERS    -18	/* search error       */
#define COULD_NOT_COMPILE_REGULAR_EXPRESSION -19	/* reg exp error      */
#define COULD_NOT_RESET_WORKING_BUFFER       -20	/* search error       */
#define SEARCH_ERROR_ON_POSITION_CACHE       -21	/* get current pos    */
#define SEARCH_ERROR_ON_DATA_READ     -22	/* error reading data */
#define SEARCH_ERROR_DURING_LOOKUP    -23	/* error searching    */
#define SEARCH_ERROR_REPOSITIONING_POINTER   -24	/* reposition error   */

/* These are the Agent errors */

#define AGENT_GET_VALUE_SET          -100	/* finding value set  */
#define AGENT_ADDING_TO_VALUE_SET    -101	/* adding a value     */
#define AGENT_REPLACING_VALUE        -102	/* replacing a value  */
#define NO_ACTION_STRING_DEFINED     -103	/* agent execution    */
#define AGENT_NOT_FOUND_FOR_KILL     -104	/* unknown agent pid  */
#define AUDIO_AGENT_LACKS_AUDIO_OBJ  -105	/* agent w/o auido obj */

/*
 * IS_STRING() - Macro to determine if a dbvalue is a character strign type
 *
 */

#define IS_STRING(n)     (db_value_type(n) == DB_TYPE_VARCHAR  || \
                          db_value_type(n) == DB_TYPE_CHAR     || \
                          db_value_type(n) == DB_TYPE_VARNCHAR || \
                          db_value_type(n) == DB_TYPE_NCHAR)


extern void esm_set_error (const int error);
extern void esm_Glo_read (DB_OBJECT * esm_glo_object_p,
			  DB_VALUE * return_argument_p,
			  const DB_VALUE * units_p,
			  const DB_VALUE * data_buffer_p);
extern void esm_Glo_print_read (DB_OBJECT * esm_glo_object_p,
				DB_VALUE * return_argument_p,
				DB_VALUE * argument_length);
extern void esm_Glo_write (DB_OBJECT * esm_glo_object_p,
			   DB_VALUE * return_argument_p, DB_VALUE * unit_p,
			   DB_VALUE * data_buffer_p);
extern void esm_Glo_insert (DB_OBJECT * esm_glo_object_p,
			    DB_VALUE * return_argument_p, DB_VALUE * unit_p,
			    DB_VALUE * data_buffer_p);
extern void esm_Glo_delete (DB_OBJECT * esm_glo_object_p,
			    DB_VALUE * return_argument_p, DB_VALUE * unit_p);
extern void esm_Glo_seek (DB_OBJECT * esm_glo_object_p,
			  DB_VALUE * return_argument_p,
			  DB_VALUE * location_p);
extern void esm_Glo_truncate (DB_OBJECT * esm_glo_object_p,
			      DB_VALUE * return_argument_p);
extern void esm_Glo_append (DB_OBJECT * esm_glo_object_p,
			    DB_VALUE * return_argument_p, DB_VALUE * unit_p,
			    DB_VALUE * data_buffer_p);
extern void esm_Glo_pathname (DB_OBJECT * esm_glo_object_p,
			      DB_VALUE * return_argument_p);
extern void esm_Glo_full_pathname (DB_OBJECT * esm_glo_object_p,
				   DB_VALUE * return_argument_p);
extern void esm_Glo_init (DB_OBJECT * esm_glo_object_p,
			  DB_VALUE * return_argument_p);
extern void esm_Glo_size (DB_OBJECT * esm_glo_object_p,
			  DB_VALUE * return_argument_p);
extern void esm_Glo_compress (DB_OBJECT * esm_glo_object_p,
			      DB_VALUE * return_argument_p);
extern void esm_Glo_create (DB_OBJECT * esm_glo_class,
			    DB_VALUE * return_argument_p,
			    DB_VALUE * path_name_p);
extern void esm_Glo_destroy (DB_OBJECT * esm_glo_object_p,
			     DB_VALUE * return_argument_p);
extern void esm_Glo_copy_to (DB_OBJECT * esm_glo_object_p,
			     DB_VALUE * return_argument_p,
			     DB_VALUE * destination_p);
extern void esm_Glo_copy_from (DB_OBJECT * esm_glo_object_p,
			       DB_VALUE * return_argument_p,
			       DB_VALUE * source_p);
extern void esm_Glo_position (DB_OBJECT * esm_glo_object_p,
			      DB_VALUE * return_argument_p);
extern void esm_Glo_like_search (DB_OBJECT * esm_glo_object_p,
				 DB_VALUE * return_argument_p,
				 DB_VALUE * search_for_object_p);
extern void esm_Glo_reg_search (DB_OBJECT * esm_glo_object_p,
				DB_VALUE * return_argument_p,
				DB_VALUE * search_for_object_p);
extern void esm_Glo_binary_search (DB_OBJECT * esm_glo_object_p,
				   DB_VALUE * return_argument_p,
				   DB_VALUE * search_for_object_p,
				   DB_VALUE * search_length_p);
extern void esm_Glo_set_error (DB_OBJECT * esm_glo_object_p,
			       DB_VALUE * return_argument_p,
			       DB_VALUE * error_value_p);
extern void esm_Glo_get_error (DB_OBJECT * esm_glo_object_p,
			       DB_VALUE * return_argument_p);
extern void esm_Glo_create_lo (DB_OBJECT * esm_glo_class_p,
			       DB_VALUE * return_argument_p);
extern void esm_Glo_import_lo (DB_OBJECT * esm_glo_class_p,
			       DB_VALUE * return_argument_p,
			       DB_VALUE * path_name_p);
extern void esm_Glo_create_fbo (DB_OBJECT * esm_glo_class_p,
				DB_VALUE * return_argument_p,
				DB_VALUE * path_name_p);

extern void esm_load_esm_classes (void);

extern void
def_instance_signature (DB_OBJECT * class_obj, const char *method_name,
			const char *return_domain, const char *arg1,
			const char *arg2, const char *arg3,
			const char *arg4, const char *arg5,
			const char *arg6, const char *arg7, const char *arg8);

extern void
def_class_signature (DB_OBJECT * class_obj, const char *method_name,
		     const char *return_domain, const char *arg1,
		     const char *arg2, const char *arg3,
		     const char *arg4, const char *arg5,
		     const char *arg6, const char *arg7, const char *arg8);

extern void esm_define_esm_classes (void);
extern void esm_add_method (char *class_name, char *method_name,
			    char *implementation_name);

extern DB_OBJECT *esm_find_holder_object (const char *pathname);
extern int esm_find_glo_count (DB_OBJECT * holder_p, int *object_count);

/* common function */
extern DB_ELO *esm_get_glo_from_holder_for_read (DB_OBJECT * glo);
extern DB_ELO *esm_get_glo_from_holder_for_write (DB_OBJECT * glo);

#endif /* _GLO_CLASS_H_ */
