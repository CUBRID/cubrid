package cubridmanager.diag;

import java.util.Date;

public class DiagDataActivityLog {
	public static final int ACT_LOG_UNDEFINED_STATE = -1;
	public static final int ACT_LOG_READY = 0;
	public static final int ACT_LOG_LOGGING = 1;
	public static final int ACT_LOG_END = 2;
	public String name = null;
	public String desc = null;
	public String templateName = null;
	public Date logStartTime = null;
	public Date logEndTime = null;
	public int loggingState;

	public DiagDataActivityLog() {
		loggingState = ACT_LOG_UNDEFINED_STATE;
	}

	public DiagDataActivityLog(DiagDataActivityLog clone) {

	}

}
