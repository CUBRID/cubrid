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

#include "passive_tran_server.hpp"

#include "log_impl.h"
#include "system_parameter.h"

bool
passive_tran_server::get_remote_storage_config ()
{
  return false;
}

void
passive_tran_server::on_boot ()
{
  assert (is_passive_transaction_server ());
}

passive_tran_server::request_handlers_map_t
passive_tran_server::get_request_handlers ()
{
  using map_value_t = request_handlers_map_t::value_type;

  map_value_t boot_info_handler_value =
	  std::make_pair (page_to_tran_request::SEND_BOOT_INFO,
			  std::bind (&passive_tran_server::receive_boot_info, std::ref (*this), std::placeholders::_1));
  map_value_t log_page_handler_value =
	  std::make_pair (page_to_tran_request::SEND_LOG_PAGE,
			  std::bind (&passive_tran_server::receive_log_page, std::ref (*this), std::placeholders::_1));
  map_value_t data_page_handler_value =
	  std::make_pair (page_to_tran_request::SEND_DATA_PAGE,
			  std::bind (&passive_tran_server::receive_data_page, std::ref (*this), std::placeholders::_1));

  std::map<page_to_tran_request, std::function<void (cubpacking::unpacker &upk)>> handlers_map;

  handlers_map.insert ({ boot_info_handler_value, log_page_handler_value, data_page_handler_value });

  return handlers_map;
}
