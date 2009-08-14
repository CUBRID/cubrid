package nbench.engine.cci;
import nbench.common.NBenchException;
import java.math.BigDecimal;
import java.sql.Timestamp;
import java.util.HashMap;

class CCIPreparedStatement
{
  long handle;
  CCIConnection conn;
  String sql;
  CCIResultSet current_res;
  CCIColInfo[] collinfos;
  HashMap<String, CCIColInfo> name_index;
  /* ------------------------------------------------------------------------ */
  /* METHODS */
  /* ------------------------------------------------------------------------ */
  private HashMap<String, CCIColInfo>
  make_name_index_for(CCIColInfo[] collinfos)
  {
    HashMap<String, CCIColInfo> map = new HashMap<String, CCIColInfo>();
    for(int i = 0; i < collinfos.length; i++)
      map.put(collinfos[i].col_name, collinfos[i]);
    return map;
  }
  CCIColInfo
  find_collinfo_for(String name, boolean raise_if_not_found)
  throws Exception
  {
    CCIColInfo ci = name_index.get(name);
    if(ci == null && raise_if_not_found)
      throw new NBenchException("column name not found:" + name);
    return ci;
  }
  /**
   */
  CCIPreparedStatement(CCIConnection conn, long handle, String sql)
  throws Exception
  {
    this.conn = conn;
    this.handle = handle;
    this.sql = sql;
    this.current_res = null;
    collinfos = conn.ds.driver.collinfo(handle);
    name_index = make_name_index_for(collinfos);
  }
  /**
   */
  void setString(int index, String val)
  throws Exception
  {
    conn.ds.driver.bind(handle, index, CCIColInfo.U_TYPE_VARCHAR, val);
  }
  /**
   */
  void setInt(int index, Integer val)
  throws Exception
  {
    conn.ds.driver.bind(handle, index, CCIColInfo.U_TYPE_INT, val);
  }
  /**
   */
  void setTimestamp(int index, Timestamp val)
  throws Exception
  {
    conn.ds.driver.bind(handle, index, CCIColInfo.U_TYPE_TIMESTAMP, val);
  }
  /**
   */
  void setBigDecimal(int index, BigDecimal val)
  throws Exception
  {
    conn.ds.driver.bind(handle, index, CCIColInfo.U_TYPE_NUMERIC, val);
  }
  /**
   * executeQuery().
   * CCI 단에서는 connection 단위의 critical section management를 한다.
   * 여기서 synchroized keyword를 붙인 이유는, Java 단에서 result set을 
   * 두개 이상 사용하는 구간을 없애기 위한 것이다.
   */
  synchronized CCIResultSet executeQuery()
  throws Exception
  {
    if(current_res != null)
      current_res.close();
    conn.ds.driver.execute(handle, false);
    current_res = new CCIResultSet(this);
    return current_res;
  }
  /**
   * executeUpdate().
   * see above comment.
   */
  synchronized int executeUpdate()
  throws Exception
  {
    if(current_res != null)
    {
      current_res.close();
      current_res = null;
    }
    return conn.ds.driver.execute(handle, false);
  }
}




