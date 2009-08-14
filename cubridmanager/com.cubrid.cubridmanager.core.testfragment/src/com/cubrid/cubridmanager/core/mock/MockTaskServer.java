package com.cubrid.cubridmanager.core.mock;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileReader;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import com.cubrid.cubridmanager.core.common.StringUtil;

/**
 * 
 * Mock command server daemon
 * 
 * a mockup command server for a JUnit test framework
 * 
 * @author pcraft
 * @version 1.0 - 2009. 06. 21 created by pcraft
 */
public class MockTaskServer extends Thread implements IMockServer {

	private static Map<String, String> requestAndResponseScriptMap = null;
	private Socket client = null;

	public static void main(String[] args) throws Exception {

		new MockTaskServer().initialize();
		
	}
	
	public void initialize() throws Exception {
		
		System.out.println("- Loading test scripts...");
		load();
		System.out.println("- Loaded...");
		
		ServerSocket ssock = new ServerSocket(8002);

		for (;;) {
			Socket sock = ssock.accept();
			MockTaskServer svr = new MockTaskServer();
			svr.bindSocket(sock);
			svr.start();
//			System.out.println("<MockTaskServer>connected</MockTaskServer>");
		}
		
	}
	
	public MockTaskServer() {
	}
	
	public void bindSocket(Socket socket) {
		this.client = socket;
	}
	
	public void run() {
		
		BufferedReader br = null;
		BufferedWriter bw = null;
		try {
			
			br = new BufferedReader(new InputStreamReader(client.getInputStream()));
			String reqHash = hashTaskScript(br);
//			System.out.println("<cmd-hash>"+reqHash+"</cmd-hash>");
			String data = requestAndResponseScriptMap.get(reqHash);
			
			if (data == null) {
				throw new NullPointerException("Mock server unit test script not found!!");
			}
			
			bw = new BufferedWriter(new OutputStreamWriter(client.getOutputStream()));
			bw.write(data);
			bw.flush();
		
		} catch (Exception e) {
			e.printStackTrace();
		} finally {
			if (br != null) try { br.close(); } catch (Exception ignored) {}
			if (bw != null) try { bw.close(); } catch (Exception ignored) {}
			if (client != null) try { client.close(); } catch (Exception ignored) {}
		}
		
//		System.out.println("<MockTaskServer>closed</MockTaskServer>");
		
	}
	
	public static String getFileText(File aFile)
	{
		FileReader fr = null;
		BufferedReader br = null;
		try
		{
			StringBuilder sb = new StringBuilder();
			fr = new FileReader(aFile);
			br = new BufferedReader(fr);
			for (;;)
			{
				String line = br.readLine();
				if (line == null)
					break;
				sb.append(line).append("\n");
			}
			return sb.toString();
		}
		catch (Exception ex)
		{
			return null;
		}
		finally
		{
			try { br.close(); } catch (Exception ignored) {}
			try { fr.close(); } catch (Exception ignored) {}
		}
	}
	
	public static String getHashedFileText(File aFile)
	{
		BufferedReader br = null;
		try
		{
			br = new BufferedReader(new FileReader(aFile));
			return hashTaskScript(br);
		}
		catch (Exception ex)
		{
			return null;
		}
		finally
		{
			try { br.close(); } catch (Exception ignored) {}
		}
	}

	public static String hashTaskScript(BufferedReader br)
	{
		List<String> list = new ArrayList<String>();
		try
		{
			for (;;)
			{
				String line = br.readLine();
				if (line == null || "".equals(line.trim()))
					break;
				list.add(line+"\n");
			}
			
			Collections.sort(list, new Comparator<String>(){
				public int compare(String o1, String o2) {
					if (o1 == null || o2 == null) 
						return 0;
					return o1.compareTo(o2);
				}
			});
			
			StringBuilder sb = new StringBuilder();
			for (int i = 0, len = list.size(); i < len; i++) {
				sb.append(list.get(i));
			}
			
//			System.err.println(sb);
			
			return StringUtil.md5(sb.toString());
		}
		catch (Exception ex)
		{
			return null;
		}
	}
	
	public void load() {
		
		requestAndResponseScriptMap = new HashMap<String, String>();
		String basepath = MockTaskServer.class.getResource(".").getPath() + "scripts/";
		seekScriptFiles(basepath);
		
	}
	
	private void seekScriptFiles(String basepath) {
		
		File dir = new File(basepath);
		File[] fileArray = dir.listFiles();	
		if (fileArray == null) {
			return;
		}
		
		for (int i = 0, len = fileArray.length; i < len; i++) {
			File reqFile = fileArray[i];
			if (reqFile.isDirectory()) {
				seekScriptFiles(basepath+reqFile.getName()+"/");
			} else if (reqFile.isFile() && reqFile.getName().endsWith(".req.txt")) {
				//System.out.println("REQ="+reqFile.getName());
				File resFile = new File(reqFile.getAbsolutePath().replaceAll("\\.req\\.txt", ".res.txt"));
				//System.out.println("RES="+resFile.getName());
				String hash = getHashedFileText(reqFile);
				String resData = getFileText(resFile);
				//System.out.println("loaded command hash="+hash);
				//System.out.println("req="+reqData);
				//System.out.println("res="+resData);
				requestAndResponseScriptMap.put(hash, resData);
				System.out.println("> Loaded script hash="+hash+" file="+reqFile.getPath()+"");
			}
		}
		
	}
	
}
