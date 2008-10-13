package cubridmanager.cubrid;

import java.util.ArrayList;

public class SuperClass {
	public String name;
	public ArrayList classAttributes = null; // String
	public ArrayList attributes = null;
	public ArrayList classMethods = null;
	public ArrayList methods = null;

	public SuperClass(String p_name, ArrayList catt, ArrayList att,
			ArrayList cmeth, ArrayList meth) {
		name = new String(p_name);
		classAttributes = new ArrayList(catt);
		attributes = new ArrayList(att);
		classMethods = new ArrayList(cmeth);
		methods = new ArrayList(meth);
	}
}
