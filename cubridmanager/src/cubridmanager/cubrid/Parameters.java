package cubridmanager.cubrid;

import java.util.ArrayList;

import cubridmanager.MainRegistry;

public class Parameters {
	public String name;
	public String value;
	public String desc;
	public boolean isEnabled;

	public Parameters(String p_name, String p_value, String p_desc, boolean Ena) {
		name = new String(p_name);
		value = new String(p_value);
		desc = new String(p_desc);
		isEnabled = Ena;
	}

	public static ArrayList ParametersInfo_get(String dbname) {
		AuthItem ai = MainRegistry.Authinfo_find(dbname);
		if (ai == null)
			return null;
		return ai.ParaInfo;
	}

}
