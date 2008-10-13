package cubridmanager.cubrid;

import java.util.ArrayList;

public class DBMethod {
	public String name;
	public String inherit;
	public ArrayList arguments = null;
	public String function;

	public DBMethod(String p_name, String p_inherit, String p_function) {
		name = new String(p_name);
		inherit = new String(p_inherit);
		function = new String(p_function);
	}
}
