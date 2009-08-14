package nbench.common;

import java.util.Map;

public interface BackendEngineClientIfs {
	void execute(String name, Map<String, Object> in, Map<String, Object> out)
			throws NBenchException;

	Object getControlObject() throws NBenchException;

	void handleTransactionAbort(Throwable t);

	void handleSessionAbort(Throwable t);

	void close() throws NBenchException;
}