package nbench.util;

import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.net.Socket;
import java.util.Scanner;

import org.json.JSONObject;
import org.json.JSONStringer;

import nbench.common.NBenchException;
import nbench.protocol.NCPEngine;
import nbench.protocol.NCPProcedureIfs;
import nbench.protocol.NCPResult;
import nbench.protocol.NCPU;

public class NBenchTUI implements Runnable {
	String host;
	int port;

	NCPEngine engine;
	boolean abort;
	JSONObject msg;

	boolean batch_mode;
	static final String prompt = "NBenchTUI>";
	Scanner scanner;
	PrintWriter output;
	PrintResponseProc printResponseProc;
	NCPResult result;

	private static String helpMessage = "help                     - session command summary\n"
			+ "list repo                - list benchmark repository names\n"
			+ "list runner              - list runner information\n"
			+ "prepare <benchmark_name> - prepare benchmark\n"
			+ "setup                    - setup database\n"
			+ "start                    - start prepared benchmark\n"
			+ "status                   - current status\n"
			+ "stop                     - stop benchmark\n"
			+ "gather                   - gather performace data\n"
			+ "shutdown                 - shutdown prime controller\n"
			+ "quit                     - quit this program\n";

	public NBenchTUI(String host, int port, String batch_file) throws Exception {
		this.host = host;
		this.port = port;
		if (batch_file != null) {
			batch_mode = true;
			scanner = new Scanner(new File(batch_file));
			output = new PrintWriter(System.out);

		} else {
			scanner = new Scanner(System.in);
			output = new PrintWriter(System.out);
		}
		printResponseProc = new PrintResponseProc();
		result = new NCPResult();
	}

	@Override
	public void run() {
		Socket socket = null;
		try {
			socket = new Socket(host, port);
			engine = new NCPEngine(0, socket, false, "NBenchTUI", NCPU
					.getMessageIfs());
			while (!abort) {
				handleUserRequest();
				// if (result.hasError()) {
				// System.out.println(result.getErrorString());
				// }
			}
		} catch (Exception e) {
			e.printStackTrace();
		} finally {
			if (socket != null && !socket.isClosed()) {
				try {
					socket.close();
				} catch (IOException e) {
					;
				}
			}
			output.println("Bye");
			output.flush();
		}
	}

	//
	// Simple command line parsing
	//
	private void printHelp() {
		output.println(helpMessage);
	}

	private String readUserRequest() throws Exception {
		if (!batch_mode) {
			output.write(prompt);
			output.flush();
		}
		return scanner.nextLine();
	}

	private void handleUserRequest() throws Exception {
		if (abort) {
			return;
		}
		printResponseProc.reLoad();
		result.clearAll();
		String req = readUserRequest();
		Scanner sc = new Scanner(req);
		if (sc.hasNext()) {
			String tok = sc.next().toLowerCase();
			// consider index structure only if there is many commands
			if (tok.equals("help")) {
				printHelp();
			} else if (tok.equals("list")) {
				handleListCommand(sc);
			} else if (tok.equals("quit")) {
				processQuit();
			} else if (tok.equals("prepare")) {
				if (!sc.hasNext()) {
					printHelp();
				} else {
					String benchmark = sc.next();
					prepareProc(benchmark);
				}
			} else if (tok.equals("setup")) {
				setupProc();
			} else if (tok.equals("start")) {
				prepareProc();
			} else if (tok.equals("stop")) {
				stopProc();
			} else if (tok.equals("status")) {
				statusProc();
			} else if (tok.equals("gather")) {
				gatherProc();
			} else if (tok.equals("shutdown")) {
				shutDownProc();
				abort = true;
			} else {
				output.println("unsupported command:" + tok);
				output.println("Type help for help message.");
			}
		}
		sc.close();
	}

	//
	//
	//
	class PrintResponseProc implements NCPProcedureIfs {

		private boolean done = false;

		public void reLoad() {
			done = false;
		}

		@Override
		public void onEnd(NCPEngine engine, String reason, NCPResult result) {
			System.out.println("Aborted:" + reason);
			done = true;
		}

		@Override
		public void onMessage(NCPEngine engine, int id, JSONObject obj,
				boolean isError, String errorMessage, boolean isWarning,
				String warningMessage, NCPResult result) throws Exception {
			System.out.println(obj.toString());
			done = true;
		}

		@Override
		public void onRawDataEnd(NCPEngine e, OutputStream os, NCPResult result)
				throws Exception {
			new Exception().printStackTrace();
			throw new NBenchException("should not happen");
		}

		@Override
		public OutputStream onRawDataStart(NCPEngine engine, NCPResult result)
				throws Exception {
			new Exception().printStackTrace();
			throw new NBenchException("should not happen");
		}

		@Override
		public boolean processDone() {
			return done;
		}

		@Override
		public String getName() {
			return "PrintResponseProc";
		}
	}

	private void handleListCommand(Scanner sc) throws Exception {
		if (sc.hasNext()) {
			String what = sc.next().toLowerCase();
			if (what.equals("repo")) {
				listRepositoryProc();
			} else if (what.equals("runner")) {
				listRunnerProc();
			} else {
				printHelp();
			}
		} else {
			printHelp();
		}
	}

	//
	// Command processing
	//
	private void listRepositoryProc() throws Exception {
		engine.sendSimpleRequest(NCPU.LIST_REPO_REQUEST);
		engine.process(printResponseProc, result);
	}

	private void listRunnerProc() throws Exception {
		engine.sendSimpleRequest(NCPU.LIST_RUNNER_REQUEST);
		engine.process(printResponseProc, result);
	}

	private void processQuit() throws Exception {
		engine.disconnect("bye");
		abort = true;
	}

	private void prepareProc(String benchmark) throws Exception {
		JSONStringer stringer = engine.openMessage(NCPU.PREPARE_REQUEST);
		stringer.key("benchmark");
		stringer.value(benchmark);
		engine.sendMessage(engine.closeMessage(stringer));
		engine.process(printResponseProc, result);
	}

	private void setupProc() throws Exception {
		engine.sendSimpleRequest(NCPU.SETUP_REQUEST);
		engine.process(printResponseProc, result);
	}

	private void prepareProc() throws Exception {
		engine.sendSimpleRequest(NCPU.START_REQUEST);
		engine.process(printResponseProc, result);
	}

	private void stopProc() throws Exception {
		engine.sendSimpleRequest(NCPU.STOP_REQUEST);
		engine.process(printResponseProc, result);
	}

	private void statusProc() throws Exception {
		engine.sendSimpleRequest(NCPU.STATUS_REQUEST);
		engine.process(printResponseProc, result);
	}

	private void gatherProc() throws Exception {
		engine.sendSimpleRequest(NCPU.GATHER_REQUEST);
		engine.process(printResponseProc, result);
	}

	private void shutDownProc() throws Exception {
		engine.sendSimpleRequest(NCPU.SHUTDOWN_REQUEST);
		engine.process(printResponseProc, result);
	}

	public static void main(String args[]) {
		String batch_file;

		if (args.length != 2 && args.length != 3) {
			System.out.println("usage: NBenchTUI <host> <port> [<batch-file>]");
			System.exit(0);
		}

		if (args.length == 3) {
			batch_file = args[2];
		} else {
			batch_file = null;
		}

		try {
			new Thread(new NBenchTUI(args[0], Integer.valueOf(args[1]),
					batch_file)).start();
		} catch (Exception e) {
			e.printStackTrace();
		}
	}
}
