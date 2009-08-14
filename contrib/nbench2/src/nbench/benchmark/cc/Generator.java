package nbench.benchmark.cc;

import java.util.Properties;
import java.io.File;
import java.io.FileInputStream;

public class Generator {

	public static void main(String args[]) {
		Properties props = null;
		if (args.length != 2) {
			System.out.println("Usage: CCGen <property file> <db configuration file>");
			System.exit(0);
		}
		try {
			props = new Properties();
			File props_file = new File(args[0]);
			props.load(new FileInputStream(props_file));
			props.setProperty("__db_config__", args[1]);
			GenContext context = new GenContext(props);
			do_emit(context);
		} catch (Exception e) {
			e.printStackTrace();
		}
	}

	public static void do_emit(GenContext context) throws Exception {
		QEngine engine = new QEngine(context);
		Thread thr = new Thread(engine);
		context.onStart(engine);
		thr.run();
		thr.join();
		context.onEnd();
	}
}
