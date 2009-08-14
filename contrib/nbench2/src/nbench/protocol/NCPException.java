package nbench.protocol;

import nbench.common.NBenchException;


public class NCPException extends NBenchException {
	private static final long serialVersionUID = 2543076064344098288L;
	public String json;
	
	public NCPException(String s) {
		super(s);
	}
	public NCPException(String s, String json) {
		super(s);
		this.json = json;
	}
}
