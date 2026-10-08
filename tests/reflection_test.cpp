#include "reflection_test.h"

#include <cstring>
#include <limits>

#include "flatbuffers/idl.h"
#include "flatbuffers/minireflect.h"
#include "flatbuffers/reflection.h"
#include "flatbuffers/reflection_generated.h"
#include "flatbuffers/verifier.h"
#include "monster_test.h"
#include "monster_test_generated.h"
#include "test_assert.h"
#include "tests/arrays_test_generated.h"
#include "tests/union_vector/union_vector_generated.h"

namespace flatbuffers {
namespace tests {

using namespace MyGame::Example;

void ReflectionTest(const std::string& tests_data_path, uint8_t* flatbuf,
                    size_t length) {
  // Load a binary schema.
  std::string bfbsfile;
  TEST_EQ(flatbuffers::LoadFile((tests_data_path + "monster_test.bfbs").c_str(),
                                true, &bfbsfile),
          true);

  // Verify it, just in case:
  flatbuffers::Verifier verifier(
      reinterpret_cast<const uint8_t*>(bfbsfile.c_str()), bfbsfile.length());
  TEST_EQ(reflection::VerifySchemaBuffer(verifier), true);

  // Make sure the schema is what we expect it to be.
  auto& schema = *reflection::GetSchema(bfbsfile.c_str());
  auto root_table = schema.root_table();

  // Check the declaration files.
  TEST_EQ_STR(root_table->name()->c_str(), "MyGame.Example.Monster");
  TEST_EQ_STR(root_table->declaration_file()->c_str(), "//monster_test.fbs");
  TEST_EQ_STR(
      schema.objects()->LookupByKey("TableA")->declaration_file()->c_str(),
      "//include_test/include_test1.fbs");
  TEST_EQ_STR(schema.objects()
                  ->LookupByKey("MyGame.OtherNameSpace.Unused")
                  ->declaration_file()
                  ->c_str(),
              "//include_test/sub/include_test2.fbs");
  TEST_EQ_STR(schema.enums()
                  ->LookupByKey("MyGame.OtherNameSpace.FromInclude")
                  ->declaration_file()
                  ->c_str(),
              "//include_test/sub/include_test2.fbs");

  // Check scheam filenames and their includes.
  TEST_EQ(schema.fbs_files()->size(), 3);

  const auto fbs0 = schema.fbs_files()->Get(0);
  TEST_EQ_STR(fbs0->filename()->c_str(), "//include_test/include_test1.fbs");
  const auto fbs0_includes = fbs0->included_filenames();
  TEST_EQ(fbs0_includes->size(), 2);

  // TODO(caspern): Should we force or disallow inclusion of self?
  TEST_EQ_STR(fbs0_includes->Get(0)->c_str(),
              "//include_test/include_test1.fbs");
  TEST_EQ_STR(fbs0_includes->Get(1)->c_str(),
              "//include_test/sub/include_test2.fbs");

  const auto fbs1 = schema.fbs_files()->Get(1);
  TEST_EQ_STR(fbs1->filename()->c_str(),
              "//include_test/sub/include_test2.fbs");
  const auto fbs1_includes = fbs1->included_filenames();
  TEST_EQ(fbs1_includes->size(), 2);
  TEST_EQ_STR(fbs1_includes->Get(0)->c_str(),
              "//include_test/include_test1.fbs");
  TEST_EQ_STR(fbs1_includes->Get(1)->c_str(),
              "//include_test/sub/include_test2.fbs");

  const auto fbs2 = schema.fbs_files()->Get(2);
  TEST_EQ_STR(fbs2->filename()->c_str(), "//monster_test.fbs");
  const auto fbs2_includes = fbs2->included_filenames();
  TEST_EQ(fbs2_includes->size(), 1);
  TEST_EQ_STR(fbs2_includes->Get(0)->c_str(),
              "//include_test/include_test1.fbs");

  // Check Root table fields
  auto fields = root_table->fields();
  auto hp_field_ptr = fields->LookupByKey("hp");
  TEST_NOTNULL(hp_field_ptr);
  auto& hp_field = *hp_field_ptr;
  TEST_EQ_STR(hp_field.name()->c_str(), "hp");
  TEST_EQ(hp_field.id(), 2);
  TEST_EQ(hp_field.type()->base_type(), reflection::Short);

  auto friendly_field_ptr = fields->LookupByKey("friendly");
  TEST_NOTNULL(friendly_field_ptr);
  TEST_NOTNULL(friendly_field_ptr->attributes());
  TEST_NOTNULL(friendly_field_ptr->attributes()->LookupByKey("priority"));

  // Make sure the table index is what we expect it to be.
  auto pos_field_ptr = fields->LookupByKey("pos");
  TEST_NOTNULL(pos_field_ptr);
  TEST_EQ(pos_field_ptr->type()->base_type(), reflection::Obj);
  auto pos_table_ptr = schema.objects()->Get(pos_field_ptr->type()->index());
  TEST_NOTNULL(pos_table_ptr);
  TEST_EQ_STR(pos_table_ptr->name()->c_str(), "MyGame.Example.Vec3");

  // Test nullability of fields: hp is a 0-default scalar, pos is a struct =>
  // optional, and name is a required string => not optional.
  TEST_EQ(hp_field.optional(), false);
  TEST_EQ(pos_field_ptr->optional(), true);
  TEST_EQ(fields->LookupByKey("name")->optional(), false);

  // Now use it to dynamically access a buffer.
  auto& root = *flatbuffers::GetAnyRoot(flatbuf);

  // Verify the buffer first using reflection based verification
  TEST_EQ(flatbuffers::Verify(schema, *schema.root_table(), flatbuf, length),
          true);

  auto hp = flatbuffers::GetFieldI<uint16_t>(root, hp_field);
  TEST_EQ(hp, 80);

  // Rather than needing to know the type, we can also get the value of
  // any field as an int64_t/double/string, regardless of what it actually is.
  auto hp_int64 = flatbuffers::GetAnyFieldI(root, hp_field);
  TEST_EQ(hp_int64, 80);
  auto hp_double = flatbuffers::GetAnyFieldF(root, hp_field);
  TEST_EQ(hp_double, 80.0);
  auto hp_string = flatbuffers::GetAnyFieldS(root, hp_field, &schema);
  TEST_EQ_STR(hp_string.c_str(), "80");

  // Get struct field through reflection
  auto pos_struct = flatbuffers::GetFieldStruct(root, *pos_field_ptr);
  TEST_NOTNULL(pos_struct);
  TEST_EQ(flatbuffers::GetAnyFieldF(*pos_struct,
                                    *pos_table_ptr->fields()->LookupByKey("z")),
          3.0f);

  auto test3_field = pos_table_ptr->fields()->LookupByKey("test3");
  auto test3_struct = flatbuffers::GetFieldStruct(*pos_struct, *test3_field);
  TEST_NOTNULL(test3_struct);
  auto test3_object = schema.objects()->Get(test3_field->type()->index());

  TEST_EQ(flatbuffers::GetAnyFieldF(*test3_struct,
                                    *test3_object->fields()->LookupByKey("a")),
          10);

  // We can also modify it.
  flatbuffers::SetField<uint16_t>(&root, hp_field, 200);
  hp = flatbuffers::GetFieldI<uint16_t>(root, hp_field);
  TEST_EQ(hp, 200);

  // We can also set fields generically:
  flatbuffers::SetAnyFieldI(&root, hp_field, 300);
  hp_int64 = flatbuffers::GetAnyFieldI(root, hp_field);
  TEST_EQ(hp_int64, 300);
  flatbuffers::SetAnyFieldF(&root, hp_field, 300.5);
  hp_int64 = flatbuffers::GetAnyFieldI(root, hp_field);
  TEST_EQ(hp_int64, 300);
  flatbuffers::SetAnyFieldS(&root, hp_field, "300");
  hp_int64 = flatbuffers::GetAnyFieldI(root, hp_field);
  TEST_EQ(hp_int64, 300);

  // Test buffer is valid after the modifications
  TEST_EQ(flatbuffers::Verify(schema, *schema.root_table(), flatbuf, length),
          true);

  // Reset it, for further tests.
  flatbuffers::SetField<uint16_t>(&root, hp_field, 80);

  // More advanced functionality: changing the size of items in-line!
  // First we put the FlatBuffer inside an std::vector.
  std::vector<uint8_t> resizingbuf(flatbuf, flatbuf + length);
  // Find the field we want to modify.
  auto& name_field = *fields->LookupByKey("name");
  // Get the root.
  // This time we wrap the result from GetAnyRoot in a smartpointer that
  // will keep rroot valid as resizingbuf resizes.
  auto rroot = flatbuffers::piv(flatbuffers::GetAnyRoot(resizingbuf.data()),
                                resizingbuf);
  SetString(schema, "totally new string", GetFieldS(**rroot, name_field),
            &resizingbuf);
  // Here resizingbuf has changed, but rroot is still valid.
  TEST_EQ_STR(GetFieldS(**rroot, name_field)->c_str(), "totally new string");
  // Now lets extend a vector by 100 elements (10 -> 110).
  auto& inventory_field = *fields->LookupByKey("inventory");
  auto rinventory = flatbuffers::piv(
      flatbuffers::GetFieldV<uint8_t>(**rroot, inventory_field), resizingbuf);
  flatbuffers::ResizeVector<uint8_t>(schema, 110, 50, *rinventory,
                                     &resizingbuf);
  // rinventory still valid, so lets read from it.
  TEST_EQ(rinventory->Get(10), 50);

  // For reflection uses not covered already, there is a more powerful way:
  // we can simply generate whatever object we want to add/modify in a
  // FlatBuffer of its own, then add that to an existing FlatBuffer:
  // As an example, let's add a string to an array of strings.
  // First, find our field:
  auto& testarrayofstring_field = *fields->LookupByKey("testarrayofstring");
  // Find the vector value:
  auto rtestarrayofstring = flatbuffers::piv(
      flatbuffers::GetFieldV<flatbuffers::Offset<flatbuffers::String>>(
          **rroot, testarrayofstring_field),
      resizingbuf);
  // It's a vector of 2 strings, to which we add one more, initialized to
  // offset 0.
  flatbuffers::ResizeVector<flatbuffers::Offset<flatbuffers::String>>(
      schema, 3, 0, *rtestarrayofstring, &resizingbuf);
  // Here we just create a buffer that contans a single string, but this
  // could also be any complex set of tables and other values.
  flatbuffers::FlatBufferBuilder stringfbb;
  stringfbb.Finish(stringfbb.CreateString("hank"));
  // Add the contents of it to our existing FlatBuffer.
  // We do this last, so the pointer doesn't get invalidated (since it is
  // at the end of the buffer):
  auto string_ptr = flatbuffers::AddFlatBuffer(
      resizingbuf, stringfbb.GetBufferPointer(), stringfbb.GetSize());
  // Finally, set the new value in the vector.
  rtestarrayofstring->MutateOffset(2, string_ptr);
  TEST_EQ_STR(rtestarrayofstring->Get(0)->c_str(), "bob");
  TEST_EQ_STR(rtestarrayofstring->Get(2)->c_str(), "hank");
  // Test integrity of all resize operations above.
  flatbuffers::Verifier resize_verifier(
      reinterpret_cast<const uint8_t*>(resizingbuf.data()), resizingbuf.size());
  TEST_EQ(VerifyMonsterBuffer(resize_verifier), true);

  // Test buffer is valid using reflection as well
  TEST_EQ(flatbuffers::Verify(schema, *schema.root_table(), resizingbuf.data(),
                              resizingbuf.size()),
          true);

  // As an additional test, also set it on the name field.
  // Note: unlike the name change above, this just overwrites the offset,
  // rather than changing the string in-place.
  SetFieldT(*rroot, name_field, string_ptr);
  TEST_EQ_STR(GetFieldS(**rroot, name_field)->c_str(), "hank");

  // Using reflection, rather than mutating binary FlatBuffers, we can also copy
  // tables and other things out of other FlatBuffers into a FlatBufferBuilder,
  // either part or whole.
  flatbuffers::FlatBufferBuilder fbb;
  auto root_offset = flatbuffers::CopyTable(
      fbb, schema, *root_table, *flatbuffers::GetAnyRoot(flatbuf), true);
  fbb.Finish(root_offset, MonsterIdentifier());
  // Test that it was copied correctly:
  AccessFlatBufferTest(fbb.GetBufferPointer(), fbb.GetSize());

  // Test buffer is valid using reflection as well
  TEST_EQ(flatbuffers::Verify(schema, *schema.root_table(),
                              fbb.GetBufferPointer(), fbb.GetSize()),
          true);
}

// Test that ForAllFields with reverse=true iterates fields in descending
// ID order. This exercises a fix for an operator precedence bug where the
// expression `size() - i + 1` was evaluated as `(size() - i) + 1` instead
// of the correct `size() - (i + 1)`, causing an out-of-bounds read.
void ForAllFieldsReverseTest(const std::string& tests_data_path) {
  // Load the binary schema.
  std::string bfbsfile;
  TEST_EQ(flatbuffers::LoadFile((tests_data_path + "monster_test.bfbs").c_str(),
                                true, &bfbsfile),
          true);

  // Verify the schema.
  flatbuffers::Verifier verifier(
      reinterpret_cast<const uint8_t*>(bfbsfile.c_str()), bfbsfile.length());
  TEST_EQ(reflection::VerifySchemaBuffer(verifier), true);

  auto& schema = *reflection::GetSchema(bfbsfile.c_str());

  // Use the Stat table which has 3 fields with sequential IDs:
  //   id:string   (id: 0)
  //   val:long     (id: 1)
  //   count:ushort (id: 2)
  auto stat_object = schema.objects()->LookupByKey("MyGame.Example.Stat");
  TEST_NOTNULL(stat_object);
  TEST_EQ(stat_object->fields()->size(), 3u);

  // Test forward iteration: fields should come in ascending ID order (0,1,2).
  {
    std::vector<uint16_t> forward_ids;
    flatbuffers::ForAllFields(
        stat_object, /*reverse=*/false,
        [&forward_ids](const reflection::Field* field) {
          forward_ids.push_back(field->id());
        });
    TEST_EQ(forward_ids.size(), 3u);
    TEST_EQ(forward_ids[0], 0);  // id
    TEST_EQ(forward_ids[1], 1);  // val
    TEST_EQ(forward_ids[2], 2);  // count
  }

  // Test reverse iteration: fields should come in descending ID order (2,1,0).
  // With the old buggy code `size() - i + 1`, at i=0 this would compute
  // index 3 - 0 + 1 = 4, which is out of bounds for a size-3 vector.
  {
    std::vector<uint16_t> reverse_ids;
    flatbuffers::ForAllFields(
        stat_object, /*reverse=*/true,
        [&reverse_ids](const reflection::Field* field) {
          reverse_ids.push_back(field->id());
        });
    TEST_EQ(reverse_ids.size(), 3u);
    TEST_EQ(reverse_ids[0], 2);  // count (highest ID first)
    TEST_EQ(reverse_ids[1], 1);  // val
    TEST_EQ(reverse_ids[2], 0);  // id (lowest ID last)
  }

  // Also test with the root Monster table which has many fields and
  // non-sequential definition order vs. ID order (e.g., pos=id:0, hp=id:2,
  // mana=id:1). This ensures the ID-to-index mapping works correctly.
  auto root_table = schema.root_table();
  TEST_NOTNULL(root_table);
  {
    std::vector<uint16_t> forward_ids;
    flatbuffers::ForAllFields(
        root_table, /*reverse=*/false,
        [&forward_ids](const reflection::Field* field) {
          forward_ids.push_back(field->id());
        });
    // Verify ascending order.
    for (size_t i = 1; i < forward_ids.size(); ++i) {
      TEST_ASSERT(forward_ids[i - 1] < forward_ids[i]);
    }
  }
  {
    std::vector<uint16_t> reverse_ids;
    flatbuffers::ForAllFields(
        root_table, /*reverse=*/true,
        [&reverse_ids](const reflection::Field* field) {
          reverse_ids.push_back(field->id());
        });
    // Verify descending order.
    for (size_t i = 1; i < reverse_ids.size(); ++i) {
      TEST_ASSERT(reverse_ids[i - 1] > reverse_ids[i]);
    }
  }
}

void MiniReflectFlatBuffersTest(uint8_t* flatbuf) {
  auto s =
      flatbuffers::FlatBufferToString(flatbuf, Monster::MiniReflectTypeTable());
  TEST_EQ_STR(
      s.c_str(),
      "{ "
      "pos: { x: 1.0, y: 2.0, z: 3.0, test1: 0.0, test2: Red, test3: "
      "{ a: 10, b: 20 } }, "
      "hp: 80, "
      "name: \"MyMonster\", "
      "inventory: [ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 ], "
      "test_type: Monster, "
      "test: { name: \"Fred\" }, "
      "test4: [ { a: 10, b: 20 }, { a: 30, b: 40 } ], "
      "testarrayofstring: [ \"bob\", \"fred\", \"bob\", \"fred\" ], "
      "testarrayoftables: [ { hp: 1000, name: \"Barney\" }, { name: \"Fred\" "
      "}, "
      "{ name: \"Wilma\" } ], "
      // TODO(wvo): should really print this nested buffer correctly.
      "testnestedflatbuffer: [ 124, 0, 0, 0, 77, 79, 78, 83, 0, 0, 114, 0, 16, "
      "0, 0, 0, 4, 0, 6, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "
      "0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "
      "0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "
      "0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "
      "0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 0, 114, 0, 0, 0, 0, 0, 0, 0, "
      "8, 0, 0, 0, 0, 0, 192, 127, 13, 0, 0, 0, 78, 101, 115, 116, 101, 100, "
      "77, 111, 110, 115, 116, 101, 114, 0, 0, 0 ], "
      "testarrayofstring2: [ \"jane\", \"mary\" ], "
      "testarrayofsortedstruct: [ { id: 0, distance: 0 }, "
      "{ id: 2, distance: 20 }, { id: 3, distance: 30 }, "
      "{ id: 4, distance: 40 } ], "
      "flex: [ 210, 4, 5, 2 ], "
      "test5: [ { a: 10, b: 20 }, { a: 30, b: 40 } ], "
      "vector_of_enums: [ Blue, Green ], "
      "scalar_key_sorted_tables: [ { id: \"miss\" } ], "
      "nan_default: nan "
      "}");

  Test test(16, 32);
  Vec3 vec(1, 2, 3, 1.5, Color_Red, test);
  flatbuffers::FlatBufferBuilder vec_builder;
  vec_builder.Finish(vec_builder.CreateStruct(vec));
  auto vec_buffer = vec_builder.Release();
  auto vec_str = flatbuffers::FlatBufferToString(vec_buffer.data(),
                                                 Vec3::MiniReflectTypeTable());
  TEST_EQ_STR(vec_str.c_str(),
              "{ x: 1.0, y: 2.0, z: 3.0, test1: 1.5, test2: Red, test3: { a: "
              "16, b: 32 } }");
}

void MiniReflectFixedLengthArrayTest() {
  // VS10 does not support typed enums, exclude from tests
#if !defined(_MSC_VER) || _MSC_VER >= 1700
  flatbuffers::FlatBufferBuilder fbb;
  MyGame::Example::ArrayStruct aStruct(2, 12, 1);
  auto aTable = MyGame::Example::CreateArrayTable(fbb, &aStruct);
  fbb.Finish(aTable);

  auto flatbuf = fbb.Release();
  auto s = flatbuffers::FlatBufferToString(
      flatbuf.data(), MyGame::Example::ArrayTableTypeTable());
  TEST_EQ_STR(
      "{ "
      "a: { a: 2.0, "
      "b: [ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 ], "
      "c: 12, "
      "d: [ { a: [ 0, 0 ], b: A, c: [ A, A ], d: [ 0, 0 ] }, "
      "{ a: [ 0, 0 ], b: A, c: [ A, A ], d: [ 0, 0 ] } ], "
      "e: 1, f: [ 0, 0 ] } "
      "}",
      s.c_str());
#endif
}

namespace {

// Describes the logical contents of a Monster buffer used by the equality
// tests. Two buffers built from the same spec are logically identical, even
// if they were laid out differently.
struct EqualityMonsterSpec {
  int16_t hp = 80;
  int16_t sub_hp1 = 20;
  float pos_x = 1.0f;
  int16_t pos_test3_a = 10;
  bool set_testf3 = false;
  float testf3 = 0.0f;
  const char* name = "MyMonster";
  const char* strvec1 = "two";
  int8_t test4_1_b = 20;
  double vec_double1 = 2.5;
  MyGame::Example::Color union_color = MyGame::Example::Color_Red;
  bool use_monster_union = false;
  bool omit_union = false;
  bool empty_inventory = false;
};

// Builds a Monster buffer from `spec`. When `alt_layout` is true the same
// logical contents are produced with a different physical layout: objects
// are created in a different order and default-valued scalar fields are
// physically written out (ForceDefaults), producing a different vtable.
std::vector<uint8_t> BuildEqualityMonster(const EqualityMonsterSpec& spec,
                                          bool alt_layout,
                                          bool size_prefixed = false) {
  using namespace MyGame::Example;
  flatbuffers::FlatBufferBuilder fbb;
  if (alt_layout) fbb.ForceDefaults(true);

  flatbuffers::Offset<flatbuffers::String> root_name, str_one, str_two;
  flatbuffers::Offset<Monster> sub0, sub1;

  auto make_strings = [&]() {
    root_name = fbb.CreateString(spec.name);
    str_one = fbb.CreateString("one");
    str_two = fbb.CreateString(spec.strvec1);
  };
  auto make_subs = [&]() {
    auto sub0_name = fbb.CreateString("sub0");
    MonsterBuilder sub0_builder(fbb);
    sub0_builder.add_name(sub0_name);
    sub0_builder.add_hp(10);
    sub0 = sub0_builder.Finish();
    auto sub1_name = fbb.CreateString("sub1");
    MonsterBuilder sub1_builder(fbb);
    sub1_builder.add_name(sub1_name);
    sub1_builder.add_hp(spec.sub_hp1);
    sub1 = sub1_builder.Finish();
  };
  // Vary the physical order in which objects are created.
  if (alt_layout) {
    make_strings();
    make_subs();
  } else {
    make_subs();
    make_strings();
  }

  const uint8_t inv_data[] = { 1, 2, 3, 4 };
  auto inventory = spec.empty_inventory
                       ? fbb.CreateVector(inv_data, 0)
                       : fbb.CreateVector(inv_data, 4);
  const std::vector<flatbuffers::Offset<flatbuffers::String>> strings = {
    str_one, str_two
  };
  auto strvec = fbb.CreateVector(strings);
  const std::vector<flatbuffers::Offset<Monster>> tables = { sub0, sub1 };
  auto tablesvec = fbb.CreateVector(tables);
  const Test test_structs[] = { Test(1, 2), Test(10, spec.test4_1_b) };
  auto test4 = fbb.CreateVectorOfStructs(test_structs, 2);
  const double dbl_data[] = { 1.5, spec.vec_double1 };
  auto dbls = fbb.CreateVector(dbl_data, 2);

  flatbuffers::Offset<void> union_val;
  Any union_type = Any_NONE;
  if (!spec.omit_union) {
    if (spec.use_monster_union) {
      auto union_name = fbb.CreateString("unionmonster");
      MonsterBuilder union_builder(fbb);
      union_builder.add_name(union_name);
      union_builder.add_hp(5);
      union_val = union_builder.Finish().Union();
      union_type = Any_Monster;
    } else {
      union_val =
          CreateTestSimpleTableWithEnum(fbb, spec.union_color).Union();
      union_type = Any_TestSimpleTableWithEnum;
    }
  }

  Vec3 pos(spec.pos_x, 2.0f, 3.0f, 4.5, Color_Red,
           Test(spec.pos_test3_a, 20));

  MonsterBuilder builder(fbb);
  builder.add_pos(&pos);
  builder.add_hp(spec.hp);
  builder.add_mana(150);        // Default: dropped unless ForceDefaults.
  builder.add_testf(3.14159f);  // Default: dropped unless ForceDefaults.
  builder.add_name(root_name);
  builder.add_inventory(inventory);
  builder.add_testarrayofstring(strvec);
  builder.add_testarrayoftables(tablesvec);
  builder.add_test4(test4);
  builder.add_vector_of_doubles(dbls);
  if (spec.set_testf3) builder.add_testf3(spec.testf3);
  if (!spec.omit_union) {
    builder.add_test_type(union_type);
    builder.add_test(union_val);
  }
  auto monster = builder.Finish();
  if (size_prefixed) {
    fbb.FinishSizePrefixed(monster);
  } else {
    fbb.Finish(monster);
  }
  const auto* begin = fbb.GetBufferPointer();
  return std::vector<uint8_t>(begin, begin + fbb.GetSize());
}

// Builds a Movie buffer (union_vector.fbs) exercising vectors of unions
// containing tables, structs and strings.
std::vector<uint8_t> BuildEqualityMovie(bool alt_layout, int32_t sword_damage,
                                        int32_t belle_books) {
  flatbuffers::FlatBufferBuilder fbb;
  if (alt_layout) fbb.ForceDefaults(true);

  auto make_main = [&]() {
    return CreateAttacker(fbb, /*sword_attack_damage=*/42).Union();
  };
  auto make_strings = [&]() {
    return std::vector<flatbuffers::Offset<void>>{
      fbb.CreateString("Other").Union(), fbb.CreateString("Unused").Union()
    };
  };
  flatbuffers::Offset<void> main_character;
  std::vector<flatbuffers::Offset<void>> union_strings;
  // Vary the physical creation order between the two layouts.
  if (alt_layout) {
    union_strings = make_strings();
    main_character = make_main();
  } else {
    main_character = make_main();
    union_strings = make_strings();
  }

  std::vector<flatbuffers::Offset<void>> characters;
  characters.push_back(fbb.CreateStruct(BookReader(belle_books)).Union());
  characters.push_back(CreateAttacker(fbb, sword_damage).Union());
  characters.push_back(fbb.CreateStruct(BookReader(2)).Union());
  characters.push_back(union_strings[0]);
  characters.push_back(union_strings[1]);
  const std::vector<uint8_t> types(
      { static_cast<uint8_t>(Character_Belle),
        static_cast<uint8_t>(Character_MuLan),
        static_cast<uint8_t>(Character_BookFan),
        static_cast<uint8_t>(Character_Other),
        static_cast<uint8_t>(Character_Unused) });
  auto characters_vec = fbb.CreateVector(characters);
  auto types_vec = fbb.CreateVector(types);

  MovieBuilder builder(fbb);
  builder.add_main_character_type(Character_MuLan);
  builder.add_main_character(main_character);
  builder.add_characters_type(types_vec);
  builder.add_characters(characters_vec);
  auto movie = builder.Finish();
  fbb.Finish(movie);
  const auto* begin = fbb.GetBufferPointer();
  return std::vector<uint8_t>(begin, begin + fbb.GetSize());
}

// Builds an ArrayTable buffer (arrays_test.fbs) exercising fixed-length
// arrays and nested structs inside structs.
std::vector<uint8_t> BuildEqualityArrayTable(int32_t b3, int32_t d1_a0) {
  flatbuffers::FlatBufferBuilder fbb;
  MyGame::Example::ArrayStruct array_struct(2.0f, 12, 1);
  array_struct.mutable_b()->Mutate(3, b3);
  array_struct.mutable_d()->GetMutablePointer(1)->mutable_a()->Mutate(0,
                                                                       d1_a0);
  auto table = MyGame::Example::CreateArrayTable(fbb, &array_struct);
  fbb.Finish(table);
  const auto* begin = fbb.GetBufferPointer();
  return std::vector<uint8_t>(begin, begin + fbb.GetSize());
}

bool BuffersDifferBytewise(const std::vector<uint8_t>& a,
                           const std::vector<uint8_t>& b) {
  return a.size() != b.size() ||
         std::memcmp(a.data(), b.data(), a.size()) != 0;
}

}  // namespace

void ReflectionEqualityTest(const std::string& tests_data_path) {
  // Load the binary schema.
  std::string bfbsfile;
  TEST_EQ(flatbuffers::LoadFile((tests_data_path + "monster_test.bfbs").c_str(),
                                true, &bfbsfile),
          true);
  auto& schema = *reflection::GetSchema(bfbsfile.c_str());
  auto& root = *schema.root_table();

  const EqualityMonsterSpec spec;
  const auto buf_a = BuildEqualityMonster(spec, false);
  const auto buf_b = BuildEqualityMonster(spec, true);

  // The two buffers encode the same data but must NOT be byte-identical,
  // proving that Equal() below is not a memcmp().
  TEST_ASSERT(BuffersDifferBytewise(buf_a, buf_b));

  std::string diff = "sentinel";
  TEST_EQ(flatbuffers::Equal(schema, root, buf_a.data(), buf_a.size(),
                             buf_b.data(), buf_b.size(), &diff),
          true);
  TEST_ASSERT(diff.empty());

  // A buffer always equals itself.
  TEST_EQ(flatbuffers::Equal(schema, root, buf_a.data(), buf_a.size(),
                             buf_a.data(), buf_a.size()),
          true);

  // Default-valued fields: hp=100 is the schema default, so buf_a omits it
  // while the ForceDefaults buffer writes it out explicitly. Same for
  // mana=150 and testf=3.14159 in the buffers above.
  {
    EqualityMonsterSpec def = spec;
    def.hp = 100;
    const auto def_a = BuildEqualityMonster(def, false);
    const auto def_b = BuildEqualityMonster(def, true);
    TEST_ASSERT(BuffersDifferBytewise(def_a, def_b));
    TEST_EQ(flatbuffers::Equal(schema, root, def_a.data(), def_a.size(),
                               def_b.data(), def_b.size()),
            true);
  }

  // Changing one element of a nested vector of tables must be reported at
  // the exact location.
  {
    EqualityMonsterSpec mod = spec;
    mod.sub_hp1 = 21;
    const auto buf_c = BuildEqualityMonster(mod, false);
    std::string d;
    TEST_EQ(flatbuffers::Equal(schema, root, buf_a.data(), buf_a.size(),
                               buf_c.data(), buf_c.size(), &d),
            false);
    TEST_EQ_STR(".testarrayoftables[1].hp", d.c_str());
  }

  // Changing one element of a vector of structs.
  {
    EqualityMonsterSpec mod = spec;
    mod.test4_1_b = 21;
    const auto buf_c = BuildEqualityMonster(mod, false);
    std::string d;
    TEST_EQ(flatbuffers::Equal(schema, root, buf_a.data(), buf_a.size(),
                               buf_c.data(), buf_c.size(), &d),
            false);
    TEST_EQ_STR(".test4[1].b", d.c_str());
  }

  // Changing one element of a vector of scalars.
  {
    EqualityMonsterSpec mod = spec;
    mod.vec_double1 = 3.5;
    const auto buf_c = BuildEqualityMonster(mod, false);
    std::string d;
    TEST_EQ(flatbuffers::Equal(schema, root, buf_a.data(), buf_a.size(),
                               buf_c.data(), buf_c.size(), &d),
            false);
    TEST_EQ_STR(".vector_of_doubles[1]", d.c_str());
  }

  // Changing one string inside a vector of strings.
  {
    EqualityMonsterSpec mod = spec;
    mod.strvec1 = "changed";
    const auto buf_c = BuildEqualityMonster(mod, false);
    std::string d;
    TEST_EQ(flatbuffers::Equal(schema, root, buf_a.data(), buf_a.size(),
                               buf_c.data(), buf_c.size(), &d),
            false);
    TEST_EQ_STR(".testarrayofstring[1]", d.c_str());
  }

  // Changing the root string and a root scalar.
  {
    EqualityMonsterSpec mod = spec;
    mod.name = "OtherMonster";
    const auto buf_c = BuildEqualityMonster(mod, false);
    std::string d;
    TEST_EQ(flatbuffers::Equal(schema, root, buf_a.data(), buf_a.size(),
                               buf_c.data(), buf_c.size(), &d),
            false);
    TEST_EQ_STR(".name", d.c_str());
  }
  {
    EqualityMonsterSpec mod = spec;
    mod.hp = 81;
    const auto buf_c = BuildEqualityMonster(mod, false);
    std::string d;
    TEST_EQ(flatbuffers::Equal(schema, root, buf_a.data(), buf_a.size(),
                               buf_c.data(), buf_c.size(), &d),
            false);
    TEST_EQ_STR(".hp", d.c_str());
  }

  // Changing a scalar inside a struct inside the root table.
  {
    EqualityMonsterSpec mod = spec;
    mod.pos_test3_a = 11;
    const auto buf_c = BuildEqualityMonster(mod, false);
    std::string d;
    TEST_EQ(flatbuffers::Equal(schema, root, buf_a.data(), buf_a.size(),
                               buf_c.data(), buf_c.size(), &d),
            false);
    TEST_EQ_STR(".pos.test3.a", d.c_str());
  }

  // NaN handling: NaN is logically equal to NaN, but not to a number.
  {
    EqualityMonsterSpec nan_spec = spec;
    nan_spec.set_testf3 = true;
    nan_spec.testf3 = std::numeric_limits<float>::quiet_NaN();
    const auto nan_a = BuildEqualityMonster(nan_spec, false);
    const auto nan_b = BuildEqualityMonster(nan_spec, true);
    TEST_ASSERT(BuffersDifferBytewise(nan_a, nan_b));
    TEST_EQ(flatbuffers::Equal(schema, root, nan_a.data(), nan_a.size(),
                               nan_b.data(), nan_b.size()),
            true);

    EqualityMonsterSpec zero_spec = spec;
    zero_spec.set_testf3 = true;
    zero_spec.testf3 = 0.0f;
    const auto zero = BuildEqualityMonster(zero_spec, false);
    std::string d;
    TEST_EQ(flatbuffers::Equal(schema, root, nan_a.data(), nan_a.size(),
                               zero.data(), zero.size(), &d),
            false);
    TEST_EQ_STR(".testf3", d.c_str());

    // NaN inside a struct field compares equal too.
    EqualityMonsterSpec nan_pos = spec;
    nan_pos.pos_x = std::numeric_limits<float>::quiet_NaN();
    const auto nan_pos_a = BuildEqualityMonster(nan_pos, false);
    const auto nan_pos_b = BuildEqualityMonster(nan_pos, true);
    TEST_EQ(flatbuffers::Equal(schema, root, nan_pos_a.data(),
                               nan_pos_a.size(), nan_pos_b.data(),
                               nan_pos_b.size()),
            true);
  }

  // Unions: same type and contents compare equal, different contents or
  // different types do not.
  {
    EqualityMonsterSpec mod = spec;
    mod.union_color = MyGame::Example::Color_Blue;
    const auto buf_c = BuildEqualityMonster(mod, false);
    std::string d;
    TEST_EQ(flatbuffers::Equal(schema, root, buf_a.data(), buf_a.size(),
                               buf_c.data(), buf_c.size(), &d),
            false);
    TEST_EQ_STR(".test.color", d.c_str());
  }
  {
    EqualityMonsterSpec mod = spec;
    mod.use_monster_union = true;
    const auto buf_c = BuildEqualityMonster(mod, false);
    std::string d;
    TEST_EQ(flatbuffers::Equal(schema, root, buf_a.data(), buf_a.size(),
                               buf_c.data(), buf_c.size(), &d),
            false);
    // The union type differs; the schema lists the value field "test"
    // before its type field "test_type" (fields are sorted by name).
    TEST_EQ_STR(".test", d.c_str());
  }
  {
    EqualityMonsterSpec mod = spec;
    mod.omit_union = true;
    const auto buf_c = BuildEqualityMonster(mod, false);
    std::string d;
    TEST_EQ(flatbuffers::Equal(schema, root, buf_a.data(), buf_a.size(),
                               buf_c.data(), buf_c.size(), &d),
            false);
    // The union is present in one buffer only; "test" sorts before
    // "test_type" in the schema, so it is reported first.
    TEST_EQ_STR(".test", d.c_str());
  }

  // A missing vector and an explicitly empty vector are not the same.
  {
    EqualityMonsterSpec mod = spec;
    mod.empty_inventory = true;
    const auto buf_c = BuildEqualityMonster(mod, false);
    std::string d;
    TEST_EQ(flatbuffers::Equal(schema, root, buf_a.data(), buf_a.size(),
                               buf_c.data(), buf_c.size(), &d),
            false);
    TEST_EQ_STR(".inventory", d.c_str());
  }

  // Corrupted buffers must never compare equal (and must not crash):
  // verification happens before any comparison.
  {
    // Truncated buffer.
    auto truncated = buf_a;
    truncated.resize(truncated.size() - 8);
    std::string d;
    TEST_EQ(flatbuffers::Equal(schema, root, buf_a.data(), buf_a.size(),
                               truncated.data(), truncated.size(), &d),
            false);
    TEST_EQ_STR("<second buffer failed verification>", d.c_str());
  }
  {
    // Root table offset pointing outside the buffer.
    auto corrupt = buf_a;
    flatbuffers::WriteScalar(corrupt.data(),
                             static_cast<flatbuffers::uoffset_t>(0xFFFFFFF0));
    std::string d;
    TEST_EQ(flatbuffers::Equal(schema, root, corrupt.data(), corrupt.size(),
                               buf_a.data(), buf_a.size(), &d),
            false);
    TEST_EQ_STR("<first buffer failed verification>", d.c_str());
  }
  {
    // Corrupt the root offset to point at the very end of the buffer, where
    // no valid table can start.
    auto corrupt = buf_a;
    flatbuffers::WriteScalar(
        corrupt.data(),
        static_cast<flatbuffers::uoffset_t>(corrupt.size() - 2));
    TEST_EQ(flatbuffers::Equal(schema, root, buf_a.data(), buf_a.size(),
                               corrupt.data(), corrupt.size()),
            false);
  }

  // Size-prefixed buffers.
  {
    const auto sp_a = BuildEqualityMonster(spec, false, true);
    const auto sp_b = BuildEqualityMonster(spec, true, true);
    TEST_ASSERT(BuffersDifferBytewise(sp_a, sp_b));
    TEST_EQ(flatbuffers::EqualSizePrefixed(schema, root, sp_a.data(),
                                           sp_a.size(), sp_b.data(),
                                           sp_b.size()),
            true);
    EqualityMonsterSpec mod = spec;
    mod.hp = 81;
    const auto sp_c = BuildEqualityMonster(mod, false, true);
    std::string d;
    TEST_EQ(flatbuffers::EqualSizePrefixed(schema, root, sp_a.data(),
                                           sp_a.size(), sp_c.data(),
                                           sp_c.size(), &d),
            false);
    TEST_EQ_STR(".hp", d.c_str());
  }
}

void ReflectionEqualityUnionVectorTest(const std::string& tests_data_path) {
  // Build a reflection schema for union_vector.fbs at runtime.
  std::string schemafile;
  TEST_EQ(flatbuffers::LoadFile(
              (tests_data_path + "union_vector/union_vector.fbs").c_str(),
              false, &schemafile),
          true);
  flatbuffers::IDLOptions idl_opts;
  idl_opts.lang_to_generate |= flatbuffers::IDLOptions::kBinary;
  flatbuffers::Parser parser(idl_opts);
  TEST_EQ(parser.Parse(schemafile.c_str()), true);
  parser.Serialize();
  auto& schema = *reflection::GetSchema(parser.builder_.GetBufferPointer());
  auto& root = *schema.root_table();

  const auto movie_a = BuildEqualityMovie(false, /*sword_damage=*/5,
                                          /*belle_books=*/7);
  const auto movie_b = BuildEqualityMovie(true, /*sword_damage=*/5,
                                          /*belle_books=*/7);

  // Same logical contents, different physical layout.
  TEST_ASSERT(BuffersDifferBytewise(movie_a, movie_b));
  TEST_EQ(flatbuffers::Equal(schema, root, movie_a.data(), movie_a.size(),
                             movie_b.data(), movie_b.size()),
          true);

  // Change a table inside a vector of unions.
  {
    const auto movie_c = BuildEqualityMovie(false, /*sword_damage=*/6,
                                            /*belle_books=*/7);
    std::string d;
    TEST_EQ(flatbuffers::Equal(schema, root, movie_a.data(), movie_a.size(),
                               movie_c.data(), movie_c.size(), &d),
            false);
    TEST_EQ_STR(".characters[1].sword_attack_damage", d.c_str());
  }

  // Change a struct inside a vector of unions.
  {
    const auto movie_c = BuildEqualityMovie(false, /*sword_damage=*/5,
                                            /*belle_books=*/8);
    std::string d;
    TEST_EQ(flatbuffers::Equal(schema, root, movie_a.data(), movie_a.size(),
                               movie_c.data(), movie_c.size(), &d),
            false);
    TEST_EQ_STR(".characters[0].books_read", d.c_str());
  }
}

void ReflectionEqualityArrayTest(const std::string& tests_data_path) {
  // Load the binary schema for arrays_test.fbs.
  std::string bfbsfile;
  TEST_EQ(flatbuffers::LoadFile((tests_data_path + "arrays_test.bfbs").c_str(),
                                true, &bfbsfile),
          true);
  auto& schema = *reflection::GetSchema(bfbsfile.c_str());
  auto& root = *schema.root_table();

  const auto arr_a = BuildEqualityArrayTable(/*b3=*/42, /*d1_a0=*/7);
  const auto arr_b = BuildEqualityArrayTable(/*b3=*/42, /*d1_a0=*/7);
  TEST_EQ(flatbuffers::Equal(schema, root, arr_a.data(), arr_a.size(),
                             arr_b.data(), arr_b.size()),
          true);

  // Change one element of a fixed-length int array inside a struct.
  {
    const auto arr_c = BuildEqualityArrayTable(/*b3=*/43, /*d1_a0=*/7);
    std::string d;
    TEST_EQ(flatbuffers::Equal(schema, root, arr_a.data(), arr_a.size(),
                               arr_c.data(), arr_c.size(), &d),
            false);
    TEST_EQ_STR(".a.b[3]", d.c_str());
  }

  // Change one element of an int array inside a nested struct array.
  {
    const auto arr_c = BuildEqualityArrayTable(/*b3=*/42, /*d1_a0=*/8);
    std::string d;
    TEST_EQ(flatbuffers::Equal(schema, root, arr_a.data(), arr_a.size(),
                               arr_c.data(), arr_c.size(), &d),
            false);
    TEST_EQ_STR(".a.d[1].a[0]", d.c_str());
  }
}

}  // namespace tests
}  // namespace flatbuffers
