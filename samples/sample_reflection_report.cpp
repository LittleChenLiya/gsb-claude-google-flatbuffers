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

// Example: structured verification failure reports via reflection.
//
// Given a schema and a buffer, flatbuffers::VerifyWithReport() not only
// tells you whether the buffer is valid, but also reports the first
// failure with the table field path, vector index, byte offset (relative
// to the start of the buffer) and an error category.
//
// This program builds a valid buffer containing nested tables, a vector of
// strings and a union, then corrupts it in three different ways and prints
// the field path and error location of each failure. It runs in well under
// 90 seconds.

#include <cstdio>
#include <string>
#include <vector>

#include "flatbuffers/flatbuffers.h"
#include "flatbuffers/idl.h"
#include "flatbuffers/reflection.h"

namespace {

const char* kSchema =
    "table Inner { val:int; s:string; }"
    "union Payload { Inner }"
    "table Node { name:string; tags:[string]; child:Node; payload:Payload; }"
    "root_type Node;";

// Builds a valid buffer for kSchema using the raw builder API.
std::vector<uint8_t> BuildValidBuffer() {
  flatbuffers::FlatBufferBuilder fbb;
  auto inner_s = fbb.CreateString("inner");
  auto inner_start = fbb.StartTable();
  fbb.AddElement<int32_t>(flatbuffers::FieldIndexToOffset(0), 42, 0);
  fbb.AddOffset(flatbuffers::FieldIndexToOffset(1), inner_s);
  auto inner = fbb.EndTable(inner_start);

  auto tag0 = fbb.CreateString("alpha");
  auto tag1 = fbb.CreateString("beta");
  std::vector<flatbuffers::Offset<flatbuffers::String>> tags;
  tags.push_back(tag0);
  tags.push_back(tag1);
  auto tags_vec = fbb.CreateVector(tags);

  auto child_name = fbb.CreateString("child");
  auto child_start = fbb.StartTable();
  fbb.AddOffset(flatbuffers::FieldIndexToOffset(0), child_name);
  auto child = fbb.EndTable(child_start);

  auto root_name = fbb.CreateString("root");
  auto root_start = fbb.StartTable();
  fbb.AddOffset(flatbuffers::FieldIndexToOffset(0), root_name);
  fbb.AddOffset(flatbuffers::FieldIndexToOffset(1), tags_vec);
  fbb.AddOffset(flatbuffers::FieldIndexToOffset(2),
                flatbuffers::Offset<void>(child));
  fbb.AddElement<uint8_t>(flatbuffers::FieldIndexToOffset(3), 1, 0);
  fbb.AddOffset(flatbuffers::FieldIndexToOffset(4),
                flatbuffers::Offset<void>(inner));
  fbb.Finish(flatbuffers::Offset<void>(fbb.EndTable(root_start)));
  return std::vector<uint8_t>(
      fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize());
}

// Returns the absolute buffer offset of a root table field.
size_t FieldLocation(std::vector<uint8_t>* buf,
                     const reflection::Object& root, const char* field_name) {
  auto table = flatbuffers::GetAnyRoot(buf->data());
  auto field = root.fields()->LookupByKey(field_name);
  auto off = table->GetOptionalFieldOffset(field->offset());
  return (reinterpret_cast<const uint8_t*>(table) - buf->data()) + off;
}

void PrintReport(const char* label,
                 const flatbuffers::ReflectionVerifyReport& report) {
  if (report.ok) {
    std::printf("%-28s ok\n", label);
  } else {
    std::printf("%-28s %s\n", label, report.ToString().c_str());
  }
}

}  // namespace

int main(int /*argc*/, const char* /*argv*/[]) {
  // Parse the schema and obtain its binary (reflection) representation.
  flatbuffers::Parser parser;
  if (!parser.Parse(kSchema)) {
    std::fprintf(stderr, "failed to parse schema\n");
    return 1;
  }
  parser.Serialize();
  auto& schema = *reflection::GetSchema(parser.builder_.GetBufferPointer());
  auto& root = *schema.root_table();

  const std::vector<uint8_t> valid = BuildValidBuffer();

  // 1. The valid buffer passes.
  PrintReport("valid buffer",
              flatbuffers::VerifyWithReport(schema, root, valid.data(),
                                            valid.size()));

  // 2. Corrupt an offset: point the "tags" vector far outside the buffer.
  {
    auto bad = valid;
    const size_t loc = FieldLocation(&bad, root, "tags");
    flatbuffers::WriteScalar(bad.data() + loc,
                             static_cast<flatbuffers::uoffset_t>(0x7FFFFF00));
    PrintReport("corrupt offset",
                flatbuffers::VerifyWithReport(schema, root, bad.data(),
                                              bad.size()));
  }

  // 3. Corrupt a string terminator in a nested table.
  {
    auto bad = valid;
    const size_t loc = FieldLocation(&bad, root, "name");
    const size_t str_off =
        loc + flatbuffers::ReadScalar<flatbuffers::uoffset_t>(bad.data() + loc);
    const size_t str_len = flatbuffers::ReadScalar<flatbuffers::uoffset_t>(
        bad.data() + str_off);
    bad[str_off + sizeof(flatbuffers::uoffset_t) + str_len] = 'X';
    PrintReport("corrupt string terminator",
                flatbuffers::VerifyWithReport(schema, root, bad.data(),
                                              bad.size()));
  }

  // 4. Corrupt a union discriminator.
  {
    auto bad = valid;
    const size_t loc = FieldLocation(&bad, root, "payload_type");
    bad[loc] = 200;  // Not a member of the Payload union.
    PrintReport("corrupt union type",
                flatbuffers::VerifyWithReport(schema, root, bad.data(),
                                              bad.size()));
  }

  return 0;
}
