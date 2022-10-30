//===-- ExecutionState.cpp ------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ExecutionState.h"

#include "Memory.h"

#include "klee/Expr/Expr.h"
#include "klee/Expr/ExprHashMap.h"
#include "klee/Expr/ExprUtil.h"
#include "klee/Module/Cell.h"
#include "klee/Module/InstructionInfoTable.h"
#include "klee/Module/KInstruction.h"
#include "klee/Module/KModule.h"
#include "klee/Support/Casting.h"
#include "klee/Support/OptionCategories.h"

#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdarg.h>
#include <string>

using namespace llvm;
using namespace klee;

namespace {
cl::opt<bool> DebugLogStateMerge(
    "debug-log-state-merge", cl::init(false),
    cl::desc("Debug information for underlying state merging (default=false)"),
    cl::cat(MergeCat));
}

/***/

std::uint32_t ExecutionState::nextID = 1;

/***/

StackFrame::StackFrame(KInstIterator _caller, KFunction *_kf)
  : caller(_caller), kf(_kf), callPathNode(0), 
    minDistToUncoveredOnReturn(0), varargs(0) {
  locals = new Cell[kf->numRegisters];
}

StackFrame::StackFrame(const StackFrame &s) 
  : caller(s.caller),
    kf(s.kf),
    callPathNode(s.callPathNode),
    allocas(s.allocas),
    minDistToUncoveredOnReturn(s.minDistToUncoveredOnReturn),
    varargs(s.varargs) {
  locals = new Cell[s.kf->numRegisters];
  for (unsigned i=0; i<s.kf->numRegisters; i++)
    locals[i] = s.locals[i];
}

StackFrame::~StackFrame() { 
  delete[] locals; 
}

/***/

ExecutionState::ExecutionState(KFunction *kf) :
    initPC(nullptr),
    pc(nullptr),
    prevPC(nullptr),
    incomingBBIndex(-1),
    depth(0),
    ptreeNode(nullptr),
    steppedInstructions(0),
    steppedMemoryInstructions(0),
    instsSinceCovNew(0),
    coveredNew(false),
    forkDisabled(false),
    target(nullptr) {
  pushFrame(nullptr, kf);
  setID();
}

ExecutionState::ExecutionState(KFunction *kf, KBlock *kb) :
    initPC(kb->instructions),
    pc(initPC),
    prevPC(pc),
    incomingBBIndex(-1),
    depth(0),
    ptreeNode(nullptr),
    steppedInstructions(0),
    steppedMemoryInstructions(0),
    instsSinceCovNew(0),
    roundingMode(llvm::APFloat::rmNearestTiesToEven),
    coveredNew(false),
    forkDisabled(false),
    target(nullptr) {
  pushFrame(nullptr, kf);
  setID();
}

ExecutionState::~ExecutionState() {
  for (const auto &cur_mergehandler: openMergeStack){
    cur_mergehandler->removeOpenState(this);
  }

  while (!stack.empty()) popFrame();
}

ExecutionState::ExecutionState(const ExecutionState& state):
    initPC(state.initPC),
    pc(state.pc),
    prevPC(state.prevPC),
    stack(state.stack),
    incomingBBIndex(state.incomingBBIndex),
    depth(state.depth),
    multilevel(state.multilevel),
    level(state.level),
    addressSpace(state.addressSpace),
    constraints(state.constraints),
    constraintsWithSymcretes(state.constraintsWithSymcretes),
    pathOS(state.pathOS),
    symPathOS(state.symPathOS),
    coveredLines(state.coveredLines),
    symbolics(state.symbolics),
    cexPreferences(state.cexPreferences),
    arrayNames(state.arrayNames),
    symcretes(state.symcretes),
    symcreteToConstraints(state.symcreteToConstraints),
    symsizesToMO(state.symsizesToMO),
    symSizes(state.symSizes),
    symAddresses(state.symAddresses),
    openMergeStack(state.openMergeStack),
    steppedInstructions(state.steppedInstructions),
    steppedMemoryInstructions(state.steppedMemoryInstructions),
    instsSinceCovNew(state.instsSinceCovNew),
    roundingMode(state.roundingMode),
    unwindingInformation(state.unwindingInformation
                             ? state.unwindingInformation->clone()
                             : nullptr),
    coveredNew(state.coveredNew),
    forkDisabled(state.forkDisabled),
    target(state.target) {
  for (const auto &cur_mergehandler: openMergeStack)
    cur_mergehandler->addOpenState(this);
}

ExecutionState *ExecutionState::branch() {
  depth++;

  auto *falseState = new ExecutionState(*this);
  falseState->setID();
  falseState->coveredNew = false;
  falseState->coveredLines.clear();

  return falseState;
}

ExecutionState *ExecutionState::withKFunction(KFunction *kf) {
  ExecutionState *newState = new ExecutionState(*this);
  newState->setID();
  newState->pushFrame(nullptr, kf);
  newState->initPC = kf->blockMap[&*kf->function->begin()]->instructions;
  newState->pc = newState->initPC;
  newState->prevPC = newState->pc;
  return newState;
}

ExecutionState *ExecutionState::withStackFrame(KFunction *kf) {
  ExecutionState *newState = new ExecutionState(*this);
  newState->setID();
  newState->pushFrame(nullptr, kf);
  return newState;
}

ExecutionState *ExecutionState::withKBlock(KBlock *kb) {
  ExecutionState *newState = new ExecutionState(*this);
  newState->setID();
  newState->initPC = kb->instructions;
  newState->pc = newState->initPC;
  newState->prevPC = newState->pc;
  return newState;
}

ExecutionState *ExecutionState::copy() {
  ExecutionState *newState = new ExecutionState(*this);
  newState->setID();
  return newState;
}

void ExecutionState::pushFrame(KInstIterator caller, KFunction *kf) {
  stack.emplace_back(StackFrame(caller, kf));
}

void ExecutionState::popFrame() {
  const StackFrame &sf = stack.back();
  for (const auto * memoryObject : sf.allocas)
    addressSpace.unbindObject(memoryObject);
  stack.pop_back();
}

void ExecutionState::addSymbolic(const MemoryObject *mo, const Array *array) {
  symbolics.emplace_back(ref<const MemoryObject>(mo), array);
}

void ExecutionState::addSymSize(MemoryObject *mo, const Array *array) {
  symsizesToMO[array] = ref<MemoryObject>(mo);
  symSizes.emplace(mo, array);
}

void ExecutionState::addSymAddress(MemoryObject *mo, const Array *array) {
  symAddresses.emplace(mo, array);
}


ref<const MemoryObject>
ExecutionState::findMemoryObject(const Array *array) const {
  for (unsigned i = 0; i != symbolics.size(); ++i) {
    const auto &symbolic = symbolics[i];
    if (array == symbolic.second) {
      return symbolic.first;
    }
  }
  return nullptr;
}


// FIXME: linear search
const Array *ExecutionState::findSymbolicArray(ref<const MemoryObject> mo) const {
  for (unsigned i = 0; i != symbolics.size(); ++i) {
    const auto &symbolic = symbolics[i];
    if (mo == symbolic.first) {
      return symbolic.second;
    }
  }
  return nullptr;
}


bool ExecutionState::isSymcrete(const Array *array) {
  return symcretes.bindings.count(array);
}

void ExecutionState::addSymcrete(
    const Array *array, const std::vector<unsigned char> &concretisation, uint64_t value) {
  assert(array && array->isSymbolicArray() &&
         "Cannot make concrete array symcrete");
  assert(isa<ConstantExpr>(array->getSize()) &&
         "Attempted to concretize object with symbolic size");
  assert(cast<ConstantExpr>(array->getSize())->getZExtValue() ==
             concretisation.size() &&
         "Given concretisation does not fit the array");
  assert(!isSymcrete(array) && "Array already symcrete");

  symcretes.bindings[array] = concretisation;
  ConstraintManager cs(constraintsWithSymcretes);

  ref<Expr> eqSymcreteExpr = EqExpr::create(
      ReadExpr::createTempRead(array, Context::get().getPointerWidth()),
      Expr::createPointer(value));

  std::vector<const Array *> arrays;
  findSymbolicObjects(eqSymcreteExpr, arrays);
  ref<Expr> evaluatedConstraint = cs.addConstraint(eqSymcreteExpr);
  
  for (const auto *array : arrays) {
    if (isSymcrete(array)) {
      symcreteToConstraints[evaluatedConstraint].insert(array);
    }
  }
}


static std::vector<unsigned char> addressToBytes(uint64_t value) {
  unsigned char *addressBytesIterator =
      reinterpret_cast<unsigned char *>(&value);
  std::vector<unsigned char> result(addressBytesIterator, addressBytesIterator + sizeof(value));
  std::reverse(result.begin(), result.end());
  return result;
}

static uint64_t bytesToAddress(const std::vector<unsigned char> &concretization) {
  uint64_t value = 0;
  assert(concretization.size() == Context::get().getPointerWidth() / CHAR_BIT &&
          "Symcrete must be a 64-bit value");
  for (unsigned bit = 0; bit < concretization.size(); ++bit) {
    value |= (concretization[bit] << (CHAR_BIT * bit));
  }
  return value;
} 

void ExecutionState::updateSymcretes(const Assignment &assignment) {
  constraintsWithSymcretes = ConstraintSet();
  ConstraintManager cs(constraintsWithSymcretes);

  Assignment copy = symcretes;
  for (const auto &assign : assignment.bindings) {
    copy.bindings[assign.first] = assign.second;
    assert(symsizesToMO.count(assign.first));
    assert(symAddresses.count(symsizesToMO.at(assign.first).get()));
    copy.bindings.erase(symAddresses.at(symsizesToMO.at(assign.first).get()));
  }

  symcretes.bindings.clear();
  for (const auto &assign : copy.bindings) {
    const std::vector<unsigned char> &concretization = assign.second;
    addSymcrete(assign.first, concretization, bytesToAddress(concretization));
  }

  for (const auto &constraint: constraints) {
    cs.addConstraint(evaluateWithSymcretes(constraint));
  }

  for (auto &symsizeToConcrete : assignment.bindings) {
    MemoryObject *mo = symsizesToMO.at(symsizeToConcrete.first).get();
    ObjectState *os = const_cast<ObjectState *>(addressSpace.findObject(mo));
    if (ref<ConstantExpr> CE = dyn_cast<ConstantExpr>(
            evaluateWithSymcretes(mo->getSizeExpr()))) {
      uint64_t oldSize = mo->size;
      if (CE->getZExtValue() <= oldSize) {
        addSymcrete(symAddresses[mo], addressToBytes(mo->address), mo->address);
        continue;
      }

      uint64_t newSize =
          std::max(2 * static_cast<uint64_t>(mo->size), CE->getZExtValue());
      assert(mo->parent);
      MemoryObject *newMO = mo->parent->allocate(newSize, mo->isLocal, /*isGlobal=*/mo->isGlobal, mo->allocSite,
                          /* FIXME: allocation alignment should be saved in MO */ 8, mo->addressExpr, mo->sizeExpr);

      const Array *oldArray = findSymbolicArray(mo);
      ObjectState *newOS =
          oldArray ? new ObjectState(newMO, oldArray, os->getDynamicType())
                   : new ObjectState(newMO, os->getDynamicType());
      addressSpace.bindObject(newMO, newOS);
      
      /// Truncated size required if we have written 
      for (unsigned i = 0; i < oldSize; i++) {
        os->write(i, os->read8(i));
      }

      addSymcrete(symAddresses[mo], addressToBytes(newMO->address), newMO->address);
      
      addSymAddress(newMO, symAddresses.at(mo));
      addSymSize(newMO, symSizes.at(mo));

      addressSpace.unbindObject(mo);
    } else {
      assert(0 && "Size does not have concretization (updateSymcretes)!");
    }
  }
}


ref<Expr> ExecutionState::evaluateWithSymcretes(const ref<Expr> e) const {
  return symcretes.evaluate(e);
}

ConstraintSet ExecutionState::evaluateConstraintsWithSymcretes() const {
  return constraintsWithSymcretes;
}

/**/

llvm::raw_ostream &klee::operator<<(llvm::raw_ostream &os, const MemoryMap &mm) {
  os << "{";
  MemoryMap::iterator it = mm.begin();
  MemoryMap::iterator ie = mm.end();
  if (it!=ie) {
    os << "MO" << it->first->id << ":" << it->second.get();
    for (++it; it!=ie; ++it)
      os << ", MO" << it->first->id << ":" << it->second.get();
  }
  os << "}";
  return os;
}

bool ExecutionState::merge(const ExecutionState &b) {
  if (DebugLogStateMerge)
    llvm::errs() << "-- attempting merge of A:" << this << " with B:" << &b
                 << "--\n";
  if (pc != b.pc)
    return false;

  // XXX is it even possible for these to differ? does it matter? probably
  // implies difference in object states?

  if (symbolics != b.symbolics)
    return false;

  {
    std::vector<StackFrame>::const_iterator itA = stack.begin();
    std::vector<StackFrame>::const_iterator itB = b.stack.begin();
    while (itA!=stack.end() && itB!=b.stack.end()) {
      // XXX vaargs?
      if (itA->caller!=itB->caller || itA->kf!=itB->kf)
        return false;
      ++itA;
      ++itB;
    }
    if (itA!=stack.end() || itB!=b.stack.end())
      return false;
  }

  std::set< ref<Expr> > aConstraints(constraints.begin(), constraints.end());
  std::set< ref<Expr> > bConstraints(b.constraints.begin(), 
                                     b.constraints.end());
  std::set< ref<Expr> > commonConstraints, aSuffix, bSuffix;
  std::set_intersection(aConstraints.begin(), aConstraints.end(),
                        bConstraints.begin(), bConstraints.end(),
                        std::inserter(commonConstraints, commonConstraints.begin()));
  std::set_difference(aConstraints.begin(), aConstraints.end(),
                      commonConstraints.begin(), commonConstraints.end(),
                      std::inserter(aSuffix, aSuffix.end()));
  std::set_difference(bConstraints.begin(), bConstraints.end(),
                      commonConstraints.begin(), commonConstraints.end(),
                      std::inserter(bSuffix, bSuffix.end()));
  if (DebugLogStateMerge) {
    llvm::errs() << "\tconstraint prefix: [";
    for (std::set<ref<Expr> >::iterator it = commonConstraints.begin(),
                                        ie = commonConstraints.end();
         it != ie; ++it)
      llvm::errs() << *it << ", ";
    llvm::errs() << "]\n";
    llvm::errs() << "\tA suffix: [";
    for (std::set<ref<Expr> >::iterator it = aSuffix.begin(),
                                        ie = aSuffix.end();
         it != ie; ++it)
      llvm::errs() << *it << ", ";
    llvm::errs() << "]\n";
    llvm::errs() << "\tB suffix: [";
    for (std::set<ref<Expr> >::iterator it = bSuffix.begin(),
                                        ie = bSuffix.end();
         it != ie; ++it)
      llvm::errs() << *it << ", ";
    llvm::errs() << "]\n";
  }

  // We cannot merge if addresses would resolve differently in the
  // states. This means:
  // 
  // 1. Any objects created since the branch in either object must
  // have been free'd.
  //
  // 2. We cannot have free'd any pre-existing object in one state
  // and not the other

  if (DebugLogStateMerge) {
    llvm::errs() << "\tchecking object states\n";
    llvm::errs() << "A: " << addressSpace.objects << "\n";
    llvm::errs() << "B: " << b.addressSpace.objects << "\n";
  }
    
  std::set<const MemoryObject*> mutated;
  MemoryMap::iterator ai = addressSpace.objects.begin();
  MemoryMap::iterator bi = b.addressSpace.objects.begin();
  MemoryMap::iterator ae = addressSpace.objects.end();
  MemoryMap::iterator be = b.addressSpace.objects.end();
  for (; ai!=ae && bi!=be; ++ai, ++bi) {
    if (ai->first != bi->first) {
      if (DebugLogStateMerge) {
        if (ai->first < bi->first) {
          llvm::errs() << "\t\tB misses binding for: " << ai->first->id << "\n";
        } else {
          llvm::errs() << "\t\tA misses binding for: " << bi->first->id << "\n";
        }
      }
      return false;
    }
    if (ai->second.get() != bi->second.get()) {
      if (DebugLogStateMerge)
        llvm::errs() << "\t\tmutated: " << ai->first->id << "\n";
      mutated.insert(ai->first);
    }
  }
  if (ai!=ae || bi!=be) {
    if (DebugLogStateMerge)
      llvm::errs() << "\t\tmappings differ\n";
    return false;
  }
  
  // merge stack

  ref<Expr> inA = ConstantExpr::alloc(1, Expr::Bool);
  ref<Expr> inB = ConstantExpr::alloc(1, Expr::Bool);
  for (std::set< ref<Expr> >::iterator it = aSuffix.begin(), 
         ie = aSuffix.end(); it != ie; ++it)
    inA = AndExpr::create(inA, *it);
  for (std::set< ref<Expr> >::iterator it = bSuffix.begin(), 
         ie = bSuffix.end(); it != ie; ++it)
    inB = AndExpr::create(inB, *it);

  // XXX should we have a preference as to which predicate to use?
  // it seems like it can make a difference, even though logically
  // they must contradict each other and so inA => !inB

  std::vector<StackFrame>::iterator itA = stack.begin();
  std::vector<StackFrame>::const_iterator itB = b.stack.begin();
  for (; itA!=stack.end(); ++itA, ++itB) {
    StackFrame &af = *itA;
    const StackFrame &bf = *itB;
    for (unsigned i=0; i<af.kf->numRegisters; i++) {
      ref<Expr> &av = af.locals[i].value;
      const ref<Expr> &bv = bf.locals[i].value;
      if (!av || !bv) {
        // if one is null then by implication (we are at same pc)
        // we cannot reuse this local, so just ignore
      } else {
        av = SelectExpr::create(inA, av, bv);
      }
    }
  }

  for (std::set<const MemoryObject*>::iterator it = mutated.begin(), 
         ie = mutated.end(); it != ie; ++it) {
    const MemoryObject *mo = *it;
    const ObjectState *os = addressSpace.findObject(mo);
    const ObjectState *otherOS = b.addressSpace.findObject(mo);
    assert(os && !os->readOnly && 
           "objects mutated but not writable in merging state");
    assert(otherOS);

    ObjectState *wos = addressSpace.getWriteable(mo, os);

    /// We can use mo->size here instead of wos->size as
    /// realloc should be completed for replacing Memory Objects.
    /// Nethertheless, it is not optimal.
    for (unsigned i=0; i<mo->size; i++) {
      ref<Expr> av = wos->read8(i);
      ref<Expr> bv = otherOS->read8(i);
      wos->write(i, SelectExpr::create(inA, av, bv));
    }
  }

  constraints = ConstraintSet();

  ConstraintManager m(constraints);
  for (const auto &constraint : commonConstraints)
    m.addConstraint(constraint);
  m.addConstraint(OrExpr::create(inA, inB));

  return true;
}

void ExecutionState::dumpStack(llvm::raw_ostream &out) const {
  unsigned idx = 0;
  const KInstruction *target = prevPC;
  for (ExecutionState::stack_ty::const_reverse_iterator
         it = stack.rbegin(), ie = stack.rend();
       it != ie; ++it) {
    const StackFrame &sf = *it;
    Function *f = sf.kf->function;
    const InstructionInfo &ii = *target->info;
    out << "\t#" << idx++;
    std::stringstream AssStream;
    AssStream << std::setw(8) << std::setfill('0') << ii.assemblyLine;
    out << AssStream.str();
    out << " in " << f->getName().str() << " (";
    // Yawn, we could go up and print varargs if we wanted to.
    unsigned index = 0;
    for (Function::arg_iterator ai = f->arg_begin(), ae = f->arg_end();
         ai != ae; ++ai) {
      if (ai!=f->arg_begin()) out << ", ";

      out << ai->getName().str();
      // XXX should go through function
      ref<Expr> value = sf.locals[sf.kf->getArgRegister(index++)].value;
      if (isa_and_nonnull<ConstantExpr>(value))
        out << "=" << value;
    }
    out << ")";
    if (ii.file != "")
      out << " at " << ii.file << ":" << ii.line;
    out << "\n";
    target = sf.caller;
  }
}

void ExecutionState::addConstraint(ref<Expr> e) {
  ConstraintManager c(constraints);
  ConstraintManager cs(constraintsWithSymcretes);
  
  std::vector<const Array *> arrays;
  findSymbolicObjects(e, arrays);
  c.addConstraint(e);
  ref<Expr> evaluatedConstraint = cs.addConstraint(evaluateWithSymcretes(e));
  
  for (const auto *array : arrays) {
    if (isSymcrete(array)) {
      symcreteToConstraints[evaluatedConstraint].insert(array);
    }
  }
}

void ExecutionState::addCexPreference(const ref<Expr> &cond) {
  cexPreferences = cexPreferences.insert(cond);
}

BasicBlock *ExecutionState::getInitPCBlock() {
  return initPC->inst->getParent();
}

BasicBlock *ExecutionState::getPrevPCBlock() {
  return prevPC->inst->getParent();
}

BasicBlock *ExecutionState::getPCBlock() { return pc->inst->getParent(); }

void ExecutionState::addLevel(BasicBlock *bb) {
  KFunction *kf = prevPC->parent->parent;
  KModule *kmodule = kf->parent;

  if (prevPC->inst->isTerminator() &&
      kmodule->mainFunctions.count(kf->function)) {
    multilevel.insert(bb);
    level.insert(bb);
  }
}
