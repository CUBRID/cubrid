package nbench.engine;
import nbench.common.*;
import nbench.common.helper.NValue;

//
//TODO 
// 1. more detail sample specification needed..
// 2. exception handling.. -_-;;
//
public class NSampleValue
{
  // "a,b,c,d,e,f,g" -> round robin
  static class RoundRobinSampleValue implements SampleValue
  {
    private int type;
    private NValue[] values;
    private int index;
    private int len;
    RoundRobinSampleValue(int type, String spec)
    throws NBenchException
    {
      this.index = 0;
      this.type = type;
      String[] vals = spec.split(",");
      this.len = vals.length;
      this.values = new NValue[len];
      for(int i = 0; i < len; i++)
	values[i] = new NValue(type, NValue.parseAs(vals[i], type));
    }
    /* ---------------------------------------- */
    /* nbench.common.SampleValue implementation */
    /* ---------------------------------------- */
    public Value nextValue()
    {
      NValue ret = values[index++];
      if(index >= len)
	index = 0;
      return ret;
    }
    public int getValueType()
    {
      return type;
    }
    public String toString()
    {
      StringBuffer sb = new StringBuffer();
      sb.append(type + "");
      sb.append("[");
      for(int i = 0; i < values.length; i++)
	sb.append(values[i].toString());
      sb.append("]");
      return sb.toString();
    }
  }

  //[1,1000] -> random value
  static class RandomSampleValue implements SampleValue
  {
    private int type;
    private int lb;
    private int ub;
    RandomSampleValue(int type, String spec)
    throws NBenchException
    {
      String[] sa = spec.substring(spec.indexOf("[")+1, 
			spec.lastIndexOf("]")).split(",");
      this.type = type;
      if(sa.length != 2 || type != ValueType.INT)
	throw new NBenchException("invalid random sample spec");
      lb = Integer.parseInt(sa[0]);
      ub = Integer.parseInt(sa[1]); //TODO throw?
    }
    /* ---------------------------------------- */
    /* nbench.common.SampleValue implementation */
    /* ---------------------------------------- */
    public Value nextValue()
    {
      int i = new java.util.Random().nextInt(ub-lb+1);
      try {
        return new NValue(type, new Integer(i+lb));
      }
      catch (NBenchException e) {
	System.out.println("wow.. should not happen");
	return null;
      }
    }
    public int getValueType()
    {
      return type;
    }
  }

/* ------------------------------------------------------------------------ */
/* The Sample Value Factory */
/* ------------------------------------------------------------------------ */
  public static SampleValue 
  getSampleValue(int type, String spec) 
  throws NBenchException
  {
    if (spec == null)
      return null;
    else if (spec.charAt(0) == '[')
      return new RandomSampleValue(type, spec);
    else
      return new RoundRobinSampleValue(type, spec);
  }
}
