#include "ConstantAddressSpace.h"

#include "AddressSpace.h"
#include "klee/Core/Context.h"

#include <queue>

using namespace klee;

void ConstantPointerGraph::addSource(const ObjectPair &objectPair) {
  if (objectGraph.count(objectPair) != 0) {
    return;
  }
  addReachableFrom(objectPair);
}

void ConstantPointerGraph::addReachableFrom(const ObjectPair &objectPair) {
  std::queue<ObjectPair> objectQueue;
  objectQueue.push(objectPair);

  while (!objectQueue.empty()) {
    auto frontObjectPair = objectQueue.front();
    objectQueue.pop();

    auto references = owningAddressSpace.referencesInFinal(frontObjectPair);

    for (auto &[offset, referencedResolution] : references) {
      if (objectGraph.count(referencedResolution.objectPair) == 0) {
        objectQueue.push(referencedResolution.objectPair);
        objectGraph.emplace(referencedResolution.objectPair,
                            ConstantResolutionList{});
      }
    }

    objectGraph[frontObjectPair] = std::move(references);
  }
}

std::size_t ConstantPointerGraph::size() const { return objectGraph.size(); }

ConstantPointerGraph::ObjectGraphContainer::const_iterator
ConstantPointerGraph::begin() const {
  return objectGraph.begin();
}

ConstantPointerGraph::ObjectGraphContainer::const_iterator
ConstantPointerGraph::end() const {
  return objectGraph.end();
}

////////////////////////////////////////////////////////////////

ConstantAddressSpace::ConstantAddressSpace(const AddressSpace &addressSpace,
                                           const Assignment &model)
    : addressSpace(addressSpace), model(model) {
  for (const auto &[object, state] : addressSpace.objects) {
    insert(ObjectPair{object, state.get()});
  }
}

bool ConstantAddressSpace::isResolution(ref<ConstantPointerExpr> address,
                                        const MemoryObject &object) const {
  auto condition = object.getBoundsCheckPointer(address);
  return model.evaluate(condition)->isTrue();
}

std::uint64_t
ConstantAddressSpace::addressOf(const MemoryObject &object) const {
  auto addressExpr = PointerExpr::create(object.getBaseExpr());

  ref<ConstantPointerExpr> constantAddressExpr =
      dyn_cast<ConstantPointerExpr>(model.evaluate(addressExpr));
  assert(!constantAddressExpr.isNull());
  return constantAddressExpr->getConstantValue()->getZExtValue();
}

std::uint64_t ConstantAddressSpace::sizeOf(const MemoryObject &object) const {
  auto sizeExpr = object.getSizeExpr();
  ref<ConstantExpr> constantSizeExpr =
      dyn_cast<ConstantExpr>(model.evaluate(sizeExpr));
  assert(!constantSizeExpr.isNull());
  return constantSizeExpr->getZExtValue();
}

ConstantAddressSpace::Iterator ConstantAddressSpace::begin() const {
  return Iterator(objects.begin());
}

ConstantAddressSpace::Iterator ConstantAddressSpace::end() const {
  return Iterator(objects.end());
}

/*
 * Returns a list of objects referenced in the the given object.
 */
ConstantResolutionList
ConstantAddressSpace::referencesInInitial(const ObjectPair &objectPair) const {
  auto [object, state] = objectPair;

  auto pointerWidth = Context::get().getPointerWidth();
  auto objectSize = sizeOf(*object);

  ConstantResolutionList references;

  for (Expr::Width i = 0; i + pointerWidth / CHAR_BIT <= objectSize; ++i) {
    auto contentPart = state->readInitialValue(i, pointerWidth);

    auto pointer = PointerExpr::create(contentPart);
    ref<ConstantPointerExpr> constantPointer =
        dyn_cast<ConstantPointerExpr>(model.evaluate(pointer, false));

    assert(!constantPointer.isNull());

    if (auto resolution = resolve(constantPointer)) {
      references.emplace(
          i, ConstantResolution{
                 constantPointer->getConstantValue()->getZExtValue(),
                 std::move(*resolution)});
    }
    assert(!constantPointer.isNull());
  }

  return references;
}

// ----------------------------------------------------------------
// FIXME: remove copy-paste in referencesIn* functions.
// ----------------------------------------------------------------

ConstantResolutionList
ConstantAddressSpace::referencesInFinal(const ObjectPair &objectPair) const {
  auto [object, state] = objectPair;

  auto pointerWidth = Context::get().getPointerWidth();
  auto objectSize = sizeOf(*object);

  ConstantResolutionList references;

  for (Expr::Width i = 0; i + pointerWidth / CHAR_BIT <= objectSize; ++i) {
    // TODO: Make exract 1 from end and Concat 1 to the beginning instead
    // TODO: replace with call to readValue()
    auto contentPart = state->read(i, pointerWidth);

    auto pointer = PointerExpr::create(contentPart);
    ref<ConstantPointerExpr> constantPointer =
        dyn_cast<ConstantPointerExpr>(model.evaluate(pointer, false));

    assert(!constantPointer.isNull());

    if (auto resolution = resolve(constantPointer)) {
      references.emplace(
          i, ConstantResolution{
                 constantPointer->getConstantValue()->getZExtValue(),
                 std::move(*resolution)});
    }
  }

  return references;
}

std::optional<ObjectPair>
ConstantAddressSpace::resolve(ref<ConstantPointerExpr> address) const {
  auto valueOfAddress = address->getConstantBase()->getZExtValue();
  auto itNextToObject = objects.upper_bound(valueOfAddress);

  if (itNextToObject != objects.begin()) {
    auto object = std::prev(itNextToObject)->second;
    if (isResolution(address, *object)) {
      return {addressSpace.findObject(object)};
    }
  }

  if (itNextToObject != objects.end()) {
    auto object = itNextToObject->second;
    if (isResolution(address, *object)) {
      return {addressSpace.findObject(object)};
    }
  }

  return std::nullopt;
}

ConstantPointerGraph ConstantAddressSpace::createPointerGraph() const {
  return ConstantPointerGraph(*this);
}

void ConstantAddressSpace::insert(const ObjectPair &objectPair) {
  auto object = objectPair.first;

  auto addressOfObject = model.evaluate(object->getBaseExpr());

  assert(isa<ConstantExpr>(addressOfObject));
  auto valueOfAddressOfObject =
      cast<ConstantExpr>(addressOfObject)->getZExtValue();
  assert(objects.count(valueOfAddressOfObject) == 0);
  objects.emplace(valueOfAddressOfObject, object);
}
