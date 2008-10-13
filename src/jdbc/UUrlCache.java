package cubrid.jdbc.jci;

import java.util.Hashtable;
import java.util.ArrayList;

public class UUrlCache {
  private Hashtable stmt_cache_table;
  private ArrayList stmt_cache_remove_list;
 
  UUrlCache()
  {
    stmt_cache_table = new Hashtable(100,5);
    stmt_cache_remove_list = new ArrayList(100);
  }

  UStmtCache getStmtCache(String sql)
  {
    UStmtCache stmt_cache;
    synchronized (stmt_cache_table) {
      stmt_cache = (UStmtCache) stmt_cache_table.get(sql);
      if (stmt_cache == null) {
        stmt_cache = new UStmtCache(sql);
        stmt_cache_table.put(sql, stmt_cache);
        synchronized (stmt_cache_remove_list) {
          stmt_cache_remove_list.add(stmt_cache);
        }
      }
      stmt_cache.incr_ref_count();
    }

    return stmt_cache;
  }

  void remove_expired_stmt(long checkTime)
  {
    UStmtCache sc;

    for (int i=0 ; i < stmt_cache_remove_list.size() ; i++) {
      sc = (UStmtCache) stmt_cache_remove_list.get(i);
      int res_count = sc.remove_expired_res(checkTime);
      synchronized (stmt_cache_table) {
        if (res_count <= 0 && sc.ref_count <= 0) {
          stmt_cache_table.remove(sc.key);

          synchronized (stmt_cache_remove_list) {
            Object lastObj = stmt_cache_remove_list.remove(stmt_cache_remove_list.size() - 1);
            if (i < stmt_cache_remove_list.size()) {
              stmt_cache_remove_list.set(i, lastObj);
              i--;
            }
          }
        }
      }
    }
  }
}
