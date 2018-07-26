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
 * loader_cl.h: Loader client definitions. Updated using design from fast loaddb prototype
 */

#ifndef _LOADER_CL_H_
#define _LOADER_CL_H_

#ident "$Id$"

//#include <stdint.h>

#include "common.hpp"
#include "dbdef.h"
#include "porting.h"

#define NUM_LDR_TYPES (LDR_TYPE_MAX + 1)

using namespace cubload;

/* *INDENT-OFF* */
class client_loader : public loader
{
  public:
    void act_setup_class_command_spec (string_t ** class_name, class_cmd_spec_t ** cmd_spec) override;
    void act_start_id (char *name) override;
    void act_set_id (int id) override;
    void act_start_instance (int id, constant_t * cons) override;
    void process_constants (constant_t * cons) override;
    void act_finish_line () override;
    void act_finish () override;

    void load_failed_error () override;
    void increment_err_total () override;
    void increment_fails () override;
};
/* *INDENT-ON* */

/* Type aliases */
typedef struct LDR_CONTEXT LDR_CONTEXT;

typedef void (*LDR_POST_COMMIT_HANDLER) (int);
typedef void (*LDR_POST_INTERRUPT_HANDLER) (int);

/* Global variables */
extern char **ignore_class_list;
extern int ignore_class_num;
extern bool skip_current_class;
extern bool skip_current_instance;

extern LDR_CONTEXT *ldr_Current_context;

/* Functions */
/* Loader initialization and shutdown functions */
extern int ldr_init (bool verbose);
extern int ldr_start (int periodic_commit);
extern int ldr_final (void);
extern int ldr_finish (LDR_CONTEXT * context, int err);

/* Action to initialize the parser context to deal with a new class */
extern void ldr_act_init_context (LDR_CONTEXT * context, const char *class_name, int len);

extern int ldr_init_class_spec (const char *class_name);
/*
 * Action to deal with instance attributes, arguments for
 * constructors and set elements
 * ldr_act is set to appropriate function depending on the context.
 */
extern void (*ldr_act) (LDR_CONTEXT * context, const char *str, int len, LDR_TYPE type);

extern void ldr_act_attr (LDR_CONTEXT * context, const char *str, int len, LDR_TYPE type);

/* Action to deal with attribute names and argument names */
extern int ldr_act_check_missing_non_null_attrs (LDR_CONTEXT * context);

extern void ldr_act_add_attr (LDR_CONTEXT * context, const char *str, int len);

/* Actions for object references */
extern void ldr_act_set_ref_class_id (LDR_CONTEXT * context, int id);
extern void ldr_act_set_ref_class (LDR_CONTEXT * context, char *name);
extern void ldr_act_set_instance_id (LDR_CONTEXT * context, int id);
extern DB_OBJECT *ldr_act_get_ref_class (LDR_CONTEXT * context);

/* Special action for class, shared, default attributes */
extern void ldr_act_restrict_attributes (LDR_CONTEXT * context, LDR_ATTRIBUTE_TYPE type);

/* Actions for constructor syntax */
extern int ldr_act_set_constructor (LDR_CONTEXT * context, const char *name);
extern int ldr_act_add_argument (LDR_CONTEXT * context, const char *name);

/* Statistics updating/retrieving functions */
extern void ldr_stats (int *errors, int *objects, int *defaults, int *lastcommit, int *fails);
extern int ldr_update_statistics (void);
#if defined (ENABLE_UNUSED_FUNCTION)
extern void print_parser_lineno (FILE * fp);
#endif

/* Callback functions  */
extern void ldr_register_post_commit_handler (LDR_POST_COMMIT_HANDLER handler, void *arg);
extern void ldr_register_post_interrupt_handler (LDR_POST_INTERRUPT_HANDLER handler, void *ldr_jmp_buf);
extern void ldr_interrupt_has_occurred (int type);

extern void ldr_act_set_skip_current_class (char *classname, size_t size);
extern bool ldr_is_ignore_class (const char *classname, size_t size);

/* log functions */
extern void print_log_msg (int verbose, const char *fmt, ...);

#endif /* _LOADER_CL_H_ */
