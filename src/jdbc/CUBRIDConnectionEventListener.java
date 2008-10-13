package cubrid.jdbc.driver;

import java.sql.*;
import javax.sql.*;
import java.util.Vector;

class CUBRIDConnectionEventListener implements ConnectionEventListener {

/*=======================================================================
 |      PRIVATE VARIABLES
 =======================================================================*/

private Vector availableConnections;
private CUBRIDConnectionPoolDataSource cpds;

/*=======================================================================
 |      CONSTRUCTOR
 =======================================================================*/

CUBRIDConnectionEventListener(CUBRIDConnectionPoolDataSource ds)
{
  availableConnections = new Vector();
  cpds = ds;
}

/*=======================================================================
 |      javax.sql.ConnectionEventListener interface
 =======================================================================*/

synchronized public void connectionClosed(ConnectionEvent event)
{
  PooledConnection pc = (PooledConnection) event.getSource();

  if (pc == null) {
    return;
  }
  
  availableConnections.add(pc);
}

public void connectionErrorOccurred(ConnectionEvent event)
{
  PooledConnection pc = (PooledConnection) event.getSource();

  if (pc == null)
    return;

  try {
    pc.close();
  } catch (Exception e) {
  }
}

/*=======================================================================
 |      PACKAGE ACCESS METHODS
 =======================================================================*/

synchronized Connection getConnection(String user, String passwd)
	throws SQLException
{
  PooledConnection pc;

  if (availableConnections.size() <= 0) {
    pc = cpds.getPooledConnection(user, passwd);
    pc.addConnectionEventListener(this);
    availableConnections.add(pc);
  }

  pc = (PooledConnection) availableConnections.remove(0);

  return (pc.getConnection());
}

} // end of class CUBRIDConnectionPool
