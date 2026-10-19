// Tests that read bsht-extract's output (assets/extracted, assets/baked) skip
// without it and name the file, so a fresh clone runs everything else. Where
// the data exists they run as usual.
#pragma once

#include <gtest/gtest.h>

#include <filesystem>

#define BSHT_NEEDS_FILE(path)                                                              \
    do {                                                                                   \
        if (!std::filesystem::exists(path))                                                \
            GTEST_SKIP() << "needs " << (path) << " (bsht-extract output)";                \
    } while (0)
