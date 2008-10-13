package cubridmanager.cubrid;

public class DBAttribute {
	public String name;
	public String type;
	public String inherit;
	public boolean isIndexed;
	public boolean isNotNull;
	public boolean isShared;
	public boolean isUnique;
	public String defaultval;

	public DBAttribute(String p_name, String p_type, String p_inherit,
			boolean p_isIndexed, boolean p_isNotNull, boolean p_isShared,
			boolean p_isUnique, String p_defaultval) {
		name = new String(p_name);
		type = new String(p_type);
		inherit = new String(p_inherit);
		isIndexed = p_isIndexed;
		isNotNull = p_isNotNull;
		isShared = p_isShared;
		isUnique = p_isUnique;
		defaultval = new String(p_defaultval);
	}
}
