// The Vita's run reports: one numbered file per boot, bsht_report_<NNN>.txt in
// ux0:data, the newest the highest number, so a crash's report survives the
// reboot that follows it.
// Headless: POSIX dirent, which vitasdk's newlib and MinGW both provide.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace app {

// "bsht_report_007.txt": three digits, wider past 999.
std::string ReportFileName(int index);

// N in "bsht_report_<N>.txt", or 0 for any other name (an older single
// bsht_report.txt, or a copy made by hand).
int ReportIndexOf(std::string_view fileName);

// One past the highest index among `names`, 1 when there is none: a gap is
// never refilled, so the newest report always has the highest number.
int NextReportIndex(const std::vector<std::string>& names);

// The entries of `dir`, not recursive, without "." and ".."; empty when it
// cannot be read.
std::vector<std::string> ListFileNames(const std::string& dir);

// `dir`/ReportFileName(NextReportIndex(ListFileNames(dir))).
std::string NextReportPath(const std::string& dir);

// This boot's report file, or "" when reports are off (no BSHT_REPORT switch
// file), so a player's memory card gets nothing.
std::string ReportPathFor(bool on, const std::string& dir);

}  // namespace app
