package cubrid.jdbc.driver;

import java.sql.*;
import javax.naming.*;
import java.io.PrintWriter;

public class CUBRIDPoolDataSourceBase extends CUBRIDDataSourceBase
{

/*=======================================================================
 |      PRIVATE VARIABLES
 =======================================================================*/

private int maxStatements;
private int initialPoolSize;
private int minPoolSize;
private int maxPoolSize;
private int maxIdleTime;
private int propertyCycle;

/*=======================================================================
 |      CONSTRUCTOR
 =======================================================================*/

protected CUBRIDPoolDataSourceBase()
{
  super();

  maxStatements = 0;
  initialPoolSize = 0;
  minPoolSize = 0;
  maxPoolSize = 0;
  maxIdleTime = 0;
  propertyCycle = 0;
}

/*=======================================================================
 |      PUBLIC METHODS
 =======================================================================*/

public int getMaxStatements()
{
  return maxStatements;
}

public int getInitialPoolSize()
{
  return initialPoolSize;
}

public int getMinPoolSize()
{
  return minPoolSize;
}

public int getMaxPoolSize()
{
  return maxPoolSize;
}

public int getMaxIdleTime()
{
  return maxIdleTime;
}

public int getPropertyCycle()
{
  return propertyCycle;
}

public void setMaxStatements(int no)
{
  maxStatements = 0;
}

public void setInitialPoolSize(int size)
{
  initialPoolSize = size;
}

public void setMinPoolSize(int size)
{
  minPoolSize = size;
}

public void setMaxPoolSize(int size)
{
  maxPoolSize = size;
}

public void setMaxIdleTime(int interval)
{
  maxIdleTime = interval;
}

public void setPropertyCycle(int interval)
{
  propertyCycle = interval;
}

/*=======================================================================
 |      PROTECTED METHODS
 =======================================================================*/

protected Reference getProperties(Reference ref)
{
  ref = super.getProperties(ref);

  ref.add(new StringRefAddr("maxStatements",
			    Integer.toString(getMaxStatements())));
  ref.add(new StringRefAddr("initialPoolSize",
			    Integer.toString(getInitialPoolSize())));
  ref.add(new StringRefAddr("minPoolSize",
			    Integer.toString(getMinPoolSize())));
  ref.add(new StringRefAddr("maxPoolSize",
			    Integer.toString(getMaxPoolSize())));
  ref.add(new StringRefAddr("maxIdleTime",
			    Integer.toString(getMaxIdleTime())));
  ref.add(new StringRefAddr("propertyCycle",
			    Integer.toString(getPropertyCycle())));

  return ref;
}

protected void setProperties(Reference ref)
{
  super.setProperties(ref);

  setMaxStatements(Integer.parseInt((String)ref.get("maxStatements").getContent()));
  setInitialPoolSize(Integer.parseInt((String)ref.get("initialPoolSize").getContent()));
  setMinPoolSize(Integer.parseInt((String)ref.get("minPoolSize").getContent()));
  setMaxPoolSize(Integer.parseInt((String)ref.get("maxPoolSize").getContent()));
  setMaxIdleTime(Integer.parseInt((String)ref.get("maxIdleTime").getContent()));
  setPropertyCycle(Integer.parseInt((String)ref.get("propertyCycle").getContent()));
}

} // end class CUBRIDDataSourceProperty
