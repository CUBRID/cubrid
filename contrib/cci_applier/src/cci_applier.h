#ifndef CCI_APPLIER_H_
#define CCI_APPLIER_H_

#ident "$Id$"

#include "error_code.h"

#define ER_CA_FAILED              -1
#define ER_CA_FILE_IO             -2
#define ER_CA_FAILED_TO_ALLOC     -3
#define ER_CA_DISCREPANT_INFO     -4

#define LA_STOP_ON_ERROR(error) \
  ((error == ER_NET_CANT_CONNECT_SERVER || \
    error == ER_OBJ_NO_CONNECT))

#define LA_RETRY_ON_ERROR(error) \
  ((error == ER_LK_UNILATERALLY_ABORTED)              || \
   (error == ER_LK_OBJECT_TIMEOUT_SIMPLE_MSG)         || \
   (error == ER_LK_OBJECT_TIMEOUT_CLASS_MSG)          || \
   (error == ER_LK_OBJECT_TIMEOUT_CLASSOF_MSG)        || \
   (error == ER_LK_PAGE_TIMEOUT)                      || \
   (error == ER_PAGE_LATCH_TIMEDOUT)                  || \
   (error == ER_PAGE_LATCH_ABORTED)                   || \
   (error == ER_LK_OBJECT_DL_TIMEOUT_SIMPLE_MSG)      || \
   (error == ER_LK_OBJECT_DL_TIMEOUT_CLASS_MSG)       || \
   (error == ER_LK_OBJECT_DL_TIMEOUT_CLASSOF_MSG)     || \
   (error == ER_LK_DEADLOCK_CYCLE_DETECTED))
#endif /* CCI_APPLIER_H_ */
