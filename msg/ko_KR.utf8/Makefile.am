
ko_KR.utf8dir = ${prefix}/msg/ko_KR.utf8
ko_KR.utf8_DATA = cubrid.msg cubrid.cat csql.msg csql.cat utils.msg utils.cat \
	esql.msg esql.cat syntax.txt

GENCAT = $(top_builddir)/util/gencat

SUFFIXES = .msg.cat

.msg.cat:
	$(GENCAT) $@ $<
