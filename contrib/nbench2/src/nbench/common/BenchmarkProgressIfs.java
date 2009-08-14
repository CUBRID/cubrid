package nbench.common;

public interface BenchmarkProgressIfs {

	void benchmarkInterrupt();

	void rampUp(long tot, long curr);

	void steadyState(long tot, long curr);

	void rampDown(long tot, long curr);
}