package nbench.script.parser;

import nbench.common.*;

public class NValueNode implements NNode
{
  public Value val;
  public NValueNode(Value val)
  {
    this.val = val;
  }
  // NNodeVisitable interface implementation
  public int nodeType() { return NNODE_VALUE; } 
  public void accept(NNodeVisitor visitor)
  {
    visitor.visit(this);
  }
  //
  public String toString() { return val.toString(); }
}
