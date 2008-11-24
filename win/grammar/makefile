
PARSER_DIR=..\..\src\parser
EXECUTABLE_DIR=..\..\src\executables

ANTLR = ..\pccts\antlr.exe
DLG = ..\pccts\dlg.exe

SQL_GRAMMAR_OUT=csql_grammar.c csql_grammar_err.c csql_grammar.dlg csql_grammar_tokens.h
SQL_SCAN=csql_grammar_scan.c csql_grammar_mode.h
LD_GRAMMAR_OUT=loader_grammar.c loader_grammar_err.c loader_grammar.dlg loader_grammar_tokens.h
LD_SCAN=loader_grammar_scan.c loader_grammar_mode.h
ES_GRAMMAR_OUT=esql_grammar.c esql_grammar_err.c esql_grammar.dlg esql_grammar_tokens.h
ES_SCAN=esql_grammar_scan.c esql_grammar_mode.h

OUT_FILES= $(SQL_GRAMMAR_OUT) $(SQL_SCAN) $(LD_GRAMMAR_OUT) $(LD_SCAN) $(ES_GRAMMAR_OUT) $(ES_SCAN)

all: $(OUT_FILES)

$(SQL_GRAMMAR_OUT): $(PARSER_DIR)/csql_grammar.g
        $(ANTLR) -ga -k 2 -fe csql_grammar_err.c -fl csql_grammar.dlg -ft csql_grammar_tokens.h -fm csql_grammar_mode.h $(PARSER_DIR)/csql_grammar.g
$(SQL_SCAN): csql_grammar.dlg
        $(DLG) -C2 -ga -m csql_grammar_mode.h csql_grammar.dlg csql_grammar_scan.c

$(LD_GRAMMAR_OUT): $(EXECUTABLE_DIR)/loader_grammar.g
        $(ANTLR) -ga -k 1 -fe loader_grammar_err.c -fl loader_grammar.dlg -ft loader_grammar_tokens.h -fm loader_grammar_mode.h $(EXECUTABLE_DIR)/loader_grammar.g
$(LD_SCAN): loader_grammar.dlg
        $(DLG) -C2 -ga -m loader_grammar_mode.h loader_grammar.dlg loader_grammar_scan.c

$(ES_GRAMMAR_OUT): $(EXECUTABLE_DIR)/esql_grammar.g
        $(ANTLR) -ga -k 1 -fe esql_grammar_err.c -fl esql_grammar.dlg -ft esql_grammar_tokens.h -fm esql_grammar_mode.h $(EXECUTABLE_DIR)/esql_grammar.g
$(ES_SCAN): esql_grammar.dlg
        $(DLG) -C2 -ga -m esql_grammar_mode.h esql_grammar.dlg esql_grammar_scan.c

clean:
	del $(OUT_FILES)
