package cubridmanager.cubrid;

public class LockWaiters {
	public String TranIndex;
	public String BlockedMode;
	public String StartAt;
	public String WaitForSec;

	public LockWaiters(String pTranIndex, String pBlockedMode, String pStartAt,
			String pWaitForSec) {
		TranIndex = pTranIndex;
		BlockedMode = pBlockedMode;
		StartAt = pStartAt;
		WaitForSec = pWaitForSec;
	}
}
