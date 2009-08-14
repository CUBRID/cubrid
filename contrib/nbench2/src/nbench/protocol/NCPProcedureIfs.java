package nbench.protocol;

import java.io.OutputStream;
import org.json.JSONObject;

public interface NCPProcedureIfs {
	void onMessage(NCPEngine engine, int id, JSONObject obj, boolean isError,
			String errorMessage, boolean isWarning, String warningMessage, NCPResult result) throws Exception;

	OutputStream onRawDataStart(NCPEngine engine, NCPResult result)
			throws Exception;

	void onRawDataEnd(NCPEngine e, OutputStream os, NCPResult result)
			throws Exception;

	void onEnd(NCPEngine engine, String reason, NCPResult result);

	boolean processDone();
	
	String getName();
}
