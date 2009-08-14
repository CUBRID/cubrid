package nbench.benchmark.cc;

import java.util.HashMap;
import java.util.Random;
import java.sql.Timestamp;

public class Article extends QItem {
	private static Integer integerZero = new Integer(0);
	private GenContext context;
	CafeObject cafe;
	private Random rand;
	private HashMap<Integer, Comment> comment_map;
	private boolean is_seed;

	Integer svc_id;
	Integer tkt_no;
	String obj_id;
	Integer tot_trbk_cnt;
	String cment_obj_url;
	Timestamp rgst_ymdt;

	public Article(GenContext context, CafeObject cf, long time)
			throws Exception {
		super(time);
		this.context = context;
		rand = new Random();
		if (cf == null) {
			cafe = context.sample.nextCafe();
			is_seed = false;
		} else {
			// this is seed article
			cafe = cf;
			is_seed = true;
		}
		comment_map = new HashMap<Integer, Comment>();
		svc_id = cafe.getCafeID();
		tkt_no = (Integer) context.sample.getGlobalSample("tkt_no");
		obj_id = (String) context.sample.getTableSample(cafe.getTableID(),
				"obj_id");
		tot_trbk_cnt = integerZero;
		cment_obj_url = null;
		rgst_ymdt = new Timestamp(time);
	}

	@Override
	int process(QEngine engine, long vt, GenContext context) throws Exception {
		int last_comment_no = 0;
		int num_reply = 0;
		int process_count = 0;
		long cum_reply_arrival = vt;

		if (!is_seed) {
			num_reply = (Integer) context.sample.getCafeTypeSample(cafe
					.getCafeType(), "num_reply");
			if (!context.reserveComment(num_reply)) {
				return 0;
			}
		} else {
			num_reply = 1;
		}

		while (process_count < num_reply) {
			boolean is_reply = context.getReplyProb();
			Comment reply;
			int sz = comment_map.size();

			cum_reply_arrival = cum_reply_arrival
					+ context.getCommentArrivalMillis();

			if (sz > 0 && is_reply) {
				// random choice will works
				while (true) {
					int index = rand.nextInt(sz);
					Comment c = comment_map.get(index + 1);
					if (c._level < 10 && c._position < 3843) {
						reply = new Comment(context, c, cum_reply_arrival);
						last_comment_no = reply.cment_no;
						break;
					}
				}
			} else {
				reply = new Comment(this, context, cum_reply_arrival);
				last_comment_no = reply.cment_no;
			}
			comment_map.put(sz + 1, reply);
			engine.registerQItem(reply);
			process_count++;
		}

		doEmit(num_reply, last_comment_no);

		if (!is_seed && !context.noMoreArticle()) {
			long next_article_arrival = vt + context.getArticleArriavalMillis();
			Article next_article = new Article(context, null,
					next_article_arrival);
			engine.registerQItem(next_article);
		}
		return 0;
	}

	private void doEmit(int total_comment_cnt, int last_comment_num)
			throws Exception {
		Integer tot_cment_cnt = total_comment_cnt;
		Integer dsp_tot_cment_cnt = tot_cment_cnt;
		Integer lst_cment_no = last_comment_num;

		Object objs[] = { svc_id, tkt_no, obj_id, tot_cment_cnt,
				dsp_tot_cment_cnt, tot_trbk_cnt, lst_cment_no, cment_obj_url,
				rgst_ymdt };
		context.emitArticle(cafe.getTableID(), objs);
	}
}
