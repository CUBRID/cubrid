package nbench.engine;
import nbench.common.*;
import java.util.Set;
import java.util.HashMap;
import java.util.HashSet;

class FrameScope implements VariableScope {
  private VariableScope parent;
  private HashMap<String, Variable> local_vars;
  public FrameScope(VariableScope parent)
  {
    this.parent = parent;
    local_vars = new HashMap<String, Variable>();
  }
  public void setParent(VariableScope parent) { this.parent = parent; }
  /* used by front end engine */
  public void setVariable(Variable v)
  {
    local_vars.put(v.getName(), v);
  }
  /* ---------------------------- */
  /* VariableScope implementation */
  /* ---------------------------- */
  public Variable getVariable(String name)
  {
    Variable v = null;
    v = local_vars.get(name);
    if(v != null)
      return v;
    if(parent != null)
      return parent.getVariable(name);
    return null; 
  }
  public Set<String> getVariableNames()
  {
    if(parent == null)
      return local_vars.keySet();
    else
    {
      Set<String> rs1 = local_vars.keySet();
      Set<String> rs2 = parent.getVariableNames();
      HashSet<String> rs12 = new HashSet<String>(rs1);
      rs12.addAll(rs2);
      return rs12;
    }
  }
}
