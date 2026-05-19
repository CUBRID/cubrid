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
#include <filesystem>
#include <algorithm>
#include <unordered_set>
#include <set>

#include "highfive/highfive.hpp"

#include "error_manager.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"
/*
 * load_open_data.cpp: load routines to read open data and write into loaddb format
 */

namespace cubload
{
  static std::unordered_set <std::string> desired_names = {"train", "test", "neighbors", "distances"};

  int read_hdf5_file (const std::string &file_name)
  {
    using namespace HighFive;

    int class_idx = 0;
    std::string create_table_header = "CREATE TABLE ";

    H5Eset_auto2 (H5E_DEFAULT, NULL, NULL);
    std::unique_ptr<File> file_ptr;
    try
      {
	file_ptr = std::make_unique <File> (file_name, File::ReadOnly);
      }
    catch (const HighFive::FileException &e)
      {
	fprintf (stdout, "%s\n", e.what());
	return ER_FAILED;
      }

    try
      {
	auto object_names = file_ptr->listObjectNames();
	if (object_names.empty())
	  {
	    fprintf (stdout, "no datasets");
	    return ER_FAILED;
	  }

	std::set <std::string> dataset_names;
	for (const auto &name : object_names)
	  {
	    DataSet dataset = file_ptr->getDataSet (name);
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

	// Use only the input file stem for class/table names; keep generated files next to the input.
	std::filesystem::path input_path (file_name);
	std::string base_name = input_path.stem ().string ();
	std::replace (base_name.begin(), base_name.end(), '-', '_');

	std::filesystem::path schema_file_path = input_path.parent_path () / (base_name + "_schema");
	std::string schema_file_name = schema_file_path.string ();
	fprintf (stdout, "schema file name: %s\n", schema_file_name.c_str());

	// open schema file
	std::ofstream schema_file (schema_file_name);

	// create a new object file with the same name as the hdf5 file without the extension
	std::filesystem::path object_file_path = input_path.parent_path () / (base_name + "_object");
	std::string object_file_name = object_file_path.string ();
	fprintf (stdout, "object file name: %s\n", object_file_name.c_str());

	// open object file
	std::ofstream object_file (object_file_name);

	// write DDL to schema_file
	object_file << "\%id " << base_name << "_train " << class_idx++ << std::endl;
	object_file << "\%id " << base_name << "_test " << class_idx++ << std::endl;
	object_file << "\%id " << base_name << "_answer " << class_idx++ << std::endl;

	// read train data
	DataSet dataset = file_ptr->getDataSet ("train");
	DataSpace dataspace = dataset.getSpace();
	std::vector<size_t> dims = dataspace.getDimensions();
	std::string data_type = dataset.getDataType().string();

	fprintf (stdout, "train data format: %s\n", data_type.c_str());

	std::string table_train = create_table_header + base_name + "_train (id BIGINT PRIMARY KEY, vec VECTOR (" +
				  std::to_string (dims[1]) + "));";
	schema_file << table_train << std::endl;

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
	train_data_vector.clear ();

	// read test data
	dataset = file_ptr->getDataSet ("test");
	dataspace = dataset.getSpace();
	dims = dataspace.getDimensions();
	data_type = dataset.getDataType().string();

	fprintf (stdout, "test data format: %s\n", data_type.c_str());

	std::string table_test = create_table_header + base_name + "_test (id BIGINT PRIMARY KEY, vec VECTOR (" +
				 std::to_string (dims[1]) + "));";
	schema_file << table_test << std::endl;

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
	test_data_vector.clear ();

	std::string table_answer = create_table_header + base_name +
				   "_answer (id BIGINT, neighbor_id BIGINT, neighbor_distance DOUBLE);";
	schema_file << table_answer << std::endl;

	// read answer data
	DataSet neighbor_dataset = file_ptr->getDataSet ("neighbors");
	DataSpace neighbor_dataspace = neighbor_dataset.getSpace();
	std::vector<size_t> neighbor_dims = dataspace.getDimensions();
	std::string neighbor_data_type = neighbor_dataset.getDataType().string();

	DataSet distance_dataset = file_ptr->getDataSet ("distances");
	DataSpace distance_dataspace = distance_dataset.getSpace();
	std::vector<size_t> distance_dims = dataspace.getDimensions();
	std::string distance_data_type = distance_dataset.getDataType().string();

	if (neighbor_dims[0] != distance_dims[0] || neighbor_dims[1] != distance_dims[1])
	  {
	    fprintf (stdout, "neighbor and distance data do not match\n");
	    return ER_FAILED;
	  }

	std::vector<std::vector<uint64_t>> test_neighbor_vector;
	std::vector<std::vector<double>> test_distance_vector;

	neighbor_dataset.read (test_neighbor_vector);
	distance_dataset.read (test_distance_vector);

	object_file << "\%class " << base_name << "_answer ([id] [neighbor_id] [neighbor_distance])" << std::endl;
	for (size_t i = 0; i < test_neighbor_vector.size (); i++)
	  {
	    std::vector<uint64_t> &vec1 = test_neighbor_vector[i];
	    std::vector<double> &vec2 = test_distance_vector[i];

	    for (size_t j = 0; j < vec1.size (); j++)
	      {
		object_file << i << " " << vec1[j] << " " << vec2[j] << std::endl;
	      }
	  }
	test_neighbor_vector.clear ();
	test_distance_vector.clear ();

	object_file.close ();
	schema_file.close ();

		std::filesystem::path absolute_schema_file_path = std::filesystem::absolute (schema_file_name);
		std::filesystem::path absolute_object_file_path = std::filesystem::absolute (object_file_name);
		fprintf (stdout, "\n[completed]\nschema file: %s\nobject file: %s\n",
			 absolute_schema_file_path.string().c_str (),
			 absolute_object_file_path.string().c_str ());
      }
    catch (const HighFive::Exception &e)
      {
	fprintf (stdout, "%s\n", e.what());
	return ER_FAILED;
      }

    return NO_ERROR;
  }
}
