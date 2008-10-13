package cubridmanager.cubrid;

import java.util.ArrayList;

import cubridmanager.MainRegistry;

public class AutoQuery {
	public String TimeDetail;
	public String QueryString;
	public String Period;
	public String QueryID;

	public AutoQuery(String p_QueryID, String p_Period, String p_TimeDetail,
			String p_QueryString) {
		TimeDetail = new String(p_TimeDetail);
		QueryString = new String(p_QueryString);
		Period = new String(p_Period);
		QueryID = new String(p_QueryID);
	}

	public static ArrayList AutoQueryInfo_get(String dbname) {
		AuthItem ai = MainRegistry.Authinfo_find(dbname);
		if (ai == null)
			return null;
		return ai.AutoQueryInfo;
	}

}
