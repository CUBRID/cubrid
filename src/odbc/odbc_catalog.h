/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

#ifndef	__ODBC_CATALOG_HEADER	/* to avoid multiple inclusion */
#define	__ODBC_CATALOG_HEADER

PUBLIC RETCODE odbc_tables (ODBC_STATEMENT * stmt,
			    char *catalog_name,
			    char *schema_name,
			    char *table_name, char *table_type);
PUBLIC RETCODE odbc_columns (ODBC_STATEMENT * stmt,
			     char *catalog_name,
			     char *schema_name,
			     char *table_name, char *column_name);
PUBLIC RETCODE odbc_statistics (ODBC_STATEMENT * stmt,
				char *catalog_name,
				char *schema_name,
				char *table_name,
				unsigned short unique,
				unsigned short reversed);
PUBLIC RETCODE odbc_special_columns (ODBC_STATEMENT * stmt,
				     short identifier_type,
				     char *catalog_name,
				     char *schema_name,
				     char *table_name,
				     short scope, short nullable);

PUBLIC RETCODE odbc_primary_keys (ODBC_STATEMENT * stmt,
				  char *catalog_name,
				  char *schema_name, char *table_name);
PUBLIC RETCODE odbc_foreign_keys (ODBC_STATEMENT * stmt,
				  char *pk_table_name, char *fk_table_name);
PUBLIC RETCODE odbc_table_privileges (ODBC_STATEMENT * stmt,
				      char *catalog_name,
				      char *schema_name, char *table_name);
PUBLIC RETCODE odbc_procedures (ODBC_STATEMENT * stmt,
				char *catalog_name,
				char *schema_name, char *proc_name);
PUBLIC RETCODE odbc_procedure_columns (ODBC_STATEMENT * stmt,
				       char *catalog_name,
				       char *schema_name,
				       char *proc_name, char *column_name);

PUBLIC RETCODE odbc_get_type_info (ODBC_STATEMENT * stmt, short data_type);
PUBLIC RETCODE odbc_get_catalog_data (ODBC_STATEMENT * stmt,
				      short col_index,
				      VALUE_CONTAINER * c_value);
PUBLIC void free_catalog_result (ST_LIST * result, RESULT_TYPE type);

#endif /* ! __ODBC_CATALOG_HEADER */
