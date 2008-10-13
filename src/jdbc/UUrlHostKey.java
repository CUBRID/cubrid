package cubrid.jdbc.jci;

public class UUrlHostKey {

  private String host; 
  private int port; 
  private String dbname; 
  private String user; 
  
  public UUrlHostKey(String host, int port, String dbname, String user)
  {
    this.host = host;
    this.port = port;
    this.dbname = dbname;
    this.user = user;
  }
  
  public int hashCode()
  {
    return host.hashCode() + port + dbname.hashCode() + user.hashCode();
  }
  
  public boolean equals(Object obj)
  {
    if (!(obj instanceof UUrlHostKey))
      return false;
    
    UUrlHostKey key = (UUrlHostKey) obj;

    if (host.equals(key.host) && port == key.port && dbname.equals(key.dbname) && user.equals(key.user) )
      return true;
    else 
      return false;
  }
  
}
