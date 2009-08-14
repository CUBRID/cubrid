package nbench.engine;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.URL;
import java.net.URLClassLoader;
import java.util.HashSet;
import java.util.Properties;
import java.util.LinkedList;
import java.util.Set;
import java.util.logging.Logger;
import java.util.logging.Level;

import org.json.JSONStringer;
import org.json.JSONObject;

import nbench.common.BenchmarkStatus;
import nbench.common.ExecutionIfs;
import nbench.common.ExecutionListenerIfs;
import nbench.common.NBenchException;
import nbench.common.ResourceIfs;
import nbench.common.ResourceProviderIfs;
import nbench.protocol.NCPEngine;
import nbench.protocol.NCPResult;
import nbench.protocol.NCPException;
import nbench.protocol.NCPProcedureIfs;
import nbench.protocol.NCPS;

public class LoadGenerator implements NCPProcedureIfs, ExecutionListenerIfs {
	private ResourceProviderIfs rp;
	private Properties props;
	private ResourceIfs logResource;
	private Logger logger;
	private FrontEngineContext context;
	private ExecutionIfs setupExecution;
	private ExecutionIfs benchmarkExecution;
	private BenchmarkStatus _status;

	public LoadGenerator(ResourceProviderIfs rp) throws Exception {
		this.rp = rp;
		logResource = rp.newResource("res:LoadGenerator.log");
		logger = ResourceErrorLogger.getLogger("nbench.engine.LoadGenerator",
				logResource);
		_status = BenchmarkStatus.INITIALIZED;
	}

	public FrontEngineContext getContext() {
		return context;
	}

	public Properties getProperties() {
		return props;
	}

	private BenchmarkStatus getStatus() {
		return _status;
	}

	private boolean isStatus(BenchmarkStatus status) {
		return _status.compareTo(status) == 0;
	}

	private void setStatus(BenchmarkStatus status) {
		if (status.compareTo(_status) != 0) {
			logger.fine("status change from:" + _status + " to:" + status);
			_status = status;
		}

		if (isStatus(BenchmarkStatus.ABORTED)
				|| isStatus(BenchmarkStatus.FINISHED)) {
			// no more error logging
			ResourceErrorLogger.removeLogger(logger);
		}
	}

	private void setError(NCPResult result, String msg) {
		logger.severe(msg);
		result.setError(msg);
	}

	private void setError(NCPResult result, Exception e) {
		logger.log(Level.SEVERE, e.getMessage(), e);
		result.setError(e);
	}

	private BenchmarkStatus onStopRequest(NCPEngine engine, NCPResult result)
			throws Exception {
		BenchmarkStatus r;

		switch (getStatus()) {

		case SETUP:
			r = setupExecution.exeStop();
			if (r.compareTo(BenchmarkStatus.FINISHED) != 0) {
				setError(result, "can't finish current status:"
						+ setupExecution.getStatusString());
			}
			return r;

		case STARTED:
			r = benchmarkExecution.exeStop();
			if (r.compareTo(BenchmarkStatus.FINISHED) != 0) {
				setError(result, "can't finish current status:"
						+ benchmarkExecution.getStatusString());
			}
			return r;

		case INITIALIZED:
		case PREPARED:
		case FINISHED:
		case ABORTED:
		case SETUP_FINISHED:
		default:
			return BenchmarkStatus.FINISHED;
		}
	}

	private String getProgressString() {
		String str = getStatus().toString();

		if (isStatus(BenchmarkStatus.SETUP)) {
			str = str + "," + setupExecution.getStatusString();
		} else if (isStatus(BenchmarkStatus.STARTED)) {
			str = str + "," + benchmarkExecution.getStatusString();
		}
		return str;
	}

	private BenchmarkStatus onGatherRequest(NCPEngine engine,
			JSONStringer stringer, NCPResult result) throws Exception {
		if (!isStatus(BenchmarkStatus.FINISHED)
				&& !isStatus(BenchmarkStatus.ABORTED)) {
			result.setWarning("current state is :" + getStatus());
			return getStatus();
		}

		ResourceErrorLogger.flushAll();

		HashSet<ResourceIfs> resources = new HashSet<ResourceIfs>();
		resources.add(logResource);

		Set<ResourceIfs> tmp;
		if (setupExecution != null) {
			tmp = setupExecution.getLogResources();
			if (tmp != null) {
				resources.addAll(tmp);
			}
		}

		if (benchmarkExecution != null) {
			tmp = benchmarkExecution.getLogResources();
			if (tmp != null) {
				resources.addAll(tmp);
			}
		}

		for (ResourceIfs resource : resources) {
			if (resource.exists()) {
				InputStream ris = resource.getResourceInputStream();

				// send LOG_INFO
				JSONStringer log_info = engine.openMessage(NCPS.LOG_INFO);
				log_info.key("name");
				log_info.value(resource.getResourceString());
				engine.sendMessage(engine.closeMessage(log_info));

				// send Raw Data
				engine.sendInputStream(ris);
				resource.close();
			}
		}
		// engine.sendSimpleResponse(NCPS.GATHER_LOG_RESPONSE, result);
		return getStatus();
	}

	private BenchmarkStatus onStartRequest(NCPEngine engine, NCPResult result)
			throws Exception {

		if (!isStatus(BenchmarkStatus.SETUP_FINISHED)) {
			setError(result, "Invalid status for start request:" + getStatus());
			return getStatus();
		}

		context.setBaseTime(System.currentTimeMillis());

		BenchmarkStatus r = benchmarkExecution.exeStart(this);
		if (r.compareTo(BenchmarkStatus.STARTED) != 0) {
			setError(result, benchmarkExecution.getStatusString());
		}

		return r;
	}

	private BenchmarkStatus onSetupRequest(NCPEngine engine, NCPResult result)
			throws Exception {
		if (!isStatus(BenchmarkStatus.PREPARED)) {
			setError(result, "Invalid status for setup request:" + getStatus());
			return getStatus();
		}

		BenchmarkStatus r = setupExecution.exeStart(this);
		if (r.compareTo(BenchmarkStatus.SETUP) != 0) {
			setError(result, setupExecution.getStatusString());
		}
		return r;
	}

	private BenchmarkStatus onPrepareRequest(NCPEngine engine, String res,
			NCPResult result) throws Exception {

		switch (getStatus()) {
		case INITIALIZED:
			break;
		case PREPARED:
		case SETUP:
		case SETUP_FINISHED:
		case STARTED:
		case FINISHED:
		case ABORTED:
			setError(result, "out of state. current state is:" + getStatus());
			return getStatus();
		}

		ResourceIfs resourceIfs = prepareResource(engine, res, result
				.enter("prepareResource"));
		result.leave();

		if (resourceIfs == null) {
			return BenchmarkStatus.ABORTED;
		}

		props = new Properties();
		props.load(resourceIfs.getResourceInputStream());
		resourceIfs.close();

		boolean r = parseAndPrepareProps(engine, result
				.enter("parseAndPrepareProps"));
		result.leave();

		if (r) {
			return BenchmarkStatus.PREPARED;
		} else {
			return BenchmarkStatus.ABORTED;
		}
	}

	private boolean parseAndPrepareProps(NCPEngine engine, NCPResult result)
			throws Exception {
		String val;
		int i;

		/* ==================== */
		/* BENCHMARK PROPERTIES */
		/* ==================== */
		String rs[] = { "benchmark" };

		for (i = 0; i < rs.length; i++) {
			val = props.getProperty(rs[i]);

			if (val == null) {
				setError(result, rs[i] + " property unspecified");
				return false;
			}

			if (prepareResource(engine, val, result) == null) {
				return false;
			}
		}

		/* ===================== */
		/* RESOURCE DISTRIBUTION */
		/* ===================== */
		for (String s : props.stringPropertyNames()) {

			if (s.startsWith("resource.")) {
				String rsc = props.getProperty(s);
				ResourceIfs resourceIfs = prepareResource(engine, rsc, result);

				if (resourceIfs == null) {
					return false;
				}
			}
		}

		/* =========================================== */
		/* JAR DISTRIBUTION : add to class loader path */
		/* =========================================== */
		LinkedList<URL> jar_url_list = new LinkedList<URL>();
		for (String s : props.stringPropertyNames()) {

			if (s.startsWith("jar.")) {
				String rsc = props.getProperty(s);
				ResourceIfs resourceIfs = prepareResource(engine, rsc, result);

				if (resourceIfs == null) {
					return false;
				}

				jar_url_list.add(new URL("jar:"
						+ resourceIfs.getURL().toExternalForm() + "!/"));
			}
		}
		URLClassLoader extLoader = null;
		if (jar_url_list.size() > 0) {
			URL urls[] = new URL[jar_url_list.size()];
			for (i = 0; i < urls.length; i++) {
				urls[i] = jar_url_list.get(i);
			}
			extLoader = URLClassLoader.newInstance(urls);
		}

		/* ========================== */
		/* USER DEFINED HOST VARIABLE */
		/* ========================== */
		UserHostVar user_hostvar = null;

		for (String k : props.stringPropertyNames()) {
			if (k.startsWith("hostvar.")) {
				String spec = k.substring("hostvar.".length());
				String var_name = spec.substring(0, spec.indexOf('.'));

				if (user_hostvar == null) {
					user_hostvar = new UserHostVar(var_name);
				} else if (!var_name.equals(user_hostvar.name)) {
					Exception e = new Exception(
							"Only one user supplied host variable:" + var_name);
					logger.log(Level.SEVERE, e.getMessage(), e);
					throw e;
				}

				String remain = spec.substring(spec.indexOf('.') + 1);
				if (remain.equals("class")) {
					user_hostvar.class_name = props.getProperty(k);
				} else if (remain.startsWith("props.")) {
					user_hostvar.map.put(remain.substring("props.".length()),
							props.getProperty(k));
				}
			}
		}

		/* ========================== */
		/* create NFrontEngineContext */
		/* ========================== */
		context = new FrontEngineContext(props, rp, extLoader);
		context.initialize();
		context.setUserHostVariable(user_hostvar);

		/* ===================== */
		/* make execution object */
		/* ===================== */
		setupExecution = new SetupExecution(this);
		benchmarkExecution = new BenchmarkExecution(this);
		setStatus(BenchmarkStatus.PREPARED);
		return true;
	}

	class ResourceRequestProc implements NCPProcedureIfs {
		private OutputStream os;
		private boolean done;

		ResourceRequestProc(OutputStream os) {
			this.os = os;
			done = false;
		}

		@Override
		public void onEnd(NCPEngine engine, String reason, NCPResult result) {
			setError(result, reason);
			logger.info("onEnd():" + reason);
		}

		@Override
		public void onMessage(NCPEngine engine, int id, JSONObject obj,
				boolean isError, String errorMessage, boolean isWarning,
				String warningMessage, NCPResult result) throws Exception {
			if (id != NCPS.RESOURCE_RESPONSE) {
				setError(result, "invalid message received :" + obj.toString());
				done = true;
			} else {
				if (isError) {
					done = true;
				}
			}
		}

		@Override
		public OutputStream onRawDataStart(NCPEngine engine, NCPResult result)
				throws Exception {
			return os;
		}

		@Override
		public void onRawDataEnd(NCPEngine e, OutputStream os, NCPResult result)
				throws Exception {
			os.close();
			done = true;
			return;
		}

		@Override
		public boolean processDone() {
			return done;
		}

		@Override
		public String getName() {
			return "ResourceRequestProc";
		}
	}

	private ResourceIfs prepareResource(NCPEngine engine, String res,
			NCPResult result) throws Exception {

		ResourceIfs resource = rp.getResource(res);
		if (resource == null) {
			resource = rp.newResource(res);
			OutputStream os = resource.getResourceOutputStream();

			JSONStringer stringer = engine.openMessage(NCPS.RESOURCE_REQUEST);
			stringer.key("resource");
			stringer.value(res);
			engine.sendMessage(engine.closeMessage(stringer));
			ResourceRequestProc proc = new ResourceRequestProc(os);
			engine.process(proc, result);
			resource.close();
			if (result.hasError()) {
				rp.removeResource(res);
				return null;
			}
		}
		return resource;
	}

	@Override
	public void onMessage(NCPEngine engine, int id, JSONObject obj,
			boolean isError, String errorMessage, boolean isWarning,
			String warningMessage, NCPResult result) throws Exception {

		if (isError) {
			setError(result, errorMessage);
			return;
		}

		result.clearAll();
		BenchmarkStatus r;
		JSONStringer stringer = null;

		switch (id) {
		case NCPS.PREPARE_REQUEST:
			logger.entering(this.getClass().getName(), "onPrepareRequest");
			r = onPrepareRequest(engine, obj.getString("resource"), result
					.enter("onPrepareRequest"));
			result.leave();
			logger.exiting(this.getClass().getName(), "onPrepareRequest",
					result);

			setStatus(r);
			engine.sendSimpleResponse(NCPS.PREPARE_RESPONSE, result);
			return;

		case NCPS.SETUP_REQUEST:
			logger.entering(this.getClass().getName(), "onSetupRequest");
			r = onSetupRequest(engine, result.enter("onSetupRequest"));
			result.leave();
			logger.exiting(this.getClass().getName(), "onSetupRequest", result);

			setStatus(r);
			engine.sendSimpleResponse(NCPS.SETUP_RESPONSE, result);
			return;

		case NCPS.START_REQUEST:
			logger.entering(this.getClass().getName(), "onStartRequest");
			r = onStartRequest(engine, result.enter("onStartRequest"));
			result.leave();
			logger.exiting(this.getClass().getName(), "onStartRequest", result);

			setStatus(r);
			engine.sendSimpleResponse(NCPS.START_RESPONSE, result);
			return;

		case NCPS.STOP_REQUEST:
			logger.entering(this.getClass().getName(), "onStopRequest");
			r = onStopRequest(engine, result.enter("onStopRequest"));
			result.leave();
			logger.exiting(this.getClass().getName(), "onStopRequest", result);

			setStatus(r);
			engine.sendSimpleResponse(NCPS.STOP_RESPONSE, result);
			return;

		case NCPS.STATUS_REQUEST:
			stringer = engine.openMessage(NCPS.STATUS_RESPONSE);
			stringer.key("status");
			stringer.value(getProgressString());
			engine.sendMessage(engine.closeMessage(stringer, result));
			return;

		case NCPS.GATHER_REQUEST:
			stringer = engine.openMessage(NCPS.GATHER_RESPONSE);
			logger.entering(this.getClass().getName(), "onGatherRequest");

			r = onGatherRequest(engine, stringer, result
					.enter("onGatherRequest"));
			result.leave();
			logger
					.exiting(this.getClass().getName(), "onGatherRequest",
							result);

			setStatus(r);
			engine.sendMessage(engine.closeMessage(stringer, result));
			return;
		default:
			throw new NCPException("unsupported message received:" + obj);
		}
	}

	@Override
	public void onEnd(NCPEngine engine, String reason, NCPResult result) {

		if (isStatus(BenchmarkStatus.SETUP)) {
			BenchmarkStatus r = setupExecution.exeStop();

			if (r.compareTo(BenchmarkStatus.FINISHED) != 0) {
				result.setError(setupExecution.getStatusString());
			}
		} else if (isStatus(BenchmarkStatus.STARTED)) {
			BenchmarkStatus r = benchmarkExecution.exeStop();

			if (r.compareTo(BenchmarkStatus.FINISHED) != 0) {
				result.setError(benchmarkExecution.getStatusString());
			}
		}

		try {
			logResource.close();
		} catch (NBenchException e) {
			logger.log(Level.SEVERE, "resource close failure", e);
			setError(result, e);
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
		return false;
	}

	@Override
	public String getName() {
		return "NCPLoadGenerator";
	}

	// ------------------------------------
	// ExecutionListenerIfs implementation
	// ------------------------------------
	@Override
	public void exeAborted(ExecutionIfs ifs, Exception e) {
		logger.log(Level.SEVERE, "execution aborted", e);
		
		setStatus(BenchmarkStatus.ABORTED);
	}

	@Override
	public void exeStopped(ExecutionIfs ifs) {
		logger.fine("execution stopped:");

		setStatus(BenchmarkStatus.FINISHED);
	}
	
	@Override
	public void exeCompleted(ExecutionIfs ifs) {
		logger.fine("execution completed:" + ifs.getStatusString());
		
		if (ifs == setupExecution) {
			setStatus(BenchmarkStatus.SETUP_FINISHED);
		} else if (ifs == benchmarkExecution) {
			setStatus(BenchmarkStatus.FINISHED);
		}
	}
}
