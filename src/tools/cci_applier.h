#ifndef CCI_APPLIER_H_
#define CCI_APPLIER_H_

#ident "$Id$"

#include "error_code.h"

#define CA_STOP_ON_ERROR(error) \
  ((error == ER_NET_CANT_CONNECT_SERVER || \
    error == ER_OBJ_NO_CONNECT))

#define ER_CA_NO_ERROR            0
#define ER_CA_FAILED              -1
#define ER_CA_FILE_IO             -2
#define ER_CA_FAILED_TO_ALLOC     -3
#define ER_CA_DISCREPANT_INFO     -4

#endif /* CCI_APPLIER_H_ */
