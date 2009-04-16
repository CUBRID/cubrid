/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * log_applier.h - DECLARATIONS FOR LOG APPLIER (AT CLIENT & SERVER)
 */

#ifndef _LOG_APPLIER_HEADER_
#define _LOG_APPLIER_HEADER_

#ident "$Id$"

#if defined (CS_MODE)
void la_test_log_page (const char *database_name, const char *log_path,
		       int page_num);
int la_apply_log_file (const char *database_name, const char *log_path);
#endif /* CS_MODE */

#endif /* _LOG_APPLIER_HEADER_ */
