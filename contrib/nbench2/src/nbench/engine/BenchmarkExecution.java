package nbench.engine;

import java.util.HashSet;
import java.util.Set;
import java.util.Timer;
import java.util.Properties;
import java.util.TimerTask;
import java.util.concurrent.atomic.AtomicInteger;

import nbench.common.BenchmarkProgressIfs;
import nbench.common.BenchmarkStatus;
import nbench.common.ExecutionIfs;
import nbench.common.ExecutionListenerIfs;
import nbench.common.BenchmarkProgressListenerIfs;
import nbench.common.ResourceIfs;

public class BenchmarkExecution implements ExecutionIfs,
		BenchmarkProgressListenerIfs {

	private long ramp_up_time_millis;
	private long run_time_millis;
	private long ramp_down_time_millis;
	private long timer_vt;
	private Timer timer;

	private BenchmarkClient[] clients;
	private AtomicInteger clientCount;
	private Thread[] clientThreads;

	private ExecutionListenerIfs listener;

	enum Progress {
		IDLE, RAMP_UP, STEADY_STATE, RAMP_DOWN, FINISHED, ABORTED
	}

	private Progress progress;

	public BenchmarkExecution(LoadGenerator generator) throws Exception {

		parseProps(generator.getProperties());
		clients = generator.getContext().createBenchmarkClients(this);

		clientCount = new AtomicInteger(0);
		for (int i = 0; i < clients.length; i++) {
			clients[i].setCounter(clientCount);
		}

		clientThreads = new Thread[clients.length];
		for (int i = 0; i < clientThreads.length; i++) {
			clientThreads[i] = new Thread(clients[i]);
		}

		progress = Progress.IDLE;
	}

	private void parseProps(Properties props) throws Exception {
		ramp_up_time_millis = 1000 * Long.valueOf(props
				.getProperty("ramp_up_time"));
		run_time_millis = 1000 * Long.valueOf(props.getProperty("run_time"));
		ramp_down_time_millis = 1000 * Long.valueOf(props
				.getProperty("ramp_down_time"));
	}

	private String getProgressString() {
		switch (progress) {
		case RAMP_UP:
			return progress.toString() + "(" + timer_vt + "/"
					+ ramp_up_time_millis + ")";
		case STEADY_STATE:
			return progress.toString() + "(" + timer_vt + "/" + run_time_millis
					+ ")";
		case RAMP_DOWN:
			return progress.toString() + "(" + timer_vt + "/"
					+ ramp_down_time_millis + ")";
		default:
			return progress.toString();
		}
	}

	private void doProgress(long millis) {
		for (int i = 0; i < clients.length; i++) {
			switch (progress) {
			case RAMP_UP:
				clients[i].rampUp(millis, timer_vt);
				break;
			case STEADY_STATE:
				clients[i].steadyState(millis, timer_vt);
				break;
			case RAMP_DOWN:
				clients[i].rampDown(millis, timer_vt);
				break;
			}
		}
	}

	void doStart() {

		if (progress.compareTo(Progress.IDLE) != 0) {
			return;
		}

		for (int i = 0; i < clientThreads.length; i++) {
			clientThreads[i].start();
		}
		progress = Progress.RAMP_UP;

		// set-up progress timer
		timer = new Timer();
		timer_vt = 0L;

		TimerTask timer_task = new TimerTask() {
			public void run() {
				long millis = 0L;

				if (clientCount.get() != clients.length) {
					doStop("Some clients aborted. (" + clientCount.get() + "/"
							+ clients.length + ") alive.", true);
				}

				timer_vt += 1000;
				switch (progress) {
				case RAMP_UP:
					millis = ramp_up_time_millis;
					if (timer_vt >= ramp_up_time_millis) {
						timer_vt = 0L;
						progress = Progress.STEADY_STATE;
					} else {
						break;
					}
					// fall through
				case STEADY_STATE:
					millis = run_time_millis;
					if (timer_vt >= run_time_millis) {
						timer_vt = 0L;
						progress = Progress.RAMP_DOWN;
					} else {
						break;
					}
					// fall through
				case RAMP_DOWN:
					millis = ramp_down_time_millis;
					if (timer_vt >= ramp_down_time_millis) {
						doStop("ramp-down done", false);
						return;
					}
					break;
				default:
					doStop("should not happen", true);
					return;
				}
				doProgress(millis);
			}
		};
		timer.scheduleAtFixedRate(timer_task, 1000, 1000);
	}

	synchronized void doStop(String message, boolean isAbort) {
		int i;

		switch (progress) {
		case IDLE:
		case ABORTED:
		case FINISHED:
			return;
		}

		try {
			// soft interrupt
			for (i = 0; i < clients.length; i++) {
				if (!clients[i].isInterrupted()) {
					clients[i].benchmarkInterrupt();
				}
			}

			// hard interrupt
			while (clientCount.get() > 0) {
				for (i = 0; i < clients.length; i++) {
					clientThreads[i].join(100);
				}

				if (clientCount.get() == 0) {
					break;
				}

				// hard interrupt
				for (i = 0; i < clients.length; i++) {
					clientThreads[i].interrupt();
				}
			}

		} catch (Exception e) {
			listener.exeAborted(this, e);
			isAbort = true;
		} finally {
			if (isAbort) {
				progress = Progress.ABORTED;
				listener.exeAborted(this, null);
			} else {
				progress = Progress.FINISHED;
				listener.exeCompleted(this);
			}
			timer.cancel();
		}
	}

	// ----------------------------
	// ExecutionIfs implementation
	// ----------------------------
	@Override
	public BenchmarkStatus exeStart(ExecutionListenerIfs listener) {
		this.listener = listener;
		doStart();
		switch (progress) {
		case IDLE:
		case RAMP_UP:
		case STEADY_STATE:
		case RAMP_DOWN:
		case FINISHED:
			return BenchmarkStatus.STARTED;
		case ABORTED:
		default:
			return BenchmarkStatus.ABORTED;
		}
	}

	@Override
	public BenchmarkStatus exeStop() {
		doStop("ExecutionIfs::exeStop()", false);

		if (progress.compareTo(Progress.FINISHED) == 0) {
			return BenchmarkStatus.FINISHED;
		} else {
			return BenchmarkStatus.ABORTED;
		}
	}

	@Override
	public String getStatusString() {
		return getProgressString();
	}

	@Override
	public Set<ResourceIfs> getLogResources() {
		HashSet<ResourceIfs> resources = new HashSet<ResourceIfs>();

		for (int i = 0; i < clients.length; i++) {
			ResourceIfs r = clients[i].getPerfLogResource();
			if (r != null) {
				resources.add(r);
			}

			r = clients[i].getLogResource();
			if (r != null) {
				resources.add(r);
			}
		}

		return resources;
	}

	// --------------------------------------------
	// BenchmarkProgressListenerIfs implementation
	// --------------------------------------------
	@Override
	public void aborted(BenchmarkProgressIfs ifs, Exception e) {
		doStop("BenchmarkProgressListenerIfs:aborted()", true);
		listener.exeAborted(this, e);
	}
}
