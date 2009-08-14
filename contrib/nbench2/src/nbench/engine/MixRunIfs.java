package nbench.engine;

import nbench.common.PerfLogIfs;
import java.util.logging.Logger;

import javax.script.Bindings;

/* returns running time */
public interface MixRunIfs {
	long run(PerfLogIfs listener, Logger logger, Bindings binds) throws Exception;

	String getMixName();
}
