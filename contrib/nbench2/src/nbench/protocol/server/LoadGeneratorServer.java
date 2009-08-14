package nbench.protocol.server;

import java.io.File;
import java.io.OutputStream;
import org.json.JSONObject;
import org.json.JSONStringer;
import nbench.common.ResourceProviderIfs;
import nbench.common.ResourceIfs;
import nbench.protocol.NCPEngine;
import nbench.protocol.NCPException;
import nbench.protocol.NCPResult;
import nbench.protocol.NCPProcedureIfs;
import nbench.protocol.NCPS;

public class LoadGeneratorServer {
	private PrimeController pc;

	public LoadGeneratorServer(PrimeController pc) {
		this.pc = pc;
	}

	// /////////////////////////////////////////////////////
	// Procedure class
	// /////////////////////////////////////////////////////

	class PrepareProc implements NCPProcedureIfs {
		PrimeController pc;
		private boolean done;

		public PrepareProc(PrimeController pc) {
			this.pc = pc;
			done = false;
		}

		@Override
		public void onEnd(NCPEngine engine, String reason, NCPResult result) {
			result.setError(reason);
			done = true;
		}

		@Override
		public void onMessage(NCPEngine engine, int id, JSONObject obj,
				boolean isError, String errorMessage, boolean isWarning,
				String warningMessage, NCPResult result) throws Exception {

			if (id == NCPS.PREPARE_RESPONSE) {
				done = true;
			} else if (id == NCPS.RESOURCE_REQUEST) {
				done = false;

				String res_str = obj.getString("resource");
				ResourceProviderIfs rp = pc.getResourceProvider();
				ResourceIfs res = null;
				try {
					res = rp.getResource(res_str);
				} catch (Exception e) {
					result.setError(e);
					engine.sendSimpleResponse(NCPS.RESOURCE_RESPONSE, result);
					done = true;
					return;
				}

				if (res == null) {
					String err = res_str + " does not exists";
					result.setError(err);
					engine.sendSimpleResponse(NCPS.RESOURCE_RESPONSE, result);
					done = true;
					return;
				} else {
					engine.sendSimpleResponse(NCPS.RESOURCE_RESPONSE, result);
					engine.sendInputStream(res.getResourceInputStream());
					res.close();
				}
			} else {
				done = true;
				result.enter("PrepareProc");
				result
						.setError("unexpected message received:"
								+ obj.toString());
				result.leave();
			}
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
			return done;
		}

		@Override
		public String getName() {
			return "PrepareProc";
		}
	}

	class ExpectProc implements NCPProcedureIfs {
		private int expect_id;
		private JSONObject resp;
		private boolean done;

		ExpectProc(int expect_id) {
			this.expect_id = expect_id;
			resp = null;
			done = false;
		}

		@Override
		public void onEnd(NCPEngine engine, String reason, NCPResult result) {
			result.setError(reason);
			done = true;
		}

		@Override
		public void onMessage(NCPEngine engine, int id, JSONObject obj,
				boolean isError, String errorMessage, boolean isWarning,
				String warningMessage, NCPResult result) throws Exception {
			
			if (id != expect_id) {
				result.enter("ExpectProc");
				result
						.setError("unexpected message received:"
								+ obj.toString());
				result.leave();
			} else if (isError) {
				;
			} else {
				resp = obj;
			}
			done = true;
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
			return done;
		}

		@Override
		public String getName() {
			return "ExpectProc";
		}
	}

	class GatherLogProc implements NCPProcedureIfs {
		private String res_base;
		private ResourceIfs resource;
		private boolean done;
		private String log_name;

		GatherLogProc(String res_base) {
			this.res_base = res_base;
			resource = null;
			done = false;
			log_name = null;
		}

		@Override
		public void onEnd(NCPEngine engine, String reason, NCPResult result) {
			result.setError(reason);
		}

		@Override
		public void onMessage(NCPEngine engine, int id, JSONObject obj,
				boolean isError, String errorMessage, boolean isWarning,
				String warningMessage, NCPResult result) throws Exception {
			
			if (resource != null) {
				resource.close();
				resource = null;
			}

			if (isError) {
				done = true;
				return;
			}

			if (id == NCPS.LOG_INFO) {
				log_name = obj.getString("name");
				resource = pc.getResourceProvider().newResource(
						res_base + File.separator + log_name);
			} else if (id == NCPS.GATHER_RESPONSE) {
				done = true;
			} else {
				result.setError("unexpected message:" + obj.toString());
				done = true;
			}
		}

		@Override
		public void onRawDataEnd(NCPEngine e, OutputStream os, NCPResult result)
				throws Exception {
			if (resource == null) {
				result.setError("resource was not provided");
			}
			resource.close();
			resource = null;
			log_name = null;
		}

		@Override
		public OutputStream onRawDataStart(NCPEngine engine, NCPResult result)
				throws Exception {
			if (resource == null) {
				result.setError("unexpected raw data stream");
				return null;
			}
			return resource.getResourceOutputStream();
		}

		@Override
		public boolean processDone() {
			return done;
		}

		@Override
		public String getName() {
			return "GatherLogProc";
		}

	}

	// ////////////////////////////////////////////////////
	// Prime Controller is the caller
	// ////////////////////////////////////////////////////

	public boolean prepareProc(NCPEngine engine, String prop_res,
			NCPResult result) throws Exception {
		JSONStringer stringer = engine.openMessage(NCPS.PREPARE_REQUEST);
		stringer.key("resource");
		stringer.value(prop_res);
		engine.sendMessage(engine.closeMessage(stringer));
		PrepareProc proc = new PrepareProc(pc);
		engine.process(proc, result);
		return !result.hasError();
	}

	public boolean setupProc(NCPEngine engine, NCPResult result)
			throws Exception {
		engine.sendSimpleRequest(NCPS.SETUP_REQUEST);
		engine.process(new ExpectProc(NCPS.SETUP_RESPONSE), result);
		return !result.hasError();
	}

	public boolean startProc(NCPEngine engine, NCPResult result)
			throws Exception {
		engine.sendSimpleRequest(NCPS.START_REQUEST);
		engine.process(new ExpectProc(NCPS.START_RESPONSE), result);
		return !result.hasError();
	}

	public boolean stopProc(NCPEngine engine, NCPResult result)
			throws Exception {
		engine.sendSimpleRequest(NCPS.STOP_REQUEST);
		engine.process(new ExpectProc(NCPS.STOP_RESPONSE), result);
		return !result.hasError();
	}

	public String statusProc(NCPEngine engine, NCPResult result)
			throws Exception {
		engine.sendSimpleRequest(NCPS.STATUS_REQUEST);
		ExpectProc proc = new ExpectProc(NCPS.STATUS_RESPONSE);
		engine.process(proc, result);
		if (result.hasError()) {
			return null;
		}
		return proc.resp.getString("status");
	}

	public boolean gatherProc(NCPEngine engine, String resource_base,
			NCPResult result) throws Exception {
		engine.sendSimpleRequest(NCPS.GATHER_REQUEST);
		GatherLogProc proc = new GatherLogProc(resource_base);
		engine.process(proc, result);
		return !result.hasError();
	}

	public boolean shutdownProc(NCPEngine engine, NCPResult result)
			throws Exception {
		engine.disconnect("shut down");
		return true;
	}
}
