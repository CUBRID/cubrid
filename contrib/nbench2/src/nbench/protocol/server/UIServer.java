package nbench.protocol.server;

import java.io.OutputStream;

import org.json.JSONStringer;
import org.json.JSONObject;

import nbench.protocol.NCPEngine;
import nbench.protocol.NCPResult;
import nbench.protocol.NCPException;
import nbench.protocol.NCPProcedureIfs;
import nbench.protocol.NCPU;

public class UIServer implements NCPProcedureIfs {
	PrimeController pc;

	public UIServer(PrimeController pc) {
		this.pc = pc;
	}

	@Override
	public void onEnd(NCPEngine engine, String reason, NCPResult result) {
	}

	@Override
	public void onMessage(NCPEngine engine, int id, JSONObject obj,
			boolean isError, String errorMessage, boolean isWarning,
			String warningMessage, NCPResult result) throws Exception {

		JSONStringer stringer = null;
		
		switch (id) {
		case NCPU.LIST_REPO_REQUEST:
			stringer = engine.openMessage(NCPU.LIST_REPO_RESPONSE);
			stringer = pc.handleListRepoRequest(stringer);
			engine.sendMessage(engine.closeMessage(stringer, result));
			break;

		case NCPU.LIST_RUNNER_REQUEST:
			stringer = engine.openMessage(NCPU.LIST_RUNNER_RESPONSE);
			stringer = pc.handleListRunnerRequest(stringer);
			engine.sendMessage(engine.closeMessage(stringer, result));
			break;

		case NCPU.PREPARE_REQUEST:
			pc.handlePrepareRequest(obj.getString("benchmark"), result
					.enter("handlePrepareRequest"));
			result.leave();
			engine.sendSimpleResponse(NCPU.PREPARE_RESPONSE, result);
			break;

		case NCPU.SETUP_REQUEST:
			pc.handleSetupRequest(result.enter("handleSetupRequest"));
			result.leave();
			engine.sendSimpleResponse(NCPU.SETUP_RESPONSE, result);
			break;
			
		case NCPU.START_REQUEST:
			pc.handleStartRequest(result.enter("handleStartRequest"));
			result.leave();
			engine.sendSimpleResponse(NCPU.START_RESPONSE, result);
			break;

		case NCPU.STATUS_REQUEST:
			stringer = engine.openMessage(NCPU.STATUS_RESPONSE);
			stringer = pc.handleStatusRequest(stringer, result
					.enter("handleStatusRequest"));
			result.leave();
			engine.sendMessage(engine.closeMessage(stringer, result));
			break;

		case NCPU.STOP_REQUEST:
			pc.handleStopRequest(result.enter("handleStopRequest"));
			result.leave();
			engine.sendSimpleResponse(NCPU.STOP_RESPONSE, result);
			break;
		case NCPU.GATHER_REQUEST:
			pc.handleGatherRequest(result.enter("handleGatherRequest"));
			result.leave();
			engine.sendSimpleResponse(NCPU.GATHER_RESPONSE, result);
			break;
		case NCPU.SHUTDOWN_REQUEST:
			pc.handleShutdownRequest(result.enter("handleShutDownRequest"));
			result.leave();
			engine.sendSimpleResponse(NCPU.SHUTDOWN_RESPONSE, result);
			System.exit(0);
			break;
		default:
			engine.disconnect("invalid request:" + obj.toString());
		}
		
		result.clearAll();
	}

	@Override
	public void onRawDataEnd(NCPEngine e, OutputStream os, NCPResult result)
			throws Exception {
		new Exception().printStackTrace();
		throw new NCPException("should not happen");
	}

	@Override
	public OutputStream onRawDataStart(NCPEngine engine, NCPResult result)
			throws Exception {
		new Exception().printStackTrace();
		throw new NCPException("should not happen");
	}

	@Override
	public boolean processDone() {
		return false;
	}

	@Override
	public String getName() {
		return "NCPUServer";
	}
}
