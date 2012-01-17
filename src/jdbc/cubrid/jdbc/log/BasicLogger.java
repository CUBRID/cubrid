package cubrid.jdbc.log;

import java.io.File;
import java.io.IOException;
import java.io.PrintWriter;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Hashtable;

public class BasicLogger implements Log {
    private static final int FATAL = 0;
    private static final int ERROR = 1;
    private static final int WARN = 2;
    private static final int INFO = 3;
    private static final int DEBUG = 4;
    private static final int TRACE = 5;
    private static final int ALL = 6;

    private int logLevel;

    private static Hashtable<String, PrintWriter> writerTable = new Hashtable<String, PrintWriter>();
    static {
	writerTable.put("stderr", new PrintWriter(System.err));
    }
    PrintWriter writer;

    public BasicLogger(String fileName) {
	initialize(fileName, ALL);
    }

    public BasicLogger(String fileName, int level) {
	initialize(fileName, level);
    }

    private void initialize(String fileName, int level) {
	logLevel = level;
	try {
	    File f = new File(fileName);
	    String canonicalPath = f.getCanonicalPath();
	    writer = writerTable.get(canonicalPath);
	    if (writer == null) {
		writer = new PrintWriter(canonicalPath);
		writerTable.put(canonicalPath, writer);
	    }
	} catch (IOException e) {
	    System.err.println("WARNING - Could not create a file for logging.\n The standard error will be using to log.");
	    e.printStackTrace();
	    writer = writerTable.get("stderr");
	}
    }

    public void logDebug(String msg) {
	logDebug(msg, null);
    }

    public void logDebug(String msg, Throwable thrown) {
	logInternal(DEBUG, msg, thrown);
    }

    public void logError(String msg) {
	logError(msg, null);
    }

    public void logError(String msg, Throwable thrown) {
	logInternal(ERROR, msg, thrown);
    }

    public void logFatal(String msg) {
	logFatal(msg, null);
    }

    public void logFatal(String msg, Throwable thrown) {
	logInternal(FATAL, msg, thrown);
    }

    public void logInfo(String msg) {
	logInfo(msg, null);
    }

    public void logInfo(String msg, Throwable thrown) {
	logInternal(INFO, msg, thrown);
    }

    public void logTrace(String msg) {
	logTrace(msg);
    }

    public void logTrace(String msg, Throwable thrown) {
	logInternal(TRACE, msg, thrown);
    }

    public void logWarn(String msg) {
	logWarn(msg);
    }

    public void logWarn(String msg, Throwable thrown) {
	logInternal(WARN, msg, thrown);
    }

    private SimpleDateFormat dateFormat = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS");
    private synchronized void logInternal(int level, String msg, Throwable thrown) {
	if (logLevel < level) {
	    return;
	}

	StringBuffer b = new StringBuffer();
	b.append(dateFormat.format(new Date())).append('|');

	switch (level) {
	case FATAL:
	    b.append("FATAL");
	    break;
	case ERROR:
	    b.append("ERROR");
	    break;
	case WARN:
	    b.append("WARN");
	    break;
	case INFO:
	    b.append("INFO");
	    break;
	case DEBUG:
	    b.append("DEBUG");
	    break;
	case TRACE:
	    b.append("TRACE");
	    break;
	}
	b.append('|').append(msg);
	writer.println(b.toString());

	if (thrown != null) {
	    thrown.printStackTrace(writer);
	}
	writer.flush();
    }
}
