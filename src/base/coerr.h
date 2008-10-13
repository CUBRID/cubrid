/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * coerr.h : CO error condition codes and formats
 *
 */

#ifndef _COERR_H_
#define _COERR_H_

#ident "$Id$"

#include "cocode.h"

#define CO_ERR_BAD_FORMAT             CO_CODE(CO_MODULE_CO, 1)
#define CO_ER_FMT_BAD_FORMAT            \
   "Condition signalled with unknown parameter type `%s'."

#define CO_ERR_CATALOG_NAME_EXISTS    CO_CODE(CO_MODULE_CO, 2)
#define CO_ER_FMT_CATALOG_NAME_EXISTS   \
   "Can't register catalog `%s' with module %d -- already registered with %d."

#define CO_ERR_CATALOG_CODE_EXISTS    CO_CODE(CO_MODULE_CO, 3)
#define CO_ER_FMT_CATALOG_CODE_EXISTS   \
   "Can't register module %d with catalog `%s' -- already registered with `%s'."

#define CO_ERR_BAD_MODULE             CO_CODE(CO_MODULE_CO, 4)
#define CO_ER_FMT_BAD_MODULE            \
   "Invalid module identifier %d -- must be non-negative and less than %d."

#define CO_ERR_BAD_CODE               CO_CODE(CO_MODULE_CO, 5)
#define CO_ER_FMT_BAD_CODE              \
   "Invalid condition code %d -- must be less than zero."

#define CO_ERR_BAD_DETAIL             CO_CODE(CO_MODULE_CO, 6)
#define CO_ER_FMT_BAD_DETAIL            \
   "Unknown detail level %d."

#define CO_ERR_BAD_POSITION           CO_CODE(CO_MODULE_CO, 7)
#define CO_ER_FMT_BAD_POSITION          \
   "Default message format cannot contain a position specifier."

#endif /* _COERR_H_ */
