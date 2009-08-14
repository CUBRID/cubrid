package nbench.engine.cci;

class CCIDataSource
{
  String ip;
  int port;
  String dbname;
  String dbuser;
  String dbpassword;
  CCIDriver driver;
  /**
   */
  CCIDataSource(String driver_path, String ip, int port, String dbname, 
	String dbuser, String dbpassword)
  throws Exception
  {
    this.driver = new CCIDriver(driver_path);
    this.ip = ip;
    this.port = port;
    this.dbname = dbname;
    this.dbuser = dbuser;
    this.dbpassword = dbpassword;
  }
  /**
   */
  CCIConnection getConnection()
  throws Exception
  {
    long conn_handle = driver.connect (ip, port, dbname, dbuser, dbpassword);
    return new CCIConnection(this, conn_handle);
  }
}
