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
 * parser_support.h - Query processor memory management module
 */

#ifndef _PARSER_SUPPORT_H_
#define _PARSER_SUPPORT_H_

#ident "$Id$"

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */

#include "config.h"

extern int qp_Packing_er_code;

/* Memory Buffer Related Routines */
extern char *pt_alloc_packing_buf (int size);
extern void pt_final_packing_buf (void);
extern void pt_enter_packing_buf (void);
extern void pt_exit_packing_buf (void);

extern void regu_set_error_with_zero_args (int err_type);

#endif /* _PARSER_SUPPORT_H_ */
