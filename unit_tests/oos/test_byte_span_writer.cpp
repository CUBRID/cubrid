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
 * test_byte_span_writer.cpp - pure unit tests for cubbase::byte_span_writer.
 *
 * Pure logic: no server, no page buffer, no fixtures. Validates the
 * bounds-check invariant that the OOS read path relies on for corruption
 * defense.
 */

#include "gtest/gtest.h"
#include "byte_span_writer.hpp"

#include <cstddef>
#include <cstring>

namespace
{
  /* Fill buf with a sentinel byte we can later check for untouched-ness. */
  void
  fill_sentinel (char *buf, std::size_t n, unsigned char sentinel = 0x7f)
  {
    std::memset (buf, sentinel, n);
  }
}

TEST (ByteSpanWriter, EmptyState)
{
  char buf[16];
  fill_sentinel (buf, sizeof (buf));

  cubbase::byte_span_writer w (cubbase::span<char> (buf, sizeof (buf)));
  EXPECT_EQ (w.written (), 0u);
  EXPECT_EQ (w.capacity (), 16u);
  EXPECT_EQ (w.remaining (), 16u);
  EXPECT_FALSE (w.full ());
}

TEST (ByteSpanWriter, AppendWithinCapacityAdvancesCursorAndCopies)
{
  char buf[16];
  fill_sentinel (buf, sizeof (buf));

  cubbase::byte_span_writer w (cubbase::span<char> (buf, sizeof (buf)));

  ASSERT_TRUE (w.append ("hello", 5));
  EXPECT_EQ (w.written (), 5u);
  EXPECT_EQ (w.remaining (), 11u);
  EXPECT_EQ (std::memcmp (buf, "hello", 5), 0);
  /* bytes beyond the cursor untouched */
  EXPECT_EQ (static_cast<unsigned char> (buf[5]), 0x7fu);
}

TEST (ByteSpanWriter, AppendOverflowFailsAndDoesNotWrite)
{
  /* The headline safety property: when the requested append would overrun
   * capacity, the call must (a) return false, (b) leave the cursor unchanged,
   * (c) write zero bytes. This is the invariant that converts a corrupt OOS
   * payload_len from a buffer overrun into a clean error return. */
  char buf[8];
  fill_sentinel (buf, sizeof (buf));

  cubbase::byte_span_writer w (cubbase::span<char> (buf, sizeof (buf)));

  /* fill 5 of 8 */
  ASSERT_TRUE (w.append ("abcde", 5));
  ASSERT_EQ (w.written (), 5u);

  /* overflow attempt: want 4 more, only 3 left */
  EXPECT_FALSE (w.append ("WXYZ", 4));

  /* cursor unchanged */
  EXPECT_EQ (w.written (), 5u);
  EXPECT_EQ (w.remaining (), 3u);

  /* prior bytes intact */
  EXPECT_EQ (std::memcmp (buf, "abcde", 5), 0);
  /* bytes beyond cursor untouched (still sentinel) */
  EXPECT_EQ (static_cast<unsigned char> (buf[5]), 0x7fu);
  EXPECT_EQ (static_cast<unsigned char> (buf[6]), 0x7fu);
  EXPECT_EQ (static_cast<unsigned char> (buf[7]), 0x7fu);
}

TEST (ByteSpanWriter, RecoveryAfterFailedAppend)
{
  /* A failed overflow must not poison the writer: a subsequent fitting
   * append must still succeed. */
  char buf[8];
  fill_sentinel (buf, sizeof (buf));

  cubbase::byte_span_writer w (cubbase::span<char> (buf, sizeof (buf)));

  ASSERT_TRUE (w.append ("abcde", 5));
  EXPECT_FALSE (w.append ("WXYZ", 4));   /* overflow, no-op */

  /* still 3 bytes of room, a 2-byte fit must succeed */
  ASSERT_TRUE (w.append ("xy", 2));
  EXPECT_EQ (w.written (), 7u);

  /* exact-fit fills to capacity */
  ASSERT_TRUE (w.append ("Z", 1));
  EXPECT_EQ (w.written (), 8u);
  EXPECT_TRUE (w.full ());
  EXPECT_EQ (std::memcmp (buf, "abcdexyZ", 8), 0);
}

TEST (ByteSpanWriter, FullWriterRejectsNonZeroAppendButAcceptsZero)
{
  char buf[4];
  fill_sentinel (buf, sizeof (buf));

  cubbase::byte_span_writer w (cubbase::span<char> (buf, sizeof (buf)));
  ASSERT_TRUE (w.append ("1234", 4));
  ASSERT_TRUE (w.full ());

  /* zero-byte append at full is a legal no-op */
  EXPECT_TRUE (w.append ("", 0));
  EXPECT_TRUE (w.append (nullptr, 0));
  EXPECT_EQ (w.written (), 4u);

  /* any non-zero append at full must fail */
  EXPECT_FALSE (w.append ("!", 1));
  EXPECT_EQ (w.written (), 4u);
  EXPECT_EQ (std::memcmp (buf, "1234", 4), 0);
}

TEST (ByteSpanWriter, ZeroCapacitySpanIsImmediatelyFull)
{
  char sentinel = static_cast<char> (0x7f);
  cubbase::byte_span_writer w (cubbase::span<char> (&sentinel, 0));

  EXPECT_EQ (w.capacity (), 0u);
  EXPECT_EQ (w.remaining (), 0u);
  EXPECT_TRUE (w.full ());

  /* any non-zero append fails and leaves memory beyond the span untouched */
  EXPECT_FALSE (w.append ("X", 1));
  EXPECT_EQ (static_cast<unsigned char> (sentinel), 0x7fu);

  /* zero-byte append is still legal */
  EXPECT_TRUE (w.append ("", 0));
}

TEST (ByteSpanWriter, OverflowByExactlyOneByteIsCaught)
{
  /* Adversarial corruption pattern: the inline length and the chain
   * total_data_length agree, but a single chunk on disk claims one extra
   * byte. The bounds check must catch the off-by-one. */
  char buf[10];
  fill_sentinel (buf, sizeof (buf));

  cubbase::byte_span_writer w (cubbase::span<char> (buf, sizeof (buf)));
  ASSERT_TRUE (w.append ("123456789", 9));
  EXPECT_EQ (w.remaining (), 1u);

  /* claim 2 bytes for the final chunk — exactly one byte over */
  EXPECT_FALSE (w.append ("AB", 2));
  EXPECT_EQ (w.written (), 9u);
  EXPECT_EQ (static_cast<unsigned char> (buf[9]), 0x7fu);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  return RUN_ALL_TESTS ();
}
