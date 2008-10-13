package cubridmanager;

import java.util.Hashtable;

public class CubridException extends Exception {
	static final long serialVersionUID = 7L;
	private static final int COUNT_ER = 3;
	public static final int ER_NORMAL = 0;
	public static final int ER_UNKNOWNHOST = 1;
	public static final int ER_CONNECT = 2;
	private static Hashtable messageString = null;
	public int ErrCode;

	CubridException(int err) {
		super();
		ErrCode = err;
	}

	public String getErrorMessage() {
		if (messageString == null)
			setMessageHash();
		return (String) messageString.get(new Integer(ErrCode));
	}

	public static String getErrorMessage(int index) {
		if (messageString == null)
			setMessageHash();
		return (String) messageString.get(new Integer(index));
	}

	private static void setMessageHash() {
		messageString = new Hashtable(COUNT_ER + 1);

		messageString.put(new Integer(COUNT_ER), "Error"); // last index
		messageString.put(new Integer(ER_NORMAL), "No Error");
		messageString.put(new Integer(ER_UNKNOWNHOST), Messages
				.getString("ERROR.UNKNOWNHOST"));
		messageString.put(new Integer(ER_CONNECT), Messages
				.getString("ERROR.CONNECTFAIL"));
	}

}
