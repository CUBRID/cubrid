package cubridmanager.cubrid;

import java.util.ArrayList;

import cubridmanager.MainRegistry;

public class VolumeInfo implements Comparable {
	public String spacename;
	public String type;
	public String location;
	public String tot;
	public String free;
	public String date;
	public int order;

	public VolumeInfo(String p_spacename, String p_type, String p_location,
			String p_tot, String p_free, String p_date, int p_order) {
		spacename = new String(p_spacename);
		type = new String(p_type);
		location = new String(p_location);
		tot = new String(p_tot);
		free = new String(p_free);
		date = new String(p_date);
		order = p_order;
	}

	public static ArrayList VolumeInfo_get(String dbname) {
		AuthItem ai = MainRegistry.Authinfo_find(dbname);
		if (ai == null)
			return null;
		return ai.Volinfo;
	}

	public int compareTo(Object obj) {
		return spacename.compareTo(((VolumeInfo) obj).spacename);
	}
}
