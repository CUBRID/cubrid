package nbench.parse;

public class Step
{
  public final static int NOOP = 0;
  public final static int TRANSACTION = 1;
  public final static int SLEEP = 2;
  public Step(String action, String value)
  {
    act = action.toLowerCase();
    val = value;
    if(act == "transaction")
      {
	this.action = TRANSACTION;
	this.value = value;
      }
    else if (act == "sleep")
      {
	this.action = SLEEP;
        try { 
	  this.intValue = Integer.parseInt(value); 
	}
	catch (Exception e) {
	  this.intValue = 0;
	}
	this.value = new Integer(intValue);
      }
    else
      {
	this.action = NOOP;
	this.value = null;
      }
    this.eTime = 0;
  }
  public String act;
  public String val;
  public int action;
  public Object value;
  public int intValue;
  public String toString()
  {
    return "action="+action+",value="+value.toString();
  }
  public long eTime;
}
