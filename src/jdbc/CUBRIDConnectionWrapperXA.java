package cubrid.jdbc.driver;

import java.sql.*;

import cubrid.jdbc.driver.CUBRIDException;
import cubrid.jdbc.driver.CUBRIDJDBCErrorCode;

import cubrid.jdbc.jci.*;

public class CUBRIDConnectionWrapperXA extends CUBRIDConnection {

/*=======================================================================
 |      PRIVATE VARIABLES 
 =======================================================================*/

private CUBRIDXAConnection xacon;
private boolean xa_started;

/*=======================================================================
 |      CONSTRUCTOR
 =======================================================================*/

protected CUBRIDConnectionWrapperXA(UConnection u, String r, String s, CUBRIDXAConnection c, boolean xa_start)
{
  super(u, r, s);
  xacon = c;
  xa_started = xa_start;
  if (xa_start == true)
    auto_commit = false;
}

/*=======================================================================
 |      java.sql.Connection interface
 =======================================================================*/

synchronized public void close() throws SQLException
{
  if (is_closed)
    return;

  this.closeConnection();
  xacon.notifyConnectionClosed();
}

public synchronized void setAutoCommit(boolean autoCommit) throws SQLException
{
  if (xa_started) {
    if (autoCommit == true) {
      throw new CUBRIDException(CUBRIDJDBCErrorCode.xa_illegal_operation);
    }
  }
  else {
    super.setAutoCommit(autoCommit);
  }
}

public synchronized void commit() throws SQLException
{
  if (xa_started) {
    throw new CUBRIDException(CUBRIDJDBCErrorCode.xa_illegal_operation);
  }
  else {
    super.commit();
  }
}

public synchronized void rollback() throws SQLException
{
  if (xa_started) {
    throw new CUBRIDException(CUBRIDJDBCErrorCode.xa_illegal_operation);
  }
  else {
    super.rollback();
  }
}

public synchronized void rollback(Savepoint savepoint) throws SQLException
{
  if (xa_started) {
    throw new CUBRIDException(CUBRIDJDBCErrorCode.xa_illegal_operation);
  }
  else {
    super.rollback(savepoint);
  }
}

public synchronized void releaseSavepoint(Savepoint savepoint)
	throws SQLException
{
  if (xa_started) {
  }
  else {
    super.releaseSavepoint(savepoint);
  }
}

public synchronized Savepoint setSavepoint() throws SQLException
{
  if (xa_started) {
    throw new CUBRIDException(CUBRIDJDBCErrorCode.xa_illegal_operation);
  }
  else {
    return (super.setSavepoint());
  }
}

public synchronized Savepoint setSavepoint(String name) throws SQLException
{
  if (xa_started) {
    throw new CUBRIDException(CUBRIDJDBCErrorCode.xa_illegal_operation);
  }
  else {
    return (super.setSavepoint(name));
  }
}

/*=======================================================================
 |      PACKAGE ACCESS METHODS
 =======================================================================*/

void autoCommit() throws SQLException
{
  if (xa_started == false) {
    super.autoCommit();
  }
}

void xa_start(UConnection u)
{
  if (xa_started == true)
    return;

  auto_commit = false;
  xa_started = true;
  if (u != null)
    u_con = u;
}

void xa_end(UConnection u)
{
  if (xa_started == false)
    return;

  xa_started = false;
  if (u != null)
    u_con = u;
  auto_commit = true;
}

} // end of class CUBRIDConnectionWrapperXA
