package nbench.common;

public interface Variable extends NameAndType
{
  Value getValue();
  void setValue(Value val);
}
