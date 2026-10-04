// Example: reflection-based verification with structured failure reporting.
//
// Builds a FlatBuffer containing nested tables, a string vector and a union,
// then corrupts the offset, the string terminator and the union type and
// prints the field path and error location for each failure.
//
// Compile:
//   g++ -std=c++17 -O0 -I include -I tests \
//       -o verify_report_example \
//       examples/verify_report_example.cpp src/reflection.cpp src/util.cpp
//
// Run:
//   ./verify_report_example tests/monster_test.bfbs

#include <cstdio>
#include <string>
#include <vector>

#include "flatbuffers/flatbuffers.h"
#include "flatbuffers/reflection.h"
#include "flatbuffers/util.h"
#include "monster_test_generated.h"

using namespace MyGame::Example;

namespace {

// Builds a Monster buffer with nested tables, a string vector and a union.
std::vector<uint8_t> BuildValidBuffer() {
  flatbuffers::FlatBufferBuilder fbb;
  auto name = fbb.CreateString("Bob");
  auto s1 = fbb.CreateString("alpha");
  auto s2 = fbb.CreateString("beta");
  auto s3 = fbb.CreateString("gamma");
  auto enemy_name = fbb.CreateString("Enemy");
  auto um_name = fbb.CreateString("UnionMonster");
  std::vector<flatbuffers::Offset<flatbuffers::String>> strs = {s1, s2, s3};
  auto vec_strs = fbb.CreateVector(strs);

  MonsterBuilder enemy_builder(fbb);
  enemy_builder.add_name(enemy_name);
  auto enemy = enemy_builder.Finish();

  MonsterBuilder um_builder(fbb);
  um_builder.add_name(um_name);
  auto um = um_builder.Finish();

  MonsterBuilder root_builder(fbb);
  root_builder.add_name(name);
  root_builder.add_testarrayofstring(vec_strs);
  root_builder.add_enemy(enemy);
  root_builder.add_test_type(Any_Monster);
  root_builder.add_test(um.Union());
  auto root = root_builder.Finish();
  FinishMonsterBuffer(fbb, root);

  return std::vector<uint8_t>(fbb.GetBufferPointer(),
                              fbb.GetBufferPointer() + fbb.GetSize());
}

// Finds the absolute location of a uoffset_t field inside the root table.
uint8_t* RootFieldLoc(uint8_t* buf, flatbuffers::voffset_t field) {
  auto root = flatbuffers::GetAnyRoot(buf);
  auto off = root->GetOptionalFieldOffset(field);
  return reinterpret_cast<uint8_t*>(root) + off;
}

void PrintResult(const char* label, const flatbuffers::VerifyResult& result) {
  if (result.ok) {
    printf("  %s: OK\n", label);
  } else {
    printf("  %s: FAIL\n", label);
    printf("    error:  %s\n", result.failure.ErrorName());
    printf("    path:   %s\n", result.failure.PathString().c_str());
    printf("    offset: %zu\n", result.failure.byte_offset);
    printf("    detail: %s\n", result.failure.message.c_str());
    if (result.failure.work_budget_exceeded) {
      printf("    (work budget exceeded)\n");
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::string schema_path = "tests/monster_test.bfbs";
  if (argc > 1) schema_path = argv[1];

  std::string bfbs;
  if (!flatbuffers::LoadFile(schema_path.c_str(), true, &bfbs)) {
    printf("Could not load schema: %s\n", schema_path.c_str());
    return 1;
  }
  auto& schema = *reflection::GetSchema(bfbs.c_str());
  auto& root_table = *schema.root_table();

  printf("Reflection verify report example\n");
  printf("Schema: %s\n\n", schema_path.c_str());

  // 1. Valid buffer — should pass.
  {
    auto buf = BuildValidBuffer();
    auto result = flatbuffers::VerifyWithReport(schema, root_table, buf.data(),
                                                buf.size());
    PrintResult("valid buffer", result);
  }

  // 2. Corrupted offset — enemy field points outside the buffer.
  {
    auto buf = BuildValidBuffer();
    auto loc = RootFieldLoc(buf.data(), Monster::VT_ENEMY);
    flatbuffers::WriteScalar<flatbuffers::uoffset_t>(loc, 0xFFFFFFF0);
    auto result = flatbuffers::VerifyWithReport(schema, root_table, buf.data(),
                                                buf.size());
    PrintResult("corrupted offset (enemy)", result);
  }

  // 3. Corrupted string terminator — first string in the vector.
  {
    auto buf = BuildValidBuffer();
    auto root = flatbuffers::GetAnyRoot(buf.data());
    auto vec_off = root->GetOptionalFieldOffset(Monster::VT_TESTARRAYOFSTRING);
    auto vec_loc = reinterpret_cast<uint8_t*>(root) + vec_off;
    auto vec_target = vec_loc + flatbuffers::ReadScalar<flatbuffers::uoffset_t>(
                                    vec_loc);
    auto vec = reinterpret_cast<
        const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>*>(
        vec_target);
    auto str = vec->Get(0);
    auto str_loc = reinterpret_cast<const uint8_t*>(str);
    auto len = flatbuffers::ReadScalar<flatbuffers::uoffset_t>(str_loc);
    auto term = str_loc + sizeof(flatbuffers::uoffset_t) + len;
    *const_cast<uint8_t*>(term) = 'X';
    auto result = flatbuffers::VerifyWithReport(schema, root_table, buf.data(),
                                                buf.size());
    PrintResult("corrupted string terminator", result);
  }

  // 4. Corrupted union type — discriminator is out of range.
  {
    auto buf = BuildValidBuffer();
    auto loc = RootFieldLoc(buf.data(), Monster::VT_TEST_TYPE);
    *loc = 99;
    auto result = flatbuffers::VerifyWithReport(schema, root_table, buf.data(),
                                                buf.size());
    PrintResult("corrupted union type", result);
  }

  // 5. Work budget cap — verification stops early.
  {
    auto buf = BuildValidBuffer();
    flatbuffers::VerifyOptions opts;
    opts.max_work = 3;
    auto result = flatbuffers::VerifyWithReport(schema, root_table, buf.data(),
                                                buf.size(), opts);
    PrintResult("work budget cap (max_work=3)", result);
  }

  return 0;
}
