package cubrid.jdbc.driver;

import java.sql.*;
import javax.sql.*;

import cubrid.jdbc.driver.CUBRIDConnectionWrapperPooling;
import cubrid.jdbc.driver.CUBRIDException;
import cubrid.jdbc.driver.CUBRIDJDBCErrorCode;

import java.util.*;

import cubrid.jdbc.jci.*;

public class CUBRIDPooledConnection implements PooledConnection
{

/*=======================================================================
 |      PROTECTED VARIABLES 
 =======================================================================*/

protected UConnection u_con;
protected boolean isClosed;
protected CUBRIDConnection curConnection;

/*=======================================================================
 |      PRIVATE VARIABLES 
 =======================================================================*/

private Vector eventListeners;

/*=======================================================================
 |      CONSTRUCTOR
 =======================================================================*/

protected CUBRIDPooledConnection()
{
  curConnection = null;
  eventListeners = new Vector();
  isClosed = false;
}

protected CUBRIDPooledConnection(UConnection c)
{
  this();
  u_con = c;
}

/*=======================================================================
 |      javax.sql.PooledConnection interface
 =======================================================================*/

synchronized public Connection getConnection() throws SQLException
{
  if (isClosed) {
    throw new CUBRIDException(CUBRIDJDBCErrorCode.pooled_connection_closed);
  }

  if (curConnection != null)
    curConnection.closeConnection();

  if (u_con.check_cas() == false) {
    u_con.reset_connection();
  }

  curConnection = new CUBRIDConnectionWrapperPooling(u_con, null, null, this);
  return curConnection;
}

synchronized public void close() throws SQLException
{
  if (isClosed)
    return;
  isClosed = true;
  if (curConnection != null)
    curConnection.closeConnection();
  u_con.close();
  eventListeners.clear();
}

synchronized public void addConnectionEventListener(ConnectionEventListener listener)
{
  if (isClosed) {
    return;
  }

  eventListeners.addElement(listener);
}

synchronized public void removeConnectionEventListener(ConnectionEventListener listener)
{
  if (isClosed) {
    return;
  }

  eventListeners.removeElement(listener);
}

/*=======================================================================
 |      PACKAGE ACCESS METHODS
 =======================================================================*/

synchronized void notifyConnectionClosed()
{
  curConnection = null;
  ConnectionEvent e = new ConnectionEvent(this);

  for (int i=0; i < eventListeners.size(); i++) {
    ((ConnectionEventListener) eventListeners.elementAt(i)).connectionClosed(e);
  }
}

synchronized void notifyConnectionErrorOccurred(SQLException ex)
{
  curConnection = null;
  ConnectionEvent e = new ConnectionEvent(this, ex);

  for (int i=0; i < eventListeners.size(); i++) {
    ((ConnectionEventListener)eventListeners.elementAt(i)).connectionErrorOccurred(e);
  }
}

} // end of class CUBRIDPooledConnection
