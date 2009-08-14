package nbench.protocol;

import java.util.HashMap;



public class NCPS {
	static HashMap<String, Integer> name2id_map;
	static HashMap<Integer, String> id2name_map;

	public static final int PREPARE_REQUEST = 1;
	public static final int PREPARE_RESPONSE = 2;
	public static final int SETUP_REQUEST = 3;
	public static final int SETUP_RESPONSE = 4;
	public static final int RESOURCE_REQUEST = 5;
	public static final int RESOURCE_RESPONSE = 6;
	public static final int START_REQUEST = 7;
	public static final int START_RESPONSE = 8;
	public static final int STATUS_REQUEST = 9;
	public static final int STATUS_RESPONSE = 10;
	public static final int STOP_REQUEST = 11;
	public static final int STOP_RESPONSE = 12;
	public static final int GATHER_REQUEST = 13;
	public static final int GATHER_RESPONSE = 14;
	public static final int LOG_INFO=15;

	static {
		name2id_map = new HashMap<String, Integer>();
		id2name_map = new HashMap<Integer, String>();
		/*   */
		name2id_map.put("PREPARE_REQUEST", PREPARE_REQUEST);
		id2name_map.put(PREPARE_REQUEST, "PREPARE_REQUEST");
		name2id_map.put("PREPARE_RESPONSE", PREPARE_RESPONSE);
		id2name_map.put(PREPARE_RESPONSE, "PREPARE_RESPONSE");
		/*   */
		name2id_map.put("SETUP_REQUEST", SETUP_REQUEST);
		id2name_map.put(SETUP_REQUEST, "SETUP_REQUEST");
		name2id_map.put("SETUP_RESPONSE", SETUP_RESPONSE);
		id2name_map.put(SETUP_RESPONSE, "SETUP_RESPONSE");
		/*   */
		name2id_map.put("RESOURCE_REQUEST", RESOURCE_REQUEST);
		id2name_map.put(RESOURCE_REQUEST, "RESOURCE_REQUEST");
		name2id_map.put("RESOURCE_RESPONSE", RESOURCE_RESPONSE);
		id2name_map.put(RESOURCE_RESPONSE, "RESOURCE_RESPONSE");
		/*   */
		name2id_map.put("START_REQUEST", START_REQUEST);
		id2name_map.put(START_REQUEST, "START_REQUEST");
		name2id_map.put("START_RESPONSE", START_RESPONSE);
		id2name_map.put(START_RESPONSE, "START_RESPONSE");
		/*   */
		name2id_map.put("STATUS_REQUEST", STATUS_REQUEST);
		id2name_map.put(STATUS_REQUEST, "STATUS_REQUEST");
		name2id_map.put("STATUS_RESPONSE", STATUS_RESPONSE);
		id2name_map.put(STATUS_RESPONSE, "STATUS_RESPONSE");
		/*   */
		name2id_map.put("STOP_REQUEST", STOP_REQUEST);
		id2name_map.put(STOP_REQUEST, "STOP_REQUEST");
		name2id_map.put("STOP_RESPONSE", STOP_RESPONSE);
		id2name_map.put(STOP_RESPONSE, "STOP_RESPONSE");
		/*   */
		name2id_map.put("GATHER_REQUEST", GATHER_REQUEST);
		id2name_map.put(GATHER_REQUEST, "GATHER_REQUEST");
		name2id_map.put("GATHER_RESPONSE", GATHER_RESPONSE);
		id2name_map.put(GATHER_RESPONSE, "GATHER_RESPONSE");
		/*   */
		name2id_map.put("LOG_INFO", LOG_INFO);
		id2name_map.put(LOG_INFO, "LOG_INFO");
	}
	
	public static NCPMessageIfs getMessageIfs() {
		return new NCPMessageIfs() {
			public int getID(String message) throws NCPException {
				return NCPS.getID(message);
			}
			public String getName(int id) throws NCPException {
				return NCPS.getName(id);
			}
		};
	}
	
	public static int getID(String message) throws NCPException {
		if (name2id_map.get(message) == null) {
			throw new NCPException("undefined message:" + message);
		}
		return name2id_map.get(message);
	}
	
	public static String getName(int id) throws NCPException {
		String ret = id2name_map.get(id);
		if (ret == null) {
			throw new NCPException ("internal error MessageID " + id + " has no name");
		}
		return ret;
	}
}
