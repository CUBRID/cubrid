package nbench.engine;

import java.util.HashSet;
import java.util.Set;
import java.util.Timer;
import java.util.TimerTask;
import java.util.concurrent.atomic.AtomicInteger;

import nbench.common.BenchmarkStatus;
import nbench.common.ExecutionIfs;
import nbench.common.ExecutionListenerIfs;
import nbench.common.ResourceIfs;

public class SetupExecution implements ExecutionIfs {
	private SetupClient[] setupClients;
	private Thread[] setupClientThreads;
	private AtomicInteger clientCount;
	private ExecutionListenerIfs listener;
	private Timer timer;

	enum Progress {
		IDLE, RUNNING, FINISHED, ABORTED
	}

	private Progress progress;

	public SetupExecution(LoadGenerator generator) throws Exception {
		setupClients = generator.getContext().createSetupClients();

		if (setupClients != null) {
			clientCount = new AtomicInteger(0);

			for (int i = 0; i < setupClients.length; i++) {
				setupClients[i].setCounter(clientCount);
			}

			setupClientThreads = new Thread[setupClients.length];
			for (int i = 0; i < setupClientThreads.length; i++) {
				setupClientThreads[i] = new Thread(setupClients[i]);
			}
			progress = Progress.IDLE;
		} else {
			progress = Progress.FINISHED;
		}
	}

	// ----------------------------
	// ExecutionIfs implementation
	// ----------------------------
	private void onCompleted() {
		timer.cancel();
		listener.exeCompleted(this);
	}

	private void onStopped() {
		timer.cancel();
		doStop(false);
		listener.exeStopped(this);
	}

	private void onAborted(Exception e) {
		timer.cancel();
		doStop(true);
		listener.exeAborted(this, e);
	}

	private synchronized void doStop(boolean isAbort) {

		switch (progress) {
		case ABORTED:
		case FINISHED:
			return;
		}

		while (clientCount.get() > 0) {
			for (int i = 0; i < setupClientThreads.length; i++) {
				try {
					setupClientThreads[i].join(100);
				} catch (InterruptedException e) {
				}
			}

			if (clientCount.get() == 0) {
				break;
			}

			for (int i = 0; i < setupClients.length; i++) {
				setupClients[i].interruptRun();
			}
		}

		if (isAbort) {
			progress = Progress.ABORTED;
		} else {
			progress = Progress.FINISHED;
		}
	}

	@Override
	public BenchmarkStatus exeStart(ExecutionListenerIfs listener) {

		if (setupClients == null) {
			return BenchmarkStatus.SETUP_FINISHED;
		}

		switch (progress) {
		case IDLE:
			break;
		case RUNNING:
			return BenchmarkStatus.SETUP;
		case FINISHED:
			return BenchmarkStatus.SETUP_FINISHED;
		case ABORTED:
		default:
			return BenchmarkStatus.ABORTED;
		}

		for (int i = 0; i < setupClientThreads.length; i++) {
			setupClientThreads[i].start();
		}
		progress = Progress.RUNNING;

		// setup progress timer task
		this.listener = listener;
		timer = new Timer();
		TimerTask timer_task = new TimerTask() {
			private int total = clientCount.get();

			public void run() {
				int curr = clientCount.get();

				if (total == curr) {
					return;
				}

				int intrCnt = 0;
				for (int i = 0; i < setupClients.length; i++) {
					SetupClient c = setupClients[i];

					if (c.isInterrupted()) {
						intrCnt++;
						if (c.getException() != null) {
							onAborted(c.getException());
							return;
						}
					}
				}

				if (intrCnt > 0) {
					onStopped();
					return;
				}

				if (curr == 0) {
					onCompleted();
					return;
				}
			}
		};
		timer.scheduleAtFixedRate(timer_task, 1000, 1000);
		return BenchmarkStatus.SETUP;
	}

	@Override
	public BenchmarkStatus exeStop() {
		
		if (setupClients != null) {
			doStop(false);
		}

		if (progress.compareTo(Progress.FINISHED) == 0) {
			return BenchmarkStatus.FINISHED;
		} else {
			return BenchmarkStatus.ABORTED;
		}

	}

	@Override
	public String getStatusString() {
		StringBuffer sb = new StringBuffer();

		if (setupClients != null) {
			for (int i = 0; i < setupClients.length; i++) {
				sb.append(setupClients[i].getStatus());
				sb.append(" ");
			}
		}
		return sb.toString();
	}

	@Override
	public Set<ResourceIfs> getLogResources() {
		HashSet<ResourceIfs> resources = new HashSet<ResourceIfs>();

		for (int i = 0; i < setupClients.length; i++) {
			ResourceIfs r = setupClients[i].getLogResource();
			if (r != null) {
				resources.add(r);
			}
		}

		return resources;
	}
}
