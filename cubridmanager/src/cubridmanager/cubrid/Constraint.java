package cubridmanager.cubrid;

import java.util.ArrayList;

public class Constraint {
	public String name;
	public String type;
	public ArrayList classAttributes = null; // String
	public ArrayList attributes = null;
	public ArrayList rule = null;

	public Constraint(String p_name, String p_type) {
		name = new String(p_name);
		type = new String(p_type);
	}
}
