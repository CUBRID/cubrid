package nbench.engine.cci;
import nbench.common.NBenchException;
import java.sql.Timestamp;
import java.math.BigDecimal;

class CCIDriver
{
  private static Object sync_object;
  private static boolean loaded;
  private static String driver_path;
  /**
   */
  static
  {
    sync_object = new Object();
    loaded = false;
    System.loadLibrary("nbenchcci");
  }
  /**
   */
  CCIDriver(String driver_path)
  throws Exception
  {
    synchronized(CCIDriver.sync_object)
    {
      if(CCIDriver.loaded == false)
      {
	CCIDriver.init(driver_path);
	CCIDriver.driver_path = driver_path;
	CCIDriver.loaded = true;
      }
    }
    if(driver_path.equals(CCIDriver.driver_path) == false)
      throw new NBenchException("another driver already loaded :" 
	+ CCIDriver.driver_path);
  }
  /**
   */
  public native static void 
  init(String driver_path)
  throws Exception;
  /**
   */
  public native long
  connect(String ip, int port, String dbname, String dbuser, 
	String dbpassword)
  throws Exception;
  /**
   */
  public native void
  disconnect(long conn_h)
  throws Exception;
  /**
   */
  public native void
  commit(long conn_h)
  throws Exception;
  /**
   */
  public native long 
  prepare(long conn_h, String stmt)
  throws Exception;
  /**
   */
  public native int
  bind(long req_h, int index, int u_type, Object val)
  throws Exception;
  /**
   * if autocommit is true, execute function should commit job implicitly
   */
  public native int
  execute(long req_h, boolean autocommit)
  throws Exception;
  /**
   */
  public native CCIColInfo[]
  collinfo(long req_h);
  /**
   */
  public native boolean
  cursor_first(long req_h); //set cursor and fetch data
  /**
   */
  public native boolean
  cursor_next(long req_h); //set curosr next and fetch data
  /**
   */
  public native Object
  fetch(long req_h, int index, int u_type);
}

