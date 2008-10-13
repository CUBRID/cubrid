package cubridmanager.cas;

public class BrokerJobStatus {
	public String ID;
	public String Priority;
	public String IP;
	public String jobTime;
	public String Request;

	public BrokerJobStatus(String p_ID, String p_Priority, String p_IP,
			String p_jobTime, String p_Request) {
		ID = new String(p_ID);
		Priority = new String(p_Priority);
		IP = new String(p_IP);
		jobTime = new String(p_jobTime);
		Request = new String(p_Request);
	}

}
