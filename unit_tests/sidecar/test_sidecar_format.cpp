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
 * test_sidecar_format.cpp - the Sidecar codec and write policy (base/sidecar_format.h)
 *
 * Records are built here from the disk format of object_representation.h, not by the codec, so the expected bytes
 * come from the format: a hot record keeps every value slot it does not replace, and expands back to the image it
 * was built from.
 */

#include "catch2/catch.hpp"

#include "sidecar_format.h"

#include "mvcc.h"
#include "object_representation_sr.h"

#include <cstring>
#include <string>
#include <vector>

namespace
{
  const int REPID = 7;

  /* An object of the heap's disk format: header, offset table, fixed area, variable values */
  struct image
  {
    std::vector<char> bytes;
    int header_size;
    int n_variable;

    RECDES recdes ()
    {
      RECDES r;
      r.data = bytes.data ();
      r.area_size = r.length = (int) bytes.size ();
      r.type = REC_HOME;
      return r;
    }
  };

  int
  header_size_of (int mvcc_flags)
  {
    int size = OR_MVCC_REP_SIZE + OR_CHN_SIZE;
    size += (mvcc_flags & OR_MVCC_FLAG_VALID_INSID) ? OR_MVCCID_SIZE : 0;
    size += (mvcc_flags & OR_MVCC_FLAG_VALID_DELID) ? OR_MVCCID_SIZE : 0;
    size += (mvcc_flags & OR_MVCC_FLAG_VALID_PREV_VERSION) ? OR_MVCC_PREV_VERSION_LSA_SIZE : 0;
    return size;
  }

  void
  put_offset (char *p, int value, int offset_size)
  {
    if (offset_size == 1)
      {
	OR_PUT_BYTE (p, value);
      }
    else if (offset_size == 2)
      {
	OR_PUT_SHORT (p, value);
      }
    else
      {
	OR_PUT_INT (p, value);
      }
  }

  int
  get_offset (const char *p, int offset_size)
  {
    return offset_size == 1 ? OR_GET_BYTE (p) : (offset_size == 2 ? OR_GET_SHORT (p) : OR_GET_INT (p));
  }

  image
  make_image (int mvcc_flags, int offset_size, const std::string &fixed, const std::vector<std::string> &vars)
  {
    image im;
    const int n = (int) vars.size ();
    const int table = DB_ALIGN (offset_size * (n + 1), INT_ALIGNMENT);

    im.header_size = header_size_of (mvcc_flags);
    im.n_variable = n;
    im.bytes.assign (im.header_size + table, 0);

    int word = REPID | (mvcc_flags << OR_MVCC_FLAG_SHIFT_BITS);
    OR_SET_VAR_OFFSET_SIZE (word, offset_size);
    OR_PUT_INT (im.bytes.data (), word);
    OR_PUT_INT (im.bytes.data () + OR_CHN_OFFSET, 42);
    for (int i = OR_MVCC_INSERT_ID_OFFSET; i < im.header_size; i++)
      {
	im.bytes[i] = (char) (0xA0 + i);
      }

    im.bytes.insert (im.bytes.end (), fixed.begin (), fixed.end ());
    int position = table + (int) fixed.size ();
    for (int j = 0; j < n; j++)
      {
	put_offset (im.bytes.data () + im.header_size + j * offset_size, position, offset_size);
	im.bytes.insert (im.bytes.end (), vars[j].begin (), vars[j].end ());
	position += (int) vars[j].size ();
      }
    put_offset (im.bytes.data () + im.header_size + n * offset_size, position, offset_size);
    return im;
  }

  std::string
  value_of (size_t length, char seed)
  {
    std::string s (length, '\0');
    for (size_t i = 0; i < length; i++)
      {
	s[i] = (char) (seed + i * 7);
      }
    return s;
  }

  SIDECAR_REF
  ref_for (int location, int length)
  {
    SIDECAR_REF ref;
    ref.pageid = 1000 + location;
    ref.volid = 3;
    ref.slotid = (INT16) (20 + location);
    ref.length = length;
    ref.flags = location % 2 ? SIDECAR_REF_BIGONE : 0;
    ref.attrid_check = (UINT16) (0xFE00 + location);
    return ref;
  }

  struct hot_result
  {
    std::vector<char> bytes;
    RECDES recdes;
    int rc;
  };

  hot_result
  build (image &im, const std::vector<int> &locations)
  {
    RECDES src = im.recdes ();
    std::vector<SIDECAR_SLOT> slots;
    for (int location : locations)
      {
	slots.push_back ({location, ref_for (location, sidecar_var_slot_length (&src, location))});
      }

    hot_result h;
    const int length = sidecar_hot_length (&src, im.n_variable, (int) slots.size (), slots.data ());
    h.bytes.assign (length > 0 ? length : 16, (char) 0x5A);
    h.recdes.data = h.bytes.data ();
    h.recdes.area_size = (int) h.bytes.size ();
    h.recdes.length = 0;
    h.rc = sidecar_build_hot (&src, im.n_variable, (int) slots.size (), slots.data (), &h.recdes);
    return h;
  }

  /* the inline record a hot record expands to, with each ref's value taken from the image it was built from */
  std::vector<char>
  expand (const RECDES &hot, image &original)
  {
    RECDES src = original.recdes ();
    const int length = sidecar_expanded_length (&hot, original.n_variable);
    REQUIRE (length > 0);

    std::vector<char> out (length, (char) 0x77);
    RECDES out_recdes;
    out_recdes.data = out.data ();
    out_recdes.area_size = length;
    std::vector<int> value_offsets (original.n_variable);
    REQUIRE (sidecar_expand_frame (&hot, original.n_variable, &out_recdes, value_offsets.data ()) == NO_ERROR);
    REQUIRE (out_recdes.length == length);

    const int n_refs = sidecar_trailer_n_refs (&hot);
    for (int i = 0; i < n_refs; i++)
      {
	const int location = sidecar_trailer_location (&hot, i);
	const char *table = src.data + original.header_size;
	const int offset_size = OR_GET_OFFSET_SIZE (src.data);
	const int start = get_offset (table + location * offset_size, offset_size);
	const int slot_length = sidecar_var_slot_length (&src, location);
	memcpy (out.data () + value_offsets[i], table + start, slot_length);
      }
    return out;
  }

  SIDECAR_POLICY
  default_policy ()
  {
    return SIDECAR_POLICY { 4060, 256, false, 16204 };
  }

  SIDECAR_IMAGE_FACTS
  facts_for (int length)
  {
    SIDECAR_IMAGE_FACTS f;
    f.length = length;
    f.header_size = 8;
    f.mvcc_user_class = true;
    f.supplemental_log = false;
    f.ha_enabled = false;
    f.has_delid = false;
    f.page_held = false;
    return f;
  }

  std::vector<int>
  choose (const SIDECAR_POLICY &policy, const SIDECAR_IMAGE_FACTS &facts, const std::vector<SIDECAR_CANDIDATE> &c)
  {
    std::vector<int> chosen (c.size () + 1, -1);
    const int n = sidecar_policy_choose (&policy, &facts, c.data (), (int) c.size (), chosen.data ());
    chosen.resize (n);
    return chosen;
  }
}

TEST_CASE ("a ref is 16 big-endian bytes in the documented order", "[sidecar]")
{
  SIDECAR_REF ref = { 0x01020304, 0x0506, 0x0708, 0x090A0B0C, 0x0D0E, 0x0F10 };
  char slot[SIDECAR_REF_SIZE];

  sidecar_ref_encode (&ref, slot);
  for (int i = 0; i < SIDECAR_REF_SIZE; i++)
    {
      CHECK ((unsigned char) slot[i] == i + 1);
    }

  SIDECAR_REF back;
  sidecar_ref_decode (slot, &back);
  CHECK (back.pageid == ref.pageid);
  CHECK (back.volid == ref.volid);
  CHECK (back.slotid == ref.slotid);
  CHECK (back.length == ref.length);
  CHECK (back.flags == ref.flags);
  CHECK (back.attrid_check == ref.attrid_check);
}

TEST_CASE ("the trailer is 8 bytes for one or two refs and 12 for three or four", "[sidecar]")
{
  CHECK (SIDECAR_TRAILER_SIZE (1) == 8);
  CHECK (SIDECAR_TRAILER_SIZE (2) == 8);
  CHECK (SIDECAR_TRAILER_SIZE (3) == 12);
  CHECK (SIDECAR_TRAILER_SIZE (4) == 12);
}

TEST_CASE ("a hot record replaces the chosen slots with refs and keeps every other byte", "[sidecar]")
{
  const std::string fixed = "FIXED-AREA-0123";
  const std::vector<std::string> vars = { value_of (300, 1), "", value_of (5000, 2), value_of (40, 3) };
  image im = make_image (OR_MVCC_FLAG_VALID_INSID, 2, fixed, vars);
  hot_result h = build (im, { 0, 2 });

  REQUIRE (h.rc == NO_ERROR);
  const RECDES &hot = h.recdes;
  CHECK (hot.length == (int) im.bytes.size () - 300 - 5000 + 2 * SIDECAR_REF_SIZE + SIDECAR_TRAILER_SIZE (2));
  CHECK (sidecar_record_has_refs (&hot));
  CHECK ((OR_GET_MVCC_FLAG (hot.data) & ~OR_MVCC_FLAG_HAS_SIDECAR) == OR_MVCC_FLAG_VALID_INSID);
  CHECK (OR_GET_MVCC_REPID (hot.data) == REPID);
  CHECK (sidecar_hot_is_well_formed (&hot, im.n_variable));

  REQUIRE (sidecar_trailer_n_refs (&hot) == 2);
  CHECK (sidecar_trailer_location (&hot, 0) == 0);
  CHECK (sidecar_trailer_location (&hot, 1) == 2);
  CHECK (sidecar_trailer_find (&hot, 0) == 0);
  CHECK (sidecar_trailer_find (&hot, 1) == -1);
  CHECK (sidecar_trailer_find (&hot, 2) == 1);
  CHECK (sidecar_trailer_find (&hot, 3) == -1);
  const char *end = hot.data + hot.length;
  CHECK ((unsigned short) OR_GET_SHORT (end - 2) == SIDECAR_TRAILER_MAGIC);
  CHECK ((unsigned short) OR_GET_SHORT (end - 4) == 2);

  SIDECAR_REF ref;
  sidecar_hot_get_ref (&hot, 1, &ref);
  CHECK (ref.pageid == 1002);
  CHECK (ref.slotid == 22);
  CHECK (ref.length == 5000);
  CHECK (ref.attrid_check == 0xFE02);

  CHECK (memcmp (hot.data + im.header_size + 12, fixed.data (), fixed.size ()) == 0);
  CHECK (sidecar_var_slot_length (&hot, 0) == SIDECAR_REF_SIZE);
  CHECK (sidecar_var_slot_length (&hot, 1) == 0);
  CHECK (sidecar_var_slot_length (&hot, 2) == SIDECAR_REF_SIZE);
  CHECK (sidecar_var_slot_length (&hot, 3) == 40);
}

TEST_CASE ("a hot record expands back to the image it was built from", "[sidecar]")
{
  const int insid = OR_MVCC_FLAG_VALID_INSID;
  const int delid = OR_MVCC_FLAG_VALID_DELID;
  const int prev = OR_MVCC_FLAG_VALID_PREV_VERSION;
  const int flag_sets[] = { 0, insid, insid | prev, insid | delid | prev };
  const int offset_sizes[] = { 1, 2, 4 };

  for (int offset_size : offset_sizes)
    {
      for (int flags : flag_sets)
	{
	  const size_t big = offset_size == 1 ? 40 : 3000;
	  const std::vector<std::string> vars = { value_of (big, 11), "", value_of (17, 12), value_of (big / 2, 13), "",
						  ""
						};
	  const std::vector<std::vector<int>> choices = { { 0 }, { 3 }, { 0, 3 }, { 0, 2, 3 }, { 2 } };
	  for (const std::vector<int> &locations : choices)
	    {
	      image im = make_image (flags, offset_size, "fx", vars);
	      hot_result h = build (im, locations);
	      INFO ("offset size " << offset_size << ", flags " << flags << ", first location " << locations[0]);
	      REQUIRE (h.rc == NO_ERROR);
	      REQUIRE (sidecar_hot_is_well_formed (&h.recdes, im.n_variable));
	      CHECK (sidecar_expanded_length (&h.recdes, im.n_variable) == (int) im.bytes.size ());
	      CHECK (expand (h.recdes, im) == im.bytes);
	    }
	}
    }
}

TEST_CASE ("a ref in the last variable slot is followed directly by the trailer", "[sidecar]")
{
  image im = make_image (0, 2, "", { value_of (10, 1), value_of (500, 2) });
  hot_result h = build (im, { 1 });

  REQUIRE (h.rc == NO_ERROR);
  /* a table of three 2-byte offsets, aligned to 8 bytes */
  CHECK (h.recdes.length == im.header_size + 8 + 10 + SIDECAR_REF_SIZE + SIDECAR_TRAILER_SIZE (1));
  CHECK (expand (h.recdes, im) == im.bytes);
}

TEST_CASE ("the codec rejects what it cannot build or expand", "[sidecar]")
{
  image im = make_image (0, 2, "f", { value_of (300, 1), value_of (300, 2), value_of (300, 3) });
  RECDES src = im.recdes ();
  SIDECAR_SLOT unordered[] = { { 2, ref_for (2, 300) }, { 0, ref_for (0, 300) } };
  SIDECAR_SLOT outside[] = { { 3, ref_for (3, 300) } };

  CHECK (sidecar_hot_length (&src, im.n_variable, 2, unordered) == SIDECAR_FORMAT_ERROR);
  CHECK (sidecar_hot_length (&src, im.n_variable, 1, outside) == SIDECAR_FORMAT_ERROR);
  CHECK (sidecar_hot_length (&src, im.n_variable, 0, outside) == SIDECAR_FORMAT_ERROR);

  hot_result h = build (im, { 1 });
  REQUIRE (h.rc == NO_ERROR);

  SECTION ("an image that already has refs")
  {
    SIDECAR_SLOT slot[] = { { 0, ref_for (0, 300) } };
    CHECK (sidecar_hot_length (&h.recdes, im.n_variable, 1, slot) == SIDECAR_FORMAT_ERROR);
  }
  SECTION ("an area too small")
  {
    std::vector<char> small (h.recdes.length - 1);
    RECDES out;
    out.data = small.data ();
    out.area_size = (int) small.size ();
    SIDECAR_SLOT slot[] = { { 1, ref_for (1, 300) } };
    CHECK (sidecar_build_hot (&src, im.n_variable, 1, slot, &out) == SIDECAR_FORMAT_ERROR);
  }
  SECTION ("a wrong magic")
  {
    h.bytes[h.recdes.length - 1] ^= 1;
    CHECK (sidecar_trailer_n_refs (&h.recdes) == SIDECAR_FORMAT_ERROR);
    CHECK (!sidecar_hot_is_well_formed (&h.recdes, im.n_variable));
    CHECK (sidecar_expanded_length (&h.recdes, im.n_variable) == SIDECAR_FORMAT_ERROR);
  }
  SECTION ("zero refs")
  {
    OR_PUT_SHORT (h.recdes.data + h.recdes.length - 4, 0);
    CHECK (sidecar_trailer_n_refs (&h.recdes) == SIDECAR_FORMAT_ERROR);
  }
  SECTION ("a location outside the representation")
  {
    CHECK (!sidecar_hot_is_well_formed (&h.recdes, 1));
  }
  SECTION ("a padding entry that is not the pad")
  {
    OR_PUT_SHORT (h.recdes.data + h.recdes.length - 6, 0);
    CHECK (!sidecar_hot_is_well_formed (&h.recdes, im.n_variable));
  }
  SECTION ("a ref slot that is not 16 bytes")
  {
    /* move the slot boundary of the ref by one byte */
    char *entry = h.recdes.data + im.header_size + 2 * 2;
    OR_PUT_SHORT (entry, OR_GET_SHORT (entry) + 1);
    CHECK (!sidecar_hot_is_well_formed (&h.recdes, im.n_variable));
  }
  SECTION ("a trailer that does not follow the variable area")
  {
    h.bytes.insert (h.bytes.end () - SIDECAR_TRAILER_SIZE (1), 'x');
    h.recdes.data = h.bytes.data ();
    h.recdes.length++;
    CHECK (sidecar_trailer_n_refs (&h.recdes) == 1);
    CHECK (!sidecar_hot_is_well_formed (&h.recdes, im.n_variable));
  }
  SECTION ("a record without the flag has no trailer")
  {
    CHECK (sidecar_trailer_n_refs (&src) == 0);
    CHECK (sidecar_trailer_find (&src, 0) == -1);
    CHECK (!sidecar_hot_is_well_formed (&src, im.n_variable));
  }
}

TEST_CASE ("a store record carries its attribute and the value bytes as they were", "[sidecar]")
{
  const std::string value = value_of (1234, 9);
  std::vector<char> area (SIDECAR_STORE_RECORD_LENGTH (value.size ()) + OR_MVCCID_SIZE);
  RECDES rec;
  rec.data = area.data ();
  rec.area_size = (int) area.size ();

  sidecar_store_record_build (77, value.data (), (int) value.size (), &rec);
  CHECK (rec.length == 16 + 1234);
  CHECK (OR_GET_MVCC_REPID (rec.data) == SIDECAR_STORE_REPID);
  CHECK (OR_GET_MVCC_FLAG (rec.data) == 0);

  SIDECAR_VALUE_HEADER header;
  const char *bytes;
  int length;

  SECTION ("as the module builds it")
  {
    REQUIRE (sidecar_store_record_parse (&rec, &header, &bytes, &length) == NO_ERROR);
    CHECK (header.attrid == 77);
    CHECK (header.format == SIDECAR_VALUE_FORMAT);
    CHECK (length == 1234);
    CHECK (std::string (bytes, length) == value);
  }
  SECTION ("after the heap adds an INSID to its header")
  {
    area.insert (area.begin () + OR_MVCC_INSERT_ID_OFFSET, OR_MVCCID_SIZE, (char) 0x11);
    rec.data = area.data ();
    rec.length += OR_MVCCID_SIZE;
    OR_PUT_INT (rec.data, SIDECAR_STORE_REPID | (OR_MVCC_FLAG_VALID_INSID << OR_MVCC_FLAG_SHIFT_BITS));
    REQUIRE (sidecar_store_record_parse (&rec, &header, &bytes, &length) == NO_ERROR);
    CHECK (header.attrid == 77);
    CHECK (std::string (bytes, length) == value);
  }
  SECTION ("after a header rewrite through MVCC_REC_HEADER, as vacuum does")
  {
    MVCC_REC_HEADER mvcc_header;
    REQUIRE (or_mvcc_get_header (&rec, &mvcc_header) == NO_ERROR);
    REQUIRE (or_mvcc_set_header (&rec, &mvcc_header) == NO_ERROR);
    CHECK (OR_GET_MVCC_FLAG (rec.data) == 0);
    REQUIRE (sidecar_store_record_parse (&rec, &header, &bytes, &length) == NO_ERROR);
    CHECK (std::string (bytes, length) == value);
  }
  SECTION ("a record of a class is not a store record")
  {
    OR_PUT_INT (rec.data, REPID);
    CHECK (sidecar_store_record_parse (&rec, &header, &bytes, &length) == SIDECAR_FORMAT_ERROR);
  }
  SECTION ("a store record never carries the flag")
  {
    OR_PUT_INT (rec.data, SIDECAR_STORE_REPID | (OR_MVCC_FLAG_HAS_SIDECAR << OR_MVCC_FLAG_SHIFT_BITS));
    CHECK (sidecar_store_record_parse (&rec, &header, &bytes, &length) == SIDECAR_FORMAT_ERROR);
  }
}

TEST_CASE ("an image within the target passes through", "[sidecar][policy]")
{
  const SIDECAR_POLICY p = default_policy ();

  CHECK (choose (p, facts_for (4060), { { 0, 4000, true, false } }).empty ());
  CHECK (choose (p, facts_for (4061), { { 0, 4000, true, false } }) == std::vector<int> { 0 });
}

TEST_CASE ("the policy externalizes the largest eligible values until the row fits the target", "[sidecar][policy]")
{
  const SIDECAR_POLICY p = default_policy ();

  /* the 4 KB value of section 6: 80 + 4,104 B, and a hot record of 4,184 - 4,104 + 16 + 8 = 104 B */
  CHECK (choose (p, facts_for (4184), { { 0, 4104, true, false } }) == std::vector<int> { 0 });

  /* 5,100 - 2,984 + 8 = 2,124 B after the 3,000 B value: the 2,000 B value stays */
  const std::vector<SIDECAR_CANDIDATE> two = { { 0, 2000, true, false }, { 1, 3000, true, false } };
  CHECK (choose (p, facts_for (5100), two) == std::vector<int> { 1 });

  /* a tie goes to the first candidate */
  const std::vector<SIDECAR_CANDIDATE> tie = { { 0, 2500, true, false }, { 1, 2500, true, false } };
  CHECK (choose (p, facts_for (5100), tie) == std::vector<int> { 0 });

  /* 12,100 - 4,984 + 8 = 7,124 B is still over 4,060; then 7,124 - 3,984 = 3,140 B; the locations come back
   * ascending */
  const std::vector<SIDECAR_CANDIDATE> three = { { 5, 3000, true, false }, { 2, 5000, true, false },
    { 9, 4000, true, false }
  };
  CHECK (choose (p, facts_for (12100), three) == (std::vector<int> { 2, 9 }));
}

TEST_CASE ("the policy skips ineligible values and values below the minimum", "[sidecar][policy]")
{
  const SIDECAR_POLICY p = default_policy ();

  CHECK (choose (p, facts_for (9000), { { 0, 8000, false, false } }).empty ());
  CHECK (choose (p, facts_for (9000), { { 0, 255, true, false }, { 1, 255, true, false } }).empty ());
  const std::vector<SIDECAR_CANDIDATE> mixed = { { 0, 8000, false, false }, { 1, 256, true, false } };
  CHECK (choose (p, facts_for (9000), mixed) == std::vector<int> { 1 });
}

TEST_CASE ("nothing is externalized when the hot record would still pass the headroom", "[sidecar][policy]")
{
  const SIDECAR_POLICY p = default_policy ();

  /* one 1,000 B value: the hot body is length - 8 - 984 + 8 */
  CHECK (choose (p, facts_for (17188), { { 0, 1000, true, false } }) == std::vector<int> { 0 });
  CHECK (choose (p, facts_for (17189), { { 0, 1000, true, false } }).empty ());
}

TEST_CASE ("the eligibility gates keep the image inline", "[sidecar][policy]")
{
  const SIDECAR_POLICY p = default_policy ();
  const std::vector<SIDECAR_CANDIDATE> c = { { 0, 8000, true, false } };
  SIDECAR_IMAGE_FACTS f = facts_for (9000);

  REQUIRE (choose (p, f, c) == std::vector<int> { 0 });

  SECTION ("a system or MVCC-disabled class")
  {
    f.mvcc_user_class = false;
    CHECK (choose (p, f, c).empty ());
  }
  SECTION ("supplemental logging")
  {
    f.supplemental_log = true;
    CHECK (choose (p, f, c).empty ());
  }
  SECTION ("HA")
  {
    f.ha_enabled = true;
    CHECK (choose (p, f, c).empty ());
  }
  SECTION ("an image with a DELID")
  {
    f.has_delid = true;
    CHECK (choose (p, f, c).empty ());
  }
  SECTION ("a permanent page held by the thread")
  {
    f.page_held = true;
    CHECK (choose (p, f, c).empty ());
  }
}

TEST_CASE ("EXTERNAL and force_external take every eligible value of their minimum, within the target too",
	   "[sidecar][policy]")
{
  SIDECAR_POLICY p = default_policy ();

  const std::vector<SIDECAR_CANDIDATE> external = { { 0, 300, true, true }, { 1, 255, true, true } };
  CHECK (choose (p, facts_for (1000), external) == std::vector<int> { 0 });

  p.force_external = true;
  const std::vector<SIDECAR_CANDIDATE> small = { { 0, 64, true, false }, { 1, 63, true, false },
    { 2, 200, false, false }, { 3, 100, true, false }
  };
  CHECK (choose (p, facts_for (500), small) == (std::vector<int> { 0, 3 }));
}

TEST_CASE ("the policy's estimate is the length of the hot record the codec builds", "[sidecar][policy]")
{
  const SIDECAR_POLICY p = default_policy ();
  const std::vector<std::string> vars = { value_of (3000, 1), value_of (100, 2), value_of (2500, 3), "",
					  value_of (700, 4), value_of (1500, 5)
					};
  image im = make_image (0, 2, "0123456789", vars);
  RECDES src = im.recdes ();

  std::vector<SIDECAR_CANDIDATE> c;
  for (int location = 0; location < im.n_variable; location++)
    {
      const int length = sidecar_var_slot_length (&src, location);
      if (length > 0)
	{
	  c.push_back ({ location, length, true, false });
	}
    }
  SIDECAR_IMAGE_FACTS f = facts_for (src.length);
  f.header_size = im.header_size;

  const std::vector<int> chosen = choose (p, f, c);
  /* 7,834 - 2,984 + 8 = 4,858 B is over the target after the 3,000 B value, 2,374 B after the 2,500 B one */
  REQUIRE (src.length == 7834);
  REQUIRE (chosen == (std::vector<int> { 0, 2 }));

  hot_result h = build (im, chosen);
  REQUIRE (h.rc == NO_ERROR);
  CHECK (h.recdes.length == src.length - (3000 - 16) - (2500 - 16) + SIDECAR_TRAILER_SIZE (2));
  CHECK (h.recdes.length <= p.inline_target);
  CHECK (expand (h.recdes, im) == im.bytes);
}
