package nbench.engine.sql;

import java.util.HashMap;
import java.sql.Connection;

import nbench.common.PerfLogIfs;

public class TemplatePreparedStatementMap {
	private class SQLTemplate {
		String sql;
		String template;

		SQLTemplate(String sql, String template) {
			this.sql = sql;
			this.template = template;
		}
	}

	private Connection conn;
	PerfLogIfs listener;
	private HashMap<String, SQLTemplate> template_map;
	private HashMap<String, ForwardingPreparedStatement> prepared_map;

	public TemplatePreparedStatementMap(int sz, Connection conn,
			PerfLogIfs listener) {
		this.conn = conn;
		this.listener = listener;
		template_map = new HashMap<String, SQLTemplate>(sz);
		prepared_map = new HashMap<String, ForwardingPreparedStatement>(sz);
	}

	public Object get(String name) {
		return prepared_map.get(name);
	}

	public Object get(String stmt, Object template) throws Exception {
		String template_string = stmt + "[" + template.toString() + "]";
		ForwardingPreparedStatement fps = prepared_map.get(template_string);
		if (fps != null) {
			return fps;
		}
		// do lazy initialization
		SQLTemplate sqlTemplate = template_map.get(stmt);
		if (sqlTemplate != null) {
			String sql = sqlTemplate.sql.replaceAll("\\$" + sqlTemplate.template
					+ "\\$", template.toString());

			ForwardingPreparedStatement ps = new ForwardingPreparedStatement(
					template_string, conn.prepareStatement(sql), listener);
			prepared_map.put(template_string, ps);
			return ps;
		} else {
			return null;
		}
	}

	public void addPrepared(String name, String stmt) throws Exception {
		ForwardingPreparedStatement ps = new ForwardingPreparedStatement(name,
				conn.prepareStatement(stmt), listener);
		prepared_map.put(name, ps);
	}

	public void addTemplatePrepared(String name, String stmt, String template)
			throws Exception {
		template_map.put(name, new SQLTemplate(stmt, template));
	}
}
