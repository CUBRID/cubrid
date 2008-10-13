package cubridmanager.cubrid;

import java.util.ArrayList;

import cubridmanager.MainRegistry;

public class UserInfo implements Comparable {
	public String userName;
	public String id;
	public String password;
	public boolean isGroup;
	public ArrayList groupNames; // String
	public ArrayList memberNames;
	public ArrayList groups; // UserInfo
	public ArrayList members;
	public ArrayList authorizations; // Authorizations

	public UserInfo(String p_Name, String p_id, String p_password,
			boolean p_isGroup) {
		userName = new String(p_Name);
		id = new String(p_id);
		password = new String(p_password);
		isGroup = p_isGroup;
		groupNames = new ArrayList();
		memberNames = new ArrayList();
		groups = new ArrayList();
		members = new ArrayList();
		authorizations = new ArrayList();
	}

	public static UserInfo UserInfo_find(ArrayList ar, String user) {
		for (int i = 0, n = ar.size(); i < n; i++) {
			UserInfo ui = (UserInfo) ar.get(i);
			if (ui.userName.equals(user))
				return ui;
		}
		return null;
	}

	public static ArrayList UserInfo_get(String dbname) {
		AuthItem ai = MainRegistry.Authinfo_find(dbname);
		if (ai == null)
			return null;
		return ai.UserInfo;
	}

	public int compareTo(Object obj) {
		return userName.compareTo(((UserInfo) obj).userName);
	}
}
