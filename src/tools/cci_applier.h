#ifndef CCI_APPLIER_H_
#define CCI_APPLIER_H_

#ident "$Id$"

#include "error_code.h"

#define CA_IS_SERVER_DOWN(e) \
  (((e) == ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED) \
   || ((e) == ER_OBJ_NO_CONNECT) || ((e) == ER_NET_SERVER_CRASHED) \
   || ((e) == ER_BO_CONNECT_FAILED))

#define CA_STOP_ON_ERROR(cci_err, server_err) \
  ((cci_err) != CCI_ER_NO_ERROR && \
  ((cci_err) != CCI_ER_DBMS || CA_IS_SERVER_DOWN (server_err)))
#define CA_RETRY_ON_ERROR(e) \
  (LA_RETRY_ON_ERROR (e) || (e) == ER_DB_NO_MODIFICATIONS)

#define ER_CA_NO_ERROR            0
#define ER_CA_FAILED              -1
#define ER_CA_FILE_IO             -2
#define ER_CA_FAILED_TO_ALLOC     -3
#define ER_CA_DISCREPANT_INFO     -4

#endif /* CCI_APPLIER_H_ */
