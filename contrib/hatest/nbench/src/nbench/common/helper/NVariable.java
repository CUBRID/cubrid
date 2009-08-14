package nbench.common.helper;
import nbench.common.Value;
import nbench.common.Variable;

public class NVariable implements Variable
{
  private String name;
  private int type;
  private Value val;
  public NVariable(String name, int type, Value val)
  {
    this.name = name;
    this.type = type;
    this.val = val;
  }
  /* ------------------------------------- */
  /* nbench.common.Variable implementation */
  /* ------------------------------------- */
  public String getName() { return name; }
  public int getType() { return type; }
  public Value getValue() { return val; }
  public void setValue(Value val) { this.val = val; }
}
