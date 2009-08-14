package nbench.benchmark.cc;

import java.sql.Timestamp;

public class Comment extends QItem {
	private Article article;
	Comment parent;

	// calculation fields
	int _level;
	int _position;

	// column fields
	Integer svc_id;
	Integer tkt_no;
	String obj_id;
	Integer cment_no;
	String cment_tp_cd;
	Integer sprr_cment_no;
	Integer upr_cment_no;
	String cment_sort_id;
	Integer prit_rnk;
	Integer rpl_lv_cnt;
	String wrtr_mbr_id;
	String wrtr_ncknm;
	String wrtr_ip;
	Integer psacn_no;
	String prfl_photo_url;
	String pwd;
	String wrtr_hompg_url;
	String scrt_wrg_yn;
	String cment_cont;
	String trbk_ttl;
	String trbk_url;
	Integer font_no;
	Integer font_sz;
	Timestamp rgst_ymdt;
	Timestamp mod_ymdt;
	Timestamp del_ymdt;
	String delr_mbr_id;
	String dsp_yn;
	String del_rsn_tp_cd;
	String del_rsn_dtl_cont;
	Integer spam_scr;

	public Comment(Article article, GenContext context, long time)
			throws Exception {
		super(time);
		this.article = article;
		parent = null;
		_level = 0;
		_position = 0;
		svc_id = article.svc_id;
		tkt_no = article.tkt_no;
		obj_id = article.obj_id;

		init_other_fields(context);

		// sprr_cment_no, upr_ment_no, cment_sort_id
		sprr_cment_no = cment_no;
		upr_cment_no = cment_no;
		cment_sort_id = "00000000000000000000";

	}

	public Comment(GenContext context, Comment parent, long time)
			throws Exception {
		super(time);
		this.article = parent.article;
		this.parent = parent;
		_level = parent._level + 1;
		parent._position++;
		_position = 0;

		svc_id = parent.svc_id;
		tkt_no = parent.tkt_no;
		obj_id = parent.obj_id;

		init_other_fields(context);

		// sprr_cment_no, upr_ment_no, cment_sort_id
		Comment tc = parent;
		while (tc != null) {
			sprr_cment_no = tc.cment_no;
			tc = tc.parent;
		}
		upr_cment_no = parent.cment_no;
		cment_sort_id = calc_sort_id(parent.cment_sort_id, _level,
				parent._position);
	}

	private void init_other_fields(GenContext ctx) throws Exception {
		SampleContext context = ctx.sample;

		cment_tp_cd = (String) context.getGlobalSample("cment_tp_cd");
		cment_no = (Integer) context.getTableSample(article.cafe.getTableID(),
				"cment_no");
		prit_rnk = (Integer) context.getGlobalSample("prit_rnk");
		rpl_lv_cnt = new Integer(_level);
		wrtr_mbr_id = (String) context.getGlobalSample("wrtr_mbr_id");
		wrtr_ncknm = (String) context.getGlobalSample("wrtr_ncknm");
		wrtr_ip = (String) context.getGlobalSample("wrtr_ip");
		psacn_no = (Integer) context.getGlobalSample("psacn_no");
		prfl_photo_url = (String) context.getGlobalSample("prfl_photo_url");
		pwd = (String) context.getGlobalSample("pwd");
		wrtr_hompg_url = (String) context.getGlobalSample("wrtr_hompg_url");
		scrt_wrg_yn = (String) context.getGlobalSample("scrt_wrg_yn");
		cment_cont = (String) context.getGlobalSample("cment_cont");
		trbk_ttl = (String) context.getGlobalSample("trbk_ttl");
		trbk_url = (String) context.getGlobalSample("trbk_url");
		font_no = (Integer) context.getGlobalSample("font_no");
		font_sz = (Integer) context.getGlobalSample("font_sz");
		rgst_ymdt = new Timestamp(time);
		mod_ymdt = rgst_ymdt;
		del_ymdt = (Timestamp) context.getGlobalSample("del_ymdt");
		delr_mbr_id = (String) context.getGlobalSample("delr_mbr_id");
		dsp_yn = (String) context.getGlobalSample("dsp_yn");
		del_rsn_tp_cd = (String) context.getGlobalSample("del_rsn_tp_cd");
		del_rsn_dtl_cont = (String) context.getGlobalSample("del_rsn_dtl_cont");
		spam_scr = (Integer) context.getGlobalSample("spam_scr");
	}

	private String calc_sort_id(String s, int level, int pos) throws Exception {
		String prefix = s.substring(0, (level - 1) * 2);
		String infix = calc_sort_id_seg(pos);
		String suffix = s.substring(level * 2);
		return prefix + infix + suffix;
	}

	private String calc_sort_id_seg(int pos) throws Exception {
		if (pos > 3843) {
			throw new Exception("Out Of Position In Reply Comment");
		}
		int high = pos / 62;
		int low = pos % 62;
		return getDigit(high) + getDigit(low);
	}

	private String getDigit(int dec) {
		if (dec < 10) {
			return "" + dec;
		} else if (dec < 10 + 26) {
			return "" + "ABCDEFGHIJKLMNOPQRSTUVWZYZ".charAt(dec - 10);
		} else {
			return "" + "abcdefghijklmnopqrstuvwxyz".charAt(dec - 36);
		}
	}

	@Override
	int process(QEngine engine, long vt, GenContext context) throws Exception {
		doEmit(context);
		return 1;
	}

	private void doEmit(GenContext context) throws Exception {
		Object objs[] = { svc_id, tkt_no, obj_id, cment_no, cment_tp_cd,
				sprr_cment_no, upr_cment_no, cment_sort_id, prit_rnk,
				rpl_lv_cnt, wrtr_mbr_id, wrtr_ncknm, wrtr_ip, psacn_no,
				prfl_photo_url, pwd, wrtr_hompg_url, scrt_wrg_yn, cment_cont,
				trbk_ttl, trbk_url, font_no, font_sz, rgst_ymdt, mod_ymdt,
				del_ymdt, delr_mbr_id, dsp_yn, del_rsn_tp_cd, del_rsn_dtl_cont,
				spam_scr };
		context.emitComment(article.cafe.getTableID(), objs);
	}
}
