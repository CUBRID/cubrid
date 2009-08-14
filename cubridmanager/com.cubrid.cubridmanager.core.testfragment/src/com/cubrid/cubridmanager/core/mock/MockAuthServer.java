package com.cubrid.cubridmanager.core.mock;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.HashMap;
import java.util.Map;

/**
 * 
 * Mock authentication server daemon
 * 
 * a mockup authentication server for a JUnit test framework
 * 
 * @author pcraft
 * @version 1.0 - 2009. 06. 21 created by pcraft
 */
public class MockAuthServer extends Thread implements IMockServer {

	private Socket client = null;
	private boolean shutdown = false;
	
	public static final String VALID_TOKEN = "4504b930fc1be99b53150f8614c1b813904e8f36e4f8cd68db53a53dd5ccd9aa7926f07dd201b6aa";
	
	private static Map<String, MockAuthServer> authSessionMap = new HashMap<String, MockAuthServer>();
	
	public static void main(String[] args) throws Exception {
		
		new MockAuthServer().initialize();
		
	}
	
	public void initialize() throws Exception {
		
		ServerSocket ssock = new ServerSocket(8001);

		for (;;) {
			Socket sock = ssock.accept();
			MockAuthServer svr = new MockAuthServer();
			svr.bindSocket(sock);
			svr.start();
			//System.out.println("<MockAuthServer>connected</MockAuthServer>");
		}
		
	}
	
	public MockAuthServer() {
	}
	
	public void bindSocket(Socket sock) {
		this.client = sock;
	}
	
	public static boolean existSession(String key) {
		return authSessionMap.containsKey(key);
	}
	
	public void shutdown() {
		this.shutdown = true;
	}
	
	public void run() {
		
		BufferedReader br = null;
		BufferedWriter bw = null;

		for (;;) {
			
			try {
				br = new BufferedReader(new InputStreamReader(client.getInputStream()));
				StringBuilder sb = new StringBuilder();
				for (;;)
				{
					String line = br.readLine();
					if (line == null || "".equals(line.trim()))
						break;
					sb.append(line).append("\n");
				}
				
				String authMsg = "task:authenticate\nstatus:success\nnote:none\ntoken:"+VALID_TOKEN+"\n\n";
				
				// authenticate
				authSessionMap.put(VALID_TOKEN, this);
				
				bw = new BufferedWriter(new OutputStreamWriter(client.getOutputStream()));
				bw.write(authMsg);
				bw.flush();
			
				while (!this.shutdown) {
					try { Thread.sleep(1000); } catch (Exception ignored) {}
				}
		
			} catch (Exception e) {
				e.printStackTrace();
			} finally {
				if (br != null) try { br.close(); } catch (Exception ignored) {}
				if (bw != null) try { bw.close(); } catch (Exception ignored) {}
				if (client != null) try { client.close(); } catch (Exception ignored) {}
			}
			
//			System.out.println("<MockAuthServer>closed</MockAuthServer>");
		}
		
	}

}
