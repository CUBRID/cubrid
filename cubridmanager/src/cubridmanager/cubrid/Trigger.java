package cubridmanager.cubrid;

import java.util.ArrayList;

import cubridmanager.MainRegistry;

public class Trigger implements Comparable {
	public String Name;
	public String ConditionTime;
	public String EventType;
	public String EventTarget;
	public String ConditionString;
	public String ActionTime;
	public String ActionType;
	public String ActionString;
	public String Status;
	public String Priority;

	public Trigger(String p_Name, String p_ConditionTime, String p_EventType,
			String p_EventTarget, String p_ConditionString,
			String p_ActionTime, String p_ActionType, String p_ActionString,
			String p_Status, String p_Priority) {
		Name = new String(p_Name);
		ConditionTime = new String(p_ConditionTime);
		EventType = new String(p_EventType);
		EventTarget = new String(p_EventTarget);
		ConditionString = new String(p_ConditionString);
		ActionTime = new String(p_ActionTime);
		ActionType = new String(p_ActionType);
		ActionString = new String(p_ActionString);
		Status = new String(p_Status);
		Priority = new String(p_Priority);
	}

	public static ArrayList TriggerInfo_get(String dbname) {
		AuthItem ai = MainRegistry.Authinfo_find(dbname);
		if (ai == null)
			return null;
		return ai.TriggerInfo;
	}

	public int compareTo(Object obj) {
		return Name.compareTo(((Trigger) obj).Name);
	}
}
