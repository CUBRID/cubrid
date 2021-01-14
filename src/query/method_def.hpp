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

//
// method_def.hpp - define structures used by method feature
//

#ifndef _METHOD_DEF_H_
#define _METHOD_DEF_H_

typedef enum
{
  METHOD_SUCCESS = 1,
  METHOD_EOF,
  METHOD_ERROR
} METHOD_CALL_STATUS;

typedef enum
{
  VACOMM_BUFFER_SEND = 1,
  VACOMM_BUFFER_ABORT
} VACOMM_BUFFER_CLIENT_ACTION;

#define VACOMM_BUFFER_HEADER_SIZE           (OR_INT_SIZE * 3)
#define VACOMM_BUFFER_HEADER_LENGTH_OFFSET  (0)
#define VACOMM_BUFFER_HEADER_STATUS_OFFSET  (OR_INT_SIZE)
#define VACOMM_BUFFER_HEADER_NO_VALS_OFFSET (OR_INT_SIZE * 2)
#define VACOMM_BUFFER_HEADER_ERROR_OFFSET   (OR_INT_SIZE * 2)

typedef enum
{
  METHOD_IS_NONE = 0,
  METHOD_IS_INSTANCE_METHOD = 1,
  METHOD_IS_CLASS_METHOD
} METHOD_TYPE;

typedef struct method_sig_node METHOD_SIG;
struct method_sig_node
{
  /* method signature */
  METHOD_SIG *next;
  char *method_name;		/* method name */
  char *class_name;		/* class for the method */
  METHOD_TYPE method_type;	/* instance or class method */
  int num_method_args;		/* number of arguments */
  int *method_arg_pos;		/* arg position in list file */

  method_sig_node () = default;
};

struct method_sig_list
{
  /* signature for methods */
  METHOD_SIG *method_sig;	/* one method signature */
  int num_methods;		/* number of signatures */

  method_sig_list () = default;
};
typedef struct method_sig_list METHOD_SIG_LIST;

#endif // _METHOD_DEF_H_
