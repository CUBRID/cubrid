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

static fs::path Root;
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
    {0, 0, 0, 0}
  };

  while (1)
    {
      int option_index = 0;
      int option_key = getopt_long (argc, argv, "yp:jh", loadjava_option, &option_index);
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

	  // e.g. $CUBRID/demodb/java/org/cubrid/path/
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
	case 'h':
	  [[fallthrough]];
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

exit:
  if (error == NO_ERROR)
    {
      Program_name = argv[0];
      // e.g. $CUBRID/demodb/java or e.g. $CUBRID/demodb/java_static
      if (Path.empty())
	{
	  Path = DYNAMIC_PATH;
	}
    }
  else
    {
      usage ();
    }

  return error;
}

static int
check_arguments ()
{
  DB_INFO *db = NULL;

  // check whether database exists
  if ((db = cfg_find_db (Dbname)) == NULL)
    {
      fprintf (stderr, "database '%s' does not exist.\n", Dbname);
      return ER_FAILED;
    }

  // DB path e.g. $CUBRID/demodb
  Root.assign (std::string (db->pathname));

  // check the specified source path of the java class file (jar) exists
  try
    {
      fs::path src_path (Src_class);
      if (!fs::exists (src_path))
	{
	  fprintf (stderr, "loadjava fail: '%s' does not exist.\n", src_path.generic_string().c_str ());
	  return ER_FAILED;
	}

      std::string ext_nm = src_path.extension().generic_string();
      if (ext_nm.empty() || ((ext_nm.compare (".class") != 0) && (ext_nm.compare (".jar") != 0)))
	{
	  fprintf (stderr, "loadjava fail: The extension name of '%s' is invalid.\n", src_path.generic_string().c_str ());
	  return ER_FAILED;
	}
    }
  catch (fs::filesystem_error &e)
    {
      fprintf (stderr, "loadjava fail: file operation error: %s\n", e.what ());
      return ER_FAILED;
    }

  return NO_ERROR;
}

static int
create_package_directories (const fs::path &dir_path)
{
  try
    {
      if (fs::exists (dir_path) == false)
	{
	  fs::create_directories (dir_path);
	  fs::permissions (dir_path,
			   fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read,
			   fs::perm_options::add);	// mkdir (java_dir_path, 0744)
	}
    }
  catch (fs::filesystem_error &e)
    {
      fprintf (stderr, "can't create directory: %s. %s\n", dir_path.generic_string ().c_str (), e.what ());
      return ER_FAILED;
    }
  return NO_ERROR;
}

static int
check_overwrite (const std::string &package_path, const std::string &class_file_name)
{
  try
    {
      fs::path static_path = Root / STATIC_PATH / package_path / class_file_name;
      fs::path dynamic_path = Root / DYNAMIC_PATH / package_path / class_file_name;

      bool exists_static = fs::exists (static_path);
      bool exists_dynamic = fs::exists (dynamic_path);

      // check whether class name exists for either static path and dynamic path
      std::string full_class_name = package_path + class_file_name;
      if (exists_static || exists_dynamic)
	{
	  if (Force_overwrite == false)
	    {
	      std::string full_class_name = package_path.empty () ? class_file_name : (package_path + SEPERATOR + class_file_name);
	      fprintf (stdout, "'%s' is exist. overwrite? (y/n): ", full_class_name.c_str ());
	      char c = getchar ();
	      if (c != 'Y' && c != 'y')
		{
		  fprintf (stdout, "loadjava is canceled\n");
		  return ER_FAILED;
		}
	    }

	  // remove the previous file (to update modified time of the JAVA directory: CBRD-24695)
	  if (exists_static && fs::is_directory (static_path) == false)
	    {
	      fs::remove (static_path);
	    }

	  // remove the previous file (to update modified time of the JAVA directory: CBRD-24695)
	  if (exists_dynamic && fs::is_directory (dynamic_path) == false)
	    {
	      fs::remove (dynamic_path);
	    }
	}
    }
  catch (fs::filesystem_error &e)
    {
      fprintf (stderr, "loadjava fail: file operation error: %s\n", e.what ());
      return ER_FAILED;
    }

  return NO_ERROR;
}

static int
copy_class_file (const fs::path &src_path, const fs::path &dest_path)
{
  try
    {
      const auto copyOptions = fs::copy_options::overwrite_existing;
      fs::copy (src_path, dest_path, copyOptions);
    }
  catch (fs::filesystem_error &e)
    {
      fprintf (stderr, "loadjava fail: file operation error: %s\n", e.what ());
      return ER_FAILED;
    }

  return NO_ERROR;
}

static int
do_load_java ()
{
  fs::path src_path (Src_class);
  std::string class_file_name = src_path.filename().generic_string();

  if (check_overwrite (package_path, class_file_name) != NO_ERROR)
    {
      return ER_FAILED;
    }

  fs::path package_dir = Root / Path / package_path;
  if (create_package_directories (package_dir) != NO_ERROR)
    {
      return ER_FAILED;
    }

  fs::path dest_path = package_dir / class_file_name;
  if (copy_class_file (src_path, dest_path) != NO_ERROR)
    {
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

  /* initialize message catalog for argument parsing and usage() */
  if (utility_initialize () != NO_ERROR)
    {
      return EXIT_FAILURE;
    }

  if (parse_argument (argc, argv) != NO_ERROR)
    {
      goto error;
    }

  if (check_arguments () != NO_ERROR)
    {
      goto error;
    }

  if (do_load_java () != NO_ERROR)
    {
      goto error;
    }

  status = EXIT_SUCCESS;

error:
  msgcat_final ();

  return (status);
}
