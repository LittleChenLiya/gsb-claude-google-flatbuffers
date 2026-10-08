/*
 * Copyright 2026 Google Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "reflection_compare_test.h"

#include <cmath>
#include <limits>

#include "flatbuffers/idl.h"
#include "flatbuffers/reflection.h"
#include "flatbuffers/reflection_generated.h"
#include "reflection_compare_test_generated.h"
#include "test_assert.h"

namespace flatbuffers {
namespace tests {

namespace {

using namespace cmp;

const char *kSchema =
    "namespace cmp;\n"
    "struct Point { x:float; y:float; }\n"
    "table VectorWrapper { values:[int]; }\n"
    "table Inner { n:int = 7; label:string; matrix:[VectorWrapper]; }\n"
    "union Any { Inner, S: string }\n"
    "table Root {\n"
    "  id:int = 42;\n"
    "  ratio:float = 1.5;\n"
    "  weird:float = nan;\n"
    "  name:string;\n"
    "  pos:Point;\n"
    "  tags:[string];\n"
    "  numbers:[long];\n"
    "  inners:[Inner];\n"
    "  any:Any;\n"
    "  anys:[Any];\n"
    "}\n"
    "root_type Root;\n";

// Produce a NaN float with a non-default NaN payload.
float NanfPayload() {
  uint32_t bits = 0x7fc12345;  // Quiet NaN with a payload.
  float f;
  memcpy(&f, &bits, sizeof(f));
  return f;
}

// Builds the shared logical object. When order_b is true, sub-objects are
// created in a different order (and unreferenced junk objects are inserted),
// which changes physical offsets/padding/vtable sharing but not the values.
// When force_defaults is true, default-valued scalars are written
// explicitly instead of being omitted.
flatbuffers::DetachedBuffer BuildLogicalObject(bool order_b,
                                               bool force_defaults,
                                               int matrix_elem = 5,
                                               bool altered_nan_payload =
                                                   false,
                                               bool mutate_nested_elem =
                                                   false,
                                               int id_value = 42) {
  flatbuffers::FlatBufferBuilder fbb(256);
  fbb.ForceDefaults(force_defaults);

  auto name = fbb.CreateString("hello");
  auto label = fbb.CreateString("inner-label");
  auto tag0 = fbb.CreateString("a");
  auto tag1 = fbb.CreateString("b");
  auto tag2 = fbb.CreateString("c");

  if (order_b) {
    // Allocate junk that shifts every subsequent offset but is never
    // referenced by the finished buffer.
    fbb.CreateString("unreferenced padding junk number one");
    fbb.CreateString("unreferenced padding junk number two");
    fbb.CreateVector(std::vector<int64_t>(3, 9));
  }

  // Inner.matrix = [[base, base+1], [base+2, matrix_elem+base]] (nested
  // vectors are expressed through wrapper tables, as the schema language
  // requires). mutate_nested_elem changes inners[0].matrix[1].values[1]
  // only (the base==0 inner, so it never affects the union members).
  auto build_inner = [&](int base, bool mutate = false) {
    std::vector<int32_t> row0;
    row0.push_back(base);
    row0.push_back(base + 1);
    std::vector<int32_t> row1;
    row1.push_back(base + 2);
    int32_t last = matrix_elem + base;
    if (mutate) last = 999;
    row1.push_back(last);
    auto r0 = fbb.CreateVector(row0);
    auto r1 = fbb.CreateVector(row1);
    std::vector<flatbuffers::Offset<VectorWrapper>> rows;
    rows.push_back(CreateVectorWrapper(fbb, r0));
    rows.push_back(CreateVectorWrapper(fbb, r1));
    auto matrix = fbb.CreateVector(rows);
    return CreateInner(fbb, /*n=*/7, label, matrix);
  };

  auto any_inner = build_inner(30);
  auto any_s = fbb.CreateString("union-string");
  std::vector<uint8_t> anys_type;
  anys_type.push_back(Any_Inner);
  anys_type.push_back(Any_S);
  std::vector<flatbuffers::Offset<void>> anys;
  anys.push_back(any_inner.Union());
  anys.push_back(any_s.Union());

  Point point(1.25f, -2.5f);
  std::vector<int64_t> numbers;
  numbers.push_back(10);
  numbers.push_back(-20);
  numbers.push_back(30);
  const float weird = altered_nan_payload
                          ? NanfPayload()
                          : std::numeric_limits<float>::quiet_NaN();

  flatbuffers::Offset<flatbuffers::Vector<uint8_t>> type_vec;
  flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<void>>>
      any_vec;
  flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<String>>> tags;
  flatbuffers::Offset<flatbuffers::Vector<int64_t>> nums;
  flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<Inner>>>
      inner_vec;

  if (order_b) {
    // Different sub-object construction order: tags before inners, and the
    // two Inner tables built in reverse order.
    tags = fbb.CreateVector(std::vector<flatbuffers::Offset<String>>{
        tag0, tag1, tag2});
    auto i1 = build_inner(3);
    auto i0 = build_inner(0, mutate_nested_elem);
    std::vector<flatbuffers::Offset<Inner>> inners{i0, i1};
    inner_vec = fbb.CreateVector(inners);
    nums = fbb.CreateVector(numbers);
    type_vec = fbb.CreateVector(anys_type);
    any_vec = fbb.CreateVector(anys);
  } else {
    auto i0 = build_inner(0, mutate_nested_elem);
    auto i1 = build_inner(3);
    std::vector<flatbuffers::Offset<Inner>> inners{i0, i1};
    inner_vec = fbb.CreateVector(inners);
    nums = fbb.CreateVector(numbers);
    type_vec = fbb.CreateVector(anys_type);
    any_vec = fbb.CreateVector(anys);
    tags = fbb.CreateVector(std::vector<flatbuffers::Offset<String>>{
        tag0, tag1, tag2});
  }

  RootBuilder rb(fbb);
  rb.add_id(id_value);
  rb.add_ratio(1.5f);
  rb.add_weird(weird);
  rb.add_name(name);
  rb.add_pos(&point);
  rb.add_tags(tags);
  rb.add_numbers(nums);
  rb.add_inners(inner_vec);
  rb.add_any_type(Any_Inner);
  rb.add_any(any_inner.Union());
  rb.add_anys_type(type_vec);
  rb.add_anys(any_vec);
  fbb.Finish(rb.Finish());
  return fbb.Release();
}

const reflection::Schema *ParseSchema() {
  static flatbuffers::Parser parser;
  static bool ready = false;
  if (!ready) {
    // Strings/structs in unions are an "advanced union feature" that is
    // gated on a code generator being selected.
    parser.opts.lang_to_generate |= flatbuffers::IDLOptions::kCpp;
    TEST_EQ(parser.Parse(kSchema), true);
    parser.Serialize();
    ready = true;
  }
  return reflection::GetSchema(parser.builder_.GetBufferPointer());
}

void ExpectEqual(const uint8_t *d1, size_t n1, const uint8_t *d2,
                 size_t n2) {
  auto schema = ParseSchema();
  auto result = flatbuffers::CompareBuffers(*schema, *schema->root_table(),
                                            d1, n1, d2, n2);
  TEST_OUTPUT_LINE("equal? %d, error: %s", result.equal,
                   result.error.c_str());
  TEST_EQ(result.equal, true);
  TEST_EQ_STR(result.error.c_str(), "");
}

void ExpectEqual(const flatbuffers::DetachedBuffer &b1,
                 const flatbuffers::DetachedBuffer &b2) {
  ExpectEqual(b1.data(), b1.size(), b2.data(), b2.size());
}

void ExpectUnequalAt(const flatbuffers::DetachedBuffer &b1,
                     const flatbuffers::DetachedBuffer &b2,
                     const char *expected_path) {
  auto schema = ParseSchema();
  auto result = flatbuffers::CompareBuffers(
      *schema, *schema->root_table(), b1.data(), b1.size(), b2.data(),
      b2.size());
  TEST_OUTPUT_LINE("equal? %d, path: %s, error: %s", result.equal,
                   result.path.c_str(), result.error.c_str());
  TEST_EQ(result.equal, false);
  TEST_EQ_STR(result.path.c_str(), expected_path);
}

const float kNaNF = std::numeric_limits<float>::quiet_NaN();

// Finish a Root table from the given optional pieces. Unset scalars keep
// their schema defaults (id=42, ratio=1.5, weird=NaN).
flatbuffers::DetachedBuffer FinishRoot(
    flatbuffers::FlatBufferBuilder &fbb,
    flatbuffers::Offset<String> name = 0, const Point *pos = nullptr,
    Any any_type = Any_NONE, flatbuffers::Offset<void> any = 0,
    flatbuffers::Offset<flatbuffers::Vector<uint8_t>> anys_type = 0,
    flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<void>>>
        anys = 0,
    float weird = kNaNF, bool size_prefixed = false) {
  RootBuilder rb(fbb);
  if (!name.IsNull()) rb.add_name(name);
  if (pos) rb.add_pos(pos);
  rb.add_any_type(any_type);
  if (any_type != Any_NONE) rb.add_any(any);
  if (!anys_type.IsNull()) rb.add_anys_type(anys_type);
  if (!anys.IsNull()) rb.add_anys(anys);
  rb.add_weird(weird);
  if (size_prefixed)
    fbb.FinishSizePrefixed(rb.Finish());
  else
    fbb.Finish(rb.Finish());
  return fbb.Release();
}

}  // namespace

void ReflectionCompareTest() {
  auto schema = ParseSchema();

  // ---- Different build order: physically different, logically equal. ----
  {
    auto a = BuildLogicalObject(/*order_b=*/false, /*force_defaults=*/false);
    auto b = BuildLogicalObject(/*order_b=*/true, /*force_defaults=*/false);
    TEST_ASSERT(a.size() != b.size() ||
                memcmp(a.data(), b.data(), a.size()) != 0);
    ExpectEqual(a, b);

    // Explicit defaults vs. omitted fields: still logically equal, and not
    // byte-identical (the forced-defaults buffer stores extra scalars).
    auto c = BuildLogicalObject(/*order_b=*/false, /*force_defaults=*/true);
    TEST_ASSERT(a.size() != c.size() ||
                memcmp(a.data(), c.data(),
                       a.size() < c.size() ? a.size() : c.size()) != 0);
    ExpectEqual(a, c);
    ExpectEqual(b, c);
  }

  // ---- Mutation of a nested vector element reports an exact location. ----
  {
    auto a = BuildLogicalObject(false, false, /*matrix_elem=*/5,
                                /*altered_nan_payload=*/false,
                                /*mutate_nested_elem=*/false);
    auto d = BuildLogicalObject(false, false, /*matrix_elem=*/5,
                                /*altered_nan_payload=*/false,
                                /*mutate_nested_elem=*/true);
    // inners[0].matrix[1].values[1]: the second wrapper, second element.
    ExpectUnequalAt(a, d, "inners[0].matrix[1].values[1]");
  }

  // ---- Top-level scalar mismatch. ----
  {
    auto a = BuildLogicalObject(false, false);
    auto altered = BuildLogicalObject(false, false, /*matrix_elem=*/5,
                                      /*altered_nan_payload=*/false,
                                      /*mutate_nested_elem=*/false,
                                      /*id_value=*/43);
    ExpectUnequalAt(a, altered, "id");
  }

  // ---- Omitted field vs. explicit default (the default of id is 42). ----
  {
    flatbuffers::FlatBufferBuilder fbb;
    fbb.ForceDefaults(true);
    auto name = fbb.CreateString("hello");
    RootBuilder rb(fbb);
    rb.add_id(42);  // Same value as the schema default, but stored.
    rb.add_name(name);
    fbb.Finish(rb.Finish());
    auto explicit_default = fbb.Release();

    flatbuffers::FlatBufferBuilder fbb2;
    auto name2 = fbb2.CreateString("hello");
    RootBuilder rb2(fbb2);
    rb2.add_name(name2);  // id omitted entirely.
    fbb2.Finish(rb2.Finish());
    auto omitted = fbb2.Release();

    // Their vtables differ (one stores the field), but they are equal.
    TEST_ASSERT(explicit_default.size() != omitted.size() ||
                memcmp(explicit_default.data(), omitted.data(),
                       omitted.size()) != 0);
    ExpectEqual(explicit_default, omitted);
  }

  // ---- NaN handling. ----
  {
    // Two buffers that both store NaN: equal even when explicitly written
    // vs. defaulted.
    {
      flatbuffers::FlatBufferBuilder f1, f2;
      auto b1 = FinishRoot(f1, 0, nullptr, Any_NONE, 0, 0, 0,
                           std::numeric_limits<float>::quiet_NaN());
      auto b2 = FinishRoot(f2);  // everything default (weird = NaN).
      ExpectEqual(b1, b2);
    }
    // Explicit NaN with a different NaN payload is still equal.
    {
      auto a = BuildLogicalObject(false, false, 5, false);
      auto b = BuildLogicalObject(false, false, 5, true);
      ExpectEqual(a, b);
    }
    // With compare_nan_equal disabled, different NaN payloads differ.
    {
      auto a = BuildLogicalObject(false, false, 5, false);
      auto b = BuildLogicalObject(false, false, 5, true);
      flatbuffers::CompareOptions opts;
      opts.compare_nan_equal = false;
      auto result = flatbuffers::CompareBuffers(
          *schema, *schema->root_table(), a.data(), a.size(), b.data(),
          b.size(), opts);
      TEST_EQ(result.equal, false);
      TEST_EQ_STR(result.path.c_str(), "weird");
    }
    // NaN vs. a finite number is never equal.
    {
      flatbuffers::FlatBufferBuilder f1, f2;
      auto b1 = FinishRoot(f1, 0, nullptr, Any_NONE, 0, 0, 0,
                           std::numeric_limits<float>::quiet_NaN());
      f2.ForceDefaults(true);
      auto b2 = FinishRoot(f2, 0, nullptr, Any_NONE, 0, 0, 0, 1.0f);
      ExpectUnequalAt(b1, b2, "weird");
    }
  }

  // ---- Strings: equal content at different backing offsets. ----
  {
    {
      flatbuffers::FlatBufferBuilder f1, f2;
      // f2 creates a junk string first to shift the real string offset.
      auto junk = f2.CreateString("junk shifts offset");
      (void)junk;
      auto s1 = f1.CreateString("text");
      auto s2 = f2.CreateString("text");
      auto b1 = FinishRoot(f1, s1);
      auto b2 = FinishRoot(f2, s2);
      TEST_ASSERT(b1.size() != b2.size());
      ExpectEqual(b1, b2);
    }
    // Different content: unequal with the right path.
    {
      flatbuffers::FlatBufferBuilder f1, f2;
      auto s1 = f1.CreateString("text");
      auto s2 = f2.CreateString("text!");
      auto b1 = FinishRoot(f1, s1);
      auto b2 = FinishRoot(f2, s2);
      ExpectUnequalAt(b1, b2, "name");
    }
    // String present vs. absent.
    {
      flatbuffers::FlatBufferBuilder f1, f2;
      auto s1 = f1.CreateString("text");
      auto b1 = FinishRoot(f1, s1);
      auto b2 = FinishRoot(f2);
      ExpectUnequalAt(b1, b2, "name");
    }
  }

  // ---- Unions: same value kind vs. different union type. ----
  {
    {
      flatbuffers::FlatBufferBuilder f1, f2;
      auto inner = CreateInner(f1, 1, flatbuffers::Offset<String>(0), flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<VectorWrapper>>>(0));
      auto str = f2.CreateString("different-kind");
      auto b1 = FinishRoot(f1, 0, nullptr, Any_Inner, inner.Union());
      auto b2 = FinishRoot(f2, 0, nullptr, Any_S, str.Union());
      ExpectUnequalAt(b1, b2, "any");
    }
    // Same union type (Inner) with equal content at different offsets.
    {
      flatbuffers::FlatBufferBuilder f1, f2;
      auto junk = f2.CreateString("junk");
      (void)junk;
      auto i1 = CreateInner(f1, 7, f1.CreateString("L"), flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<VectorWrapper>>>(0));
      auto i2 = CreateInner(f2, 7, f2.CreateString("L"), flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<VectorWrapper>>>(0));
      auto b1 = FinishRoot(f1, 0, nullptr, Any_Inner, i1.Union());
      auto b2 = FinishRoot(f2, 0, nullptr, Any_Inner, i2.Union());
      ExpectEqual(b1, b2);
    }
    // Vector of unions with a differing member: exact index in the path.
    {
      flatbuffers::FlatBufferBuilder f1, f2;
      auto i1 = CreateInner(f1, 1, flatbuffers::Offset<String>(0), flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<VectorWrapper>>>(0));
      auto i2 = CreateInner(f2, 2, flatbuffers::Offset<String>(0), flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<VectorWrapper>>>(0));
      std::vector<uint8_t> t1, t2;
      t1.push_back(Any_Inner);
      t2.push_back(Any_Inner);
      std::vector<flatbuffers::Offset<void>> v1, v2;
      v1.push_back(i1.Union());
      v2.push_back(i2.Union());
      auto b1 = FinishRoot(f1, 0, nullptr, Any_NONE, 0, f1.CreateVector(t1),
                           f1.CreateVector(v1));
      auto b2 = FinishRoot(f2, 0, nullptr, Any_NONE, 0, f2.CreateVector(t2),
                           f2.CreateVector(v2));
      ExpectUnequalAt(b1, b2, "anys[0].n");
    }
  }

  // ---- Corrupt buffers are rejected, never judged by raw bytes. ----
  {
    auto a = BuildLogicalObject(false, false);
    // A byte-identical copy is logically equal...
    std::vector<uint8_t> copy(a.data(), a.data() + a.size());
    ExpectEqual(a.data(), a.size(), copy.data(), copy.size());

    // ...but corrupt the root offset of the copy: a raw memcmp would still
    // find the buffers nearly identical; verification must reject it.
    copy[0] = 0xff;
    copy[1] = 0xff;
    {
      auto result = flatbuffers::CompareBuffers(
          *schema, *schema->root_table(), a.data(), a.size(), copy.data(),
          copy.size());
      TEST_EQ(result.equal, false);
      TEST_ASSERT(result.error.find("verification failed") !=
                  std::string::npos);
    }
    // Truncated buffer.
    {
      auto result = flatbuffers::CompareBuffers(
          *schema, *schema->root_table(), a.data(), a.size(), a.data(), 3);
      TEST_EQ(result.equal, false);
    }
    // Both corrupt: unequal by default...
    {
      std::vector<uint8_t> corrupt1(a.data(), a.data() + a.size());
      std::vector<uint8_t> corrupt2(a.data(), a.data() + a.size());
      corrupt1[0] = 0xfe;
      corrupt2[0] = 0xfd;
      auto result = flatbuffers::CompareBuffers(
          *schema, *schema->root_table(), corrupt1.data(), corrupt1.size(),
          corrupt2.data(), corrupt2.size());
      TEST_EQ(result.equal, false);
      // ...but equal when the caller opts in.
      flatbuffers::CompareOptions opts;
      opts.equal_invalid_buffers = true;
      result = flatbuffers::CompareBuffers(
          *schema, *schema->root_table(), corrupt1.data(), corrupt1.size(),
          corrupt2.data(), corrupt2.size(), opts);
      TEST_EQ(result.equal, true);
    }
  }

  // ---- Size-prefixed buffers use the dedicated entry point. ----
  {
    flatbuffers::FlatBufferBuilder f1, f2;
    auto s1 = f1.CreateString("text");
    auto s2 = f2.CreateString("text");
    auto b1 = FinishRoot(f1, s1, nullptr, Any_NONE, 0, 0, 0, kNaNF,
                         /*size_prefixed=*/true);
    auto b2 = FinishRoot(f2, s2, nullptr, Any_NONE, 0, 0, 0, kNaNF,
                         /*size_prefixed=*/true);
    auto result = flatbuffers::CompareSizePrefixedBuffers(
        *schema, *schema->root_table(), b1.data(), b1.size(), b2.data(),
        b2.size());
    TEST_EQ(result.equal, true);

    flatbuffers::FlatBufferBuilder f3;
    auto s3 = f3.CreateString("different");
    auto b3 = FinishRoot(f3, s3, nullptr, Any_NONE, 0, 0, 0, kNaNF,
                         /*size_prefixed=*/true);
    result = flatbuffers::CompareSizePrefixedBuffers(
        *schema, *schema->root_table(), b1.data(), b1.size(), b3.data(),
        b3.size());
    TEST_EQ(result.equal, false);
    TEST_EQ_STR(result.path.c_str(), "name");
  }

  // ---- CompareTables compares already-decoded roots without verifying. ----
  {
    auto a = BuildLogicalObject(false, false);
    auto b = BuildLogicalObject(true, false);
    auto result = flatbuffers::CompareTables(
        *schema, *schema->root_table(),
        *flatbuffers::GetRoot<flatbuffers::Table>(a.data()),
        *flatbuffers::GetRoot<flatbuffers::Table>(b.data()));
    TEST_EQ(result.equal, true);
  }

  // ---- Struct field mismatch with an exact path. ----
  {
    flatbuffers::FlatBufferBuilder f1, f2;
    Point p1(1.25f, -2.5f);
    Point p2(1.25f, -2.75f);
    auto b1 = FinishRoot(f1, 0, &p1);
    auto b2 = FinishRoot(f2, 0, &p2);
    ExpectUnequalAt(b1, b2, "pos.y");
  }

  TEST_OUTPUT_LINE("ReflectionCompareTest completed.");
}

}  // namespace tests
}  // namespace flatbuffers
