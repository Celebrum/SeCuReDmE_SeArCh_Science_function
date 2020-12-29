/*-------------------------------------------------------------------------
 * Copyright (C) 2020, 4paradigm
 * jit_wrapper_test.cc
 *
 * Author: chenjing
 * Date: 2020/3/12
 *--------------------------------------------------------------------------
 **/
#include "vm/jit_wrapper.h"
#include "codec/fe_row_codec.h"
#include "gtest/gtest.h"
#include "udf/udf.h"
#include "vm/engine.h"
#include "vm/simple_catalog.h"

namespace fesql {
namespace vm {

class JITWrapperTest : public ::testing::Test {};

std::shared_ptr<SimpleCatalog> GetTestCatalog() {
    fesql::type::Database db;
    db.set_name("db");
    ::fesql::type::TableDef *table = db.add_tables();
    table->set_name("t1");
    table->set_catalog("db");
    {
        ::fesql::type::ColumnDef *column = table->add_columns();
        column->set_type(::fesql::type::kDouble);
        column->set_name("col_1");
    }
    {
        ::fesql::type::ColumnDef *column = table->add_columns();
        column->set_type(::fesql::type::kInt64);
        column->set_name("col_2");
    }
    auto catalog = std::make_shared<SimpleCatalog>();
    catalog->AddDatabase(db);
    return catalog;
}

std::shared_ptr<CompileInfo> Compile(const std::string &sql,
                                     const EngineOptions &options,
                                     std::shared_ptr<SimpleCatalog> catalog) {
    base::Status status;
    BatchRunSession session;
    Engine engine(catalog, options);
    if (!engine.Get(sql, "db", session, status)) {
        LOG(WARNING) << "Fail to compile sql";
        return nullptr;
    }
    return session.GetCompileInfo();
}

void simple_test(const EngineOptions &options) {
    auto catalog = GetTestCatalog();
    std::string sql = "select col_1, col_2 from t1;";
    auto compile_info = Compile(sql, options, catalog);
    auto &sql_context = compile_info->get_sql_context();
    std::string ir_str = sql_context.ir;
    ASSERT_FALSE(ir_str.empty());
    FeSQLJITWrapper *jit = FeSQLJITWrapper::Create();
    ASSERT_TRUE(jit->Init());
    FeSQLJITWrapper::InitJITSymbols(jit);

    base::RawBuffer ir_buf(const_cast<char *>(ir_str.data()), ir_str.size());
    ASSERT_TRUE(jit->AddModuleFromBuffer(ir_buf));

    auto fn_name = sql_context.physical_plan->GetFnInfos()[0]->fn_name();
    auto fn = jit->FindFunction(fn_name);
    ASSERT_TRUE(fn != nullptr);

    int8_t buf[1024];
    auto schema = catalog->GetTable("db", "t1")->GetSchema();
    codec::RowBuilder row_builder(*schema);
    row_builder.SetBuffer(buf, 1024);
    row_builder.AppendDouble(3.14);
    row_builder.AppendInt64(42);

    fesql::codec::Row row(base::RefCountedSlice::Create(buf, 1024));
    fesql::codec::Row output = CoreAPI::RowProject(fn, row);
    codec::RowView row_view(*schema, output.buf(), output.size());
    double c1;
    int64_t c2;
    ASSERT_EQ(row_view.GetDouble(0, &c1), 0);
    ASSERT_EQ(row_view.GetInt64(1, &c2), 0);
    ASSERT_EQ(c1, 3.14);
    ASSERT_EQ(c2, 42);
    delete jit;
}

TEST_F(JITWrapperTest, test) {
    EngineOptions options;
    options.set_keep_ir(true);
    simple_test(options);
}

#ifdef LLVM_EXT_ENABLE
TEST_F(JITWrapperTest, test_mcjit) {
    EngineOptions options;
    options.set_keep_ir(true);
    options.jit_options().set_enable_mcjit(true);
    options.jit_options().set_enable_gdb(true);
    options.jit_options().set_enable_perf(true);
    options.jit_options().set_enable_vtune(true);
    simple_test(options);
}
#endif

TEST_F(JITWrapperTest, test_window) {
    EngineOptions options;
    options.set_keep_ir(true);
    options.set_performance_sensitive(false);
    auto catalog = GetTestCatalog();
    auto compile_info = Compile(
        "select col_1, sum(col_2) over w, "
        "distinct_count(col_2) over w "
        "from t1 "
        "window w as ("
        "PARTITION by col_2 ORDER BY col_2 "
        "ROWS BETWEEN 1 PRECEDING AND CURRENT ROW);",
        options, catalog);
    auto &sql_context = compile_info->get_sql_context();
    std::string ir_str = sql_context.ir;

    // clear this dict to ensure jit wrapper reinit all symbols
    // this should be removed by better symbol init utility

    ASSERT_FALSE(ir_str.empty());
    FeSQLJITWrapper *jit = FeSQLJITWrapper::Create();
    ASSERT_TRUE(jit->Init());
    FeSQLJITWrapper::InitJITSymbols(jit);

    base::RawBuffer ir_buf(const_cast<char *>(ir_str.data()), ir_str.size());
    ASSERT_TRUE(jit->AddModuleFromBuffer(ir_buf));

    auto fn_name = sql_context.physical_plan->GetFnInfos()[0]->fn_name();
    auto fn = jit->FindFunction(fn_name);
    ASSERT_TRUE(fn != nullptr);
    delete jit;
}

}  // namespace vm
}  // namespace fesql

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    fesql::vm::Engine::InitializeGlobalLLVM();
    return RUN_ALL_TESTS();
}
