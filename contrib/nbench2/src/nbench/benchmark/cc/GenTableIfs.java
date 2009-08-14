package nbench.benchmark.cc;

public interface GenTableIfs {
	void init(String prefix, int table_id) throws Exception;
	void emitArticle(Object[] objs) throws Exception;
	void emitComment(Object[] objs) throws Exception;
	void close() throws Exception;
}
