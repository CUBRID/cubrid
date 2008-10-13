package cubrid.jdbc.driver;

import java.sql.*;

import cubrid.jdbc.jci.*;

public class CUBRIDConnectionWrapperPooling extends CUBRIDConnection {

/*=======================================================================
 |      PRIVATE VARIABLES 
 =======================================================================*/

private CUBRIDPooledConnection pcon;

/*=======================================================================
 |      CONSTRUCTOR
 =======================================================================*/

protected CUBRIDConnectionWrapperPooling(UConnection u, String r, String s, CUBRIDPooledConnection p)
{
  super(u, r, s);
  pcon = p;
}

/*=======================================================================
 |      java.sql.Connection interface
 =======================================================================*/

public synchronized void close() throws SQLException
{
  if (is_closed)
    return;

  closeConnection();
  pcon.notifyConnectionClosed();
}

}
