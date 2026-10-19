// bsht-extract's command line.
#pragma once

#include "extract/pipeline.h"

#include <string>
#include <vector>

namespace extract {

// ListExporters prints the exporter scripts a release must carry, one a line.
enum class Action { Run, Help, Fingerprint, ListExporters, Usage };

struct Parsed {
    Action action = Action::Usage;
    Options options;
    std::string fingerprintFile;
    std::string error;   // set for Usage when an argument was wrong
};

// args[0] is the program. `defaults` carries the paths found beside it.
Parsed ParseArguments(const std::vector<std::string>& args, const Options& defaults);

}  // namespace extract
