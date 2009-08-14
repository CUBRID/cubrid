package nbench.common;

import java.sql.Types;
import java.math.BigDecimal;
import java.util.HashMap;
import java.sql.Timestamp;

public class ValueType {
	public final static int INTEGER = Types.INTEGER;
	public final static int STRING = Types.VARCHAR;
	public final static int TIMESTAMP = Types.TIMESTAMP;
	public final static int NUMERIC = Types.NUMERIC;

	private static HashMap<String, Integer> name_type_map;
	private static HashMap<Integer, String> type_name_map;
	static {
		name_type_map = new HashMap<String, Integer>();
		name_type_map.put("INTEGER", INTEGER);
		name_type_map.put("STRING", STRING);
		name_type_map.put("TIMESTAMP", TIMESTAMP);
		name_type_map.put("NUMERIC", NUMERIC);

		type_name_map = new HashMap<Integer, String>();
		type_name_map.put(INTEGER, "INTEGER");
		type_name_map.put(STRING, "STRING");
		type_name_map.put(TIMESTAMP, "TIMESTAMP");
		type_name_map.put(NUMERIC, "NUMERIC");
	}

	public static int typeOfName(String name) throws NBenchException {
		Integer t = ValueType.name_type_map.get(name.toUpperCase());
		if (t != null)
			return t.intValue();
		throw new NBenchException("unsupported type name:" + name);
	}

	public static String typeOfId(int id) throws NBenchException {
		String s = ValueType.type_name_map.get(id);
		if (s != null)
			return s;
		throw new NBenchException("unsupported type id:" + id);
	}

	public static Object convertTo(int type, Object from)
			throws NBenchException {

		if (from == null) {
			return null;
		}
		switch (type) {
		case INTEGER: {
			if (from instanceof Integer) {
				return from;
			} else if (from instanceof Double) {
				return ((Double) from).intValue();
			} else if (from instanceof String) {
				return Integer.valueOf((String) from);
			}
			break;
		}
		case STRING: {
			if (from instanceof String) {
				return from;
			}
			break;
		}
		case TIMESTAMP: {
			if (from instanceof Timestamp) {
				return from;
			}
			break;
		}
		case NUMERIC: {
			if (from instanceof BigDecimal) {
				return from;
			}
			break;
		}
		default:
			throw new NBenchException("unsupported type id:" + type);
		}
		throw new NBenchException("unsupported conversion:from "
				+ from.getClass().toString() + " to " + typeOfId(type));

	}
}
