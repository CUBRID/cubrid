package cubrid.jdbc.net;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketTimeoutException;

import cubrid.jdbc.jci.UConnection;
import cubrid.jdbc.jci.UErrorCode;
import cubrid.jdbc.jci.UJciException;
import cubrid.jdbc.jci.UTimedDataInputStream;

public class BrokerHandler {
    private static int TIMEOUT_UNIT = 1000;

    public static Socket connectBroker(String ip, int port, int timeout)
	    throws IOException, UJciException {
	Socket toBroker = null;
	UTimedDataInputStream in = null;
	DataOutputStream out = null;
	long begin = System.currentTimeMillis();

	try {
	    toBroker = new Socket();
	    InetSocketAddress brokerAddress = new InetSocketAddress(ip, port);
	    if (timeout <= 0) {
		toBroker.connect(brokerAddress);
	    } else {
		toBroker.connect(brokerAddress, timeout);
		timeout -= (System.currentTimeMillis() - begin);
		if (timeout <= 0) {
		    toBroker.close();
		    throw new UJciException(UErrorCode.ER_TIMEOUT);
		}
	    }

	    toBroker.setSoTimeout(TIMEOUT_UNIT);
	    in = new UTimedDataInputStream(toBroker.getInputStream(), ip, port, timeout);
	    out = new DataOutputStream(toBroker.getOutputStream());
	    out.write(UConnection.driverInfo);
	    out.flush();
	    int code = in.readInt();
	    if (code < 0) {
		throw new UJciException(code);
	    } else if (code == 0) {
		return toBroker;
	    }

	    // if (code > 0) { only windows }
	    toBroker.setSoLinger(true, 0);
	    toBroker.close();

	    toBroker = new Socket(); // need instantiation
	    brokerAddress = new InetSocketAddress(ip, code);
	    if (timeout <= 0) {
		toBroker.connect(brokerAddress);
	    } else {
		timeout -= (System.currentTimeMillis() - begin);
		if (timeout <= 0) {
		    throw new UJciException(UErrorCode.ER_TIMEOUT);
		}
		toBroker.connect(brokerAddress, timeout);
	    }

	    return toBroker;
	} catch (SocketTimeoutException e) {
	    if (toBroker != null) {
		toBroker.close();
	    }
	    throw new UJciException(UErrorCode.ER_TIMEOUT, e);
	} catch (IOException e) {
	    if (toBroker != null) {
		toBroker.close();
	    }
	    throw new UJciException(UErrorCode.ER_CONNECTION, e);
	} finally {
	}
    }

    private static byte[] PING_INFO = { 'P', 'I', 'N', 'G', 0, 0, 0, 0, 0, 0 };
    public static void pingBroker(String ip, int port, int timeout)
	    throws IOException {
	Socket toBroker = null;
	DataInputStream in = null;
	DataOutputStream out = null;
	long begin = System.currentTimeMillis();

	try {
	    toBroker = new Socket();
	    InetSocketAddress brokerAddress = new InetSocketAddress(ip, port);
	    if (timeout <= 0) {
		toBroker.connect(brokerAddress);
	    } else {
		toBroker.connect(brokerAddress, timeout);
		timeout -= (System.currentTimeMillis() - begin);
		if (timeout <= 0) {
		    String msg = UErrorCode.codeToMessage(UErrorCode.ER_TIMEOUT);
		    throw new SocketTimeoutException(msg);
		}
		toBroker.setSoTimeout(timeout);
	    }

	    in = new DataInputStream(toBroker.getInputStream());
	    out = new DataOutputStream(toBroker.getOutputStream());

	    out.write(PING_INFO);
	    out.flush();
	    in.readInt();
	} finally {
	    if (in != null) in.close();
	    if (out != null) out.close();
	    if (toBroker != null) toBroker.close();
	}
    }

    private static byte[] CANCEL_INFO = { 'C', 'A', 'N', 'C', 'E', 'L' };
    public static void cancelBroker(String ip, int port, int process, int timeout)
	    throws IOException, UJciException {
	Socket toBroker = null;
	DataInputStream in = null;
	DataOutputStream out = null;
	long begin = System.currentTimeMillis();

	try {
	    toBroker = new Socket();
	    InetSocketAddress brokerAddress = new InetSocketAddress(ip, port);
	    if (timeout <= 0) {
		toBroker.connect(brokerAddress);
	    } else {
		toBroker.connect(brokerAddress, timeout);
		timeout -= (System.currentTimeMillis() - begin);
		if (timeout <= 0) {
		    throw new UJciException(UErrorCode.ER_TIMEOUT);
		}
		toBroker.setSoTimeout(timeout);
	    }

	    in = new DataInputStream(toBroker.getInputStream());
	    out = new DataOutputStream(toBroker.getOutputStream());

	    out.write(CANCEL_INFO);
	    out.writeInt(process);
	    out.flush();

	    int error = in.readInt();
	    if (error < 0) {
		throw new UJciException(UErrorCode.CAS_ER_QUERY_CANCEL);
	    }
	} catch (SocketTimeoutException e) {
	    throw new UJciException(UErrorCode.ER_TIMEOUT);
	} finally {
	    if (in != null) in.close();
	    if (out != null) out.close();
	    if (toBroker != null) toBroker.close();
	}
    }

}
