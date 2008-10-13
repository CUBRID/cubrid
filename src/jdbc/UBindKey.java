package cubrid.jdbc.jci;

public class UBindKey {

  private int num_key;
  private Object[] values;
  
  public UBindKey(Object[] values)
  {
    if (values == null)
      num_key = 0;
    else {
      num_key = values.length;
      this.values = (Object[])values.clone();
    }
  }
  
  public int hashCode()
  {
    if (num_key == 0)
      return 0;
    return values[0].hashCode();
  }
  
  public boolean equals(Object obj)
  {
    if (obj instanceof UBindKey) {
      UBindKey k = (UBindKey) obj;
      if (k == null) {
        if (num_key == 0)
          return true;
        else
          return false;
      }
  
      if (num_key == k.num_key) {
        for (int i=0 ; i < num_key ; i++) {
          if (values[i].equals(k.values[i]) == false)
            return false;
        }
        return true;
      }
      else {
        return false;
      }
    }
    else {
      return obj.equals(this);
    }
  
  }

}
