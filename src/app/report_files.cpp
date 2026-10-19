#include "app/report_files.h"

#include <dirent.h>

#include <algorithm>
#include <cstdio>

namespace app {

namespace {

constexpr std::string_view kPrefix = "bsht_report_";
constexpr std::string_view kSuffix = ".txt";

}  // namespace

std::string ReportFileName(int index) {
    char name[48];
    std::snprintf(name, sizeof name, "bsht_report_%03d.txt", index);
    return name;
}

int ReportIndexOf(std::string_view fileName) {
    if (fileName.size() <= kPrefix.size() + kSuffix.size()) return 0;
    if (fileName.substr(0, kPrefix.size()) != kPrefix) return 0;
    if (fileName.substr(fileName.size() - kSuffix.size()) != kSuffix) return 0;
    const std::string_view digits = fileName.substr(kPrefix.size(), fileName.size() - kPrefix.size() - kSuffix.size());
    if (digits.size() > 9) return 0;
    int index = 0;
    for (char c : digits) {
        if (c < '0' || c > '9') return 0;
        index = index * 10 + (c - '0');
    }
    return index;
}

int NextReportIndex(const std::vector<std::string>& names) {
    int highest = 0;
    for (const std::string& n : names) highest = std::max(highest, ReportIndexOf(n));
    return highest + 1;
}

std::vector<std::string> ListFileNames(const std::string& dir) {
    std::vector<std::string> names;
    DIR* d = opendir(dir.c_str());
    if (!d) return names;
    while (const dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name != "." && name != "..") names.push_back(name);
    }
    closedir(d);
    return names;
}

std::string NextReportPath(const std::string& dir) {
    return dir + "/" + ReportFileName(NextReportIndex(ListFileNames(dir)));
}

std::string ReportPathFor(bool on, const std::string& dir) {
    return on ? NextReportPath(dir) : std::string();
}

}  // namespace app
