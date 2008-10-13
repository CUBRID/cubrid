package cubrid.jdbc.jci;

import java.util.Hashtable;
import cubrid.jdbc.driver.*;
import java.sql.*;

public class UResCache {

  UBindKey key;
  
  private boolean used;
  private UStatementCacheData cache_data;
 
  public UResCache(UBindKey key)
  {
    this.key = key;

    cache_data = null;
    used = true;
  }
  
  public UStatementCacheData getCacheData()
  {
    used = true;

    return (new UStatementCacheData(cache_data));
  }
  
  public void saveCacheData(UStatementCacheData cd)
  {
    if (cd.srvCacheTime <= 0)
      return;

    synchronized (this) {
      if (cache_data == null || cd.srvCacheTime > cache_data.srvCacheTime) {
        cache_data = cd;
      }
    }
  }

  boolean isExpired(long checkTime)
  {
    if (cache_data != null && used == false) {
      return true;
    }
    else {
      used = false;
      return false;
    }
  }
}
