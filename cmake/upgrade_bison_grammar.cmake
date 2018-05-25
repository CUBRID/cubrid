file(READ ${GRAMMAR_INPUT_FILE} yy )

# replace old bison directives with new ones

string(REPLACE "%{/*%CODE_REQUIRES_START%*/" "%code requires{" mod_yy "${yy}" )
string(REPLACE "%{/*%CODE_PROVIDES_START%*/" "%code provides{" mod_yy "${mod_yy}" )
string(REPLACE "/*%CODE_END%*/%}" "}" mod_yy "${mod_yy}" )
file(WRITE ${GRAMMAR_OUTPUT_FILE} "${mod_yy}")
