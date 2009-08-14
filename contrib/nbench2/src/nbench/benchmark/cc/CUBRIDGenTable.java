package nbench.benchmark.cc;

import java.io.File;
import java.io.FileOutputStream;
import java.sql.Timestamp;
import java.text.CharacterIterator;
import java.text.StringCharacterIterator;
import java.util.Calendar;
import java.util.GregorianCalendar;

public class CUBRIDGenTable implements GenTableIfs {
	private Calendar calendar;
	private FileOutputStream fos_comment;
	private FileOutputStream fos_article;

	CUBRIDGenTable() throws Exception {
	}

	@Override
	public void init(String prefix, int table_id) throws Exception {
		calendar = new GregorianCalendar();
		// comment output
		File file = new File(prefix + "_table_" + table_id + "_comment");
		fos_comment = new FileOutputStream(file);
		String comment_line = "%class cmp_cment_"
				+ table_id
				+ "(svc_id tkt_no obj_id cment_no cment_tp_cd sprr_cment_no"
				+ " upr_cment_no cment_sort_id prit_rnk rpl_lv_cnt wrtr_mbr_id wrtr_ncknm"
				+ " wrtr_ip psacn_no prfl_photo_url pwd wrtr_hompg_url scrt_wrg_yn cment_cont"
				+ " trbk_ttl trbk_url font_no font_sz rgst_ymdt mod_ymdt del_ymdt delr_mbr_id"
				+ " dsp_yn del_rsn_tp_cd del_rsn_dtl_cont spam_scr)\n";
		fos_comment.write(comment_line.getBytes());

		// article output
		file = new File(prefix + "_table_" + table_id + "_article");
		fos_article = new FileOutputStream(file);
		String article_line = "%class cmp_cment_obj_" + table_id
				+ " (svc_id tkt_no obj_id tot_cment_cnt dsp_tot_cment_cnt"
				+ " tot_trbk_cnt lst_cment_no cment_obj_url rgst_ymdt)\n";
		fos_article.write(article_line.getBytes());
	}

	@Override
	public void emitArticle(Object[] objs) throws Exception {
		emit_a_row(fos_article, objs);
	}

	@Override
	public void emitComment(Object[] objs) throws Exception {
		emit_a_row(fos_comment, objs);
	}

	@Override
	public void close() throws Exception {
		fos_article.close();
		fos_comment.close();
	}

	private void emit_a_row(FileOutputStream fos, Object[] objs)
			throws Exception {
		for (int i = 0; i < objs.length; i++) {
			Object o = objs[i];

			if (o == null) {
				emit_null(fos);
			} else if (o instanceof String) {
				emit_char(fos, (String) o);
			} else if (o instanceof Integer) {
				emit_integer(fos, (Integer) o);
			} else if (o instanceof Timestamp) {
				emit_timestamp(fos, (Timestamp) o);
			} else {
				throw new Exception("Unsupported object type for emit:"
						+ o.getClass().toString());
			}
		}
		emit_one_row_done(fos);
	}

	private void emit_char(FileOutputStream outs, String obj) throws Exception {
		emit_char(outs, obj, false);
	}

	private void emit_char(FileOutputStream outs, String obj, boolean esc)
			throws Exception {

		String str = obj;
		outs.write('\'');
		if (esc) {
			StringBuilder result = new StringBuilder();
			StringCharacterIterator iterator = new StringCharacterIterator(str);
			char c = iterator.current();
			while (c != CharacterIterator.DONE) {
				if (c == '\'')
					result.append('\'');
				result.append(c);
				c = iterator.next();
			}
			str = result.toString();
		}
		outs.write(str.getBytes("UTF-8"));
		outs.write('\'');

		outs.write(' ');
	}

	private void emit_timestamp(FileOutputStream outs, Timestamp obj)
			throws Exception {

		Timestamp ts = obj;
		calendar.setTimeInMillis(ts.getTime());
		String am_pm;
		String str;
		
		if (calendar.get(Calendar.AM_PM) == Calendar.AM) {
			am_pm = "AM";
		} else {
			am_pm = "PM";
		}
		
		// example) '11:13:26 PM 10/01/2007'
		str = String.format("timestamp '%02d:%02d:%02d %s %02d/%02d/%04d'",
				calendar.get(Calendar.HOUR), calendar.get(Calendar.MINUTE),
				calendar.get(Calendar.SECOND), am_pm, calendar
						.get(Calendar.MONTH) + 1, calendar
						.get(Calendar.DAY_OF_MONTH), calendar
						.get(Calendar.YEAR));
		
		outs.write(str.getBytes());
		outs.write(' ');
	}

	private void emit_integer(FileOutputStream outs, Integer obj)
			throws Exception {

		outs.write(obj.toString().getBytes());
		outs.write(' ');
	}

	private void emit_null(FileOutputStream outs) throws Exception {
		outs.write("NULL ".getBytes());
	}

	private void emit_one_row_done(FileOutputStream outs) throws Exception {
		outs.write('\n');
	}

}
