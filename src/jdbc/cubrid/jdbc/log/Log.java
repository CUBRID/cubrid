package cubrid.jdbc.log;

public interface Log {
	void logDebug(String msg);
	void logDebug(String msg, Throwable thrown);
	void logError(String msg);
	void logError(String msg, Throwable thrown);
	void logFatal(String msg);
	void logFatal(String msg, Throwable thrown);
	void logInfo(String msg);
	void logInfo(String msg, Throwable thrown);
	void logTrace(String msg);
	void logTrace(String msg, Throwable thrown);
	void logWarn(String msg);
	void logWarn(String msg, Throwable thrown);
}
