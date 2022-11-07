//===-- Locations.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Module/Locations.h"
#include "klee/Module/KModule.h"
#include "klee/Module/KInstruction.h"
#include "klee/Support/ErrorHandling.h"
#include "llvm/IR/Module.h"

#include <sstream>

using namespace klee;
using namespace llvm;


KInstruction *Location::initInstruction(KModule *module) {
  auto f = module->module->getFunction(function);
  if (!f)
    klee_error("Cannot resolve function %s in llvm bitcode.", function.c_str());
  auto kf = module->functionMap[f];
  if (offset >= kf->numInstructions)
    klee_error("Cannot get instruction %u in %s which has only %u instructions",
      offset, function.c_str(), kf->numInstructions);
  instruction = kf->instructions[offset];
  return instruction;
}

bool Location::isTheSameAsIn(KInstruction *instr) const {
  if (instruction)
    return instruction == instr;
  return instr->info->line == line;
}

bool isOSSeparator(char c) {
  return c == '/' || c == '\\';
}

bool Location::isInside(const FunctionInfo &info) const {
  size_t suffixSize = 0;
  int m = info.file.size() - 1, n = filename.size() - 1;
  for (;
       m >= 0 && n >= 0 && info.file[m] == filename[n];
       m--, n--) {
    suffixSize++;
    if (isOSSeparator(filename[n]))
      return true;
  }
  return suffixSize >= 3 && (n == -1 ? (m == -1 || isOSSeparator(info.file[m])) : (m == -1 && isOSSeparator(filename[n])));
}

std::string Location::toString() const {
  std::stringstream out;
  if (hasFunctionWithOffset())
    out << "instruction №" << offset << " in function " << function;
  else
    out << filename << ":" << line;;
  return out.str();
}

bool Location::isInside(KBlock *block) const {
  auto first = block->getFirstInstruction()->info->line;
  if (first > line)
    return false;
  auto last = block->getLastInstruction()->info->line;
  return line <= last; // and `first <= line` from above
}

std::string LocatedEvent::toString() const {
  return location.toString();
}

void PathForest::addSubTree(LocatedEvent * loc, PathForest *subTree) {
  layer.insert(std::make_pair(loc, subTree));
}

void PathForest::addLeaf(LocatedEvent * loc) {
  addSubTree(loc, new PathForest());
}

void PathForest::addTrace(std::vector<LocatedEvent *> *trace) {
  auto forest = this;
  for (auto event : *trace) {
    auto it = forest->layer.find(event);
    if (it == forest->layer.end()) {
      auto next = new PathForest();
      forest->layer.insert(std::make_pair(event, next));
      forest = next;
    } else {
      forest = it->second;
    }
  }
}

bool PathForest::empty() const {
  return layer.empty();
}

void PathForest::normalize() {
  for (auto &p : layer) {
    auto child = p.second;
    if (child == nullptr)
      child = new PathForest();
    if (!child->empty())
      continue;
    child->addLeaf(p.first);
  }
}

PathForest::~PathForest() {
  for (auto p : layer) {
    if (p.second != nullptr)
      delete p.second;
  }
}
