package nbench.script.parser;

public interface NNode extends NNodeVisitable
{
  public final static int NNODE_BINOP = 1;
  public final static int NNODE_VALUE = 2;
  public final static int NNODE_IDENTIFIER = 3;
  public int nodeType();
}
