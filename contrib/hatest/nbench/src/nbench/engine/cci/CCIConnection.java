package nbench.engine.cci;
import nbench.common.NBenchException;

class CCIConnection
{
  CCIDataSource ds;
  long handle;
  /**
   */
  CCIConnection(CCIDataSource ds, long handle)
  throws Exception
  {
    this.ds = ds;
    this.handle = handle;
  }
  /**
   */
  CCIPreparedStatement prepareStatement(String str)
  throws Exception
  {
    long req_handle = ds.driver.prepare(handle, str);
    return new CCIPreparedStatement(this, req_handle, str);
  }
  /**
   */
  void commit()
  throws Exception
  {
    ds.driver.commit(handle);
  }
  /**
   */
  void setAutoCommit(boolean autocommit)
  throws Exception
  {
    throw new NBenchException("autocommit mode is not supported");
  }
  /**
   */
  boolean getAutoCommit()
  throws Exception
  {
    throw new NBenchException("autocommit is not supported");
  }
}
