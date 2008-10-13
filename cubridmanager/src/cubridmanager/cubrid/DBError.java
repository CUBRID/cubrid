package cubridmanager.cubrid;

public class DBError {
	public String DbName = null;
	public String Time = null;
	public String ErrorCode = null;
	public String Description = null;

	public DBError(String pDbName, String pTime, String pErrorCode,
			String pDescription) {
		DbName = pDbName;
		Time = pTime;
		ErrorCode = pErrorCode;
		Description = pDescription;
	}
}
