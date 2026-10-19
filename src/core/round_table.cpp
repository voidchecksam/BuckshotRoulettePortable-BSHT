#include "core/round_table.h"

#include "core/json_lite.h"

#include <cstdio>
#include <string>

namespace core {

namespace {

using core::json::FindArray;
using core::json::ReadBool;
using core::json::ReadInt;
using core::json::ReadString;
using core::json::SplitObjects;

}  // namespace

bool RoundTable::LoadFromString(const char* json) {
    rounds_.clear();
    batches_.clear();
    items_.clear();
    if (!json) return false;

    const char* begin = json;
    const char* end = json + std::strlen(json);

    // A document with no rounds array is not a round table.
    const char* rb = nullptr;
    const char* re = nullptr;
    if (!FindArray(begin, end, "rounds", &rb, &re)) return false;

    std::vector<core::json::Span> objs;
    SplitObjects(rb, re, objs);
    for (const auto& o : objs) {
        RoundDef r{};
        r.roundIndex             = ReadInt (o.first, o.second, "roundIndex", 0);
        r.startingHealth         = ReadInt (o.first, o.second, "startingHealth", 0);
        r.amountLive             = ReadInt (o.first, o.second, "amountLive", 0);
        r.amountBlank            = ReadInt (o.first, o.second, "amountBlank", 0);
        r.usingItems             = ReadBool(o.first, o.second, "usingItems", false);
        r.shufflingArray         = ReadBool(o.first, o.second, "shufflingArray", false);
        r.insertingInRandomOrder = ReadBool(o.first, o.second, "insertingInRandomOrder", false);
        r.numberOfItemsToGrab    = ReadInt (o.first, o.second, "numberOfItemsToGrab", 0);
        r.showingIndicator       = ReadBool(o.first, o.second, "showingIndicator", false);
        r.indicatorNumber        = ReadInt (o.first, o.second, "indicatorNumber", 0);
        r.sequenceHidden         = ReadBool(o.first, o.second, "sequenceHidden", false);
        r.bootingUpCounter       = ReadBool(o.first, o.second, "bootingUpCounter", false);
        r.isFirstRound           = ReadBool(o.first, o.second, "isFirstRound", false);
        r.hasIntroductoryText    = ReadBool(o.first, o.second, "hasIntroductoryText", false);
        r.hasIntro2              = ReadBool(o.first, o.second, "hasIntro2", false);
        rounds_.push_back(r);
    }
    if (rounds_.empty()) return false;

    const char* bb = nullptr;
    const char* be = nullptr;
    if (FindArray(begin, end, "batches", &bb, &be)) {
        objs.clear();
        SplitObjects(bb, be, objs);
        for (const auto& o : objs) {
            BatchDef b{};
            b.batchIndex = ReadInt(o.first, o.second, "batchIndex", 0);
            const char* ib = nullptr;
            const char* ie = nullptr;
            if (FindArray(o.first, o.second, "roundIndices", &ib, &ie)) {
                core::json::ReadIntArray(ib, ie, b.roundIndices);
            }
            batches_.push_back(b);
        }
    }

    const char* itb = nullptr;
    const char* ite = nullptr;
    if (FindArray(begin, end, "items", &itb, &ite)) {
        objs.clear();
        SplitObjects(itb, ite, objs);
        for (const auto& o : objs) {
            ItemPoolEntry e{};
            e.itemName      = ReadString(o.first, o.second, "itemName");
            e.amount_active = ReadInt(o.first, o.second, "amount_active", 0);
            e.amount_main   = ReadInt(o.first, o.second, "amount_main", 0);
            e.amount_don    = ReadInt(o.first, o.second, "amount_don", 0);
            if (!e.itemName.empty()) items_.push_back(e);
        }
    }

    return true;
}

bool RoundTable::LoadFromFile(const char* path) {
    if (!path) return false;
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;

    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) { std::fclose(f); return false; }

    std::string buf;
    buf.resize(static_cast<std::size_t>(n));
    const std::size_t got = std::fread(&buf[0], 1, static_cast<std::size_t>(n), f);
    std::fclose(f);
    buf.resize(got);

    return LoadFromString(buf.c_str());
}

const RoundDef& RoundTable::Round(std::size_t i) const {
    if (rounds_.empty()) return emptyRound_;
    if (i >= rounds_.size()) return rounds_.back();
    return rounds_[i];
}

const BatchDef& RoundTable::Batch(std::size_t i) const {
    if (batches_.empty()) return emptyBatch_;
    if (i >= batches_.size()) return batches_.back();
    return batches_[i];
}

int RoundTable::PoolWeight(const char* itemName, bool doubleOrNothing) const {
    if (!itemName) return 0;
    for (const ItemPoolEntry& e : items_) {
        if (e.itemName == itemName) {
            return doubleOrNothing ? e.amount_don : e.amount_main;
        }
    }
    return 0;
}

}  // namespace core
