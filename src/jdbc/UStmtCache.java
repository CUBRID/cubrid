package cubrid.jdbc.jci;

import java.util.Hashtable;
import java.util.ArrayList;

public class UStmtCache {
  String key;
  
  private Hashtable res_cache_table;
  private ArrayList res_cache_remove_list;
  int ref_count;

  UStmtCache(String key)
  {
    this.key = key;

    res_cache_table = new Hashtable(30);
    res_cache_remove_list = new ArrayList(100);
    ref_count = 0;
  }

  public UResCache get(UBindKey key)
  {
    UResCache res_cache;
   
    synchronized (res_cache_table) {
      res_cache = (UResCache) res_cache_table.get(key);
      if (res_cache == null) {
        res_cache = new UResCache(key);
        res_cache_table.put(key, res_cache);
        synchronized (res_cache_remove_list) {
          res_cache_remove_list.add(res_cache);
        }
      }
      return res_cache;
    }
  }

  synchronized void incr_ref_count()
  {
    ref_count++;
  }

  synchronized void decr_ref_count()
  {
    ref_count--;
  }

  void clear() {
    synchronized (res_cache_table) {
      res_cache_table.clear();
      synchronized (res_cache_remove_list) {
        res_cache_remove_list.clear();
      }
    }
  }

  int remove_expired_res(long checkTime) {
    UResCache rc;

    for (int i=0 ; i < res_cache_remove_list.size() ; i++) {
      rc = (UResCache) res_cache_remove_list.get(i);
      if (rc.isExpired(checkTime)) {
        res_cache_table.remove(rc.key);

        synchronized (res_cache_remove_list) {
          Object lastObj = res_cache_remove_list.remove(res_cache_remove_list.size() - 1);
          if (i < res_cache_remove_list.size()) {
            res_cache_remove_list.set(i, lastObj);
            i--;
          }
        }
      }
    }
    return res_cache_remove_list.size();
  }
}
