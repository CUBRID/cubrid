package cubridmanager.cubrid;

public class DBResolution {
	public String name;
	public String className;
	public String alias;

	public DBResolution(String p_name, String p_className, String p_alias) {
		name = new String(p_name);
		className = new String(p_className);
		alias = new String(p_alias);
	}
}
