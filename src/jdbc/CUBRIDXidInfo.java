package cubrid.jdbc.driver;

import javax.transaction.xa.*;
import java.util.*;
import cubrid.jdbc.jci.*;

class CUBRIDXidInfo {

/*=======================================================================
 |      PACKAGE ACCESS CONSTANT VALUES                                  
 =======================================================================*/

static final int STATUS_NOFLAG = 0,
		 STATUS_STARTED = 1,
		 STATUS_SUSPENDED = 2,
		 STATUS_PREPARED = 3,
		 STATUS_RECOVERED = 4,
		 STATUS_COMPLETED = 5;

/*=======================================================================
 |      PACKAGE ACCESS VARIABLES 
 =======================================================================*/

Xid xid;
UConnection ucon;
int status;

/*=======================================================================
 |      CONSTRUCTOR
 =======================================================================*/

CUBRIDXidInfo(Xid xid, UConnection ucon, int status)
{
  this.xid = xid;
  this.ucon = ucon;
  this.status = status;
}

/*=======================================================================
 |      PACKAGE ACCESS METHODS
 =======================================================================*/

boolean compare(CUBRIDXidInfo x)
{
  return (compare(x.xid));
}

boolean compare(Xid x)
{
  if ((xid.getFormatId() == x.getFormatId()) &&
      Arrays.equals(xid.getBranchQualifier(), x.getBranchQualifier()) &&
      Arrays.equals(xid.getGlobalTransactionId(), x.getGlobalTransactionId()))
  {
    return true;
  }
  return false;
}

} // end of class CUBRIDXidInfo
