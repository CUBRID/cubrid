package cubrid.jdbc.driver;

import java.sql.*;
import javax.naming.*;
import java.io.PrintWriter;
import java.net.InetAddress;

public class CUBRIDDataSourceBase
{

/*=======================================================================
 |      PRIVATE VARIABLES 
 =======================================================================*/

// DataSource Standard Properties
private String          databaseName;
private String          dataSourceName;
private String          description;
private String          networkProtocol;
private String          password;
private int             portNumber;
private String          roleName;
private String          serverName;
private String          user;

private int		loginTimeout;
private PrintWriter	logWriter;

private String		dataSourceID;

/*=======================================================================
 |      CONSTRUCTOR
 =======================================================================*/

protected CUBRIDDataSourceBase()
{
  databaseName = "";
  dataSourceName = "";
  description = "";
  networkProtocol = "";
  password = "";
  portNumber = 0;
  roleName = "";
  serverName = "";
  user = "public";

  loginTimeout = 0;
  logWriter = null;

  dataSourceID = null;
}

/*=======================================================================
 |      javax.sql.DataSource, javax.sql.ConnectionPoolDataSource,
 |	javax.sql.XADataSource interface
 =======================================================================*/

public PrintWriter getLogWriter() throws SQLException
{
  return logWriter;
}

public void setLogWriter(PrintWriter out) throws SQLException
{
  logWriter = out;
}

public void setLoginTimeout(int seconds) throws SQLException
{
  loginTimeout = seconds;
}

public int getLoginTimeout() throws SQLException
{
  return loginTimeout;
}

/*=======================================================================
 |      PUBLIC METHODS
 =======================================================================*/

public String getDatabaseName()
{
  return databaseName;
}

public String getDataSourceName()
{
  return dataSourceName;
}

public String getDescription()
{
  return description;
}

public String getNetworkProtocol()
{
  return networkProtocol;
}

public String getPassword()
{
  return  password;
}

public int getPortNumber()
{
  return portNumber;
}

public String getRoleName()
{
  return roleName;
}

public String getServerName()
{
  return serverName;
}

public String getUser()
{
  return user;
}

public void setDatabaseName(String dbName)
{
  databaseName = dbName;
}

public void setDataSourceName(String dsName)
{
  dataSourceName = dsName;
}

public void setDescription(String desc)
{
  description = desc;
}

public void setNetworkProtocol(String netProtocol)
{
  networkProtocol = netProtocol;
}

public void setPassword(String psswd)
{
  password = psswd;
}

public void setPortNumber(int p)
{
  portNumber = p;
}

public void setRoleName(String rName)
{
  roleName = rName;
}

public void setServerName(String svName)
{
  serverName = svName;
}

public void setUser(String uName)
{
  user = uName;
}

/*=======================================================================
 |      PACKAGE ACCESS METHODS
 =======================================================================*/

synchronized String getDataSourceID(String username)
{
  if (dataSourceID == null) {
    try {
      dataSourceID = InetAddress.getByName(serverName).getHostAddress();
    } catch (Exception e) {
      dataSourceID = serverName;
    }
    dataSourceID = dataSourceID + ":" + portNumber + ":" + databaseName;
  }

  return (dataSourceID + ":" + username);
}

/*=======================================================================
 |      PROTECTED METHODS
 =======================================================================*/

protected Reference getProperties(Reference ref)
{
  ref.add(new StringRefAddr("serverName", getServerName()));
  ref.add(new StringRefAddr("databaseName", getDatabaseName()));
  ref.add(new StringRefAddr("portNumber", Integer.toString(getPortNumber())));
  // ref.add(new StringRefAddr("url", getUrl()));
  ref.add(new StringRefAddr("dataSourceName", getDataSourceName()));
  ref.add(new StringRefAddr("description", getDescription()));
  ref.add(new StringRefAddr("networkProtocol", getNetworkProtocol()));
  ref.add(new StringRefAddr("password", getPassword()));
  ref.add(new StringRefAddr("roleName", getRoleName()));
  ref.add(new StringRefAddr("user", getUser()));

  return ref;
}

protected void setProperties(Reference ref)
{
  setServerName((String) ref.get("serverName").getContent());
  setDatabaseName((String) ref.get("databaseName").getContent());
  setPortNumber(Integer.parseInt((String)ref.get("portNumber").getContent()));
  // uni_ds.setUrl((String) ref.get("url").getContent());
  setDataSourceName((String) ref.get("dataSourceName").getContent());
  setDescription((String) ref.get("description").getContent());
  setNetworkProtocol((String) ref.get("networkProtocol").getContent());
  setPassword((String) ref.get("password").getContent());
  setRoleName((String) ref.get("roleName").getContent());
  setUser((String) ref.get("user").getContent());
}

protected void writeLog(String log)
{
  if (logWriter != null) {
    java.util.Date dt = new java.util.Date(System.currentTimeMillis());
    logWriter.println("[" + dt + "] " + log);
    logWriter.flush();
  }
}

} // end class CUBRIDDataSourceProperty
