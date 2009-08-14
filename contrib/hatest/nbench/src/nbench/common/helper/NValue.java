package nbench.common.helper;
import nbench.common.Value;
import nbench.common.NBenchException;
import nbench.common.ValueType;
import java.text.DateFormat;
import java.util.HashMap;
import java.math.BigDecimal;
import java.sql.Timestamp;

public class NValue implements nbench.common.Value
{
  private int type;
  private Object val;
  /**
   */
  public NValue(int type, Object val) throws NBenchException
  {
    this.type = type;
    this.val = val;
    switch(type)
    {
      case ValueType.INT:
	if(val instanceof java.lang.Integer) 
	  break;
	throw new NBenchException("Integer value expected");
      case ValueType.STRING:
	if(val instanceof java.lang.String) 
	  break;
	throw new NBenchException("String value expected");
      case ValueType.TIMESTAMP:
	if(val instanceof java.sql.Timestamp) 
	  break;
	throw new NBenchException("Timestamp value expected");
      case ValueType.NUMERIC:
	if(val instanceof java.math.BigDecimal)
	  break;
	throw new NBenchException("BigDecimal value expected");
      default:
	throw new NBenchException("unsupported type");
    }
  }
  /**
   */
  public NValue(Object val) throws NBenchException
  {
    if(val instanceof java.lang.Integer)
    {
      type = ValueType.INT;
      this.val = val;
    }
    else if (val instanceof BigDecimal)
    {
      type = ValueType.NUMERIC;
      this.val = val;
    }
    else if (val instanceof java.lang.String)
    {
      type = ValueType.STRING;
      this.val = val;
    }
    else if (val instanceof java.sql.Timestamp)
    {
      type = ValueType.TIMESTAMP;
      this.val = val;
    }
    else 
      throw new NBenchException("unsupported type object:" + val);
  }
  /**
   */
  public Object getAs(int target)
  {
    if(target == type)
      return val;
    try
    {
      switch(target)
      {
      case ValueType.INT:
	return Integer.valueOf(val.toString());
      case ValueType.STRING: 
	return val.toString();
      case ValueType.TIMESTAMP:
	  return Timestamp.valueOf(val.toString());
      case ValueType.NUMERIC:
	return new BigDecimal(val.toString());
      default:
	new NBenchException().printStackTrace();
	return null;
      }
    }
    catch (Exception e)
    {
      e.printStackTrace();
      return null;
    }
  }
  public int getType()
  {
    return type;
  }
  public String toString()
  {
    return val.toString();
  }
  public static Object parseAs(String str, int tt)
  throws NBenchException
  {
    try
    {
      switch(tt)
      {
      case ValueType.INT:
	return Integer.valueOf(str);
      case ValueType.STRING: 
	return str;
      case ValueType.TIMESTAMP:
	  return Timestamp.valueOf(str);
      case ValueType.NUMERIC:
	return new BigDecimal(str);
      default:
	throw new NBenchException("unsupported type" + tt);
      }
    }
    catch (Exception e)
    {
      return new NBenchException(e.toString());
    }
  }
/* ------------------------------------------------------------------------ */
/* */
/* ------------------------------------------------------------------------ */
  public static boolean eq(Value lval, Value rval, int tt)
  {
    switch(tt)
    {
    case ValueType.INT:
    {
      Integer i = (Integer)lval.getAs(tt);
      Integer j = (Integer)rval.getAs(tt);
      return i.intValue() == j.intValue();
    }
    case ValueType.STRING:
    {
      String i = (String)lval.getAs(tt);
      String j = (String)rval.getAs(tt);
      return i.equals(j);
    }
    case ValueType.TIMESTAMP:
    {
      Timestamp i = (Timestamp)lval.getAs(tt);
      Timestamp j = (Timestamp)rval.getAs(tt);
      return i.equals(j);
    }
    case ValueType.NUMERIC:
    {
      BigDecimal i = (BigDecimal)lval.getAs(tt);
      BigDecimal j = (BigDecimal)rval.getAs(tt);
	return i.compareTo(j) == 0;
    }
    default:
      return false;
    }
  }
  public static boolean gt(Value lval, Value rval, int tt)
  {
    switch(tt)
    {
    case ValueType.INT:
    {
      Integer i = (Integer)lval.getAs(tt);
      Integer j = (Integer)rval.getAs(tt);
      return i.intValue() > j.intValue();
    }
    case ValueType.STRING:
    {
      String i = (String)lval.getAs(tt);
      String j = (String)rval.getAs(tt);
      return i.compareTo(j) > 0;
    }
    case ValueType.TIMESTAMP:
    {
      Timestamp i = (Timestamp)lval.getAs(tt);
      Timestamp j = (Timestamp)rval.getAs(tt);
      return i.after(j);
    }
    case ValueType.NUMERIC:
    {
      BigDecimal i = (BigDecimal)lval.getAs(tt);
      BigDecimal j = (BigDecimal)rval.getAs(tt);
	return i.compareTo(j) > 0;
    }
    default:
      return false;
    }
  }
}
