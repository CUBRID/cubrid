package nbench.common;

import java.io.OutputStream;

public interface PerfLogIfs {
	enum LogType {
		TRANSACTION, FRAME, QUERY
	};

	void setupLog(OutputStream ous, long base_time);

	void teardownLog();

	void startLogItem(long time, LogType type, String name) throws Exception;

	void endLogItem(long time, LogType type, String name) throws Exception;

	void endsWithError(long time, LogType type, String emsg) throws Exception;
}
