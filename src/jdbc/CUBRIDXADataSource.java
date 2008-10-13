package cubrid.jdbc.driver;

import java.sql.*;
import java.util.*;
import java.io.*;
import javax.sql.*;
import javax.naming.*;

import cubrid.jdbc.driver.CUBRIDXAConnection;

/**
 * Title:
 *        CUBRID JDBC Driver
 * Description:
 * @version 3.0
*/

public class CUBRIDXADataSource  extends CUBRIDPoolDataSourceBase
		implements XADataSource,
			   Referenceable,
			   Serializable
{

/*=======================================================================
 |      CONSTRUCTORS
 =======================================================================*/

public CUBRIDXADataSource()
{
  super();
}

protected CUBRIDXADataSource(Reference ref)
{
  super();
  setProperties(ref);
}

/*=======================================================================
 |      javax.sql.XADataSource interface
 =======================================================================*/

public synchronized XAConnection getXAConnection() throws SQLException
{
  return getXAConnection(getUser(), getPassword());
}

public synchronized XAConnection getXAConnection(String username, String passwd)
		throws SQLException
{
  return (new CUBRIDXAConnection(this, getServerName(), getPortNumber(), getDatabaseName(), username, passwd));
}

/*=======================================================================
 |      javax.naming.Referenceable interface
 =======================================================================*/

public  synchronized Reference getReference() throws NamingException
{
  Reference ref = new Reference(this.getClass().getName(),
		     "cubrid.jdbc.driver.CUBRIDDataSourceObjectFactory", null);

  ref = getProperties(ref);
  return ref;
}

} // end of class CUBRIDXADataSource
