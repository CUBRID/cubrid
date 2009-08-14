package nbench.common;

public class NBenchException extends Exception {
	private static final long serialVersionUID = 3517667903145597503L;

	public NBenchException() {
		super();
	}

	public NBenchException(Exception e) {
		super(e);
		// e.printStackTrace();
	}

	public NBenchException(String s) {
		super(s);
	}
}
