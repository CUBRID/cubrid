package cubridmanager.cubrid;

import java.util.ArrayList;

import cubridmanager.MainRegistry;

public class SchemaInfo implements Comparable {
	public String name;
	public String type;
	public String schemaowner;
	public String virtual;
	public String ldbname;
	public String is_partitionGroup;
	public String partitionGroupName;
	public ArrayList classAttributes = null; // DBAttribute
	public ArrayList attributes = null; // DBAttribute
	public ArrayList classMethods = null; // DBMethod
	public ArrayList methods = null; // DBMethod
	public ArrayList classResolutions = null; // DBResolution
	public ArrayList resolutions = null; // DBResolution
	public ArrayList constraints = null; // Constraint
	public ArrayList superClasses = null; // String
	public ArrayList subClasses = null;
	public ArrayList OidList = null;
	public ArrayList methodFiles = null;
	public ArrayList querySpecs = null;

	public SchemaInfo(String p_name, String p_type, String p_schemaowner,
			String p_virtual) {
		name = new String(p_name);
		type = new String(p_type);
		schemaowner = new String(p_schemaowner);
		virtual = new String(p_virtual);
	}

	public static ArrayList SchemaInfo_get(String dbname) {
		AuthItem ai = MainRegistry.Authinfo_find(dbname);
		if (ai == null)
			return null;
		return ai.Schema;
	}

	public boolean isSystemClass() {
		if (type.equals("system"))
			return true;
		else
			return false;
	}

	public int compareTo(Object obj) {
		return name.compareTo(((SchemaInfo) obj).name);
	}
}
