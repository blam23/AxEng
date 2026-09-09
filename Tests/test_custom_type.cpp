#include "axenglib/custom_type.h"
#include <gtest/gtest.h>

using namespace ax::type;

TEST(TypeDef, Basic)
{
	TypeDef def{};
	def.field_map.emplace("test1", FieldDef{.start = 0, .size = 12});
	def.overall_size = 12;

	ASSERT_EQ(def.field_map.size(), 1);
	ASSERT_EQ(def.overall_size, 12);
	ASSERT_EQ(def.field_map["test1"].start, 0);
	ASSERT_EQ(def.field_map["test1"].size, 12);
}


TEST(PoolViewTest, GetFieldValidAndIndependent)
{
    TypeDef def;
    def.overall_size = 8;
    def.field_map["a"] = FieldDef{ 0, sizeof(uint32_t) };
    def.field_map["b"] = FieldDef{ 4, sizeof(uint32_t) };

    Pool pool(def, 2);
    PoolView view(pool);

    auto a0 = view.get_field<uint32_t>(0, "a");
    ASSERT_NE(a0, nullptr);
    auto b0 = view.get_field<uint32_t>(0, "b");
    ASSERT_NE(b0, nullptr);

    auto a1 = view.get_field<uint32_t>(1, "a");
    ASSERT_NE(a1, nullptr);
    auto b1 = view.get_field<uint32_t>(1, "b");
    ASSERT_NE(b1, nullptr);

    *a0 = 10u;
    *b0 = 20u;
    *a1 = 30u;
    *b1 = 40u;

    EXPECT_EQ(*a0, 10u);
    EXPECT_EQ(*b0, 20u);
    EXPECT_EQ(*a1, 30u);
    EXPECT_EQ(*b1, 40u);
}

TEST(PoolViewTest, GetFieldUnknownNameReturnsNull)
{
    TypeDef def;
    def.overall_size = 4;
    def.field_map["val"] = FieldDef{ 0, sizeof(uint32_t) };

    Pool pool(def, 1);
    PoolView view(pool);

    auto p = view.get_field<uint32_t>(0, "missing");
    EXPECT_EQ(p, nullptr);
}