package nbench.engine;

import java.util.List;
import java.util.logging.Level;
import java.util.logging.Logger;
import javax.script.ScriptEngine;
import javax.script.Bindings;
import org.apache.commons.math.random.RandomDataImpl;

import nbench.common.BackendEngineClientIfs;
import nbench.common.NBenchException;
import nbench.common.NBenchTransactionFailException;
import nbench.common.PerfLogIfs;
import nbench.common.PerfLogIfs.LogType;
import nbench.parse.Transaction;

public class MixRunFactory {
	static class RatioMixRun implements MixRunIfs {
		String name;
		double apdf[];
		double bound;
		ScriptRun runs[];
		SLA sla[];
		int think_times[];
		RandomDataImpl rand;
		ScriptEngine se;
		Bindings binds;

		RatioMixRun(String name, ScriptEngine se, String value,
				String transaction_common, List<Transaction> trs,
				int think_times[]) throws NBenchException {
			int i;

			String pdf[] = value.split(":");
			if (pdf.length != trs.size()) {
				throw new NBenchException(
						"internal error: values.length != trs.size()");
			}

			this.name = name;
			this.se = se;

			apdf = new double[pdf.length];
			runs = new ScriptRun[pdf.length];
			rand = new RandomDataImpl();
			sla = new SLA[pdf.length];
			this.think_times = think_times;

			bound = 0;
			for (i = 0; i < pdf.length; i++) {
				double d = Double.valueOf(pdf[i]);
				apdf[i] = bound + d;
				bound += d;
				runs[i] = new ScriptRun(trs.get(i).name, se,
						transaction_common, trs.get(i).script);
				sla[i] = new SLA(trs.get(i).sla);
			}

			// add fence post additional value
			apdf[apdf.length - 1] += 100;
		}

		@Override
		public long run(PerfLogIfs listener, Logger logger, Bindings binds)
				throws Exception {
			double r = rand.nextUniform(0, 1) * bound;
			long st = 0, et = 0;

			for (int i = 0; i < runs.length; i++) {
				if (apdf[i] >= r) {

					try {

						if (think_times[i] > 0) {
							Thread.sleep(think_times[i]);
						}

						st = System.currentTimeMillis();
						listener.startLogItem(st, LogType.TRANSACTION, runs[i]
								.getName());

						runs[i].run(binds);

						et = System.currentTimeMillis();
						if (et - st > sla[i].sla) {
							listener.endsWithError(et, LogType.TRANSACTION,
									"SLA");
							logger.log(Level.SEVERE, "SLA violation: SLA("
									+ sla[i].sla + "), real:" + (et - st));
						} else {
							listener.endLogItem(et, LogType.TRANSACTION,
									runs[i].getName());
						}

						sla[i].update((int) (et - st), true);
						return et - st;

					} catch (Exception e) {

						/* check if this is a transaction fail exception */
						Throwable t = e.getCause();
						Throwable prev_t = null;

						while (t != null && (prev_t != t)) {
							if (t instanceof NBenchTransactionFailException) {
								NBenchTransactionFailException ee = (NBenchTransactionFailException) t;

								logger.log(Level.SEVERE, "Transaction fail:",
										ee.getSQLException());
								et = System.currentTimeMillis();

								listener.endsWithError(et, LogType.TRANSACTION,
										t.toString());

								sla[i].update((int) (et - st), false);

								/* give notification to the back-end */
								BackendEngineClientIfs bec = (BackendEngineClientIfs) binds
										.get("__bec__");
								
								if (bec != null) {
									bec.handleTransactionAbort(t);
								}
								return 0;
							}

							prev_t = t;
							t = t.getCause();
						}

						/* give notification to the back-end */
						BackendEngineClientIfs bec = (BackendEngineClientIfs) binds
								.get("__bec__");
						
						if (bec != null) {
							bec.handleSessionAbort(t);
						}
						logger.log(Level.SEVERE, "Uncaught exception", e);
						throw e;
					}
				}
			}
			throw new Exception("should not reach here");
		}

		@Override
		public String getMixName() {
			return name;
		}
	}

	public static MixRunIfs createMixRun(ScriptEngine se, String name,
			String type, String value, String transaction_common,
			List<Transaction> trs, int[] think_times) throws NBenchException {
		if (type.equals("ratio")) {
			return new RatioMixRun(name, se, value, transaction_common, trs,
					think_times);
		} else {
			throw new NBenchException("unsupported mix type:" + type);
		}
	}
}
