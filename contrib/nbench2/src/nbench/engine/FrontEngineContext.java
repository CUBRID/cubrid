package nbench.engine;

import java.io.File;
import java.lang.reflect.Constructor;
import java.util.Properties;
import java.util.List;
import java.util.LinkedList;
import java.util.HashMap;
import javax.script.Bindings;
import javax.script.Compilable;
import javax.script.ScriptEngine;
import javax.script.ScriptEngineManager;
import javax.tools.*;
import nbench.common.*;
import nbench.parse.Benchmark;
import nbench.parse.Mix;
import nbench.parse.Transaction;
import nbench.parse.BenchmarkXMLParser;

public class FrontEngineContext {

	private Properties props;
	private ResourceProviderIfs rp;
	private ClassLoader externalLoader;
	private Properties backendProperties;
	private Benchmark benchmark;
	private long baseTime;
	private BackendEngineIfs backendEngineIfs;
	private Constructor<?> frameConstructor;
	private Class<?> sampleClass;
	private ScriptEngineManager scriptEngineManager;
	// user host variable
	private UserHostVar userHostVar;
	private HashMap<Mix, UserHostVarIfs> mixUserHostVarIfsMap;

	public FrontEngineContext(Properties props, ResourceProviderIfs rp,
			ClassLoader extLoader) throws NBenchException {
		this.props = props;
		this.rp = rp;
		this.externalLoader = extLoader;
	}

	public String getProperty(String key) {
		return props.getProperty(key);
	}

	public UserHostVar getUserHostVar() {
		return userHostVar;
	}

	public void initialize() throws NBenchException {
		try {
			makeBackendProps();
			createBackendEngine();
			compileAndRegisterHostClass();
			prepareScriptEngineManager();
		} catch (NBenchException e) {
			throw e;
		} catch (Exception e) {
			throw new NBenchException(e);
		}
	}

	public ResourceIfs newBenchmarkClientPerfLogResource(BenchmarkClient client)
			throws Exception {
		return rp.newResource("res:perf_log" + File.separator
				+ client.getPerfLogName());
	}

	public ResourceIfs newBenchmarkClientErrorLogResource(BenchmarkClient client)
			throws Exception {
		return rp.newResource("res:err_log" + File.separator
				+ client.getErrorLogName());
	}

	public ResourceIfs newSetupClientLogResource(SetupClient cient)
			throws Exception {
		return rp.newResource("res:setup_log" + File.separator
				+ cient.getLogName());
	}

	private void makeBackendProps() throws Exception {
		backendProperties = new Properties();
		for (String s : props.stringPropertyNames()) {
			if (s.startsWith("backend.")) {
				String val = props.getProperty(s);
				backendProperties.put(s.substring("backend.".length()), val);
			}
		}
		baseTime = System.currentTimeMillis();
	}

	public long getBaseTime() {
		return baseTime;
	}

	public void setBaseTime(long base_time) {
		this.baseTime = base_time;
	}

	private RuntimeSource compileRuntimeSource() throws Exception {
		BenchmarkXMLParser parser = new BenchmarkXMLParser();
		benchmark = parser.parseBenchmarkXML(rp.getResource(props
				.getProperty("benchmark")));

		return RuntimeSourceFactory.compile("nbench.runtime", benchmark);
	}

	private void compileAndRegisterHostClass() throws Exception {
		RuntimeSource source = compileRuntimeSource();
		boolean compile_frame = source.frame_file != null && source.frame_file.length() > 0;
		boolean compile_sample = source.sample_file != null && source.sample_file.length() > 0;

		if (false) { // DEBUG
			if (compile_frame) {
				System.out.println(source.frame_file);
			}
			if (compile_sample) {
				System.out.println(source.sample_file);
			}
		}

		if (!compile_sample && !compile_frame) {
			return;
		}

		JavaCompiler compiler = ToolProvider.getSystemJavaCompiler();
		StandardJavaFileManager fileManager = compiler.getStandardFileManager(
				null, null, null);
		JavaClassLoaderImpl ldi = new JavaClassLoaderImpl(getClass()
				.getClassLoader());
		JavaFileManagerImpl fmi = new JavaFileManagerImpl(fileManager, ldi);

		LinkedList<JavaFileObject> file_list = new LinkedList<JavaFileObject>();

		if (compile_frame) {
			JavaFileObjectImpl fi = new JavaFileObjectImpl(
					"string:///nbench/runtime/FrameHostVar", source.frame_file);
			file_list.add(fi);
		}

		if (compile_sample) {
			JavaFileObjectImpl fi = new JavaFileObjectImpl(
					"string:///nbench/runtime/SampleHostVar",
					source.sample_file);
			file_list.add(fi);
		}

		JavaCompiler.CompilationTask task = compiler.getTask(null, fmi, null,
				null, null, file_list);

		if (task.call()) {
			try {
				if (compile_frame) {
					Class<?> CLS = fmi.getClassLoader(
							StandardLocation.CLASS_PATH).loadClass(
							"nbench.runtime.FrameHostVar");
					frameConstructor = CLS
							.getConstructor(BackendEngineClientIfs.class);
				}
				if (compile_sample) {
					sampleClass = fmi.getClassLoader(
							StandardLocation.CLASS_PATH).loadClass(
							"nbench.runtime.SampleHostVar");
				}
			} catch (ClassNotFoundException e) {
				throw new NBenchException(e);
			} catch (Exception e) {
				throw new NBenchException(e);
			}
		}
	}

	private void createBackendEngine() throws Exception {
		backendEngineIfs = (BackendEngineIfs) Class.forName(
				props.getProperty("backend_engine"), true, externalLoader)
				.newInstance();
		backendEngineIfs.configure(backendProperties, rp, externalLoader);
	}

	private void prepareScriptEngineManager() throws Exception {
		scriptEngineManager = new ScriptEngineManager();
		ScriptEngine se = scriptEngineManager.getEngineByName("JavaScript");
		if (se == null) {
			throw new NBenchException("JavaScript engine is not supported");
		}
		if (!(se instanceof Compilable)) {
			throw new NBenchException("JavaScript engine is not Compilable");
		}
	}

	private List<Transaction> pivotTransaction(Transaction[] trs, String[] names)
			throws NBenchException {
		LinkedList<Transaction> list = new LinkedList<Transaction>();
		for (int i = 0; i < names.length; i++) {
			Transaction tr = null;
			for (int j = 0; j < trs.length; j++) {
				if (names[i].equals(trs[j].name)) {
					tr = trs[j];
					break;
				}
			}
			if (tr == null) {
				throw new NBenchException("unresolved transaction '" + names[i]
						+ "' in mix");
			}
			list.add(tr);
		}
		return list;
	}

	public SetupClient[] createSetupClients() throws Exception {
		int nclient;
		int i;
		int pos;
		SetupClient[] clients;

		nclient = 0;
		for (i = 0; i < benchmark.mixes.length; i++) {
			Mix mix = benchmark.mixes[i];

			if (mix.setup != null) {
				nclient++;
			}
		}

		if (nclient == 0) {
			return null;
		}

		clients = new SetupClient[nclient];

		pos = 0;
		for (i = 0; i < benchmark.mixes.length; i++) {
			Mix mix = benchmark.mixes[i];

			if (mix.setup == null) {
				continue;
			}

			ScriptEngine se = scriptEngineManager.getEngineByName("JavaScript");
			Bindings binds = se.createBindings();
			ScriptRun run = new ScriptRun(mix.name, se,
					benchmark.transaction_common, mix.setup);

			SetupClient client = new SetupClient(this, run, binds);
			populateHostVariables(mix, client, binds);

			clients[pos++] = client;
		}

		return clients;
	}

	public BenchmarkClient[] createBenchmarkClients(
			BenchmarkProgressListenerIfs listener) throws Exception {
		int nclient = 0;
		int i, j;
		int pos;
		BenchmarkClient[] clients;

		for (i = 0; i < benchmark.mixes.length; i++) {
			Mix mix = benchmark.mixes[i];
			nclient += mix.nthread;
		}

		clients = new BenchmarkClient[nclient];
		pos = 0;
		for (i = 0; i < benchmark.mixes.length; i++) {
			Mix mix = benchmark.mixes[i];

			for (j = 0; j < mix.nthread; j++) {
				ScriptEngine se = scriptEngineManager
						.getEngineByName("JavaScript");

				Bindings binds = se.createBindings();

				List<Transaction> trs = pivotTransaction(
						benchmark.transactions, mix.trs);

				MixRunIfs run = MixRunFactory.createMixRun(se, mix.name,
						mix.type, mix.value, benchmark.transaction_common, trs,
						mix.think_times);

				BenchmarkClient client = new BenchmarkClient(run, this,
						listener, binds);
				clients[pos++] = client;

				populateHostVariables(mix, client.getBenchmarkPerfLogIfs(),
						binds);
			}
		}

		return clients;
	}

	private void populateHostVariables(Mix mix, PerfLogIfs client,
			Bindings binds) throws Exception {

		// Frame host variable
		BackendEngineClientIfs bec = backendEngineIfs.createClient(client);
		if (frameConstructor != null) {
			binds.put("Frame", frameConstructor.newInstance(bec));
		}

		// Sample host variable
		if (sampleClass != null) {
			binds.put("Sample", sampleClass.newInstance());
		}

		//  __bec__ host variable
		binds.put("__bec__", bec);
		
		// Control host variable
		binds.put("Control", bec.getControlObject());

		// User defined host variable
		// do lazy binding
		if(userHostVar != null) {
			UserHostVarIfs uhv = mixUserHostVarIfsMap.get(mix);
			if (uhv == null) {
				uhv = (UserHostVarIfs) Class.forName(userHostVar.class_name, true,
						externalLoader).newInstance();
				uhv.prepareSetup(userHostVar.map, rp);
				mixUserHostVarIfsMap.put(mix, uhv);
			}

			binds.put(userHostVar.name, uhv);
		}
	}

	public void setUserHostVariable(UserHostVar user_hostvar) {
		userHostVar = user_hostvar;
		mixUserHostVarIfsMap = new HashMap<Mix, UserHostVarIfs>();
	}
}
