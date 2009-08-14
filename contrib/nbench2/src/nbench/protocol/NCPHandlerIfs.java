package nbench.protocol;

import org.json.JSONObject;

public interface NCPHandlerIfs {
	enum Mode {
		SERVER, CLIENT
	}

	Mode getMode();

	String getProtocolProperty(String name) throws Exception;

	void setProtocolProperty(String name, String value) throws Exception;

	void handleEnd(NCPEngine nCPEngine, String reason);

	void handleMessage(NCPEngine nCPEngine, String message, JSONObject msg)
			throws Exception;

	void handleRawData(NCPEngine nCPEngine, byte[] data, int len, boolean more)
			throws Exception;
}
