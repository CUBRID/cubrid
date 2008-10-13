
ko_KR.euckrdir = ${prefix}/msg/ko_KR.euckr
ko_KR.euckr_DATA = cubrid.msg cubrid.cat csql.msg csql.cat utils.msg utils.cat \
	esql.msg esql.cat syntax.txt

GENCAT = $(top_builddir)/util/gencat

SUFFIXES = .msg.cat

.msg.cat:
	$(GENCAT) $@ $<
