#pragma once

T_CCI_A_TYPE GetCASTypeA(DBTYPE type);
T_CCI_U_TYPE GetCASTypeU(DBTYPE type);
DBTYPE GetOledbTypeFromName(LPOLESTR wszName);

void show_error(char *msg, int code, T_CCI_ERROR *error);