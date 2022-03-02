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

#ifndef _METHOD_STRUCT_CLIENT_INFO_HPP_
#define _METHOD_STRUCT_CLIENT_INFO_HPP_

#ident "$Id$"

#include <string>

#include "packer.hpp"
#include "packable_object.hpp"

namespace cubmethod
{
  struct db_client_info : public cubpacking::packable_object
  {
    std::string broker_name;
    std::string cas_name;
    std::string db_name;
    std::string db_user;
    std::string client_ip;

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;
  };

  struct db_parameter_info : public cubpacking::packable_object
  {
    db_client_info client_info;
    int tran_isolation; // DB_TRAN_ISOLATION in dbtran_def.h
    int wait_msec;

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;
  };
}

using METHOD_CLIENT_INFO = cubmethod::db_client_info;

#endif // _METHOD_STRUCT_CLIENT_INFO_HPP_
