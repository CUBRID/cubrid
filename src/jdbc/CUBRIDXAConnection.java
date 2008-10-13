package cubrid.jdbc.driver;

import java.sql.*;
import javax.sql.*;
import java.util.*;
import javax.transaction.xa.XAResource;

import cubrid.jdbc.driver.CUBRIDConnectionWrapperXA;
import cubrid.jdbc.driver.CUBRIDException;
import cubrid.jdbc.driver.CUBRIDJDBCErrorCode;
import cubrid.jdbc.driver.CUBRIDXAResource;

import cubrid.jdbc.jci.*;

/**
 * Title:        CUBRID JDBC Driver
 * Description:
 * @version 3.0
 */

public class CUBRIDXAConnection extends CUBRIDPooledConnection
		implements XAConnection
{

/*=======================================================================
 |      PRIVATE VARIABLES
 =======================================================================*/

private String serverName;
private int portNumber;
private String databaseName;
private String username;
private String passwd;

private CUBRIDXAResource xares;
private CUBRIDXADataSource xads;

private boolean xa_started;
private String xacon_key;

/*=======================================================================
 |      CONSTRUCTOR
 =======================================================================*/

protected CUBRIDXAConnection(CUBRIDXADataSource xads, String serverName, int portNumber, String databaseName, String username, String passwd) throws SQLException
{
  this.serverName = serverName;
  this.portNumber = portNumber;
  this.databaseName = databaseName;
  this.username = username;
  this.passwd = passwd;

  u_con = createUConnection();

  this.xads = xads;
  xares = null;

  xa_started = false;
  xacon_key = xads.getDataSourceID(username);
}

/*=======================================================================
 |      javax.sql.XAConnection interface
 =======================================================================*/

synchronized public XAResource getXAResource() throws SQLException
{
  if (isClosed) {
    throw new CUBRIDException(CUBRIDJDBCErrorCode.xa_connection_closed);
  }

  if (xares == null) {
    xares = new CUBRIDXAResource(this, xacon_key);
  }

  return xares;
}

synchronized public Connection getConnection() throws SQLException
{
  if (isClosed) {
    throw new CUBRIDException(CUBRIDJDBCErrorCode.xa_connection_closed);
  }

  if (curConnection != null)
    curConnection.closeConnection();

  if (u_con == null) {
    u_con = createUConnection();
  }

  curConnection = new CUBRIDConnectionWrapperXA(u_con, null, null, this, xa_started);
  return curConnection;
}

/*=======================================================================
 |      PACKAGE ACCESS METHODS
 =======================================================================*/

synchronized void notifyConnectionClosed()
{
  super.notifyConnectionClosed();

  if (xa_started == true)
    u_con = null;
}

synchronized UConnection xa_end_tran(UConnection u)
{
  if (u_con == null) {
    u_con = u;
    return null;
  }
  return u;
}

synchronized UConnection xa_start(int flag, UConnection u)
{
  if (xa_started == true)
    return null;

  xa_started = true;

  if (flag == XAResource.TMJOIN || flag == XAResource.TMRESUME) {
    if (u_con != null) {
      u_con.close();
    }
    u_con = u;
  }

  if (curConnection != null) {
    if (flag == XAResource.TMNOFLAGS) {
      try {
	curConnection.rollback();
      } catch (SQLException e) {
      }
    }
    ((CUBRIDConnectionWrapperXA) curConnection).xa_start(u_con);
  }

  return u_con;
}

synchronized boolean xa_end()
{
  if (xa_started == false)
    return true;

  try {
    if (u_con != null) {
      u_con = createUConnection();
    }
  } catch (SQLException e) {
    return false;
  }

  if (curConnection != null)
    ((CUBRIDConnectionWrapperXA) curConnection).xa_end(u_con);

  xa_started = false;

  return true;
}

UConnection createUConnection() throws SQLException
{
  return (UJCIManager.connect(serverName, portNumber, databaseName, username, passwd));
}

} // end of class CUBRIDXAConnection
