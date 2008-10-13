
PARSER_DIR=..\..\src\parser
EXECUTABLE_DIR=..\..\src\executables

ANTLR = ..\pccts\antlr.exe
DLG = ..\pccts\dlg.exe

SQL_GRAMMAR_OUT=sqlx_grammar.c sqerr.c sq.dlg sqtokens.h
SQL_SCAN=sqscan.c sqmode.h
LD_GRAMMAR_OUT=ld.c lderr.c ld.dlg ldtokens.h
LD_SCAN=ldscan.c ldmode.h
ES_GRAMMAR_OUT=es.c eserr.c es.dlg estokens.h
ES_SCAN=esscan.c esmode.h

OUT_FILES= $(SQL_GRAMMAR_OUT) $(SQL_SCAN) $(LD_GRAMMAR_OUT) $(LD_SCAN) $(ES_GRAMMAR_OUT) $(ES_SCAN)

all: $(OUT_FILES)

$(SQL_GRAMMAR_OUT): $(PARSER_DIR)/sqlx_grammar.g
        $(ANTLR) -ga -k 2 -fe sqerr.c -fl sq.dlg -ft sqtokens.h -fm sqmode.h $(PARSER_DIR)/sqlx_grammar.g
$(SQL_SCAN): sq.dlg
        $(DLG) -C2 -ga -m sqmode.h sq.dlg sqscan.c

$(LD_GRAMMAR_OUT): $(EXECUTABLE_DIR)/ld.g
        $(ANTLR) -ga -k 1 -fe lderr.c -fl ld.dlg -ft ldtokens.h -fm ldmode.h $(EXECUTABLE_DIR)/ld.g
$(LD_SCAN): ld.dlg
        $(DLG) -C2 -ga -m ldmode.h ld.dlg ldscan.c

$(ES_GRAMMAR_OUT): $(EXECUTABLE_DIR)/es.g
        $(ANTLR) -ga -k 1 -fe eserr.c -fl es.dlg -ft estokens.h -fm esmode.h $(EXECUTABLE_DIR)/es.g
$(ES_SCAN): es.dlg
        $(DLG) -C2 -ga -m esmode.h es.dlg esscan.c

clean:
	del $(OUT_FILES)
