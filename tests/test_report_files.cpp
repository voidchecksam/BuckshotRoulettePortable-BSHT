#include "app/report_files.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

// Each boot writes its own numbered report, and the newest is always the
// highest number, so a crash's report survives the reboot that follows it.

TEST(ReportFiles, NamesAreNumberedWithThreeDigits) {
    EXPECT_EQ(app::ReportFileName(7), "bsht_report_007.txt");
    EXPECT_EQ(app::ReportFileName(123), "bsht_report_123.txt");
    EXPECT_EQ(app::ReportFileName(1234), "bsht_report_1234.txt");
}

TEST(ReportFiles, OnlyNumberedReportsCount) {
    EXPECT_EQ(app::ReportIndexOf("bsht_report_012.txt"), 12);
    EXPECT_EQ(app::ReportIndexOf("bsht_report_1234.txt"), 1234);
    EXPECT_EQ(app::ReportIndexOf("bsht_report.txt"), 0) << "the old single file";
    EXPECT_EQ(app::ReportIndexOf("bsht_report_good.txt"), 0) << "a copy kept by hand";
    EXPECT_EQ(app::ReportIndexOf("bsht_report_5.txt.bak"), 0);
    EXPECT_EQ(app::ReportIndexOf("bsht_report_.txt"), 0);
    EXPECT_EQ(app::ReportIndexOf("psp2core-1-eboot.bin.psp2dmp"), 0);
}

// One past the HIGHEST: a gap left by deleting a report is never
// refilled, so the newest report always has the highest number.
TEST(ReportFiles, TheNextIsOnePastTheHighest) {
    EXPECT_EQ(app::NextReportIndex({}), 1);
    EXPECT_EQ(app::NextReportIndex({"bsht_report_001.txt", "bsht_report_003.txt", "bsht_report.txt", "x.txt"}), 4);
}

TEST(ReportFiles, ConsecutiveBootsNeverShareAFile) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "bsht_report_files_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    EXPECT_TRUE(app::ListFileNames((dir / "missing").string()).empty());

    const std::string first = app::NextReportPath(dir.string());
    EXPECT_EQ(first, dir.string() + "/bsht_report_001.txt");   // "/" as ux0:data/ wants
    std::FILE* f = std::fopen(first.c_str(), "w");
    ASSERT_NE(f, nullptr);
    std::fclose(f);
    const std::vector<std::string> names = app::ListFileNames(dir.string());
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "bsht_report_001.txt");
    EXPECT_EQ(app::NextReportPath(dir.string()), dir.string() + "/bsht_report_002.txt");
    fs::remove_all(dir);
}

// A player's memory card stays clean: without the switch file there is no
// report path, and Report writes nothing to an empty path.
TEST(ReportFiles, OffMeansNoFile) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "bsht_report_off_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    EXPECT_TRUE(app::ReportPathFor(false, dir.string()).empty());
    EXPECT_TRUE(app::ListFileNames(dir.string()).empty());
    fs::remove_all(dir);
}

TEST(ReportFiles, OnNamesTheNextFile) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "bsht_report_on_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    EXPECT_EQ(app::ReportPathFor(true, dir.string()), dir.string() + "/bsht_report_001.txt");
    fs::remove_all(dir);
}
