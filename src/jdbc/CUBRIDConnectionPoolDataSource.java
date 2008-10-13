package cubrid.jdbc.driver;

import java.sql.*;
import javax.sql.*;
import java.io.*;
import javax.naming.*;

import cubrid.jdbc.driver.CUBRIDPooledConnection;

import cubrid.sql.*;
import cubrid.jdbc.jci.*;

public class CUBRIDConnectionPoolDataSource extends CUBRIDPoolDataSourceBase
		implements ConnectionPoolDataSource,
			   Referenceable,
			   Serializable
{

/*=======================================================================
 |      CONSTRUCTORS
 =======================================================================*/

public CUBRIDConnectionPoolDataSource()
{
  super();
}

protected CUBRIDConnectionPoolDataSource(Reference ref)
{
  super();
  setProperties(ref);
}

/*=======================================================================
 |      javax.sql.ConnectionPoolDataSource interface
 =======================================================================*/

public synchronized PooledConnection getPooledConnection() throws SQLException
{
  return getPooledConnection(getUser(), getPassword());
}

public synchronized PooledConnection getPooledConnection(String username, String passwd) throws SQLException
{
  UConnection u_con = UJCIManager.connect(getServerName(), getPortNumber(), getDatabaseName(), username, passwd);

  return (new CUBRIDPooledConnection(u_con));
}

/*=======================================================================
 |      javax.naming.Referenceable interface
 =======================================================================*/

public  synchronized Reference getReference()  throws  NamingException
{
  Reference ref = new Reference(this.getClass().getName(),
		     "cubrid.jdbc.driver.CUBRIDDataSourceObjectFactory", null);

  ref = getProperties(ref);
  return ref;
}

} // class CUBRIDDataSource end
