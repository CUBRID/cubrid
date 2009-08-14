package nbench.common;

public interface ExecutionListenerIfs {
	void exeCompleted(ExecutionIfs ifs);

	void exeStopped(ExecutionIfs ifs);
	
	void exeAborted(ExecutionIfs ifs, Exception e);
}
