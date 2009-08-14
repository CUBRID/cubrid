package nbench.sample;

import java.util.Random;
import nbench.common.SampleIfs;

public class HistoSample implements SampleIfs {
	private int[] ids;
	private int[] chisto;
	private int cum;
	private Random rand = new Random();
	
	public HistoSample(String ids, String histo) throws Exception {
		String [] ids_s = ids.split(",");
		String [] histo_s = histo.split(",");
		if(ids_s.length != histo_s.length) {
			throw new Exception(ids + "<> in size " + histo);
		}
		this.ids = new int[ids_s.length];
		this.chisto = new int[histo_s.length];
		cum = 0;
		for(int i = 0; i < ids_s.length; i++) {
			this.ids[i] = Integer.valueOf(ids_s[i]);
			cum += Integer.valueOf(histo_s[i]);
			chisto[i] = cum;
		}
	}
	@Override
	public Object nextValue() throws Exception {
		int r = rand.nextInt(cum + 1);
		for(int i = 0; i < chisto.length; i++) {
			if(r <= chisto[i]) {
				return new Integer(ids[i]);
			}
		}
		throw new Exception("internal error");
	}
}
