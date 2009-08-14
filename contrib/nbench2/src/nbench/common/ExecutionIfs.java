package nbench.common;

import java.util.Set;

public interface ExecutionIfs {
	
	BenchmarkStatus exeStart(ExecutionListenerIfs listener);

	BenchmarkStatus exeStop();
	
	String getStatusString();
	
	Set<ResourceIfs> getLogResources();
}
