package nbench.engine;

import java.util.logging.Level;
import java.util.logging.StreamHandler;
import java.util.logging.Logger;
import java.util.HashMap;
import nbench.common.ResourceIfs;

public class ResourceErrorLogger {

	static class ResourceErrorHandler extends StreamHandler {
		ResourceErrorHandler(ResourceIfs resource) throws Exception {
			setOutputStream(resource.getResourceOutputStream());
		}
	}

	private static HashMap<Logger, ResourceErrorHandler> errorLogMap;

	static {
		errorLogMap = new HashMap<Logger, ResourceErrorHandler>();
	}

	public static Logger getLogger(String name, ResourceIfs resource)
			throws Exception {
		Logger logger = Logger.getLogger(name);

		ResourceErrorHandler eh = new ResourceErrorHandler(resource);
		eh.setLevel(Level.ALL);
		
		logger.addHandler(eh);
		logger.setLevel(Level.ALL);
		logger.setUseParentHandlers(false);

		addErrorLogger(logger, eh);
		return logger;
	}

	private static void addErrorLogger(Logger logger, ResourceErrorHandler eh) {
		synchronized (errorLogMap) {
			errorLogMap.put(logger, eh);
		}
	}

	public static void flushAll() {
		synchronized (errorLogMap) {
			for (Logger logger : errorLogMap.keySet()) {
				ResourceErrorHandler eh = errorLogMap.get(logger);
				eh.flush();
			}
		}
	}

	public static void removeLogger(Logger logger) {
		ResourceErrorHandler eh = null;
		
		synchronized (errorLogMap) {
			if(errorLogMap.containsKey(logger)) {
				eh = errorLogMap.remove(logger);
			}
		}
		
		if(eh != null) {
			eh.flush();
			logger.removeHandler(eh);
		}
	}
}
