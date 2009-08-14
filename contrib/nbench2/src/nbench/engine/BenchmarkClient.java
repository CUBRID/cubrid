package nbench.engine;

import java.util.logging.*;
import java.util.concurrent.atomic.AtomicInteger;

import javax.script.Bindings;

import nbench.common.BackendEngineClientIfs;
import nbench.common.PerfLogIfs;
import nbench.common.BenchmarkProgressIfs;
import nbench.common.BenchmarkProgressListenerIfs;
import nbench.common.NBenchException;
import nbench.common.ResourceIfs;

public class BenchmarkClient implements Runnable, BenchmarkProgressIfs {
	private int id;
	private MixRunIfs run;
	private FrontEngineContext context;
	private BenchmarkProgressListenerIfs listener;
	private Bindings binds;
	private boolean interrupted;
	private ResourceIfs errorLogResource;
	private Logger errorLogger;
	private ResourceIfs perfLogResource;
	private PerfLogIfs perfLogger;
	private AtomicInteger runnerCount;
	private static int id_serial = 0;

	// ramp-up/down related fields
	private double leap_factor;
	private static double lf_tbl[] = { 0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2,
			0.1, 0.0 };

	private static synchronized int getIDSerial() {
		id_serial++;
		return id_serial;
	}

	class MyLogger {
		public void log (Object o) {
			errorLogger.fine(o.toString());
		}
	}
	
	public BenchmarkClient(MixRunIfs run, FrontEngineContext context,
			BenchmarkProgressListenerIfs listener, Bindings binds)
			throws Exception {
		this.id = getIDSerial();
		this.run = run;
		this.context = context;
		this.listener = listener;
		this.binds = binds;
		interrupted = false;

		// set-up error log
		errorLogResource = context.newBenchmarkClientErrorLogResource(this);
		errorLogger = ResourceErrorLogger.getLogger(this.getClass().getName()
				+ id, errorLogResource);

		// set-up performance log
		perfLogResource = context.newBenchmarkClientPerfLogResource(this);
		perfLogger = (PerfLogIfs) Class.forName(
				context.getProperty("logger_class")).newInstance();
	}

	public PerfLogIfs getBenchmarkPerfLogIfs() {
		return perfLogger;
	}

	public String getPerfLogName() {
		return run.getMixName() + "_" + id;
	}

	public String getErrorLogName() {
		return run.getMixName() + "_" + id + "_errlog";
	}

	public boolean isInterrupted() {
		return interrupted;
	}

	public void setCounter(AtomicInteger runnerCount) {
		this.runnerCount = runnerCount;
		runnerCount.incrementAndGet();
	}

	public ResourceIfs getLogResource() {
		return errorLogResource;
	}

	public ResourceIfs getPerfLogResource() {
		return perfLogResource;
	}

	private void calc_leap_factor(long tot, long cur) {
		double lf = 0.0;
		if (tot > 0) {
			lf = (double) cur / (double) tot;
			if (lf < 1.0) {
				int idx = (int) (lf * 10);
				lf = lf_tbl[idx];
			}
		}
		leap_factor = lf;
	}

	private void doze(long rt) {
		if (leap_factor == 0)
			return;
		long sleep_time = (long) ((double) rt * leap_factor / (1.0 - leap_factor));
		try {
			if (sleep_time > 0) {
				Thread.sleep(sleep_time);
			}
		} catch (InterruptedException e) {
			errorLogger.info("interrupted");
		}
	}

	public void run() {
		binds.put("Logger", new MyLogger());
		
		try {
			long base_time = context.getBaseTime();
			perfLogger.setupLog(perfLogResource.getResourceOutputStream(),
					base_time);
			while (!interrupted && true) {
				long rt = run.run(perfLogger, errorLogger, binds);
				doze(rt);
			}

		} catch (InterruptedException e) {
			interrupted = true;

		} catch (Exception e) {
			interrupted = true;
			errorLogger.log(Level.SEVERE, "Uncaught exception", e);
			listener.aborted(this, e);
			return;

		} finally {
			if (runnerCount != null) {
				runnerCount.decrementAndGet();
			}

			try {
				BackendEngineClientIfs bec = (BackendEngineClientIfs) binds
						.get("__bec__");
				if (bec != null) {
					bec.close();
				}
			} catch (Exception e) {
			}

			perfLogger.teardownLog();
			try {
				perfLogResource.close();

			} catch (NBenchException e) {
			}

			ResourceErrorLogger.removeLogger(errorLogger);
		}
	}

	// ------------------------------------
	// BenchmarkProgressIfs implementation
	// ------------------------------------
	@Override
	public void benchmarkInterrupt() {
		interrupted = true;
	}

	@Override
	public void rampUp(long tot, long curr) {
		calc_leap_factor(tot, curr);
	}

	@Override
	public void steadyState(long tot, long curr) {
		calc_leap_factor(0, 0);
	}

	@Override
	public void rampDown(long tot, long curr) {
		calc_leap_factor(tot, curr);
	}
}
