////===-- LocationInfo.h ----------------------------------*- C++ -*-===//
////
////                     The KLEE Symbolic Virtual Machine
////
//// This file is distributed under the University of Illinois Open Source
//// License. See LICENSE.TXT for details.
////
////===----------------------------------------------------------------------===//

#ifndef KLEE_LOCATIONINFO_H
#define KLEE_LOCATIONINFO_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace llvm {
class Function;
class GlobalVariable;
class Instruction;
class Module;
} // namespace llvm

namespace klee {
struct PhysicalLocationJson;
}

namespace klee {

/// @brief Immutable struct representing location in source code.
struct LocationInfo {
  /// @brief Path to source file for that location.
  const std::string file;

  /// @brief Code line in source file.
  const uint64_t line;

  /// @brief Column number in source file.
  const std::optional<uint64_t> column;

  /// @brief Converts location info to SARIFs representation
  /// of location.
  /// @param location location info in source code.
  /// @return SARIFs representation of location.
  PhysicalLocationJson serialize() const;
};

LocationInfo getLocationInfo(const llvm::Function *func);
LocationInfo getLocationInfo(const llvm::Instruction *inst);
LocationInfo getLocationInfo(const llvm::GlobalVariable *global);

} // namespace klee

#endif /* KLEE_LOCATIONINFO_H */
