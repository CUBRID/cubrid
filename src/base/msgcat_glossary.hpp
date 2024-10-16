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

//
// Message catalog set for glossary
//

#ifndef _MSGCAT_SET_GLOSSARY_HPP_
#define _MSGCAT_SET_GLOSSARY_HPP_

#include "message_catalog.h"

/*
 * Message id in the set MSGCAT_SET_GLOSSARY
 * in the message catalog MSGCAT_CATALOG_CUBRID (file cubrid.msg).
 */

#define MSGCAT_GLOSSARY_START                                1
#define MSGCAT_GLOSSARY_CLASS                                1
#define MSGCAT_GLOSSARY_TRIGGER                              2
#define MSGCAT_GLOSSARY_SERIAL                               3
#define MSGCAT_GLOSSARY_SERVER                               4
#define MSGCAT_GLOSSARY_SYNONYM                              5
#define MSGCAT_GLOSSARY_PROCEDURE                            6

#define MSGCAT_GET_GLOSSARY_MSG(id) \
  msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_GLOSSARY, id)

#endif // _MSGCAT_SET_GLOSSARY_HPP_
