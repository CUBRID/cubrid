package nbench.script.parser;

public class NBinOpNode implements NNode
{
  public final static int ADD = 1;
  public final static int SUB = 2;
  public final static int MUL = 3;
  public final static int DIV = 4;
  public final static int MOD = 5;

  public int op;
  public NNode left;
  public NNode right;
  public NBinOpNode(int op, NNode left, NNode right)
  {
    this.op = op;
    this.left = left;
    this.right = right;
  }
  // NNode implementation
  public int nodeType() { return NNODE_BINOP; } 
  public void accept(NNodeVisitor visitor)
  {
    visitor.visit(this);
  }
  //
  public String toString()
  {
    return opToString();
  }
  public String opToString() 
  { 
    switch(op)
    {
    case ADD: return "binop(+)";
    case SUB: return "binop(-)";
    case MUL: return "binop(*)";
    case DIV: return "binop(/)";
    case MOD: return "binop(%)";
    default: return "unknown";
    }
  }
}
