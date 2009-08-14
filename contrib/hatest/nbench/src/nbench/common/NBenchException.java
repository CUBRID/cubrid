package nbench.common;

public class NBenchException extends Exception
{
  //make javac happy anyway..
  public final static long serialVersionUID  = 11223321L; 
  public NBenchException() 
  {
    super();
  }
  public NBenchException(String s)
  {
    super(s);
  }
}
