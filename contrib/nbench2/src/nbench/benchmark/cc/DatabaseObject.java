package nbench.benchmark.cc;

import java.util.Map;
import java.util.Properties;
import java.util.Random;
import java.util.HashMap;

import nbench.common.ResourceIfs;
import nbench.common.ResourceProviderIfs;
import nbench.common.UserHostVarIfs;

public class DatabaseObject implements UserHostVarIfs {

	private class HistoChoice {
		private Random rand;
		private long[] histo;
		private Object[] objects;
		private double[] pdf;
		private long total;
		private int curr_index;

		public HistoChoice(int max_size) {
			rand = new Random();
			histo = new long[max_size];
			objects = new Object[max_size];
			pdf = new double[max_size];
			total = 0L;
			curr_index = 0;
		}

		public void add(long histo_val, Object obj) throws Exception {
			if (curr_index < histo.length) {
				histo[curr_index] = histo_val;
				objects[curr_index] = obj;
				total += histo_val;
				curr_index++;
			} else {
				throw new Exception("out of histo index");
			}
			calc_pdf();
		}

		public Object choose() throws Exception {
			double r = rand.nextDouble();
			for (int i = 0; i < curr_index; i++) {
				if (pdf[i] >= r) {
					return objects[i];
				}
			}
			new Exception().printStackTrace();
			throw new Exception("should not happen");
		}

		private void calc_pdf() throws Exception {
			if (curr_index <= 0) {
				throw new Exception("empty histogram");
			}

			double cd = 0;
			int i;
			for (i = 0; i < curr_index; i++) {
				double d = (double) histo[i] / (double) total;
				cd += d;
				pdf[i] = cd;
			}
			pdf[i - 1] = 1.0;
		}
	}

	private class CafeType {
		Random rand;
		int comment_size_factor;

		HistoChoice table_index_choice;
		long total_cafe_type_comments;
		int last_generated_cafe_id;

		public CafeType(String val, int[] table_ids) {
			rand = new Random();
			comment_size_factor = Integer.valueOf(val);
			table_index_choice = new HistoChoice(table_ids.length);
		}

		public int choose_table_index() throws Exception {
			Integer tid = (Integer) table_index_choice.choose();
			return tid.intValue();
		}
	}

	private class Table {
		final String value;
		final int table_id;

		public Table(int table_id, String val) {
			this.table_id = table_id;
			value = val;
		}
	}

	private class Cell {
		final int row_idx;
		final int col_idx;
		final int num_cafe;
		final int size_factor;
		int cafe_id_start_inclusive;

		Cell(int r, int c, int nc, int sf) {
			row_idx = r;
			col_idx = c;
			num_cafe = nc;
			size_factor = sf;
		}
	}

	private int modular_number;
	private CafeType[] cafe_types;
	private Table[] tables;
	private int[] tableIDs;
	private Cell[][] cells;
	private HashMap<Integer, CafeObject> cafeObjectMap;
	private HistoChoice cafe_type_choice;

	//
	// for script interfacing
	//
	private HashMap<Integer, CafeObject> activeArticleCafeObjectMap;
	private int active_articles;
	private Random aaRand;

	public DatabaseObject(Properties props) throws Exception {
		doInitialize(props);
	}

	public DatabaseObject() throws Exception {
	}

	private void doInitialize(Properties props) throws Exception {
		generateTable(props);
	}

	private void generateTable(Properties props) throws Exception {
		int i, j;
		int num_tables;

		//
		// create and initialize cafe_types array
		//
		cafe_types = new CafeType[Integer.valueOf(props
				.getProperty("cafe_type_num"))];
		modular_number = Integer.valueOf(props.getProperty("table_id_modular"));

		//
		// get the number of actual row
		//
		num_tables = 0;
		for (String key : props.stringPropertyNames()) {
			if (key.startsWith("table_id.")) {
				num_tables++;
			}
		}

		if (num_tables <= 0) {
			throw new Exception("no table specified");
		}
		cafe_type_choice = new HistoChoice(cafe_types.length);

		//
		// create ROW and array of row IDs
		//
		tables = new Table[num_tables];
		tableIDs = new int[num_tables];
		int tmp = 0;

		for (String key : props.stringPropertyNames()) {
			if (key.startsWith("table_id.")) {
				int table_id = Integer.valueOf(key.substring("table_id."
						.length()));
				tables[tmp] = new Table(table_id, props.getProperty(key));
				tableIDs[tmp] = table_id;
				tmp++;
			}
		}

		cells = new Cell[tables.length][cafe_types.length];

		//
		// parse COLUMN
		//
		for (String key : props.stringPropertyNames()) {
			if (key.startsWith("cafe_type.")) {
				int col_idx = Integer.valueOf(key.substring("cafe_type."
						.length()));
				cafe_types[col_idx] = new CafeType(props.getProperty(key),
						tableIDs);
			}
		}

		//
		// create CELL
		//
		int total_cafe_ids = 0;

		for (i = 0; i < tables.length; i++) {
			String[] nums = tables[i].value.split(":");
			for (j = 0; j < cafe_types.length; j++) {
				int nc = Integer.valueOf(nums[j]);
				if (nc > 0) {
					total_cafe_ids += nc;
					cells[i][j] = new Cell(i, j, nc,
							cafe_types[j].comment_size_factor);
				}
			}
		}
		cafeObjectMap = new HashMap<Integer, CafeObject>(total_cafe_ids);

		//
		// type 0 class cafe id < type 1 class cafe id < ...
		//
		int cafe_id_curr = 0;

		for (i = 0; i < cafe_types.length; i++) {
			long comment_sum = 0;

			for (j = 0; j < tables.length; j++) {
				Cell c = cells[j][i];
				if (c == null) {
					continue;
				}
				boolean first = true;
				for (int k = 0; k < c.num_cafe; k++) {
					cafe_id_curr = next_cafe_ID(cafe_id_curr,
							tables[j].table_id);
					if (first) {
						c.cafe_id_start_inclusive = cafe_id_curr;
						first = false;
					}

					registerCafe(tables[j].table_id, i, cafe_id_curr);
				}

				long cell_sum = cafe_types[i].comment_size_factor * c.num_cafe;
				comment_sum += cell_sum;
				cafe_types[i].table_index_choice.add(cell_sum, new Integer(j));
			}
			cafe_types[i].total_cafe_type_comments = comment_sum;
			cafe_types[i].last_generated_cafe_id = cafe_id_curr;
		}

		for (i = 0; i < cafe_types.length; i++) {
			cafe_type_choice.add(cafe_types[i].total_cafe_type_comments,
					new Integer(i));
		}
	}

	private void registerCafe(int table_id, int cafe_type, int cafe_id) {
		CafeObject cafe = new CafeObject(table_id, cafe_id, cafe_type);

		cafeObjectMap.put(cafe_id, cafe);
	}

	private int next_cafe_ID(int curr, int need_mod_val) {
		int curr_mod_val = curr % modular_number;
		return curr + modular_number + (need_mod_val - curr_mod_val);
	}

	private int choose_cafe_ID(Random rand, int start_inclusive, int n) {
		int idx = rand.nextInt(n);

		return start_inclusive + modular_number * idx;
	}

	private int choose_cafe_type() throws Exception {
		Integer i = (Integer) cafe_type_choice.choose();

		return i.intValue();
	}

	private int nextCafeID() throws Exception {
		int cafe_type = choose_cafe_type();
		int table_id_idx = cafe_types[cafe_type].choose_table_index();

		Cell cell = cells[table_id_idx][cafe_type];
		return choose_cafe_ID(cafe_types[cafe_type].rand,
				cell.cafe_id_start_inclusive, cell.num_cafe);
	}

	HashMap<Integer, CafeObject> getCafeObjectMap() {
		return cafeObjectMap;
	}

	int[] getTableIDs() {
		return tableIDs;
	}

	int getNumCafeTypes() {
		return cafe_types.length;
	}

	CafeObject getCafeObject(int cafe_id) throws Exception {
		CafeObject cafe = cafeObjectMap.get(cafe_id);
		if (cafe == null) {
			throw new Exception("no such cafe object:" + cafe_id);
		}
		return cafe;
	}

	public CafeObject chooseCafeObjectFromTable() throws Exception {
		int cafe_id = nextCafeID();
		CafeObject cafe = cafeObjectMap.get(cafe_id);

		if (cafe == null) {
			throw new Exception("invalid cafe_id");
		}
		return cafe;
	}

	// //////////////////////////////
	// Script Interfacing Functions
	// //////////////////////////////
	@Override
	public void prepareSetup(Map<String, String> map, ResourceProviderIfs rp)
			throws Exception {
		ResourceIfs res = rp.getResource(map.get("prop"));
		if (res == null) {
			throw new Exception("no property file:" + map.get("prop"));
		}

		Properties props = new Properties();
		props.load(res.getResourceInputStream());
		res.close();

		doInitialize(props);
		
		activeArticleCafeObjectMap = new HashMap<Integer, CafeObject>();
		active_articles = 0;
		aaRand = new Random();
	}

	public CafeObject chooseCafeForNewActiveArticle() throws Exception {
		CafeObject cafe = chooseCafeObjectFromTable();
		
		activeArticleCafeObjectMap.put(active_articles++, cafe);
		return cafe;
	}
	
	public CafeObject chooseCafe() throws Exception {
		if(active_articles == 0) {
			return null;
		}
		
		return activeArticleCafeObjectMap.get(aaRand.nextInt(active_articles));
	}

	// //////////////////////////////////////////////////////////////////////
	// TEST
	// //////////////////////////////////////////////////////////////////////
	public static void main(String[] args) {
		if (args.length != 1) {
			System.out.println("usage: TableCafeInfo <property file>");
			System.exit(0);
		}
		try {
			if (true) {
				Properties props = new Properties();
				java.io.File file = new java.io.File(args[0]);
				props.load(new java.io.FileInputStream(file));
				DatabaseObject info = new DatabaseObject(props);

				long start_timestamp = System.currentTimeMillis();
				int i;
				for (i = 0; i < 1000000; i++) {
					CafeObject cafe = info.chooseCafeObjectFromTable();
					if (i < 1000) {
						System.out.println("cafe_id:" + cafe.getCafeID()
								+ ", cafe_type:" + cafe.getCafeType()
								+ ", table_id:" + cafe.getTableID());
					}
				}
				long end_timestamp = System.currentTimeMillis();
				System.out
						.println("total " + i + " nextCafe() call in "
								+ +(end_timestamp - start_timestamp)
								+ " milli-seconds");
			}
		} catch (Exception e) {
			e.printStackTrace();
		}
	}
}
