#include "KTestBuilder.h"
#include "ConstantAddressSpace.h"

#include <algorithm>
#include <klee/Expr/Expr.h>
#include <klee/Support/OptionCategories.h>
#include <llvm/Support/CommandLine.h>

using namespace klee;

llvm::cl::opt<unsigned> UninitMemoryTestMultiplier(
    "uninit-memory-test-multiplier", llvm::cl::init(6),
    llvm::cl::desc("Generate additional number of duplicate tests due to "
                   "irreproducibility of uninitialized memory "
                   "(default=6)"),
    llvm::cl::cat(TestGenCat));

llvm::cl::opt<bool> OnlyOutputMakeSymbolicArrays(
    "only-output-make-symbolic-arrays", llvm::cl::init(false),
    llvm::cl::desc(
        "Only output test data with klee_make_symbolic source (default=false)"),
    llvm::cl::cat(TestGenCat));

static std::unordered_map<const MemoryObject *, std::size_t>
enumerateObjects(const std::vector<Symbolic> &symbolics,
                 const ConstantPointerGraph &pointerGraph) {
  std::unordered_map<const MemoryObject *, std::size_t> enumeration;
  enumeration.reserve(pointerGraph.size());

  // Add all symbolics to enumeration and remember which memory objects
  // are not counted as symbolics
  std::unordered_set<const MemoryObject *> objectsOfSymbolics;
  objectsOfSymbolics.reserve(symbolics.size());

  std::size_t symbolicNumCounter = 0;
  for (const auto &singleSymbolic : symbolics) {
    auto singleObjectOfSymbolic = singleSymbolic.memoryObject.get();
    objectsOfSymbolics.insert(singleObjectOfSymbolic);
    enumeration.emplace(singleObjectOfSymbolic, symbolicNumCounter);
    ++symbolicNumCounter;
  }

  // Iterate over all remaining objects
  std::size_t constantNumCounter = symbolics.size();
  for (const auto &[objectPair, resolution] : pointerGraph) {
    if (objectsOfSymbolics.count(objectPair.first) == 0) {
      enumeration.emplace(objectPair.first, constantNumCounter);
      ++constantNumCounter;
    }
  }

  return enumeration;
}

////////////////////////////////////////////////////////////////////////

KTestBuilder::KTestBuilder(const ExecutionState &state, const Assignment &model)
    : model_(model), constantAddressSpace_(state.addressSpace, model),
      constantPointerGraph_(constantAddressSpace_.createPointerGraph()),
      ktest_({}) {
  initialize(state);
}

void KTestBuilder::initialize(const ExecutionState &state) {
  for (auto &[object, symbolicsSet] : state.symbolics) {
    for (auto &symbolic : symbolicsSet) {
      if (OnlyOutputMakeSymbolicArrays ? symbolic.isMakeSymbolic()
                                       : symbolic.isReproducible()) {
        assert(object == symbolic.memoryObject.get());
        constantPointerGraph_.addSource(ObjectPair{symbolic.memoryObject.get(),
                                                   symbolic.objectState.get()});
        symbolics.push_back(symbolic);
      }
    }
  }

  std::sort(symbolics.begin(), symbolics.end(),
            [](const Symbolic &lhs, const Symbolic &rhs) {
              return lhs.num < rhs.num;
            });

  auto uninitObjectNum = std::count_if(
      symbolics.begin(), symbolics.end(), [](const auto &singleSymbolic) {
        return isa<UninitializedSource>(singleSymbolic.array()->source);
      });

  order_ = enumerateObjects(symbolics, constantPointerGraph_);

  ktest_.numObjects = order_.size();
  ktest_.objects = new KTestObject[ktest_.numObjects]();
  ktest_.uninitCoeff = UninitMemoryTestMultiplier * uninitObjectNum;
}

std::size_t KTestBuilder::countObjectsFromOrder(
    const ConstantResolutionList &resolution) const {
  return std::count_if(
      resolution.begin(), resolution.end(), [this](const auto &entry) {
        const auto &singleResolution = entry.second;
        const auto &resolvedObjectPair = singleResolution.objectPair;
        return order_.count(resolvedObjectPair.first) != 0;
      });
}

////////////////////////////////////////////////////////////////////////

KTestBuilder &KTestBuilder::fillArgcArgv(unsigned argc, char **argv,
                                         unsigned symArgc, unsigned symArgv) {
  ktest_.numArgs = argc;
  ktest_.args = argv;
  ktest_.symArgvLen = symArgc;
  ktest_.symArgvs = symArgv;
  return *this;
}

KTestBuilder &KTestBuilder::fillFinalPointers() {
  for (const auto &[objectPair, resolution] : constantPointerGraph_) {
    // Select KTestObject for the object
    const auto objectNum = order_.at(objectPair.first);
    auto &ktestObject = ktest_.objects[objectNum];

    ktestObject.address = constantAddressSpace_.addressOf(*objectPair.first);
    ktestObject.name = const_cast<char *>(objectPair.first->name.c_str());

    // Allocate memory for pointers
    if (resolution.empty()) {
      continue;
    }
    ktestObject.content.numFinalPointers = countObjectsFromOrder(resolution);
    ktestObject.content.finalPointers =
        new Pointer[ktestObject.content.numFinalPointers];

    std::size_t currentPointerIdx = 0;
    // Populate pointers
    for (const auto &[offset, singleResolution] : resolution) {
      auto [writtenAddress, resolvedObjectPair] = singleResolution;

      if (order_.count(resolvedObjectPair.first) == 0) {
        continue;
      }

      auto referencesToObjectNum = order_.at(resolvedObjectPair.first);
      auto addressOfReferencedObject =
          constantAddressSpace_.addressOf(*resolvedObjectPair.first);

      auto &pointerObject =
          ktestObject.content.finalPointers[currentPointerIdx];
      pointerObject.indexOfObject = referencesToObjectNum;
      pointerObject.indexOffset = writtenAddress - addressOfReferencedObject;
      pointerObject.offset = offset;

      ++currentPointerIdx;
    }
  }
  return *this;
}

KTestBuilder &KTestBuilder::fillInitialPointers() {
  // Required only for symbolics
  for (const auto &symbolic : symbolics) {
    auto object = symbolic.memoryObject.get();
    auto &ktestObject = ktest_.objects[order_.at(object)];

    // TODO: symbolic objects might have been freed and not contained in
    // the address space anymore.

#ifndef NDEBUG
    auto array = symbolic.array();
#endif

    auto resolution = constantAddressSpace_.referencesInInitial(
        {symbolic.memoryObject.get(), symbolic.objectState.get()});

#ifndef NDEBUG
    assert(symbolic.array() == array);
#endif

    if (resolution.empty()) {
      continue;
    }

    ktestObject.content.numPointers = countObjectsFromOrder(resolution);
    ktestObject.content.pointers = new Pointer[ktestObject.content.numPointers];

    std::size_t currentPointerIdx = 0;
    for (const auto &[offset, singleResolution] : resolution) {
      auto [writtenAddress, resolvedObjectPair] = singleResolution;

      if (order_.count(resolvedObjectPair.first) == 0) {
        // This object is not in address space anymore.
        // TODO: it could be a symbolic
        continue;
      }

      auto referencesToObjectNum = order_.at(resolvedObjectPair.first);
      auto addressOfReferencedObject =
          constantAddressSpace_.addressOf(*resolvedObjectPair.first);

      auto &pointerObject = ktestObject.content.pointers[currentPointerIdx];
      pointerObject.indexOfObject = referencesToObjectNum;
      pointerObject.indexOffset = writtenAddress - addressOfReferencedObject;
      pointerObject.offset = offset;

      ++currentPointerIdx;
    }
  }
  return *this;
}

KTestBuilder &KTestBuilder::fillInitialContent() {
  // Required only for symbolics
  for (const auto &symbolic : symbolics) {
    auto object = symbolic.memoryObject.get();
    auto &ktestObject = ktest_.objects[order_.at(object)];

    auto array = symbolic.array();
    const auto &modelForArray = model_.bindings.at(array);

    auto objectSize = constantAddressSpace_.sizeOf(*object);
    ktestObject.content.numBytes = objectSize;
    ktestObject.content.bytes = new unsigned char[objectSize];

    auto buffer = ktestObject.content.bytes;
    for (std::uint64_t offset = 0; offset < objectSize; ++offset) {
      buffer[offset] = modelForArray.load(offset);
    }
  }

  return *this;
}

KTestBuilder &KTestBuilder::fillFinalContent() {
  // Requied for all object in graph
  for (auto &[objectPair, resolutionList] : constantPointerGraph_) {
    auto [object, state] = objectPair;

    auto &ktestObject = ktest_.objects[order_.at(object)];

    auto objectSize = constantAddressSpace_.sizeOf(*object);
    ktestObject.content.numBytes = objectSize;
    ktestObject.content.finalBytes = new unsigned char[objectSize];

    auto buffer = ktestObject.content.finalBytes;
    for (std::uint64_t offset = 0; offset < objectSize; ++offset) {
      auto byteExpr = state->read8(offset);
      if (auto pointerByteExpr = dyn_cast<PointerExpr>(byteExpr)) {
        byteExpr = pointerByteExpr->getValue();
      }

      auto evaluatedByteExpr = model_.evaluate(byteExpr, false);

      ref<ConstantExpr> constantByteExpr =
          dyn_cast<ConstantExpr>(evaluatedByteExpr);
      assert(!constantByteExpr.isNull());

      buffer[offset] = constantByteExpr->getZExtValue();
    }
  }

  return *this;
}

KTest KTestBuilder::build() { return std::move(ktest_); }