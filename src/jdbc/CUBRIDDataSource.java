package cubrid.jdbc.driver;


import java.sql.*;
import java.util.*;
import java.io.*;
import javax.sql.*;
import javax.naming.*;

import cubrid.jdbc.driver.CUBRIDConnection;
// import javax.naming.spi.*;

import cubrid.sql.*;
import cubrid.jdbc.jci.*;
import cubrid.jdbc.driver.*;

/**
 * Title:
 *        CUBRID JDBC Driver
 * Description:
 * @version 3.0
*/

public class CUBRIDDataSource  extends CUBRIDDataSourceBase
			       implements javax.sql.DataSource,
                                          javax.naming.Referenceable,
                                          java.io.Serializable
{

/*=======================================================================
 |      CONSTRUCTORS
 =======================================================================*/

public CUBRIDDataSource()
{
  super();
}

protected CUBRIDDataSource(Reference ref)
{
  super();
  setProperties(ref);
}

/*=======================================================================
 |      javax.sql.DataSource interface
 =======================================================================*/

public Connection getConnection() throws SQLException
{
  return getConnection(null, null);
}

public Connection getConnection(String username, String passwd)
	throws SQLException
{
  String dataSourceName = getDataSourceName();
  Connection con;

  if (dataSourceName == null || dataSourceName.length() == 0) {
    if (username == null)
      username = getUser();
    if (passwd == null)
      passwd = getPassword();

    UConnection u_con = UJCIManager.connect(getServerName(),
					    getPortNumber(),
					    getDatabaseName(),
					    username,
					    passwd);

    writeLog("getConnection(" + username + ")");
    con = new CUBRIDConnection(u_con, null, username);
  }
  else {
    CUBRIDConnectionPoolDataSource cpds;

    cpds = CUBRIDConnectionPoolManager.getConnectionPoolDataSource(dataSourceName);
    if (username == null)
      username = cpds.getUser();
    if (passwd == null)
      passwd = cpds.getPassword();
    con = CUBRIDConnectionPoolManager.getConnection(cpds, username, passwd);
  }

  return con;
}

/*=======================================================================
 |      javax.naming.Referenceable interface
 =======================================================================*/

public synchronized Reference getReference() throws NamingException
{
  Reference ref = new Reference(this.getClass().getName(),
	     "cubrid.jdbc.driver.CUBRIDDataSourceObjectFactory", null);

  ref = getProperties(ref);
  writeLog("Bind DataSource");
  return ref;
}

} // end of class CUBRIDDataSource
