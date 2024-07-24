/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * loadjava.cpp - loadjava utility
 */

#ident "$Id$"

#include "config.h"

#include <cassert>
#include <string>
#include <regex>
#include <filesystem>

#include "cubrid_getopt.h"
#include "error_code.h"
#include "message_catalog.h"
#include "utility.h"
#include "databases_file.h"
#if defined(WINDOWS)
#include "porting.h"
#endif /* WINDOWS */

namespace fs = std::filesystem;
using namespace std::regex_constants;

#define JAVA_DIR                "java"
#define JAVA_STATIC_DIR         "java_static"

#if defined(WINDOWS)
#define SEPERATOR               "\\"
#else /* ! WINDOWS */
#define SEPERATOR               "/"
#endif /* !WINDOWS */

static const std::string JAVA_PACKAGE_PATTERN = "^([a-z_]{1}[a-z0-9_]*(\\.[a-z_]{1}[a-z0-9_]*)*)$";
static const std::string SEPARATOR_STRING (SEPERATOR);

static const std::string DYNAMIC_PATH = JAVA_DIR;
static const std::string STATIC_PATH = JAVA_STATIC_DIR;

static std::string Path;
static char *Program_name = NULL;
static char *Dbname = NULL;
std::string Src_class;
std::regex Java_package_reg (JAVA_PACKAGE_PATTERN, ECMAScript | icase | optimize);

static int Force_overwrite = false;
static std::string package_path;

static void
usage (void)
{
  fprintf (stderr, "%s", msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADJAVA, LOADJAVA_MSG_USAGE));
}

static int
parse_argument (int argc, char *argv[])
{
  int error = NO_ERROR;
  struct option loadjava_option[] =
  {
    {"overwrite", 0, 0, 'y'},
    {"package", 1, 0, 'p'},
    {"jni", 0, 0, 'j'},
    {"user", 0, 0, 'u'},
    {0, 0, 0, 0}
  };

  while (1)
    {
      int option_index = 0;
      int option_key = getopt_long (argc, argv, "yp:u:jh", loadjava_option, &option_index);
      if (option_key == -1)
	{
	  break;
	}

      switch (option_key)
	{
	case 'y':
	  Force_overwrite = true;
	  break;
	case 'p':
	{
	  // check valid package name
	  if (optarg == NULL)
	    {
	      error = ER_FAILED;
	      goto exit;
	    }

	  std::string package_name (optarg);
	  if (!package_name.empty())
	    {
	      bool is_matched = std::regex_search (package_name, Java_package_reg);
	      if (!is_matched)
		{
		  fprintf (stderr, "invalid java package name\n");
		  return ER_FAILED;
		}
	      // replace all for package name's dot to SEPARATER
	      // e.g. org.cubrid.abc => org/cubrid/abc
	      package_path = std::regex_replace (package_name, std::regex ("\\."), SEPARATOR_STRING);
	    }
	}
	break;
	case 'j':
	  Path = STATIC_PATH;
	  break;
        case 'u':
          
          break;
	case 'h':
	/* fall through */
	default:
	  error = ER_FAILED;
	  goto exit;
	}
    }

  if (optind + 1 < argc)
    {
      Dbname = argv[optind];
      Src_class = argv[optind + 1];
    }
  else
    {
      error = ER_FAILED;
      goto exit;
    }

  Program_name = argv[0];

exit:
  if (error != NO_ERROR)
    {
      usage ();
    }

  return error;
}

static int
create_package_directories (const fs::path &java_dir_path)
{
  try
    {
      if (fs::exists (java_dir_path) == false)
	{
	  fs::create_directories (java_dir_path);
	  fs::permissions (java_dir_path,
			   fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read,
			   fs::perm_options::add);	// mkdir (java_dir_path, 0744)
	}
    }
  catch (fs::filesystem_error &e)
    {
      fprintf (stderr, "can't create directory: %s. %s\n", java_dir_path.generic_string ().c_str (), e.what ());
      return ER_FAILED;
    }
  return NO_ERROR;
}

static int
copy_file (const fs::path &java_dir_path)
{
  try
    {
      fs::path src_path = fs::path (Src_class);
      if (fs::exists (src_path) == false)
	{
	  return ER_FAILED;
	}

      std::string class_file_name = src_path.filename().generic_string();
      fs::path class_file_path = java_dir_path / class_file_name;

      bool is_exists = fs::exists (class_file_path);
      if (Force_overwrite == false && is_exists == true)
	{
	  fprintf (stdout, "'%s' is exist. overwrite? (y/n): ", class_file_path.c_str ());
	  char c = getchar ();
	  if (c != 'Y' && c != 'y')
	    {
	      fprintf (stdout, "loadjava is canceled\n");
	      return NO_ERROR;
	    }
	}

      // remove the previous file (to update modified time of the JAVA directory: CBRD-24695)
      if (is_exists && fs::is_directory (class_file_path) == false)
	{
	  fs::remove (class_file_path);
	}

      const auto copyOptions = fs::copy_options::overwrite_existing;
      fs::copy (src_path, class_file_path, copyOptions);
    }
  catch (fs::filesystem_error &e)
    {
      fprintf (stderr, "loadjava fail: file operation error: %s\n", e.what ());
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * main() - loadjava main function
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
main (int argc, char *argv[])
{
  int status = EXIT_FAILURE;
  DB_INFO *db = NULL;
  fs::path java_dir_path;

  /* initialize message catalog for argument parsing and usage() */
  if (utility_initialize () != NO_ERROR)
    {
      return EXIT_FAILURE;
    }

  if (parse_argument (argc, argv) != NO_ERROR)
    {
      goto error;
    }

  if ((db = cfg_find_db (Dbname)) == NULL)
    {
      fprintf (stderr, "database '%s' does not exist.\n", Dbname);
      goto error;
    }

  // DB path e.g. $CUBRID/demodb
  java_dir_path.assign (std::string (db->pathname));

  // e.g. $CUBRID/demodb/java or e.g. $CUBRID/demodb/java_static
  if (Path.empty())
    {
      Path = DYNAMIC_PATH;
    }
  java_dir_path.append (Path);

  // e.g. $CUBRID/demodb/java/org/cubrid/path/
  java_dir_path.append (package_path);

  if (create_package_directories (java_dir_path) != NO_ERROR)
    {
      goto error;
    }

  if (copy_file (java_dir_path) != NO_ERROR)
    {
      goto error;
    }

  status = EXIT_SUCCESS;

error:
  msgcat_final ();

  return (status);
}
