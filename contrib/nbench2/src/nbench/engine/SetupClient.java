package nbench.engine;

import java.io.OutputStream;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.logging.Level;
import java.util.logging.Logger;

import javax.script.Bindings;

import nbench.common.BackendEngineClientIfs;
import nbench.common.NBenchTransactionFailException;
import nbench.common.PerfLogIfs;
import nbench.common.ResourceIfs;

public class SetupClient implements Runnable, PerfLogIfs {
	private ScriptRun run;
	private Bindings binds;
	private Exception exception;
	private AtomicInteger clientCount;
	private ResourceIfs logResource;
	private Logger logger;
	private Status status;
	private boolean interrupted;
	private SetupContext setupContext;

	enum Status {
		IDLE, RUNNING, FINISHED, ABORTED
	}

	private class SetupContext {
		private Object privateObject;
		boolean setupDone;
		String progressString;

		SetupContext() {
			privateObject = null;
			setupDone = false;
			progressString = "";
		}

		public Object getPrivateObject() {
			return privateObject;
		}

		public void setPrivateObject(Object obj) {
			privateObject = obj;
		}

		public void setupDone() {
			setupDone = true;
		}

		public void setProgress(Object obj) {

			if (obj instanceof Double) {
				progressString = String.format("%.3f", (Double) obj);

			} else {
				progressString = obj.toString();
			}
		}
	}
	
	public class MyLogger {
		public void log(Object s) {
			logger.fine(s.toString());
		}
	}

	public SetupClient(FrontEngineContext context, ScriptRun run, Bindings binds)
			throws Exception {
		this.run = run;
		this.binds = binds;

		logResource = context.newSetupClientLogResource(this);
		logger = ResourceErrorLogger.getLogger(this.getClass().getName() + "_"
				+ getLogName(), logResource);

		status = Status.IDLE;
		interrupted = false;
	}

	public Exception getException() {
		return exception;
	}

	public boolean isInterrupted() {
		return interrupted;
	}

	public String getLogName() {
		return run.getName();
	}

	public ResourceIfs getLogResource() {
		return logResource;
	}

	public String getStatus() {
		String str = run.getName() + ":" + status.toString();

		if (status.compareTo(Status.RUNNING) == 0) {
			str = str + "(" + setupContext.progressString + ")";
		}
		return str;
	}

	public void setCounter(AtomicInteger clientCount) {
		this.clientCount = clientCount;
		clientCount.incrementAndGet();
	}

	public void interruptRun() {
		interrupted = true;
	}

	// ------------------------
	// Runnable implementation
	// ------------------------
	private void checkExcpetion(Exception e) throws Exception {
		Throwable t = e.getCause();
		Throwable prev_t = null;
		while (t != null && (prev_t != t)) {
			if (t instanceof NBenchTransactionFailException) {
				NBenchTransactionFailException ee = (NBenchTransactionFailException) t;

				logger.log(Level.SEVERE, "Transaction fail:", ee
						.getSQLException());
				return; //continue on SQLException
			}
			prev_t = t;
			t = t.getCause();
		}
		throw e;
	}

	@Override
	public void run() {
		setupContext = new SetupContext();
		binds.put("Context", setupContext);
		binds.put("Logger", new MyLogger());
		
		status = Status.RUNNING;
		try {
			while (!setupContext.setupDone && !interrupted) {
				try {
					run.run(binds);
				} catch (Exception e) {
					checkExcpetion(e);
				}
			}

			if (interrupted) {
				logger.log(Level.INFO, "Soft interrupted");
				status = Status.ABORTED;
			} else {
				logger.log(Level.INFO, "Completed");
				status = Status.FINISHED;
			}

		} catch (InterruptedException e) {
			logger.log(Level.INFO, "Hard interrupted", e);
			interrupted = true;
			status = Status.ABORTED;

		} catch (Exception e) {
			logger.log(Level.SEVERE, "Exception occurred", e);
			interrupted = true;
			exception = e;
			status = Status.ABORTED;
			return;

		} finally {
			clientCount.decrementAndGet();
			ResourceErrorLogger.removeLogger(logger);
			logger = null;

			try {
				BackendEngineClientIfs bec = (BackendEngineClientIfs) binds
						.get("__bec__");
				if (bec != null) {
					bec.close();
				}
			} catch (Exception e) {
			}
		}
	}

	// --------------------------
	// PerfLogIfs implementation
	// --------------------------
	@Override
	public void endLogItem(long time, LogType type, String name)
			throws Exception {
		// do not log performance data
	}

	@Override
	public void endsWithError(long time, LogType type, String emsg)
			throws Exception {
		status = Status.ABORTED;
	}

	@Override
	public void setupLog(OutputStream ous, long base_time) {
		// do not log performance data
	}

	@Override
	public void startLogItem(long time, LogType type, String name)
			throws Exception {
		// do not log performance data
	}

	@Override
	public void teardownLog() {
		// do not log performance data
	}
}
