package nbench.protocol;

import java.util.HashMap;


public class NCPU {
	static HashMap<String, Integer> name2id_map;
	static HashMap<Integer, String> id2name_map;

	public static final int LIST_REPO_REQUEST = 1;
	public static final int LIST_REPO_RESPONSE = 2;
	public static final int LIST_RUNNER_REQUEST = 3;
	public static final int LIST_RUNNER_RESPONSE = 4;
	public static final int PREPARE_REQUEST = 11;
	public static final int PREPARE_RESPONSE = 12;
	public static final int START_REQUEST = 13;
	public static final int START_RESPONSE = 14;
	public static final int STATUS_REQUEST = 15;
	public static final int STATUS_RESPONSE = 16;
	public static final int STOP_REQUEST = 17;
	public static final int STOP_RESPONSE = 18;
	public static final int GATHER_REQUEST = 19;
	public static final int GATHER_RESPONSE = 20;
	public static final int SHUTDOWN_REQUEST = 21;
	public static final int SHUTDOWN_RESPONSE = 22;
	public static final int SETUP_REQUEST = 23;
	public static final int SETUP_RESPONSE = 24;
	
	static {
		name2id_map = new HashMap<String, Integer>();
		id2name_map = new HashMap<Integer, String>();

		/* LIST REPO */
		name2id_map.put("LIST_REPO_REQUEST", LIST_REPO_REQUEST);
		id2name_map.put(LIST_REPO_REQUEST, "LIST_REPO_REQUEST");
		name2id_map.put("LIST_REPO_RESPONSE", LIST_REPO_RESPONSE);
		id2name_map.put(LIST_REPO_RESPONSE, "LIST_REPO_RESPONSE");
		/* LIST RUNNER */
		name2id_map.put("LIST_RUNNER_REQUEST", LIST_RUNNER_REQUEST);
		id2name_map.put(LIST_RUNNER_REQUEST, "LIST_RUNNER_REQUEST");
		name2id_map.put("LIST_RUNNER_RESPONSE", LIST_RUNNER_RESPONSE);
		id2name_map.put(LIST_RUNNER_RESPONSE, "LIST_RUNNER_RESPONSE");
		/* PREPARE */
		name2id_map.put("PREPARE_REQUEST", PREPARE_REQUEST);
		id2name_map.put(PREPARE_REQUEST, "PREPARE_REQUEST");
		name2id_map.put("PREPARE_RESPONSE", PREPARE_RESPONSE);
		id2name_map.put(PREPARE_RESPONSE, "PREPARE_RESPONSE");
		/* START */
		name2id_map.put("START_REQUEST", START_REQUEST);
		id2name_map.put(START_REQUEST, "START_REQUEST");
		name2id_map.put("START_RESPONSE", START_RESPONSE);
		id2name_map.put(START_RESPONSE, "START_RESPONSE");
		/* STATUS */
		name2id_map.put("STATUS_REQUEST", STATUS_REQUEST);
		id2name_map.put(STATUS_REQUEST, "STATUS_REQUEST");
		name2id_map.put("STATUS_RESPONSE", STATUS_RESPONSE);
		id2name_map.put(STATUS_RESPONSE, "STATUS_RESPONSE");
		/* STOP */
		name2id_map.put("STOP_REQUEST", STOP_REQUEST);
		id2name_map.put(STOP_REQUEST, "STOP_REQUEST");
		name2id_map.put("STOP_RESPONSE", STOP_RESPONSE);
		id2name_map.put(STOP_RESPONSE, "STOP_RESPONSE");
		/* GATHER */
		name2id_map.put("GATHER_REQUEST", GATHER_REQUEST);
		id2name_map.put(GATHER_REQUEST, "GATHER_REQUEST");
		name2id_map.put("GATHER_RESPONSE", GATHER_RESPONSE);
		id2name_map.put(GATHER_RESPONSE, "GATHER_RESPONSE");
		/* SHUTDOWN */
		name2id_map.put("SHUTDOWN_REQUEST", SHUTDOWN_REQUEST);
		id2name_map.put(SHUTDOWN_REQUEST, "SHUTDOWN_REQUEST");
		name2id_map.put("SHUTDOWN_RESPONSE", SHUTDOWN_RESPONSE);
		id2name_map.put(SHUTDOWN_RESPONSE, "SHUTDOWN_RESPONSE");
		/* SETUP */
		name2id_map.put("SETUP_REQUEST", SETUP_REQUEST);
		id2name_map.put(SETUP_REQUEST, "SETUP_REQUEST");
		name2id_map.put("SETUP_RESPONSE", SETUP_RESPONSE);
		id2name_map.put(SETUP_RESPONSE, "SETUP_RESPONSE");
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
	
	public static NCPMessageIfs getMessageIfs() {
		return new NCPMessageIfs() {
			public int getID(String message) throws NCPException {
				return NCPU.getID(message);
			}
			public String getName(int id) throws NCPException {
				return NCPU.getName(id);
			}
		};
	}
}
