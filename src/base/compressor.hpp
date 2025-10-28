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
 * compressor.hpp
 */

#ifndef _COMPRESSOR_HPP_
#define _COMPRESSOR_HPP_

#include "lz4.h"
#include "error_manager.h"

#include <memory>
#include <variant>

namespace cubcompress
{
  struct LZ4 {};
  /* add here */

  /* options */
  struct lz4_options
  {
    /* accel must be a value between 1 and LZ4_ACCELERATION_MAX (usually 65537).  */
    /* trade-off: accel ≈ 1 = best compression ratio.				  */
    /*		  larger accel = higher throughput.				  */
    int accel = 1;
  };
  /* add here */

  using options = std::variant<lz4_options /*, add here */>;

  /* ---------------------------------------------------------------------------- */
  /* LZ4									  */
  /* ---------------------------------------------------------------------------- */
  struct LZ4ContextDeleter
  {
    void operator () (LZ4_stream_t *ctx) const noexcept
    {
      if (ctx)
	{
	  (void) LZ4_freeStream (ctx);
	}
    }
  };

  inline LZ4_stream_t *lz4_stream ()
  {
    thread_local std::unique_ptr<LZ4_stream_t, LZ4ContextDeleter> context { nullptr };

    if (!context)
      {
	context.reset (LZ4_createStream ());
	if (!context)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 1, "failed to create LZ4 stream context");
	  }
      }
    return context.get ();
  }

  inline int lz4_bound (int size)
  {
    return LZ4_compressBound (size);
  }

  inline int lz4_compress (const char *src, char *dst, int src_size, int dst_capacity, int accel)
  {
    LZ4_stream_t *ctx;
    int compressed_size;

    ctx = lz4_stream ();
    if (!ctx)
      {
	/* failed to get lz4 stream (context) */
	return 0;
      }

    LZ4_resetStream_fast (ctx);

    compressed_size = LZ4_compress_fast_extState (ctx, src, dst, src_size, dst_capacity, accel);
    if (compressed_size <= 0)
      {
	/* failed to compress */
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 1, "failed to compress with LZ4_compress_fast_extState");
      }
    return compressed_size;
  }

  inline int lz4_decompress (const char *src, char *dst, int compressed_size, int dst_capacity)
  {
    int decompressed_size;

    decompressed_size = LZ4_decompress_safe (src, dst, compressed_size, dst_capacity);
    if (decompressed_size < 0)
      {
	/* failed to decompress */
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 1, "failed to decompress with LZ4_decompress_safe");
      }
    return decompressed_size;
  }

  /* ---------------------------------------------------------------------------- */
  /* interface									  */
  /* ---------------------------------------------------------------------------- */
  template <typename T>
  inline int bound (int size)
  {
    static_assert (std::is_same_v<T, LZ4>, "T must be LZ4");

    if constexpr (std::is_same<T, LZ4>::value)
      {
	return lz4_bound (size);
      }
    return -1;
  }

  template <typename T>
  inline int compress (const void *src, int src_size, void *dst, int dst_capacity, const options &option)
  {
    static_assert (std::is_same_v<T, LZ4>, "T must be LZ4");

    if constexpr (std::is_same<T, LZ4>::value)
      {
	if (auto *lz4_option = std::get_if<lz4_options> (&option))
	  {
	    return lz4_compress (static_cast<const char *> (src), static_cast<char *> (dst), src_size, dst_capacity,
				 lz4_option->accel);
	  }
      }
    return -1;
  }

  template <typename T>
  inline int compress (const void *src, int src_size, void *dst, int dst_capacity)
  {
    static_assert (std::is_same_v<T, LZ4>, "T must be LZ4");

    if constexpr (std::is_same<T, LZ4>::value)
      {
	lz4_options option;
	return compress<T> (src, src_size, dst, dst_capacity, option);
      }
    return -1;
  }

  template <typename T>
  inline int decompress (const void *src, int src_size, void *dst, int dst_capacity)
  {
    static_assert (std::is_same_v<T, LZ4>, "T must be LZ4");

    if constexpr (std::is_same<T, LZ4>::value)
      {
	return lz4_decompress (static_cast<const char *> (src), static_cast<char *> (dst), src_size, dst_capacity);
      }
    return -1;
  }
}

#endif
