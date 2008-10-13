package cubrid.jdbc.driver;

import java.sql.*;
import java.util.Hashtable;
import javax.naming.*;

import cubrid.jdbc.driver.CUBRIDException;
import cubrid.jdbc.driver.CUBRIDJDBCErrorCode;

abstract class CUBRIDConnectionPoolManager {

/*=======================================================================
 |      STATIC VARIABLES
 =======================================================================*/

private static Hashtable connectionPooltable;
private static Hashtable poolDataSourceTable;

static {
  connectionPooltable = new Hashtable();
  poolDataSourceTable = new Hashtable();
}

/*=======================================================================
 |      PACKAGE ACCESS METHODS
 =======================================================================*/

static Connection getConnection(CUBRIDConnectionPoolDataSource pds, String user, String passwd) throws SQLException
{
  CUBRIDConnectionEventListener cp;
  
  String key = pds.getDataSourceID(user);

  synchronized (connectionPooltable) {
    cp = (CUBRIDConnectionEventListener) connectionPooltable.get(key);

    if (cp == null) {
      cp = addConnectionPool(key, pds);
    }
  }

  return (cp.getConnection(user, passwd));
}

static CUBRIDConnectionPoolDataSource getConnectionPoolDataSource(String dsName)
	throws SQLException
{
  CUBRIDConnectionPoolDataSource cpds;

  synchronized (poolDataSourceTable) {
    cpds = (CUBRIDConnectionPoolDataSource) poolDataSourceTable.get(dsName);

    if (cpds == null) {
      try {
	Context ctx = new InitialContext();
	cpds = (CUBRIDConnectionPoolDataSource) ctx.lookup(dsName);
      } catch (NamingException e) {
	throw new CUBRIDException(CUBRIDJDBCErrorCode.unknown, e.toString());
      }

      poolDataSourceTable.put(dsName, cpds);
    }
  }

  return cpds;
}

/*=======================================================================
 |      PRIVATE METHODS
 =======================================================================*/

static private CUBRIDConnectionEventListener addConnectionPool(String key, CUBRIDConnectionPoolDataSource pds)
{
  CUBRIDConnectionEventListener cp = new CUBRIDConnectionEventListener(pds);
  connectionPooltable.put(key, cp);
  return cp;
}

} // end of class CUBRIDConnectionPoolManager
