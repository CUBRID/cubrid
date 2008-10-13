package cubrid.jdbc.driver;

import  cubrid.jdbc.*;
import  java.util.*;
import  javax.naming.*;
import  javax.naming.spi.*;
import  javax.sql.*;

import cubrid.jdbc.driver.CUBRIDConnectionPoolDataSource;
import cubrid.jdbc.driver.CUBRIDDataSource;
import cubrid.jdbc.driver.CUBRIDXADataSource;

/**
 * Title:        CUBRID JDBC Driver
 * Description:
 * @version 3.0
 */

public class CUBRIDDataSourceObjectFactory implements ObjectFactory
{

/*=======================================================================
 |      javax.naming.spi.ObjectFactory interface
 =======================================================================*/

public Object getObjectInstance(Object refObj,
				Name  name,
				Context nameCtx,
				Hashtable env)  throws Exception
{
  Reference ref = (Reference) refObj;

  if (ref.getClassName().equals("cubrid.jdbc.driver.CUBRIDDataSource") ||
      ref.getClassName().equals("cubrid.jdbc.driver.CUBRIDDataSource"))
  {
    return (new CUBRIDDataSource(ref));
  }
  if
  (ref.getClassName().equals("cubrid.jdbc.driver.CUBRIDConnectionPoolDataSource") ||
   ref.getClassName().equals("cubrid.jdbc.driver.CUBRIDConnectionPoolDataSource"))
  {
    return (new CUBRIDConnectionPoolDataSource(ref));
  }
  if (ref.getClassName().equals("cubrid.jdbc.driver.CUBRIDXADataSource") ||
      ref.getClassName().equals("cubrid.jdbc.driver.CUBRIDXADataSource"))
  {
    return (new CUBRIDXADataSource(ref));
  }
  return  null;
}

} // end of CUBRIDDataSourceObjectFactory
