package cubrid.jdbc.driver;

public class CUBRIDEnum {
	private short shortVal;
	private String stringVal;
	private CUBRIDConnection con;

	public CUBRIDEnum(CUBRIDConnection con, short shortVal) {
		this.con = con;
		this.shortVal = shortVal;
		stringVal = null;
	}

	public CUBRIDEnum(CUBRIDEnum e) {
		con = e.con;
		shortVal = e.shortVal;
	}

	public short getShortVal() {
		return shortVal;
	}

	public String getStringVal() {
		if (stringVal != null)
			return stringVal;
		return shortVal + "";
	}

	public void setStringVal(String stringVal) {
		this.stringVal = stringVal;
	}

	public String toString() {
		if (stringVal != null)
			return stringVal;
		return shortVal + "";
	}
}
