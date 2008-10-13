package cubridmanager.cubrid;

import java.util.ArrayList;

import cubridmanager.MainRegistry;

public class LocalDatabase implements Comparable {
	public String Name;
	public String NameIn_Host;
	public String Type;
	public String Host;
	public String U_ID;
	public String MaxActive;
	public String MinActive;
	public String DecayConstant;
	public String Directory;
	public String ObjectID;

	public LocalDatabase(String p_Name, String p_NameIn_Host, String p_Type,
			String p_Host, String p_U_ID, String p_MaxActive,
			String p_MinActive, String p_DecayConstant, String p_Directory,
			String p_ObjectID) {
		Name = new String(p_Name);
		NameIn_Host = new String(p_NameIn_Host);
		Type = new String(p_Type);
		Host = new String(p_Host);
		U_ID = new String(p_U_ID);
		MaxActive = new String(p_MaxActive);
		MinActive = new String(p_MinActive);
		DecayConstant = new String(p_DecayConstant);
		Directory = new String(p_Directory);
		ObjectID = new String(p_ObjectID);
	}

	public static ArrayList LocalDatabaseInfo_get(String dbname) {
		AuthItem ai = MainRegistry.Authinfo_find(dbname);
		if (ai == null)
			return null;
		return ai.LDBInfo;
	}

	public int compareTo(Object obj) {
		return Name.compareTo(((LocalDatabase) obj).Name);
	}
}
