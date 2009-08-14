package nbench.util;

import java.io.File;
import java.net.ConnectException;
import java.net.Socket;

import nbench.engine.LoadGenerator;
import nbench.protocol.NCPEngine;
import nbench.protocol.NCPResult;
import nbench.protocol.NCPS;

public class NBenchLoadGenerator {
	private static String prepareDirectory(String dir) {
		File d = new File(dir);
		if (!d.exists()) {
			if (!d.mkdirs()) {
				System.out.println("failed to create directory:" + dir);
				System.exit(1);
			}
		} else if (!d.isDirectory()) {
			System.out.println(dir + " is not a directory");
			System.exit(1);
		}
		return d.getPath();
	}

	public static void deleteDirectory(File path) throws Exception {
		if (path.exists()) {
			File[] files = path.listFiles();
			for (int i = 0; i < files.length; i++) {
				if (files[i].isDirectory()) {
					deleteDirectory(files[i]);
				} else {
					files[i].delete();
				}
			}
		}
		path.delete();
	}

	public static void main(String[] args) {
		final String usage = "Usage: NCPLoadGenerator <host> <port> <whoami> <base_dir>\n"
				+ "\t<host>     : host\n"
				+ "\t<port>     : port\n"
				+ "\t<whoami>   : my identifier\n"
				+ "\t<base_dir> : base directory for logging and resourceIfs resolution\n";

		if (args.length != 4) {
			System.out.println(usage);
			System.exit(0);
		}

		Socket sock = null;
		FileSystemResourceProvider rp = null;
		try {
			String host = args[0];
			String port = args[1];
			String whoami = args[2];
			String base_dir = args[3];

			String base_dir_norm = prepareDirectory(base_dir);
			String resource_dir = prepareDirectory(base_dir_norm
					+ File.separator + "resource" + File.separator + whoami);
			File file = new File(resource_dir);
			if (file.exists()) {
				if (file.isDirectory()) {
					deleteDirectory(file);
				} else {
					file.delete();
				}
			}
			rp = new FileSystemResourceProvider(resource_dir);

			LoadGenerator generator = new LoadGenerator(rp);
			sock = new Socket(host, Integer.valueOf(port));
			NCPEngine engine = new NCPEngine(0, sock, false, whoami, NCPS
					.getMessageIfs());
			NCPResult result = new NCPResult();
			//DO request processing loop
			engine.process(generator, result);
		} catch (ConnectException e) {
			System.out.println(e.toString());
		} catch (Exception e) {
			e.printStackTrace();
		}
	}
}
