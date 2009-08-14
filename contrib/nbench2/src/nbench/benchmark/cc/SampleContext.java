package nbench.benchmark.cc;

import java.util.Properties;
import java.util.HashMap;

import nbench.common.SampleIfs;
import nbench.sample.*;

class SampleContext {
	private SampleIfsProvider globalSample;
	private IndexedSampleIfsProvider tableSample;
	private IndexedSampleIfsProvider cafeTypeSample;
	private DatabaseObject database;

	public SampleContext(Properties props) throws Exception {
		globalSample = new SampleIfsProvider();
		initGlobalSample();
		database = new DatabaseObject(props);
		tableSample = new IndexedSampleIfsProvider();
		cafeTypeSample = new IndexedSampleIfsProvider();
	}

	private void initGlobalSample() throws Exception {
		SampleIfs nullSample = new SingleSample(null);
		globalSample.addSampleIfs("tkt_no", new RangeSample(1, 5));
		globalSample.addSampleIfs("cment_tp_cd", new SingleSample("NS"));
		globalSample.addSampleIfs("prit_rnk", new SingleSample(new Integer(0)));
		globalSample.addSampleIfs("wrtr_mbr_id", new StringSample(8, 16));
		globalSample.addSampleIfs("wrtr_ncknm", new StringSample(10, 20));
		globalSample.addSampleIfs("wrtr_ip", new RandomIPSample());
		globalSample.addSampleIfs("psacn_no", new SingleSample(new Integer(0)));
		globalSample
				.addSampleIfs("prfl_photo_url", new SingleSample("http://"));
		globalSample.addSampleIfs("pwd", new SingleSample("password"));
		globalSample
				.addSampleIfs("wrtr_hompg_url", new SingleSample("http://"));
		globalSample.addSampleIfs("scrt_wrg_yn", new SingleSample("N"));
		globalSample.addSampleIfs("cment_cont", new StringSample(80, 160));
		globalSample.addSampleIfs("trbk_ttl", new SingleSample(
				"trackback titile"));
		globalSample.addSampleIfs("trbk_url", new SingleSample(
				"http://trackback"));
		globalSample.addSampleIfs("font_no", new RangeSample(0, 10));
		globalSample.addSampleIfs("font_sz", new RangeSample(1, 10));
		globalSample.addSampleIfs("mod_ymdt", nullSample);
		globalSample.addSampleIfs("del_ymdt", nullSample);
		globalSample.addSampleIfs("delr_mbr_id", nullSample);
		globalSample.addSampleIfs("dsp_yn", new SingleSample("Y"));
		globalSample.addSampleIfs("del_rsn_tp_cd", new SingleSample("-1"));
		globalSample.addSampleIfs("del_rsn_dtl_cont", nullSample);
		globalSample.addSampleIfs("spam_scr", new RangeSample(0, 100));
	}

	// --------------------------------------------------
	// Sample related
	// --------------------------------------------------
	public void putGlobalSampleIfs(String key, SampleIfs sample) {
		globalSample.addSampleIfs(key, sample);
	}

	public int[] getTableIDs() {
		return database.getTableIDs();
	}

	public void putTableSampleIfs(int table_id, String key, SampleIfs sample) {
		tableSample.addSampleIfs(table_id, key, sample);
	}

	public int getNumCafeTypes() {
		return database.getNumCafeTypes();
	}

	public void putCafeTypeSampleIfs(int cafe_type, String key, SampleIfs sample) {
		cafeTypeSample.addSampleIfs(cafe_type, key, sample);
	}

	public CafeObject nextCafe() throws Exception {
		CafeObject cafe = database.chooseCafeObjectFromTable();
		if (cafe == null) {
			throw new Exception("should not happen");
		}
		return cafe;
	}

	public HashMap<Integer, CafeObject> getCafeObjectMap() {
		return database.getCafeObjectMap();
	}

	public Object getGlobalSample(String key) throws Exception {
		Object val = globalSample.nextValue(key);
		return val;
	}

	public Object getTableSample(int table_id, String key) throws Exception {
		Object val = tableSample.nextValue(table_id, key);

		if (val == null) {
			throw new Exception("no such sample in table context" + key);
		}
		return val;
	}

	public Object getCafeTypeSample(int cafe_type, String key) throws Exception {
		Object val = cafeTypeSample.nextValue(cafe_type, key);
		if (val == null) {
			throw new Exception("no such sample in table context" + key);
		}
		return val;
	}
}
