package nbench.common;

import java.util.Properties;

public interface BackendEngineIfs {
	void configure(Properties props, ResourceProviderIfs rp, ClassLoader loader)
			throws NBenchException;

	BackendEngineClientIfs createClient(PerfLogIfs listener)
			throws NBenchException;
}
