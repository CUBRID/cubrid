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

/*
 * copy_stream_kind.h - COPY's tag on the byte-stream transport.
 *
 * The transport declares only the bound of the tag space; the value is this
 * consumer's to name. It is shared because the two halves of the COPY binding
 * sit on opposite sides of the wire: the client packs the open request
 * (copy_from_init) and the server registers the factory that answers it
 * (copy_session). The value is reserved in the allocation list beside
 * STREAM_KIND_MAX in stream_session.hpp.
 */

#ifndef _COPY_STREAM_KIND_H_
#define _COPY_STREAM_KIND_H_

#define STREAM_KIND_COPY 0

#endif /* _COPY_STREAM_KIND_H_ */
