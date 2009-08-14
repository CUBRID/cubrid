package nbench.parse;
import nbench.common.*;
import nbench.common.helper.NValue;

public class ConditionItem
{
  public String expr;
  public String op;
  public Object value;
  public int type;
  public ConditionItem(String expr, String op, String value, int type)
  throws NBenchException
  {
    this.expr = expr;
    this.op = op;
    this.value = NValue.parseAs(value, type);
    this.type = type;
  }
  public String toString()
  {
    return expr + ":" + op + ":" + value.toString();
  }
}
