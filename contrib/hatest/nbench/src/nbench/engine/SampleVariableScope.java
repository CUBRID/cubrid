package nbench.engine;
import nbench.common.*;
import nbench.common.helper.NVariable;
import nbench.parse.SampleSpace;
import java.util.HashMap;
import java.util.Set;

class SampleVariableScope implements VariableScope
{
  private String name;
  private HashMap<String, SampleValue> n2sv_hash;
  private HashMap<String, Variable> n2var_hash;
  public SampleVariableScope(SampleSpace ss)
  throws NBenchException
  {
    this.name = ss.name;
    n2sv_hash = new HashMap<String, SampleValue>();
    n2var_hash = new HashMap<String, Variable>();
    for (NameAndType nat : ss.S.keySet())
    {
      String spec = ss.S.get(nat);
      SampleValue sv = NSampleValue.getSampleValue(nat.getType(), spec);
      n2sv_hash.put(nat.getName(), sv);
      n2var_hash.put(nat.getName(), 
		       new NVariable(nat.getName(), nat.getType(), null));
    }
  }
  void roll()
  {
    for(String s : n2var_hash.keySet())
    {
      Variable var = n2var_hash.get(s);
      SampleValue sv = n2sv_hash.get(s);
      var.setValue(sv.nextValue());
    }
  }
  /* ---------------------------- */
  /* VariableScope implementation */
  /* ---------------------------- */
  public Variable getVariable(String name)
  {
    return n2var_hash.get(name);
  }
  public Set<String> getVariableNames()
  {
    return n2var_hash.keySet();
  }
}
