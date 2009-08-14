package nbench.benchmark.cc;

import java.io.File;
import java.io.FileOutputStream;
import java.sql.Timestamp;
import java.text.CharacterIterator;
import java.text.StringCharacterIterator;
import java.util.Calendar;
import java.util.GregorianCalendar;

public class MySQLGenTable implements GenTableIfs {
	private Calendar calendar;
	private FileOutputStream fos_comment;
	private FileOutputStream fos_article;
	

	public MySQLGenTable() {
	}
	
	@Override
	public void init(String prefix, int table_id) throws Exception {
		calendar = new GregorianCalendar();
		// comment output
		File file = new File(prefix + "_table_" + table_id + "_comment");
		fos_comment = new FileOutputStream(file);
		
		// article output
		file = new File(prefix + "_table_" + table_id + "_article");
		fos_article = new FileOutputStream(file);
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
		String str;

		// example) '2007/01/01 17:01:02'
		str = String.format("'%04d/%02d/%02d %02d:%02d:%02d'",
				calendar.get(Calendar.YEAR),
				calendar.get(Calendar.MONTH) + 1,
				calendar.get(Calendar.DAY_OF_MONTH),
				calendar.get(Calendar.HOUR_OF_DAY), 
				calendar.get(Calendar.MINUTE),
				calendar.get(Calendar.SECOND));
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
