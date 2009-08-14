package nbench.engine.cci;
import nbench.common.NBenchException;
import java.math.BigDecimal;
import java.sql.Timestamp;
import java.util.List;
import java.util.HashMap;

class CCIResultSet
{
  /* ------------------------------------------------------------------------ */
  /* FIELDS */
  /* ------------------------------------------------------------------------ */
  CCIPreparedStatement pstmt;
  boolean closed;
  boolean has_more;
  boolean last_cursor;
  /* ------------------------------------------------------------------------ */
  /* METHODS */
  /* ------------------------------------------------------------------------ */
  /**
   */
  CCIResultSet(CCIPreparedStatement pstmt)
  {
    this.pstmt = pstmt;
    closed = false;
    has_more = pstmt.conn.ds.driver.cursor_first(pstmt.handle);
    if(has_more == false)
      last_cursor = true;
  }
  /**
   */
  synchronized void close()
  throws Exception
  {
    closed = true;
  }
  /**
   */
  synchronized boolean next()
  throws Exception
  {
    if(last_cursor)
      return false;
    if(has_more)
    {
      has_more = false;
      return true;
    }
    has_more = pstmt.conn.ds.driver.cursor_next(pstmt.handle);
    if(has_more == false)
      last_cursor = true;
    return has_more;
  }
  /**
   */
  synchronized int getInt(String name)
  throws Exception
  {
    CCIColInfo ci = pstmt.find_collinfo_for(name, true);
    Integer val = (Integer)pstmt.conn.ds.driver.fetch(pstmt.handle, 
	ci.index, CCIColInfo.U_TYPE_INT);
    return val.intValue();
  }
  /**
   */
  synchronized String getString(String name)
  throws Exception
  {
    CCIColInfo ci = pstmt.find_collinfo_for(name, true);
    String val = (String)pstmt.conn.ds.driver.fetch(pstmt.handle, 
	ci.index, CCIColInfo.U_TYPE_VARCHAR);
    return val;
  }
  /**
   */
  synchronized Timestamp getTimestamp(String name)
  throws Exception
  {
    CCIColInfo ci = pstmt.find_collinfo_for(name, true);
    Timestamp val = (Timestamp)pstmt.conn.ds.driver.fetch(pstmt.handle, 
	ci.index, CCIColInfo.U_TYPE_TIMESTAMP);
    return val;
  }
  /**
   */
  synchronized BigDecimal getBigDecimal(String name)
  throws Exception
  {
    CCIColInfo ci = pstmt.find_collinfo_for(name, true);
    BigDecimal val = (BigDecimal)pstmt.conn.ds.driver.fetch(pstmt.handle, 
	ci.index, CCIColInfo.U_TYPE_NUMERIC);
    return val;
  }
}
