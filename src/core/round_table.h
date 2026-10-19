// Round definitions and item pool weights.
//
// This data lives in no .gd file: it is Resource sub-resources inside
// scenes/main.tscn, extracted to JSON by tools/godot/export_round_table.gd.
//
// 15 rounds in 3 batches of 5. Starting health runs 2 -> 4 -> 6 and items
// dealt run 0 -> 2 -> 4 across the batches.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace core {

// Mirrors RoundClass.gd field-for-field.
struct RoundDef {
    // roundIndex is not unique across batches: batch 1 is [0,0,1,0,0] and
    // batch 2 is all zeros in the real data, so a round is identified by
    // (batch, position), never by this value alone.
    int  roundIndex = 0;

    // 0 means "carry over from the previous round" rather than "start dead".
    int  startingHealth = 0;

    int  amountLive  = 0;
    int  amountBlank = 0;
    bool usingItems = false;
    bool shufflingArray = false;
    bool insertingInRandomOrder = false;
    int  numberOfItemsToGrab = 0;
    bool showingIndicator = false;
    int  indicatorNumber = 0;
    bool sequenceHidden = false;
    bool bootingUpCounter = false;
    bool isFirstRound = false;
    bool hasIntroductoryText = false;
    bool hasIntro2 = false;

    int ShellCount() const { return amountLive + amountBlank; }
};

// Mirrors AmountResource.gd. amount_main is the base-game pool weight and
// amount_don the Double-or-Nothing weight. Four items carry amount_main == 0
// and are therefore DoN-exclusive: adrenaline, burner phone, expired medicine
// and inverter.
struct ItemPoolEntry {
    std::string itemName;
    int amount_active = 0;
    int amount_main = 0;
    int amount_don = 0;

    bool DoubleOrNothingOnly() const { return amount_main == 0 && amount_don > 0; }
};

struct BatchDef {
    int              batchIndex = 0;
    std::vector<int> roundIndices;
};

class RoundTable {
public:
    bool LoadFromFile(const char* path);
    bool LoadFromString(const char* json);

    std::size_t RoundCount() const { return rounds_.size(); }
    std::size_t BatchCount() const { return batches_.size(); }

    // Clamped: an out-of-range index yields the last round rather than reading
    // past the end. Sequences query this while a round is being torn down.
    const RoundDef& Round(std::size_t i) const;
    const BatchDef& Batch(std::size_t i) const;

    const std::vector<ItemPoolEntry>& Items() const { return items_; }

    // Pool weight for an item in the mode being played.
    int PoolWeight(const char* itemName, bool doubleOrNothing) const;

private:
    std::vector<RoundDef>      rounds_;
    std::vector<BatchDef>      batches_;
    std::vector<ItemPoolEntry> items_;
    RoundDef                   emptyRound_{};
    BatchDef                   emptyBatch_{};
};

}  // namespace core
