package nbench.engine.helper;
import nbench.common.*;
import java.util.Map;
import java.util.List;
import java.util.ArrayList;
import java.util.Set;
import java.util.Properties;
import java.sql.Timestamp;
import java.math.BigDecimal;

/* -------------------------------------------------------------------------- */
public class DummyBackendEngine implements BackendEngine {
/* -------------------------------------------------------------------------- */

class MockValue implements Value
{
  public Object getAs(int vt)
  {
    switch(vt)
    {
    case ValueType.INT: return new Integer(0);
    case ValueType.STRING: return new String("");
    case ValueType.TIMESTAMP: return new Timestamp(0L);
    case ValueType.NUMERIC: return new BigDecimal(0);
    default : return null;
    }
  }
}
class MockVariable implements Variable
{
  private String name;
  public MockVariable(String name) { this.name = name; }
  public String getName() { return name; }
  public int getType() { return ValueType.STRING; }
  public Value getValue() { return new MockValue(); }
  public void setValue(Value val) {}
}
class MockVariableScope implements VariableScope
{
  public Variable getVariable(String name) { return new MockVariable(name); }
  public Set<String> getVariableNames() { return null; }
}
class DummyBackendEngineClient implements BackendEngineClient
{
  DummyBackendEngineClient() {}
  public List<VariableScope>
  execute(String name, VariableScope in) throws NBenchException
  { 
    List<VariableScope> res = new ArrayList<VariableScope>();
    res.add(new MockVariableScope());
    return res;
  }
}

public void configure (Properties props) throws NBenchException
{ }

public void
prepareForStatement(String name,List<NameAndType> in, List<NameAndType> out)
throws NBenchException
{ }

public void consolidateForRun() throws NBenchException
{ }

public BackendEngineClient createClient() throws NBenchException
{
  return new DummyBackendEngineClient();
}


/* -------------------------------------------------------------------------- */
} /* DummyBackendEngine */
/* -------------------------------------------------------------------------- */

