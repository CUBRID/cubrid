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
 * load_client_loader.h: Loader client definitions. Updated using design from fast loaddb prototype
 */

#ifndef _LOAD_CLIENT_LOADER_H_
#define _LOAD_CLIENT_LOADER_H_

#ident "$Id$"

#include "dbdef.h"
#include "load_common.hpp"
#include "porting.h"

#define NUM_LDR_TYPES (LDR_TYPE_MAX + 1)

/* *INDENT-OFF* */
namespace cubload
{

  class client_loader : public loader
  {
    public:
      void check_class (const char *class_name, int class_id) override;
      int setup_class (const char *class_name) override;
      void setup_class (string_type *class_name, class_command_spec_type *cmd_spec) override;
      void destroy () override;

      void start_line (int object_id) override;
      void process_line (constant_type *cons) override;
      void finish_line () override;

      void load_failed_error () override;
      void increment_err_total () override;
      void increment_fails () override;
  };
}
/* *INDENT-ON* */

/* Type aliases */
typedef void (*LDR_POST_COMMIT_HANDLER) (int);
typedef void (*LDR_POST_INTERRUPT_HANDLER) (int);

/* Global variables */
extern char **ignore_class_list;
extern int ignore_class_num;

/* Functions */
/* Loader initialization and shutdown functions */
extern int ldr_init (bool verbose);
extern int ldr_start (int periodic_commit);
extern int ldr_final (void);

extern int ldr_init_class_spec (const char *class_name);

/* Statistics updating/retrieving functions */
extern void ldr_stats (int *errors, int *objects, int *defaults, int *lastcommit, int *fails);
extern int ldr_update_statistics (void);

/* Callback functions  */
extern void ldr_register_post_commit_handler (LDR_POST_COMMIT_HANDLER handler, void *arg);
extern void ldr_register_post_interrupt_handler (LDR_POST_INTERRUPT_HANDLER handler, void *ldr_jmp_buf);
extern void ldr_interrupt_has_occurred (int type);

/* log functions */
extern void print_log_msg (int verbose, const char *fmt, ...);

#endif /* _LOAD_CLIENT_LOADER_H_ */
