package cubridmanager.cubrid;

public class DBUserInfo {
	public String dbname = null;
	public String dbuser = null;
	public String dbpassword = null;

	public boolean isDBAGroup = false;

	public DBUserInfo(String name, String user, String password) {
		dbname = new String(name);
		dbuser = new String(user);
		dbpassword = new String(password);
	}
}
