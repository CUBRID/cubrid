package nbench.engine;

import nbench.common.NBenchException;

public class SLA {
	int sla;
	int count_fit;
	int count_exceed;
	boolean check;

	SLA(int sla) {
		this.sla = sla;
	}

	void startChecking() {
		check = true;
	}

	void update(int v, boolean succ) throws NBenchException {
		if (!succ || v > sla) {
			count_exceed++;
		} else {
			count_fit++;
		}
		
		/*
		 * s = p + Z(0.95) * sqrt(p*(1-p)/n)
		 * p = 0.9
		 */
		double total = count_exceed + count_fit;
		if(total > 50) {
			double actual = (double)count_exceed/total;
			double bound = 0.9 + 1.65 *  Math.sqrt(0.09/total);
			if(actual > bound) {
				throw new NBenchException("SLA violation: tot:"
						+ (count_fit + count_exceed) + ", succ:" + count_fit
						+ ", fail:" + count_exceed);
			}
		}
	}
}
