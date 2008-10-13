package cubridmanager;

import java.util.ArrayList;

import cubridmanager.cubrid.DBUserInfo;

public class CubridManagerUserInfo {
	public String cmUser = null;
	public String cmPassword = new String();
	public byte CASAuth;
	public boolean IsDBAAuth;
	public ArrayList listDBUserInfo = new ArrayList();

	public CubridManagerUserInfo(String cmUserID) {
		cmUser = new String(cmUserID);
	}

	public boolean addDBUserInfo(String name, String user, String password) {
		DBUserInfo ui = new DBUserInfo(name, user, password);
		return listDBUserInfo.add(ui);
	}

	public boolean addDBUserInfo(DBUserInfo ui) {
		return listDBUserInfo.add(ui);
	}

	public DBUserInfo getDBUserInfo(String name) {
		DBUserInfo dbuser;
		for (int i = 0, n = listDBUserInfo.size(); i < n; i++) {
			dbuser = (DBUserInfo) listDBUserInfo.get(i);
			if (dbuser.dbname.equals(name))
				return dbuser;
		}
		return null;
	}
}
