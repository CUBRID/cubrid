package nbench.util;

import nbench.protocol.server.PrimeController;


public class NBenchPrimeController {
	public static void main(String args[]) {
		if (args.length != 3) {
			System.out
					.println("usage: NBenchPrimeController <repository_dir> <port> <uport>");
			System.exit(0);
		}
		String repository_dir = args[0];
		int port = Integer.valueOf(args[1]);
		int uport = Integer.valueOf(args[2]);
		try {
			PrimeController PC = new PrimeController(repository_dir, port,
					uport);
			Thread thr = new Thread(PC);
			thr.start();
			thr.join();
		} catch (Exception e) {
			e.printStackTrace();
		}
	}
}
