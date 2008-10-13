package cubrid.jdbc.jci;

public class UConKey {

private String conKey;

public UConKey(String s)
{
  conKey = s;
}

String getKeyString()
{
  return (conKey == null ? "" : conKey);
}

}
