package nbench.common.helper;
import nbench.common.*;
import java.util.Map;
import java.util.Set;
import java.util.HashSet;
import java.util.Iterator;

//
// 현재 scope에서 backend sqlmap statement의 inputmap을 만들어야 한다
// 또한 backend client에서는 variable scope로 부터 map을 만들어야 하므로
// 양자간의 변환이 자유로운 variable scope를 만들어서 넘겨준다.
//
public class MapVariableScope implements VariableScope
{
  private Map map;
  public MapVariableScope(Map map) 
  {
    this.map = map;
  }
  public Map getMap() 
  { 
    return map;
  }
  /* -------------------------------------- */
  /* VariableScope interface implementation */
  /* -------------------------------------- */
  public Variable getVariable(final String name)
  {
	  /* debug */
	  /*
	  int mapsize = map.size();
	  Set set = map.keySet();
	  
	  Iterator it = set.iterator();
	  int i = 0;
	  while(it.hasNext())
	  {
		  String lname = (String)it.next();
		System.out.println("MAP KEY " + i + "(" + name +"): " + lname + "(value:" + map.get(lname)+ ")");
		i++;
	  }
	  */
	  /* end debug */
	  
    Object obj;
    final NValue nval;
    if((obj = map.get(name)) == null)
      return null;
    try { 
      nval = new NValue(obj);
    }
    catch(Exception e) {
      System.out.println(e.toString()); //TODO exceptin handling
      return null;
    }
    return new Variable()
    {
      String n = name;
      NValue v = nval;
      public Value getValue() { return v; }
      public void setValue(Value val) {}
      public String getName() { return n; }
      public int getType() { return v.getType(); }
    };
  }
  public Set<String> getVariableNames()
  {
	Set<String> set = new HashSet<String>();
    for(Object o : map.keySet())
      set.add(o.toString());
    return set;
  }
}
