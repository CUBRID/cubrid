/*
 *
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

#include "load_open_data.hpp"

#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <unordered_set>
#include <set>

#include "highfive/highfive.hpp"

#include "error_manager.h"

/*
 * load_open_data.cpp: load routines to read open data and write into loaddb format
 */

namespace cubload
{
  static std::unordered_set <std::string> desired_names = {"train", "test", "neighbors", "distance"};

  int read_hdf5_file (const std::string &file_name)
  {
    using namespace HighFive;

    File file (file_name, File::ReadOnly);

    auto object_names = file.listObjectNames();
    if (object_names.empty())
      {
	fprintf (stdout, "no datasets");
	return ER_FAILED;
      }

    std::set <std::string> dataset_names;
    for (const auto &name : object_names)
      {
	DataSet dataset = file.getDataSet (name);
	DataSpace dataspace = dataset.getSpace();
	std::vector<size_t> dims = dataspace.getDimensions();
	fprintf (stdout, "dataset %s: %lu x %lu\n", name.c_str(), dims[0], dims[1]);

	dataset_names.insert (name);
      }

    // if dataset_names does not contain "train", "test", "neighbors" and "distances", return error
    for (const auto &name : desired_names)
      {
	if (dataset_names.find (name) == dataset_names.end ())
	  {
	    fprintf (stdout, "dataset %s not found\n", name.c_str());
	    fprintf (stdout, "invalid hdf5 format\n");
	    return ER_FAILED;
	  }
      }

    // create a new schema file with the same name as the hdf5 file without the extension
    std::string base_name = file_name.substr (0, file_name.find_last_of ('.'));
    std::replace (base_name.begin(), base_name.end(), '-', '_');

    std::string schema_file_name = base_name + "_schema";
    fprintf (stdout, "schema file name: %s\n", schema_file_name.c_str());

    // open schema file
    std::ofstream schema_file (schema_file_name);

    // create table SQL
    std::string table_ddl = "CREATE TABLE ";

    std::string table_train = table_ddl + base_name + "_train (id INT NOT NULL PRIMARY KEY, vec STRING);";
    std::string table_test = table_ddl + base_name + "_test (id INT NOT NULL PRIMARY KEY, vec STRING);";

    // write DDL to schema_file
    schema_file << table_train << std::endl;
    schema_file << table_test << std::endl;

    schema_file.close ();

    // create a new object file with the same name as the hdf5 file without the extension
    std::string object_file_name = base_name + "_object";
    fprintf (stdout, "object file name: %s\n", object_file_name.c_str());

    // open object file
    std::ofstream object_file (object_file_name);

    // write DDL to schema_file
    object_file << "\%id " << base_name << "_train " << 60 << std::endl;
    object_file << "\%id " << base_name << "_test " << 61 << std::endl;

    // read train data
    DataSet dataset = file.getDataSet ("train");
    DataSpace dataspace = dataset.getSpace();
    std::vector<size_t> dims = dataspace.getDimensions();
    std::string data_type = dataset.getDataType().string();

    fprintf (stdout, "train data format: %s\n", data_type.c_str());

    std::vector<std::vector<float>> train_data_vector;
    dataset.read (train_data_vector);

    object_file << "\%class " << base_name << "_train ([id] [vec])" << std::endl;
    for (size_t i = 0; i < train_data_vector.size (); i++)
      {
	std::vector<float> &vec = train_data_vector[i];
	std::string vec_str = "'[";
	for (size_t j = 0; j < vec.size (); j++)
	  {
	    vec_str += std::to_string (vec[j]);
	    if (j < vec.size () - 1)
	      {
		vec_str += ",";
	      }
	  }
	vec_str += "]'";

	object_file << i << " " << vec_str << std::endl;
      }

    // read test data
    dataset = file.getDataSet ("test");
    dataspace = dataset.getSpace();
    dims = dataspace.getDimensions();
    data_type = dataset.getDataType().string();

    fprintf (stdout, "test data format: %s\n", data_type.c_str());

    std::vector<std::vector<float>> test_data_vector;
    dataset.read (test_data_vector);

    object_file << "\%class " << base_name << "_test ([id] [vec])" << std::endl;
    for (size_t i = 0; i < test_data_vector.size (); i++)
      {
	std::vector<float> &vec = test_data_vector[i];
	std::string vec_str = "'[";
	for (size_t j = 0; j < vec.size (); j++)
	  {
	    vec_str += std::to_string (vec[j]);
	    if (j < vec.size () - 1)
	      {
		vec_str += ",";
	      }
	  }
	vec_str += "]'";

	object_file << i << " " << vec_str << std::endl;
      }

    object_file.close ();

    return NO_ERROR;
  }
}
