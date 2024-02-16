#include "CodeLocation.h"

#include "klee/ADT/Ref.h"
#include "klee/Module/LocationInfo.h"
#include "klee/Module/SarifReport.h"

#include <cinttypes>
#include <optional>
#include <string>
#include <utility>

using namespace klee;

CodeLocation::CodeLocation(const Path::PathIndex &pathIndex,
                           const KValue *source,
                           const std::string &sourceFilename,
                           uint64_t sourceCodeLine,
                           std::optional<uint64_t> sourceCodeColumn)
    : pathIndex(pathIndex), source(source),
      location(LocationInfo{sourceFilename, sourceCodeLine, sourceCodeColumn}) {
}

ref<CodeLocation>
CodeLocation::create(const Path::PathIndex &pathIndex, const KValue *source,
                     const std::string &sourceFilename, uint64_t sourceCodeLine,
                     std::optional<uint64_t> sourceCodeColumn = std::nullopt) {
  return new CodeLocation(pathIndex, source, sourceFilename, sourceCodeLine,
                          sourceCodeColumn);
}

ref<CodeLocation>
CodeLocation::create(const KValue *source, const std::string &sourceFilename,
                     uint64_t sourceCodeLine,
                     std::optional<uint64_t> sourceCodeColumn = std::nullopt) {
  return new CodeLocation(Path::PathIndex{0, 0}, source, sourceFilename,
                          sourceCodeLine, sourceCodeColumn);
}

PhysicalLocationJson CodeLocation::serialize() const {
  return location.serialize();
}
