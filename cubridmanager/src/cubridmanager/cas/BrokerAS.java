package cubridmanager.cas;

public class BrokerAS {
	public String ID;
	public String PID;
	public String ClientRequests;
	public String PSize;
	public String Status;
	public String CPU;
	public String CTime;
	public String LastAccessTime;
	public String JobInfo;

	public BrokerAS(String p_ID, String p_PID, String p_ClientRequests,
			String p_PSize, String p_Status, String p_CPU, String p_CTime,
			String p_LastAccessTime, String p_JobInfo) {
		ID = new String(p_ID);
		PID = new String(p_PID);
		ClientRequests = new String(p_ClientRequests);
		PSize = new String(p_PSize);
		Status = new String(p_Status);
		CPU = new String(p_CPU);
		CTime = new String(p_CTime);
		LastAccessTime = new String(p_LastAccessTime);
		JobInfo = new String(p_JobInfo);
	}
}
