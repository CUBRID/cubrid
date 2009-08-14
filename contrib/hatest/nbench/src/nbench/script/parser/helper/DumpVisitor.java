package nbench.script.parser.helper;
import nbench.script.parser.*;
import nbench.script.parser.NNodeVisitor;

public class DumpVisitor implements NNodeVisitor
{
  private int indent_level;
  public DumpVisitor()
  {
    this.indent_level = 0;
  }
  private void inc() { indent_level++; }
  private void dec() { indent_level--; }
  private void println(String str)
  {
    for(int i = 0; i < indent_level; i++)
      System.out.print("  ");
    System.out.println(str);
  }
  private void print(String str)
  {
    for(int i = 0; i < indent_level; i++)
      System.out.print("  ");
    System.out.print(str);
  }
  /////////////////////////////////////////
  // NNodeVisitor interface implementation
  /////././////////////////////////////////
  public void visit(NNode n)
  {
    switch(n.nodeType())
    {
    case NNode.NNODE_IDENTIFIER:
      visit((NIdentifierNode)n);
      break;
    case NNode.NNODE_BINOP:
      visit((NBinOpNode)n);
      break;
    case NNode.NNODE_VALUE:
      visit((NValueNode)n);
      break;
    default:
      break;
    }
  }
  public void visit(NIdentifierNode n)
  {
    inc();
    println("Variable:" + n.var);
    dec();
  }
  public void visit(NValueNode n)
  {
    inc();
    println("Value:" + n.val);
    dec();
  }
  public void visit(NBinOpNode n)
  {
    inc();
    println("Op:" + n.opToString());
    n.left.accept(this);
    n.right.accept(this);
    dec();
  }
}
