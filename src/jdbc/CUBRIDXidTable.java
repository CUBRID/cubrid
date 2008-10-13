package cubrid.jdbc.driver;

import javax.transaction.xa.Xid;
import java.util.*;

abstract class CUBRIDXidTable {

/*=======================================================================
 |      STATIC VARIABLES
 =======================================================================*/
   
private static Hashtable xaTable;

static {
  xaTable = new Hashtable();
}

/*=======================================================================
 |      PACKAGE ACCESS METHODS
 =======================================================================*/

static boolean putXidInfo(String key, CUBRIDXidInfo xidInfo)
{
  Vector xidArray;

  synchronized (xaTable) {
    xidArray = (Vector) xaTable.get(key);

    if (xidArray == null) {
      xidArray = new Vector();
      xaTable.put(key, xidArray);
    }
  }

  synchronized (xidArray) {
    for (int i=0 ; i < xidArray.size() ; i++) {
      if (xidInfo.compare((CUBRIDXidInfo) xidArray.get(i)))
	return false;
    }
    xidArray.add(xidInfo);
  }
  return true;
}

static CUBRIDXidInfo getXid(String key, Xid xid)
{
  Vector xidArray;

  synchronized (xaTable) {
    xidArray = (Vector) xaTable.get(key);
    if (xidArray == null)
      return null;
  }

  synchronized (xidArray) {
    CUBRIDXidInfo xidInfo;
    for (int i=0 ; i < xidArray.size() ; i++) {
      xidInfo = (CUBRIDXidInfo) xidArray.get(i);
      if (xidInfo.compare(xid))
	return xidInfo;
    }
  }

  return null;
}

static void removeXid(String key, Xid xid)
{
  Vector xidArray;

  synchronized (xaTable) {
    xidArray = (Vector) xaTable.get(key);
    if (xidArray == null)
      return;
  }

  synchronized (xidArray) {
    CUBRIDXidInfo xidInfo;
    for (int i=0 ; i < xidArray.size() ; i++) {
      xidInfo = (CUBRIDXidInfo) xidArray.get(i);
      if (xidInfo.compare(xid)) {
	xidArray.remove(i);
	return;
      }
    }
  }
}

} // end of class CUBRIDXidTable
