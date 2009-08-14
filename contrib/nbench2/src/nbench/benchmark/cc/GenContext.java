package nbench.benchmark.cc;

import java.sql.Timestamp;
import java.util.HashSet;
import java.util.HashMap;
import java.util.Properties;
import java.io.File;
import java.io.FileInputStream;

import nbench.common.SampleIfs;
import nbench.sample.*;

public class GenContext {
	private long start_time;
	private SampleIfs articleArrivalMillis;
	private SampleIfs commentArrivalSecs;
	private SampleIfs replyPropSample;
	private long num_comments;
	private long curr_num_comments;
	private long curr_num_articles;
	private long reserved_num_comments;
	private int[] mean_replies_per_cafe_type;
	private int[] stddev_replies_per_cafe_type;
	private Properties props;
	SampleContext sample;

	private HashMap<Integer, HashSet<GenTableIfs>> genTableIfsSetMap;
	private HashMap<String, Class<GenTableIfs>> genClassMap;

	public GenContext(Properties props) {
		this.props = props;
		this.sample = null;
		this.genTableIfsSetMap = new HashMap<Integer, HashSet<GenTableIfs>>();
	}

	private SampleContext initSampleContext(String path) throws Exception {
		File file = new File(path);
		Properties prop = new Properties();
		FileInputStream fis = new FileInputStream(file);
		prop.load(fis);
		fis.close();
		return new SampleContext(prop);
	}

	@SuppressWarnings("unchecked")
	private void makeGenTableClassList(Properties props) throws Exception {
		genClassMap = new HashMap<String, Class<GenTableIfs>>();

		for (String key : props.stringPropertyNames()) {
			if (key.startsWith("gen_class.")) {
				String prefix = key.substring("gen_class.".length());
				String cls = props.getProperty(key);
				Class<GenTableIfs> clz = (Class<GenTableIfs>) Class
						.forName(cls);
				genClassMap.put(prefix, clz);
			}
		}
		if (genClassMap.size() < 1) {
			throw new Exception(
					"at least one gen_class.xxx property should be set in property file");
		}
	}

	public void onStart(QEngine engine) throws Exception {
		sample = initSampleContext(props.getProperty("__db_config__"));
		parseProps();

		HashMap<Integer, CafeObject> cafe_map = sample.getCafeObjectMap();
		for (int cafe_id : cafe_map.keySet()) {
			CafeObject cafe = cafe_map.get(cafe_id);

			int table_id = cafe.getTableID();
			if (!genTableIfsSetMap.containsKey(table_id)) {
				sample.putTableSampleIfs(table_id, "obj_id",
						new UniqueNumberStringSample(1000000));
				sample.putTableSampleIfs(table_id, "cment_no",
						new AutoIncrementSample(0));

				HashSet<GenTableIfs> set = new HashSet<GenTableIfs>();
				for (String key : genClassMap.keySet()) {
					Class<GenTableIfs> clz = genClassMap.get(key);
					GenTableIfs gen = clz.newInstance();
					gen.init(key, table_id);
					set.add(gen);
				}
				genTableIfsSetMap.put(table_id, set);
			}
		}

		// make sure that there is at least one article for every cafe
		for (int cafe_id : cafe_map.keySet()) {
			CafeObject cafe = cafe_map.get(cafe_id);
			Article article = new Article(this, cafe, start_time);
			engine.registerQItem(article);
		}
		// make initial seed
		Article seed_article = new Article(this, null, start_time + 1);
		engine.registerQItem(seed_article);
	}

	public void onEnd() throws Exception {
		for (int table_id : genTableIfsSetMap.keySet()) {
			HashSet<GenTableIfs> set = genTableIfsSetMap.get(table_id);
			for (GenTableIfs table : set) {
				table.close();
			}
		}
	}

	private void parseProps() throws Exception {
		num_comments = Long.valueOf(props.getProperty("num_comments"));
		curr_num_comments = 0;
		curr_num_articles = 0;
		reserved_num_comments = 0;
		start_time = Timestamp.valueOf(props.getProperty("start_time"))
				.getTime();
		articleArrivalMillis = new PositivePoissonSample(Integer.valueOf(props
				.getProperty("article_arrival_millis")));
		commentArrivalSecs = new PositivePoissonSample(Integer.valueOf(props
				.getProperty("comment_arrival_secs")));

		// get sample information of the number of replies for each cafe type
		int cafe_type_num = sample.getNumCafeTypes();

		mean_replies_per_cafe_type = new int[cafe_type_num];
		stddev_replies_per_cafe_type = new int[cafe_type_num];
		String[] vals = props.getProperty("mean_replies_per_cafe_type").split(
				":");
		for (int i = 0; i < cafe_type_num; i++) {
			mean_replies_per_cafe_type[i] = Integer.valueOf(vals[i]);
		}
		vals = props.getProperty("stddev_replies_per_cafe_type").split(":");
		for (int i = 0; i < cafe_type_num; i++) {
			stddev_replies_per_cafe_type[i] = Integer.valueOf(vals[i]);
		}

		for (int i = 0; i < cafe_type_num; i++) {
			SampleIfs s = new PositiveGaussianSample(
					mean_replies_per_cafe_type[i],
					stddev_replies_per_cafe_type[i]);
			sample.putCafeTypeSampleIfs(i, "num_reply", s);
		}

		double reply_prob = Double.valueOf(props.getProperty("reply_prob"));
		if (reply_prob >= 1) {
			throw new Exception("invalid reply_prob property value");
		}
		replyPropSample = new BooleanSample(reply_prob);
		makeGenTableClassList(props);
	}

	public long getStartTime() {
		return start_time;
	}

	public int getArticleArriavalMillis() throws Exception {
		Integer i = (Integer) articleArrivalMillis.nextValue();
		return i.intValue();
	}

	public int getCommentArrivalMillis() throws Exception {
		Integer i = (Integer) commentArrivalSecs.nextValue();
		return i * 1000;
	}

	public boolean getReplyProb() throws Exception {
		Boolean b = (Boolean) replyPropSample.nextValue();
		return b;
	}

	public boolean noMoreArticle() {
		return curr_num_comments > num_comments
				|| reserved_num_comments > num_comments;
	}

	public synchronized boolean reserveComment(int nc) {
		if (reserved_num_comments + nc < num_comments) {
			reserved_num_comments += nc;
			return true;
		}
		return false;
	}

	public void emitComment(int table_id, Object[] objs) throws Exception {
		HashSet<GenTableIfs> set = genTableIfsSetMap.get(table_id);
		for (GenTableIfs table : set) {
			table.emitComment(objs);
		}
		curr_num_comments++;
	}

	public void emitArticle(int table_id, Object[] objs) throws Exception {
		HashSet<GenTableIfs> set = genTableIfsSetMap.get(table_id);
		for (GenTableIfs table : set) {
			table.emitArticle(objs);
		}
		curr_num_articles++;
	}
}
