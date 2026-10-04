#include "reflection_test.h"

#include "flatbuffers/idl.h"
#include "flatbuffers/minireflect.h"
#include "flatbuffers/reflection.h"
#include "flatbuffers/reflection_generated.h"
#include "flatbuffers/verifier.h"
#include "monster_test.h"
#include "monster_test_generated.h"
#include "test_assert.h"
#include "tests/arrays_test_generated.h"

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

void ReflectionVerifyReportTest(const std::string& tests_data_path) {
  // Load the binary schema for the monster type.
  std::string bfbsfile;
  TEST_EQ(flatbuffers::LoadFile((tests_data_path + "monster_test.bfbs").c_str(),
                                true, &bfbsfile),
          true);
  auto& schema = *reflection::GetSchema(bfbsfile.c_str());
  auto& root = *schema.root_table();

  // Build a valid buffer containing nested tables (which share one vtable),
  // a vector of strings, and a union.
  flatbuffers::FlatBufferBuilder fbb;
  auto child1_name = fbb.CreateString("ChildOne");
  auto child2_name = fbb.CreateString("ChildTwo");
  auto s0 = fbb.CreateString("alpha");
  auto s1 = fbb.CreateString("beta");
  auto name = fbb.CreateString("Reflecto");
  MonsterBuilder child1(fbb);
  child1.add_name(child1_name);
  child1.add_hp(80);
  auto child1_off = child1.Finish();
  MonsterBuilder child2(fbb);
  child2.add_name(child2_name);
  child2.add_hp(90);
  auto child2_off = child2.Finish();
  std::vector<flatbuffers::Offset<Monster>> children;
  children.push_back(child1_off);
  children.push_back(child2_off);
  auto children_vec = fbb.CreateVector(children);
  std::vector<flatbuffers::Offset<flatbuffers::String>> strings;
  strings.push_back(s0);
  strings.push_back(s1);
  auto strings_vec = fbb.CreateVector(strings);
  auto union_val = CreateTestSimpleTableWithEnum(fbb, Color_Red);
  MonsterBuilder monster(fbb);
  monster.add_name(name);
  monster.add_hp(100);
  monster.add_testarrayoftables(children_vec);
  monster.add_testarrayofstring(strings_vec);
  monster.add_test_type(Any_TestSimpleTableWithEnum);
  monster.add_test(union_val.Union());
  fbb.Finish(monster.Finish());
  std::vector<uint8_t> valid(fbb.GetBufferPointer(),
                             fbb.GetBufferPointer() + fbb.GetSize());

  // Locate the absolute buffer offset of a root table field.
  auto field_loc = [&](std::vector<uint8_t>& buf, const char* field_name,
                       size_t* loc) {
    auto table = flatbuffers::GetAnyRoot(buf.data());
    auto field = root.fields()->LookupByKey(field_name);
    TEST_NOTNULL(field);
    auto off = table->GetOptionalFieldOffset(field->offset());
    TEST_NE(off, 0);
    *loc = (reinterpret_cast<const uint8_t*>(table) - buf.data()) + off;
  };

  // The valid buffer passes, agrees with the boolean API, and the two
  // children really do share a single vtable.
  {
    auto report =
        flatbuffers::VerifyWithReport(schema, root, valid.data(), valid.size());
    TEST_EQ(report.ok, true);
    TEST_EQ(report.budget_exceeded, false);
    TEST_EQ(report.failure.category, flatbuffers::ReflectionVerifyError::kNone);
    TEST_EQ(flatbuffers::Verify(schema, root, valid.data(), valid.size()),
            true);
    auto monster_ptr = MyGame::Example::GetMonster(valid.data());
    // Compare vtable addresses of the two children (Table is a private
    // base in generated code, so compute them from the raw bytes).
    auto vtable_of = [](const void* t) {
      auto p = reinterpret_cast<const uint8_t*>(t);
      return p - flatbuffers::ReadScalar<flatbuffers::soffset_t>(p);
    };
    TEST_EQ(vtable_of(monster_ptr->testarrayoftables()->Get(0)),
            vtable_of(monster_ptr->testarrayoftables()->Get(1)));
  }

  // Corrupt an offset: the report must point at the corresponding field.
  {
    auto bad = valid;
    size_t loc = 0;
    field_loc(bad, "testarrayofstring", &loc);
    flatbuffers::WriteScalar(bad.data() + loc,
                             static_cast<flatbuffers::uoffset_t>(0x7FFFFF00));
    auto report =
        flatbuffers::VerifyWithReport(schema, root, bad.data(), bad.size());
    TEST_EQ(report.ok, false);
    TEST_EQ(report.budget_exceeded, false);
    TEST_EQ(report.failure.category,
            flatbuffers::ReflectionVerifyError::kInvalidOffset);
    TEST_EQ_STR(report.failure.path.c_str(), "testarrayofstring");
    TEST_EQ(report.failure.byte_offset, loc);
    TEST_EQ(report.failure.vector_index,
            flatbuffers::kReflectionVerifyNoVectorIndex);
    TEST_EQ(flatbuffers::Verify(schema, root, bad.data(), bad.size()), false);
    // The summary line mentions the field path and the error location.
    TEST_ASSERT(report.ToString().find("testarrayofstring") !=
                std::string::npos);
    TEST_ASSERT(report.ToString().find("invalid offset") != std::string::npos);

    // The corrupted-offset buffer must also be safely rejected for every
    // truncation of the input, without reading out of bounds.
    for (size_t cut = 0; cut <= bad.size(); cut++) {
      auto trunc_report =
          flatbuffers::VerifyWithReport(schema, root, bad.data(), cut);
      TEST_EQ(trunc_report.ok, false);
    }
  }

  // Corrupt a string terminator: the report must point at the string field.
  {
    auto bad = valid;
    size_t loc = 0;
    field_loc(bad, "name", &loc);
    const size_t str_off =
        loc + flatbuffers::ReadScalar<flatbuffers::uoffset_t>(bad.data() + loc);
    const size_t str_len = flatbuffers::ReadScalar<flatbuffers::uoffset_t>(
        bad.data() + str_off);
    const size_t term = str_off + sizeof(flatbuffers::uoffset_t) + str_len;
    TEST_EQ(bad[term], 0);
    bad[term] = 'X';
    auto report =
        flatbuffers::VerifyWithReport(schema, root, bad.data(), bad.size());
    TEST_EQ(report.ok, false);
    TEST_EQ(report.failure.category,
            flatbuffers::ReflectionVerifyError::kStringNotTerminated);
    TEST_EQ_STR(report.failure.path.c_str(), "name");
    TEST_EQ(report.failure.byte_offset, term);
    TEST_EQ(flatbuffers::Verify(schema, root, bad.data(), bad.size()), false);
  }

  // Corrupt a union discriminator: the report must point at the union field.
  {
    auto bad = valid;
    size_t loc = 0;
    field_loc(bad, "test_type", &loc);
    bad[loc] = 200;  // Not a member of the Any union.
    auto report =
        flatbuffers::VerifyWithReport(schema, root, bad.data(), bad.size());
    TEST_EQ(report.ok, false);
    TEST_EQ(report.failure.category,
            flatbuffers::ReflectionVerifyError::kUnionTypeMismatch);
    TEST_EQ_STR(report.failure.path.c_str(), "test");

    // A discriminator of NONE with a value still present is inconsistent.
    auto bad2 = valid;
    field_loc(bad2, "test_type", &loc);
    bad2[loc] = 0;
    auto report2 =
        flatbuffers::VerifyWithReport(schema, root, bad2.data(), bad2.size());
    TEST_EQ(report2.ok, false);
    TEST_EQ(report2.failure.category,
            flatbuffers::ReflectionVerifyError::kUnionTypeMismatch);
  }

  // A zero-length buffer is rejected without being read.
  {
    auto report = flatbuffers::VerifyWithReport(schema, root, nullptr, 0);
    TEST_EQ(report.ok, false);
    TEST_EQ(report.failure.category,
            flatbuffers::ReflectionVerifyError::kBufferTooSmall);
    TEST_EQ(report.failure.byte_offset, 0);
  }

  // Every truncation of the valid buffer is handled safely. Truncations
  // that remove referenced data are rejected; truncations that only remove
  // unreferenced trailing padding may still pass, but then the boolean
  // verifier must agree.
  for (size_t cut = 0; cut < valid.size(); cut++) {
    auto report =
        flatbuffers::VerifyWithReport(schema, root, valid.data(), cut);
    if (report.ok) {
      TEST_EQ(flatbuffers::Verify(schema, root, valid.data(), cut), true);
    } else {
      TEST_NE(report.failure.category,
              flatbuffers::ReflectionVerifyError::kNone);
    }
  }
  // Truncating into the last string's NUL terminator is rejected.
  {
    auto report = flatbuffers::VerifyWithReport(schema, root, valid.data(),
                                                valid.size() - 4);
    TEST_EQ(report.ok, false);
    TEST_EQ(report.failure.category,
            flatbuffers::ReflectionVerifyError::kOutOfBounds);
  }

  // A work budget that is too small stops verification early and is flagged
  // separately from a content failure; a generous budget passes.
  {
    flatbuffers::ReflectionVerifyOptions opts;
    opts.max_work = 3;
    auto report =
        flatbuffers::VerifyWithReport(schema, root, valid.data(), valid.size(),
                                      opts);
    TEST_EQ(report.ok, false);
    TEST_EQ(report.budget_exceeded, true);
    TEST_EQ(report.failure.category,
            flatbuffers::ReflectionVerifyError::kBudgetExceeded);
    TEST_ASSERT(report.ToString().find("budget") != std::string::npos);

    flatbuffers::ReflectionVerifyOptions generous;
    generous.max_work = 1000000;
    TEST_EQ(flatbuffers::VerifyWithReport(schema, root, valid.data(),
                                          valid.size(), generous)
                .ok,
            true);
  }

  // Size-prefixed buffers are supported too.
  {
    flatbuffers::FlatBufferBuilder sized_fbb;
    auto sized_name = sized_fbb.CreateString("Sized");
    MonsterBuilder sized(sized_fbb);
    sized.add_name(sized_name);
    sized.add_hp(10);
    sized_fbb.FinishSizePrefixed(sized.Finish());
    auto report = flatbuffers::VerifySizePrefixedWithReport(
        schema, root, sized_fbb.GetBufferPointer(), sized_fbb.GetSize());
    TEST_EQ(report.ok, true);
    TEST_EQ(report.ok, flatbuffers::VerifySizePrefixed(
                          schema, root, sized_fbb.GetBufferPointer(),
                          sized_fbb.GetSize()));
  }

  // A self-recursive schema for DAG / deep-nesting tests.
  flatbuffers::Parser parser;
  TEST_EQ(parser.Parse("table Node { a:Node; b:Node; child:Node; "
                       "name:string; } root_type Node;"),
          true);
  parser.Serialize();
  auto& node_schema = *reflection::GetSchema(parser.builder_.GetBufferPointer());
  auto& node_root = *node_schema.root_table();

  // Two fields referencing the same table (a DAG) must terminate and pass.
  {
    flatbuffers::FlatBufferBuilder dag_fbb;
    auto leaf_start = dag_fbb.StartTable();
    auto leaf = dag_fbb.EndTable(leaf_start);
    auto root_start = dag_fbb.StartTable();
    dag_fbb.AddOffset(flatbuffers::FieldIndexToOffset(0),
                      flatbuffers::Offset<void>(leaf));
    dag_fbb.AddOffset(flatbuffers::FieldIndexToOffset(1),
                      flatbuffers::Offset<void>(leaf));
    dag_fbb.Finish(flatbuffers::Offset<void>(dag_fbb.EndTable(root_start)));
    auto report = flatbuffers::VerifyWithReport(
        node_schema, node_root, dag_fbb.GetBufferPointer(), dag_fbb.GetSize());
    TEST_EQ(report.ok, true);
  }

  // Deep nesting beyond the depth limit is reported as such.
  {
    const int kDeep = 300;
    flatbuffers::FlatBufferBuilder deep_fbb;
    flatbuffers::uoffset_t prev = 0;
    for (int i = 0; i < kDeep; i++) {
      auto start = deep_fbb.StartTable();
      if (prev) {
        deep_fbb.AddOffset(flatbuffers::FieldIndexToOffset(2),
                           flatbuffers::Offset<void>(prev));
      }
      prev = deep_fbb.EndTable(start);
    }
    deep_fbb.Finish(flatbuffers::Offset<void>(prev));
    auto report = flatbuffers::VerifyWithReport(node_schema, node_root,
                                                deep_fbb.GetBufferPointer(),
                                                deep_fbb.GetSize());
    TEST_EQ(report.ok, false);
    TEST_EQ(report.failure.category,
            flatbuffers::ReflectionVerifyError::kDepthLimitExceeded);
    // The reported path is bounded.
    TEST_ASSERT(report.failure.path.size() < 10000);

    // With a raised depth limit the same buffer verifies fine.
    flatbuffers::ReflectionVerifyOptions deep_opts;
    deep_opts.max_depth = 1000;
    TEST_EQ(flatbuffers::VerifyWithReport(node_schema, node_root,
                                          deep_fbb.GetBufferPointer(),
                                          deep_fbb.GetSize(), deep_opts)
                .ok,
            true);
  }

  // A failure at the end of a very deep chain: the report path must stay
  // bounded (report construction itself obeys a work budget).
  {
    const int kVeryDeep = 5000;
    flatbuffers::FlatBufferBuilder chain_fbb;
    auto deep_name = chain_fbb.CreateString("deep");
    flatbuffers::uoffset_t prev = 0;
    for (int i = 0; i < kVeryDeep; i++) {
      auto start = chain_fbb.StartTable();
      if (prev) {
        chain_fbb.AddOffset(flatbuffers::FieldIndexToOffset(2),
                            flatbuffers::Offset<void>(prev));
      } else {
        chain_fbb.AddOffset(flatbuffers::FieldIndexToOffset(3), deep_name);
      }
      prev = chain_fbb.EndTable(start);
    }
    chain_fbb.Finish(flatbuffers::Offset<void>(prev));
    std::vector<uint8_t> chain(
        chain_fbb.GetBufferPointer(),
        chain_fbb.GetBufferPointer() + chain_fbb.GetSize());

    // Walk to the deepest node and corrupt its string terminator.
    auto child_field = node_root.fields()->LookupByKey("child");
    auto name_field = node_root.fields()->LookupByKey("name");
    const flatbuffers::Table* cur = flatbuffers::GetAnyRoot(chain.data());
    while (auto next = cur->GetPointer<const flatbuffers::Table*>(
               child_field->offset())) {
      cur = next;
    }
    auto str = cur->GetPointer<const flatbuffers::String*>(
        name_field->offset());
    TEST_NOTNULL(str);
    const size_t term = (reinterpret_cast<const uint8_t*>(str) -
                         chain.data()) +
                        sizeof(flatbuffers::uoffset_t) + str->size();
    chain[term] = 'Y';

    flatbuffers::ReflectionVerifyOptions opts;
    opts.max_depth = 10000;
    opts.max_tables = 100000;
    auto report = flatbuffers::VerifyWithReport(node_schema, node_root,
                                                chain.data(), chain.size(),
                                                opts);
    TEST_EQ(report.ok, false);
    TEST_EQ(report.failure.category,
            flatbuffers::ReflectionVerifyError::kStringNotTerminated);
    TEST_EQ(report.failure.byte_offset, term);
    // The path is capped even though the chain is 5000 tables deep.
    TEST_ASSERT(report.failure.path.size() < 128 * 80);
    TEST_ASSERT(report.failure.path.find("/...") != std::string::npos);
  }
}

}  // namespace tests
}  // namespace flatbuffers

