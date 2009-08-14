package nbench.script.parser;

public interface NNodeVisitable
{
  void accept(NNodeVisitor v);
}
