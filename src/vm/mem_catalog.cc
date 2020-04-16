/*-------------------------------------------------------------------------
 * Copyright (C) 2020, 4paradigm
 * mem_catalog.cc
 *
 * Author: chenjing
 * Date: 2020/3/25
 *--------------------------------------------------------------------------
 **/
#include "vm/mem_catalog.h"
#include <algorithm>
namespace fesql {
namespace vm {
MemSegmentIterator::MemSegmentIterator(const MemSegment* table,
                                       const vm::Schema* schema)
    : table_(table),
      schema_(schema),
      start_iter_(table->cbegin()),
      end_iter_(table->cend()),
      iter_(table->cbegin()) {}
MemSegmentIterator::MemSegmentIterator(const MemSegment* table,
                                       const vm::Schema* schema, int32_t start,
                                       int32_t end)
    : table_(table),
      schema_(schema),
      start_iter_(table_->begin() + start),
      end_iter_(table_->begin() + end),
      iter_(start_iter_) {}
MemSegmentIterator::~MemSegmentIterator() {}

// TODO(chenjing): speed up seek for memory iterator
void MemSegmentIterator::Seek(uint64_t ts) {
    iter_ = start_iter_;
    while (iter_ != end_iter_) {
        if (iter_->first <= ts) {
            return;
        }
        iter_++;
    }
}
void MemSegmentIterator::SeekToFirst() { iter_ = start_iter_; }
const uint64_t MemSegmentIterator::GetKey() { return iter_->first; }
const Row& fesql::vm::MemSegmentIterator::GetValue() { return iter_->second; }
void MemSegmentIterator::Next() { iter_++; }

bool MemSegmentIterator::Valid() { return end_iter_ != iter_; }

MemWindowIterator::MemWindowIterator(const MemSegmentMap* partitions,
                                     const Schema* schema)
    : WindowIterator(),
      partitions_(partitions),
      schema_(schema),
      iter_(partitions->cbegin()) {}

MemWindowIterator::~MemWindowIterator() {}

void MemWindowIterator::Seek(const std::string& key) {
    iter_ = partitions_->find(key);
}
void MemWindowIterator::SeekToFirst() { iter_ = partitions_->cbegin(); }
void MemWindowIterator::Next() { iter_++; }
bool MemWindowIterator::Valid() { return partitions_->cend() != iter_; }
std::unique_ptr<RowIterator> MemWindowIterator::GetValue() {
    std::unique_ptr<RowIterator> it = std::unique_ptr<RowIterator>(
        new MemSegmentIterator(&(iter_->second), schema_));
    return std::move(it);
}
const Row MemWindowIterator::GetKey() { return Row(iter_->first); }

MemSegmentHandler::MemSegmentHandler()
    : TableHandler(),
      table_name_(""),
      db_(""),
      schema_(nullptr),
      types_(),
      index_hint_(),
      table_() {}
MemSegmentHandler::MemSegmentHandler(const Schema* schema)
    : TableHandler(),
      table_name_(""),
      db_(""),
      schema_(schema),
      types_(),
      index_hint_(),
      table_() {}
MemSegmentHandler::MemSegmentHandler(const std::string& table_name,
                                     const std::string& db,
                                     const Schema* schema)
    : TableHandler(),
      table_name_(table_name),
      db_(db),
      schema_(schema),
      types_(),
      index_hint_(),
      table_() {}

MemSegmentHandler::~MemSegmentHandler() {}
std::unique_ptr<IteratorV<uint64_t, Row>> MemSegmentHandler::GetIterator()
    const {
    std::unique_ptr<MemSegmentIterator> it(
        new MemSegmentIterator(&table_, schema_));
    return std::move(it);
}
std::unique_ptr<WindowIterator> MemSegmentHandler::GetWindowIterator(
    const std::string& idx_name) {
    return std::unique_ptr<WindowIterator>();
}

void MemSegmentHandler::AddRow(const uint64_t key, const Row& row) {
    table_.push_back(std::make_pair(key, row));
}
void MemSegmentHandler::AddRow(const Row& row) {
    table_.push_back(std::make_pair(0, row));
}
const Types& MemSegmentHandler::GetTypes() { return types_; }

void MemSegmentHandler::Sort(const bool is_asc) {
    if (is_asc) {
        AscComparor comparor;
        std::sort(table_.begin(), table_.end(), comparor);
    } else {
        DescComparor comparor;
        std::sort(table_.begin(), table_.end(), comparor);
    }
}
void MemSegmentHandler::Reverse() {
    std::reverse(table_.begin(), table_.end());
}
IteratorV<uint64_t, Row>* MemSegmentHandler::GetIterator(int8_t* addr) const {
    if (nullptr == addr) {
        return new MemSegmentIterator(&table_, schema_);
    } else {
        return new (addr) MemSegmentIterator(&table_, schema_);
    }
}

MemPartitionHandler::MemPartitionHandler(const Schema* schema)
    : PartitionHandler(),
      table_name_(""),
      db_(""),
      schema_(schema),
      is_asc_(true) {}
MemPartitionHandler::MemPartitionHandler(const std::string& table_name,
                                         const std::string& db,
                                         const Schema* schema)
    : PartitionHandler(),
      table_name_(table_name),
      db_(db),
      schema_(schema),
      is_asc_(true) {}
MemPartitionHandler::~MemPartitionHandler() {}
const Schema* MemPartitionHandler::GetSchema() { return schema_; }
const std::string& MemPartitionHandler::GetName() { return table_name_; }
const std::string& MemPartitionHandler::GetDatabase() { return db_; }
const Types& MemPartitionHandler::GetTypes() { return types_; }
const IndexHint& MemPartitionHandler::GetIndex() { return index_hint_; }
bool MemPartitionHandler::AddRow(const std::string& key, uint64_t ts,
                                 const Row& row) {
    auto iter = partitions_.find(key);
    if (iter == partitions_.cend()) {
        partitions_.insert(std::pair<std::string, MemSegment>(
            key, {std::make_pair(ts, Row(row.data(), row.size()))}));
    } else {
        iter->second.push_back(std::make_pair(ts, Row(row.data(), row.size())));
    }
    return true;
}
std::unique_ptr<WindowIterator> MemPartitionHandler::GetWindowIterator() {
    std::unique_ptr<WindowIterator> it = std::unique_ptr<WindowIterator>(
        new MemWindowIterator(&partitions_, schema_));
    return std::move(it);
}
void MemPartitionHandler::Sort(const bool is_asc) {
    if (is_asc) {
        AscComparor comparor;
        for (auto& segment : partitions_) {
            std::sort(segment.second.begin(), segment.second.end(), comparor);
        }
    } else {
        DescComparor comparor;
        for (auto& segment : partitions_) {
            std::sort(segment.second.begin(), segment.second.end(), comparor);
        }
    }
}
void MemPartitionHandler::Reverse() {
    is_asc_ = !is_asc_;
    for (auto& segment : partitions_) {
        std::reverse(segment.second.begin(), segment.second.end());
    }
}
const bool MemPartitionHandler::IsAsc() { return is_asc_; }
void MemPartitionHandler::Print() {
    for (auto iter = partitions_.cbegin(); iter != partitions_.cend(); iter++) {
        std::cout << iter->first << ":";
        for (auto segment_iter = iter->second.cbegin();
             segment_iter != iter->second.cend(); segment_iter++) {
            std::cout << segment_iter->first << ",";
        }
        std::cout << std::endl;
    }
}

std::unique_ptr<WindowIterator> MemTableHandler::GetWindowIterator(
    const std::string& idx_name) {
    return std::unique_ptr<WindowIterator>();
}
std::unique_ptr<IteratorV<uint64_t, Row>> MemTableHandler::GetIterator() const {
    std::unique_ptr<MemTableIterator> it(
        new MemTableIterator(&table_, schema_));
    return std::move(it);
}
IteratorV<uint64_t, Row>* MemTableHandler::GetIterator(int8_t* addr) const {
    return new (addr) MemTableIterator(&table_, schema_);
}

MemTableHandler::MemTableHandler()
    : TableHandler(),
      table_name_(""),
      db_(""),
      schema_(nullptr),
      types_(),
      index_hint_(),
      table_(),
      other_tables_() {}
MemTableHandler::MemTableHandler(const Schema* schema)
    : TableHandler(),
      table_name_(""),
      db_(""),
      schema_(schema),
      types_(),
      index_hint_(),
      table_(),
      other_tables_() {}
MemTableHandler::MemTableHandler(const std::string& table_name,
                                 const std::string& db, const Schema* schema)
    : TableHandler(),
      table_name_(table_name),
      db_(db),
      schema_(schema),
      types_(),
      index_hint_(),
      table_(),
      other_tables_() {}
void MemTableHandler::AddRow(const Row& row) { table_.push_back(row); }
void MemTableHandler::Reverse() { std::reverse(table_.begin(), table_.end()); }
MemTableHandler::~MemTableHandler() {}
MemTableIterator::MemTableIterator(const MemTable* table,
                                   const vm::Schema* schema)
    : table_(table),
      schema_(schema),
      start_iter_(table->cbegin()),
      end_iter_(table->cend()),
      iter_(table->cbegin()) {}
MemTableIterator::MemTableIterator(const MemTable* table,
                                   const vm::Schema* schema, int32_t start,
                                   int32_t end)
    : table_(table),
      schema_(schema),
      start_iter_(table_->begin() + start),
      end_iter_(table_->begin() + end),
      iter_(start_iter_) {}
MemTableIterator::~MemTableIterator() {}
void MemTableIterator::Seek(uint64_t ts) { iter_ = start_iter_ + ts; }
void MemTableIterator::SeekToFirst() { iter_ = start_iter_; }
const uint64_t MemTableIterator::GetKey() {
    return Valid() ? iter_ - start_iter_ : -1;
}
bool MemTableIterator::Valid() { return end_iter_ != iter_; }
void MemTableIterator::Next() { iter_++; }
const Row& MemTableIterator::GetValue() { return *iter_; }

}  // namespace vm
}  // namespace fesql
