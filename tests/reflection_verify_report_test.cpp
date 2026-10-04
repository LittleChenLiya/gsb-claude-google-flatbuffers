// Test and fuzz regression for reflection-based verification with structured
// failure reporting.
//
// Builds a FlatBuffer containing nested tables, a vector of strings and a
// union, then corrupts the offset, the string terminator and the union type
// and checks that the report points at the right field. Also covers the
// zero-length buffer, truncation, shared vtables, deep nesting and the work
// budget cap.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "flatbuffers/flatbuffers.h"
#include "flatbuffers/reflection.h"
#include "flatbuffers/util.h"
#include "monster_test_generated.h"

namespace {

int g_failures = 0;

#define CHECK(cond, ...)                                                 \
  do {                                                                   \
    if (!(cond)) {                                                       \
      printf("FAIL: %s (line %d): ", #cond, __LINE__);                   \
      printf(__VA_ARGS__);                                               \
      printf("\n");                                                      \
      g_failures++;                                                      \
    }                                                                    \
  } while (0)

#define CHECK_EQ(a, b)                                                   \
  do {                                                                   \
    auto _a = (a);                                                       \
    auto _b = (b);                                                       \
    if (!(_a == _b)) {                                                   \
      printf("FAIL: %s != %s (line %d): %lld != %lld\n", #a, #b,         \
             __LINE__, (long long)_a, (long long)_b);                    \
      g_failures++;                                                      \
    }                                                                    \
  } while (0)

#define CHECK_STR_EQ(a, b)                                                 \
  do {                                                                     \
    if (std::string(a) != std::string(b)) {                                \
      printf("FAIL: %s != %s (line %d): \"%s\" != \"%s\"\n", #a, #b,       \
             __LINE__, std::string(a).c_str(), std::string(b).c_str());    \
      g_failures++;                                                        \
    }                                                                      \
  } while (0)
using namespace MyGame::Example;

// Builds a Monster buffer with nested tables, a string vector and a union.
std::vector<uint8_t> BuildValidBuffer() {
  flatbuffers::FlatBufferBuilder fbb;
  // Create all nested objects first, before starting any table.
  auto name = fbb.CreateString("Bob");
  auto s1 = fbb.CreateString("alpha");
  auto s2 = fbb.CreateString("beta");
  auto s3 = fbb.CreateString("gamma");
  auto enemy_name = fbb.CreateString("Enemy");
  auto um_name = fbb.CreateString("UnionMonster");
  std::vector<flatbuffers::Offset<flatbuffers::String>> strs = {s1, s2, s3};
  auto vec_strs = fbb.CreateVector(strs);

  // Build innermost tables first.
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

std::string LoadSchemaFile(const std::string& path) {
  std::string bfbs;
  CHECK(flatbuffers::LoadFile(path.c_str(), true, &bfbs),
        "could not load schema %s", path.c_str());
  return bfbs;
}

// Finds the absolute location of a uoffset_t field inside the root table.
uint8_t* RootFieldLoc(uint8_t* buf, flatbuffers::voffset_t field) {
  auto root = flatbuffers::GetAnyRoot(buf);
  auto off = root->GetOptionalFieldOffset(field);
  return reinterpret_cast<uint8_t*>(root) + off;
}

void TestValid(const reflection::Schema& schema) {
  auto buf = BuildValidBuffer();
  auto& root = *schema.root_table();
  auto result = flatbuffers::VerifyWithReport(schema, root, buf.data(),
                                              buf.size());
  CHECK(result.ok, "valid buffer should verify: %s",
        result.failure.message.c_str());
  CHECK_EQ(static_cast<int>(result.failure.error),
           static_cast<int>(flatbuffers::VerifyError::kNone));
  CHECK_EQ(flatbuffers::Verify(schema, root, buf.data(), buf.size()), true);
}

void TestCorruptOffset(const reflection::Schema& schema) {
  auto buf = BuildValidBuffer();
  auto loc = RootFieldLoc(buf.data(), Monster::VT_ENEMY);
  flatbuffers::WriteScalar<flatbuffers::uoffset_t>(loc, 0xFFFFFFF0);

  auto& root = *schema.root_table();
  auto result = flatbuffers::VerifyWithReport(schema, root, buf.data(),
                                              buf.size());
  CHECK(!result.ok, "corrupted offset should fail");
  CHECK_EQ(static_cast<int>(result.failure.error),
           static_cast<int>(flatbuffers::VerifyError::kInvalidOffset));
  CHECK_STR_EQ(result.failure.PathString(), std::string("root.enemy"));
  CHECK(result.failure.byte_offset < buf.size(),
        "byte offset %zu out of range %zu", result.failure.byte_offset,
        buf.size());
  printf("  corrupt offset -> %s at +%zu (%s)\n",
         result.failure.PathString().c_str(), result.failure.byte_offset,
         result.failure.ErrorName());
}

void TestCorruptStringTerminator(const reflection::Schema& schema) {
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

  auto& root_obj = *schema.root_table();
  auto result = flatbuffers::VerifyWithReport(schema, root_obj, buf.data(),
                                              buf.size());
  CHECK(!result.ok, "corrupted string terminator should fail");
  CHECK_EQ(static_cast<int>(result.failure.error),
           static_cast<int>(flatbuffers::VerifyError::kStringNotTerminated));
  CHECK_STR_EQ(result.failure.PathString(),
           std::string("root.testarrayofstring[0]"));
  printf("  corrupt string -> %s at +%zu (%s)\n",
         result.failure.PathString().c_str(), result.failure.byte_offset,
         result.failure.ErrorName());
}

void TestCorruptUnionType(const reflection::Schema& schema) {
  auto buf = BuildValidBuffer();
  auto loc = RootFieldLoc(buf.data(), Monster::VT_TEST_TYPE);
  *loc = 99;

  auto& root = *schema.root_table();
  auto result = flatbuffers::VerifyWithReport(schema, root, buf.data(),
                                              buf.size());
  CHECK(!result.ok, "corrupted union type should fail");
  CHECK_EQ(static_cast<int>(result.failure.error),
           static_cast<int>(flatbuffers::VerifyError::kInvalidUnionType));
  CHECK_STR_EQ(result.failure.PathString(), std::string("root.test"));
  printf("  corrupt union type -> %s at +%zu (%s)\n",
         result.failure.PathString().c_str(), result.failure.byte_offset,
         result.failure.ErrorName());
}

void TestZeroLength(const reflection::Schema& schema) {
  std::vector<uint8_t> empty;
  auto& root = *schema.root_table();
  auto result = flatbuffers::VerifyWithReport(schema, root, empty.data(), 0);
  CHECK(!result.ok, "zero-length buffer should fail");
  CHECK_EQ(static_cast<int>(result.failure.error),
           static_cast<int>(flatbuffers::VerifyError::kBufferTooSmall));
}

void TestTruncation(const reflection::Schema& schema) {
  auto buf = BuildValidBuffer();
  auto& root = *schema.root_table();
  for (size_t len = 0; len < buf.size(); len++) {
    auto result = flatbuffers::VerifyWithReport(schema, root, buf.data(), len);
    if (!result.ok) {
      CHECK(result.failure.byte_offset < FLATBUFFERS_MAX_BUFFER_SIZE,
            "truncated byte offset %zu is wrapped garbage",
            result.failure.byte_offset);
      // Path may be empty if the failure occurs at the root table start.
      CHECK(result.failure.error != flatbuffers::VerifyError::kNone,
            "truncation failure should have an error category");
    }
  }
  auto result = flatbuffers::VerifyWithReport(schema, root, buf.data(),
                                              buf.size());
  CHECK(result.ok, "full buffer should verify");
}

void TestSharedVTable(const reflection::Schema& schema) {
  flatbuffers::FlatBufferBuilder fbb;
  auto n1 = fbb.CreateString("A");
  auto n2 = fbb.CreateString("B");
  MonsterBuilder b1(fbb);
  b1.add_name(n1);
  auto o1 = b1.Finish();
  MonsterBuilder b2(fbb);
  b2.add_name(n2);
  auto o2 = b2.Finish();
  (void)o1;
  auto root = o2;
  FinishMonsterBuffer(fbb, root);
  auto buf = std::vector<uint8_t>(fbb.GetBufferPointer(),
                                  fbb.GetBufferPointer() + fbb.GetSize());
  auto& root_obj = *schema.root_table();
  auto result = flatbuffers::VerifyWithReport(schema, root_obj, buf.data(),
                                              buf.size());
  CHECK(result.ok, "shared vtable buffer should verify: %s",
        result.failure.message.c_str());
}

void TestDeepNesting(const reflection::Schema& schema) {
  flatbuffers::FlatBufferBuilder fbb;
  flatbuffers::uoffset_t child = 0;
  for (int i = 0; i < 10; i++) {
    auto name = fbb.CreateString("Monster");
    MonsterBuilder b(fbb);
    b.add_name(name);
    b.add_enemy(flatbuffers::Offset<Monster>(child));
    child = b.Finish().o;
  }
  FinishMonsterBuffer(fbb, flatbuffers::Offset<Monster>(child));
  auto buf = std::vector<uint8_t>(fbb.GetBufferPointer(),
                                  fbb.GetBufferPointer() + fbb.GetSize());
  auto& root = *schema.root_table();
  flatbuffers::VerifyOptions opts;
  opts.max_depth = 5;
  auto result = flatbuffers::VerifyWithReport(schema, root, buf.data(),
                                              buf.size(), opts);
  CHECK(!result.ok, "deep nesting should fail");
  CHECK_EQ(static_cast<int>(result.failure.error),
           static_cast<int>(flatbuffers::VerifyError::kMaxDepthExceeded));
  CHECK(result.failure.PathString().find("root.enemy") == 0,
        "path should start at root.enemy, got %s",
        result.failure.PathString().c_str());
  printf("  deep nesting -> %s (%s)\n", result.failure.PathString().c_str(),
         result.failure.ErrorName());
}

void TestWorkBudget(const reflection::Schema& schema) {
  auto buf = BuildValidBuffer();
  auto& root = *schema.root_table();
  flatbuffers::VerifyOptions opts;
  opts.max_work = 3;
  auto result = flatbuffers::VerifyWithReport(schema, root, buf.data(),
                                              buf.size(), opts);
  CHECK(!result.ok, "tiny work budget should fail");
  CHECK_EQ(static_cast<int>(result.failure.error),
           static_cast<int>(flatbuffers::VerifyError::kWorkBudgetExceeded));
  CHECK(result.failure.work_budget_exceeded, "flag should be set");
  printf("  work budget -> %s (%s)\n", result.failure.PathString().c_str(),
         result.failure.ErrorName());
}

// Fuzz regression: mutate every byte of a valid buffer and every truncation
// length, ensuring verification never crashes and always produces a valid
// report.
void TestFuzzRegression(const reflection::Schema& schema) {
  auto buf = BuildValidBuffer();
  auto& root = *schema.root_table();
  int checked = 0;
  for (size_t i = 0; i < buf.size(); i++) {
    auto mutated = buf;
    mutated[i] ^= 0xFF;
    auto result = flatbuffers::VerifyWithReport(schema, root, mutated.data(),
                                                mutated.size());
    if (!result.ok) {
      CHECK(result.failure.byte_offset < FLATBUFFERS_MAX_BUFFER_SIZE,
            "mutated byte offset %zu is wrapped garbage",
            result.failure.byte_offset);
      // Path may be empty if the failure occurs at the root table start.
      CHECK(result.failure.error != flatbuffers::VerifyError::kNone,
            "failure should have an error category");
    }
    checked++;
  }
  for (size_t len = 0; len <= buf.size(); len++) {
    auto result = flatbuffers::VerifyWithReport(schema, root, buf.data(), len);
    if (!result.ok) {
      CHECK(result.failure.byte_offset < FLATBUFFERS_MAX_BUFFER_SIZE,
            "truncated byte offset %zu is wrapped garbage",
            result.failure.byte_offset);
    }
    checked++;
  }
  printf("  fuzz regression: %d cases checked\n", checked);
}

}  // namespace

int main(int argc, char** argv) {
  std::string schema_path = "tests/monster_test.bfbs";
  if (argc > 1) schema_path = argv[1];
  auto bfbs = LoadSchemaFile(schema_path);
  auto& schema = *reflection::GetSchema(bfbs.c_str());

  printf("Running reflection verify report tests...\n");
  TestValid(schema);
  TestCorruptOffset(schema);
  TestCorruptStringTerminator(schema);
  TestCorruptUnionType(schema);
  TestZeroLength(schema);
  TestTruncation(schema);
  TestSharedVTable(schema);
  TestDeepNesting(schema);
  TestWorkBudget(schema);
  TestFuzzRegression(schema);

  if (g_failures == 0) {
    printf("ALL TESTS PASSED\n");
    return 0;
  } else {
    printf("%d TEST(S) FAILED\n", g_failures);
    return 1;
  }
}
