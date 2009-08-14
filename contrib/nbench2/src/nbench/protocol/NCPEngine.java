package nbench.protocol;

import java.net.Socket;
import java.util.HashMap;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.OutputStream;

import org.json.JSONObject;
import org.json.JSONStringer;

public class NCPEngine {
	private int engineId;
	private Socket socket;
	private DataInputStream is;
	private DataOutputStream os;
	byte buffer[];

	private NCPMessageIfs msgIfs;
	private HashMap<String, String> prop_map;

	private static int MAX_DATA_LENGTH = 8192;

	public NCPEngine(int id, Socket socket, boolean server_mode, String value,
			NCPMessageIfs msgIfs) throws Exception {
		this.engineId = id;
		this.socket = socket;
		is = new DataInputStream(socket.getInputStream());
		os = new DataOutputStream(socket.getOutputStream());
		buffer = new byte[MAX_DATA_LENGTH];
		prop_map = new HashMap<String, String>(2);
		if (!server_mode) {
			setProtocolProperty("__whoami__", value);
			connect();
		} else {
			setProtocolProperty("__nonce__", value);
			receive();
		}
		this.msgIfs = msgIfs;
	}

	private boolean getRaw(short header) {
		return (header & 0x8000) > 0;
	}

	private short setRaw(short header) {
		return (short) (header | 0x8000);
	}

	private boolean getContinue(short header) {
		return (header & 0x4000) > 0;
	}

	private short setContinue(short header) {
		return (short) (header | 0x4000);
	}

	private short getLength(short header) {
		return (short) (header & 0x3fff);
	}

	private short setLength(short header, short length) {
		return (short) (header | length);
	}

	private void assertMessage(short header) throws Exception {
		if (getRaw(header) || getContinue(header)) {
			throw new NCPException("unexpected frame received.. header:"
					+ header);
		}
	}

	private void connect() throws Exception {
		JSONStringer req = openMessage("__CONNECTION_REQUEST__");
		req.key("__whoami__");
		req.value(getProtocolProperty("__whoami__"));
		sendMessage(closeMessage(req));

		short header = is.readShort();
		assertMessage(header);
		JSONObject resp = receiveMessage(header);
		if (!resp.getString("__message__").equals("__CONNECTION_RESPONSE__")) {
			throw new NCPException("__CONNECTION_RESPONSE__ expected. received:"
					+ resp.toString());
		} else {
			setProtocolProperty("__nonce__", resp.getString("__nonce__"));
		}
	}

	private void receive() throws Exception {
		short header = is.readShort();
		assertMessage(header);
		JSONObject req = receiveMessage(header);
		if (!req.getString("__message__").equals("__CONNECTION_REQUEST__")) {
			throw new NCPException("__CONNECTION_REQUEST__ expected. received:"
					+ req.toString());
		} else {
			setProtocolProperty("__whoami__", req.getString("__whoami__"));
		}

		JSONStringer resp = openMessage("__CONNECTION_RESPONSE__");
		resp.key("__nonce__");
		resp.value(getProtocolProperty("__nonce__"));
		sendMessage(closeMessage(resp));
	}

	public String getProtocolProperty(String name) throws Exception {
		String s = prop_map.get(name);
		if (s == null) {
			throw new NCPException("unsupported get-property:" + name);
		}
		return s;
	}

	private void setProtocolProperty(String name, String value)
			throws Exception {
		prop_map.put(name, value);
	}

	private boolean handleRawFrame(NCPProcedureIfs handler, DataInputStream is,
			byte buffer[], int length, boolean cont, NCPResult result,
			OutputStream os) throws Exception {
		int len;

		while (length > 0) {
			if (length > buffer.length) {
				len = buffer.length;
			} else {
				len = length;
			}
			is.readFully(buffer, 0, len);
			length = length - len;

			if (os != null) {
				os.write(buffer, 0, len);
			}
			return !(cont || length > 0);
		}
		return true;
	}

	private JSONObject receiveMessage(short header) throws Exception {
		boolean cont = getContinue(header);
		int length = getLength(header);
		if (cont == true) {
			throw new NCPException(
					"NCPEngine error: C bit set in message frame");
		}
		is.readFully(buffer, 0, length);
		String jsonString = new String(buffer, 0, length);
		JSONObject msg = new JSONObject(jsonString);

		String message = msg.getString("__message__");
		if (message == null) {
			throw new NCPException("message field missing:" + msg.toString());
		}
		return msg;
	}

	public int getId() {
		return engineId;
	}

	public void process(NCPProcedureIfs handler, final NCPResult result) {
		while (!handler.processDone()) {
			try {
				short header = is.readShort();
				boolean raw = getRaw(header);
				boolean cont = getContinue(header);
				int length = getLength(header);

				if (raw) {
					OutputStream os = handler.onRawDataStart(this, result);
					while (true) {
						boolean done = handleRawFrame(handler, is, buffer,
								length, cont, result, os);
						if (!done) {
							header = is.readShort();
							raw = getRaw(header);
							cont = getContinue(header);
							length = getLength(header);
						} else {
							break;
						}
					}
					handler.onRawDataEnd(this, os, result);
				} else {
					JSONObject rr = receiveMessage(header);
					String message = rr.getString("__message__");
					if (rr.getString("__message__").equals("__ABORT_REQUEST__")) {
						handler.onEnd(this, rr.getString("reason"), result);
						return;
					} else {
						boolean isError = rr.has("__error__");
						String errorMessage = null;
						if (isError) {
							errorMessage = rr.getString("__error__");
							result.setError(errorMessage);
						}
						boolean isWarning = rr.has("__warning__");
						String warningMessage = null;
						if (isWarning) {
							warningMessage = rr.getString("__warning__");
							result.setWarning(warningMessage);
						}
						handler.onMessage(this, msgIfs.getID(message), rr,
								isError, errorMessage, isWarning,
								warningMessage, result);
					}
				}
			} catch (Exception e) {
				e.printStackTrace(); //DEBUG
				result.setError(e);
				break;
			}
		}
	}

	public void sendMessage(JSONObject msg) throws Exception {
		sendMessage(msg.toString());
	}

	public void sendMessage(String msg) throws Exception {
		byte data[] = msg.getBytes();
		int len, pos;
		len = data.length;
		pos = 0;
		while (len > 0) {
			if (len > 0x3fff) {
				os.writeShort(0x4000 | 0x3fff);
				os.write(data, pos, 0x3fff);
				pos += 0x3fff;
				len -= 0x3fff;
			} else {
				os.writeShort((short) len);
				os.write(data, pos, len);
				break;
			}
		}
	}

	public void sendSimpleRequest(int id) throws Exception {
		String message = msgIfs.getName(id);
		JSONStringer stringer = openMessage(message);
		sendMessage(closeMessage(stringer));
	}

	public void disconnect(String reason) throws Exception {
		JSONStringer stringer = openMessage("__ABORT_REQUEST__");
		stringer.key("reason");
		stringer.value(reason);
		sendMessage(closeMessage(stringer));
		socket.close();
	}

	public void sendFile(File file) throws Exception {
		FileInputStream fis = new FileInputStream(file);
		sendInputStream(fis);
	}

	public void sendInputStream(InputStream is) throws Exception {
		byte buffer[] = new byte[4096];
		int read;
		short header;
		while ((read = is.read(buffer)) > 0) {
			header = 0;
			header = setRaw(header);
			header = setContinue(header);
			header = setLength(header, (short) read);
			os.writeShort(header);
			os.write(buffer, 0, read);
		}
		header = (short) 0x8000;
		os.writeShort(header);
	}

	private JSONStringer openMessage(String req_name) throws Exception {
		JSONStringer stringer = new JSONStringer();
		stringer.object();
		stringer.key("__message__");
		stringer.value(req_name);
		return stringer;
	}

	public JSONStringer openMessage(int req_id) throws Exception {
		String req_name = msgIfs.getName(req_id);
		return openMessage(req_name);
	}

	public String closeMessage(JSONStringer stringer) throws Exception {
		stringer.endObject();
		return stringer.toString();
	}

	public String closeMessage(JSONStringer stringer, NCPResult result)
			throws Exception {
		if (result.hasError()) {
			stringer.key("__error__");
			stringer.value(result.getErrorString());
		}

		if (result.hasInfo()) {
			stringer.key("__warning__");
			stringer.value(result.getWarningString());
		}
		return closeMessage(stringer);
	}

	public void sendSimpleResponse(int id, NCPResult result) throws Exception {
		JSONStringer stringer = openMessage(id);
		sendMessage(closeMessage(stringer, result));
	}
}
