package nbench.script.parser;

public class NIdentifierNode implements NNode
{
  public String var;
  public NIdentifierNode(String var)
  {
    this.var = var;
  }
  // NNodeVisitable interface implementation
  public int nodeType() { return NNODE_IDENTIFIER; } 
  public void accept(NNodeVisitor visitor)
  {
    visitor.visit(this);
  }
  //
  public String toString() { return var; }
}
