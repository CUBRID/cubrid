package nbench.protocol.server;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.LinkedList;
import java.util.Properties;
import java.util.Map;
import java.util.HashMap;
import java.util.Collections;

import org.json.JSONStringer;
import nbench.common.NBenchException;
import nbench.common.ResourceProviderIfs;
import nbench.protocol.NCPEngine;
import nbench.protocol.NCPResult;
import nbench.protocol.NCPS;
import nbench.protocol.NCPU;
import nbench.util.FileSystemResourceProvider;

public class PrimeController implements Runnable {
	private final String repository_dir;
	private Properties repo_prop;
	private ResourceProviderIfs rp;
	private int port;
	private int uport;
	private Map<LoadGeneratorServer, NCPEngine> drivers;
	private boolean is_shutdown;
	private Thread driverMasterThread;
	private String benchmarkName;
	private long benchmarkStartTime;
	private boolean no_more_load_generator;

	public PrimeController(String repository_dir, int port, int uport)
			throws Exception {
		this.repository_dir = repository_dir;
		this.port = port;
		this.uport = uport;
		drivers = Collections
				.synchronizedMap(new HashMap<LoadGeneratorServer, NCPEngine>());
		initResourceProvider();
		no_more_load_generator = false;
	}

	private void initResourceProvider() throws Exception {
		try {
			File dir = new File(repository_dir);
			if (!dir.isDirectory()) {
				throw new NBenchException(repository_dir
						+ " is not a directory");
			}

			File repository_prop = new File(dir.getPath() + File.separator
					+ "repository.properties");
			if (!repository_prop.exists()) {
				throw new NBenchException("Can't open "
						+ repository_prop.getPath());
			}

			repo_prop = new Properties();
			repo_prop.load(new FileInputStream(repository_prop));

			rp = new FileSystemResourceProvider(repository_dir);
		} catch (Exception e) {
			throw new NBenchException(e);
		}
	}

	public ResourceProviderIfs getResourceProvider() {
		return rp;
	}

	private void registerDriver(LoadGeneratorServer server, NCPEngine engine) {
		drivers.put(server, engine);
	}

	void unregisterDriver(LoadGeneratorServer server) {
		drivers.remove(server);
	}

	@Override
	public void run() {
		final PrimeController pc = this;
		int userverId = 0;
		Runnable driverMaster = null;

		/* launch driver master thread */
		try {
			driverMaster = new Runnable() {
				private ServerSocket server_sock;
				int driverId = 0;

				@Override
				public void run() {
					try {
						server_sock = new ServerSocket(port);
						while (!no_more_load_generator
								&& !Thread.currentThread().isInterrupted()) {
							Socket sock = server_sock.accept();
							LoadGeneratorServer server = new LoadGeneratorServer(
									pc);
							NCPEngine engine = new NCPEngine(driverId++, sock,
									true, "0", NCPS.getMessageIfs());
							registerDriver(server, engine);
						}
					} catch (Exception e) {
						shutdown(e);
					} finally {
						if (server_sock != null && !server_sock.isClosed()) {
							try {
								server_sock.close();
							} catch (IOException e) {
								;
							}
						}
						server_sock = null;
					}
				}
			};
			driverMasterThread = new Thread(driverMaster);
			driverMasterThread.start();
		} catch (Exception e) {
			e.printStackTrace();
			shutdown(e);
		}

		/* handle user connection */
		try {
			ServerSocket ss = new ServerSocket(uport);
			while (!is_shutdown) {
				if (Thread.currentThread().isInterrupted()) {
					shutdown(null);
					break;
				}

				Socket sock = ss.accept();
				try {
					UIServer server = new UIServer(pc);
					NCPEngine engine = new NCPEngine(userverId++, sock, true,
							"0", NCPU.getMessageIfs());
					NCPResult result = new NCPResult();
					engine.process(server, result);
				} catch (IOException e) {
					// continue on client down
					e.printStackTrace();
				} catch (Exception e) {
					throw (e);
				}
			}

			driverMasterThread.join();
			shutdown(null);
		} catch (Exception e) {
			shutdown(e);
		}
	}

	private synchronized void shutdown(Exception exception) {
		is_shutdown = true;

		exception.printStackTrace();
		if (driverMasterThread != null) {
			driverMasterThread.interrupt();
		}

		for (NCPEngine engine : drivers.values()) {
			try {
				engine.disconnect("shutdown");
			} catch (Exception e) {
			}
		}
	}

	// -----------------------------------------------------
	// REQUEST PROCESSING
	// -----------------------------------------------------
	public JSONStringer handleListRepoRequest(JSONStringer stringer)
			throws Exception {
		stringer.key("name");
		stringer.array();
		for (String k : repo_prop.stringPropertyNames()) {
			stringer.value(k);
		}
		stringer.endArray();
		return stringer;
	}

	public JSONStringer handleListRunnerRequest(JSONStringer stringer)
			throws Exception {
		stringer.key("runner");
		stringer.array();
		for (LoadGeneratorServer server : drivers.keySet()) {
			NCPEngine engine = drivers.get(server);
			stringer.object();
			stringer.key("id");
			stringer.value(engine.getId());
			stringer.key("info");
			stringer.value(engine.getProtocolProperty("__whoami__"));
			stringer.endObject();
		}
		stringer.endArray();
		return stringer;
	}

	public boolean handlePrepareRequest(String benchmark, NCPResult result)
			throws Exception {
		String benchmark_props = null;
		no_more_load_generator = true;

		for (String k : repo_prop.stringPropertyNames()) {
			if (k.equals(benchmark)) {
				benchmark_props = repo_prop.getProperty(k);
			}
		}

		if (benchmark_props == null) {
			result.setError("no such benchmark:" + benchmark);
			return false;
		}

		for (LoadGeneratorServer driver : drivers.keySet()) {
			NCPEngine e = drivers.get(driver);
			boolean r = driver.prepareProc(e, benchmark_props, result
					.enter("prepareProc"));
			result.leave();
			if (!r) {
				return false;
			}
		}
		this.benchmarkName = benchmark;
		return true;
	}

	public boolean handleSetupRequest(NCPResult result) throws Exception {
		boolean res = true;

		for (LoadGeneratorServer driver : drivers.keySet()) {
			NCPEngine e = drivers.get(driver);
			boolean r = driver.setupProc(e, result.enter("setupProc"));
			result.leave();

			if (!r) {
				res = false;
				break;
			}
		}
		return res;
	}

	public boolean handleStartRequest(NCPResult result) throws Exception {
		benchmarkStartTime = System.currentTimeMillis();

		LinkedList<LoadGeneratorServer> list = new LinkedList<LoadGeneratorServer>();
		boolean res = true;
		for (LoadGeneratorServer driver : drivers.keySet()) {
			NCPEngine e = drivers.get(driver);
			boolean r = driver.startProc(e, result.enter("startProc"));
			result.leave();
			if (r) {
				list.add(driver);
			} else {
				res = false;
				break;
			}
		}
		return res;
	}

	public JSONStringer handleStatusRequest(JSONStringer stringer,
			NCPResult result) throws Exception {
		stringer.key("status");
		stringer.array();
		for (LoadGeneratorServer driver : drivers.keySet()) {
			NCPEngine e = drivers.get(driver);
			String r = driver.statusProc(e, result.enter("statusProc"));
			result.leave();
			if (result.hasError()) {
				return null;
			}
			stringer.object();
			stringer.key("id");
			stringer.value(e.getId());
			stringer.key("status");
			stringer.value(r);
			stringer.endObject();
		}
		stringer.endArray();
		return stringer;
	}

	public boolean handleStopRequest(NCPResult result) throws Exception {
		boolean res = true;
		for (LoadGeneratorServer driver : drivers.keySet()) {
			NCPEngine e = drivers.get(driver);
			boolean r = driver.stopProc(e, result.enter("stopProc"));
			result.leave();
			if (!r) {
				/* continue on error */
				res = false;
				result.setError("failed to stop:"
						+ e.getProtocolProperty("__whoami__"));
			}
		}
		return res;
	}

	public boolean handleGatherRequest(NCPResult result) throws Exception {
		boolean res = true;
		String resLogBase;

		resLogBase = "res:" + "log" + File.separator + benchmarkName
				+ File.separator + benchmarkStartTime;

		for (LoadGeneratorServer driver : drivers.keySet()) {
			NCPEngine e = drivers.get(driver);

			String driverLogBase = resLogBase + File.separator
					+ e.getProtocolProperty("__whoami__");
			boolean r = driver.gatherProc(e, driverLogBase, result
					.enter("gatherProc"));
			result.leave();

			if (!r) {
				/* continue on error */
				res = false;
				result.setWarning("failed to gather log from:"
						+ e.getProtocolProperty("__whoami__"));
			}
		}
		return res;
	}

	public synchronized boolean handleShutdownRequest(NCPResult result)
			throws Exception {
		boolean r = true;
		for (LoadGeneratorServer driver : drivers.keySet()) {
			NCPEngine e = drivers.get(driver);
			r = driver.shutdownProc(e, result.enter("shutdownProc"));
			result.leave();
			if (!r) {
				r = false;
				result.setWarning("failed to shutdown:"
						+ e.getProtocolProperty("__whoami__"));
			}
			unregisterDriver(driver);
		}
		return r;
	}

}
