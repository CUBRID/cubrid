package cubrid.jdbc.driver;

import javax.transaction.xa.*;
import java.io.*;

public class CUBRIDXid implements Xid, Serializable {

/*=======================================================================
 |      PRIVATE VARIABLES
 =======================================================================*/

private int formatId;
private byte[] globalTransactionId;
private byte[] branchQualifier;

/*=======================================================================
 |      CONSTRUCTOR
 =======================================================================*/

public CUBRIDXid(int fid, byte[] gid, byte[] bid)
{
  formatId = fid;
  globalTransactionId = gid;
  branchQualifier = bid;
}

/*=======================================================================
 |      javax.transaction.xa.Xid interface
 =======================================================================*/

public byte[] getBranchQualifier()
{
  return branchQualifier;
}

public int getFormatId()
{
  return formatId;
}

public byte[] getGlobalTransactionId()
{
  return globalTransactionId;
}

}  // end of class CUBRIDXid
