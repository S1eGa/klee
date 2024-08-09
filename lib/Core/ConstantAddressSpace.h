//===-- ConstantAddressSpace.h ----------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_CONSTANTADDRESSSPACE_H
#define KLEE_CONSTANTADDRESSSPACE_H

#include "AddressSpace.h"
#include "Memory.h"

#include <cstdint>
#include <optional>

namespace klee {

class ConstantAddressSpace;

struct ConstantResolution {
  std::uint64_t writtenAddress;
  ObjectPair objectPair;
};

typedef std::unordered_map<Expr::Width, ConstantResolution>
    ConstantResolutionList;

class ConstantPointerGraph {
  friend class ConstantAddressSpace;

  struct InternalObjectPairHash {
    std::size_t operator()(const ObjectPair &objectPair) const {
      return std::hash<const MemoryObject *>{}(objectPair.first);
    }
  };

  typedef std::unordered_map<ObjectPair, ConstantResolutionList,
                             InternalObjectPairHash>
      ObjectGraphContainer;

public:
  void addSource(const ObjectPair &objectPair);

  std::size_t size() const;

  ObjectGraphContainer::const_iterator begin() const;
  ObjectGraphContainer::const_iterator end() const;

private:
  ConstantPointerGraph(const ConstantAddressSpace &owningAddressSpace)
      : owningAddressSpace(owningAddressSpace) {}

  /*
   * Returns a graph of recursively reachable objects from the given
   * object.
   */
  void addReachableFrom(const ObjectPair &objectPair);

private:
  ObjectGraphContainer objectGraph;
  const ConstantAddressSpace &owningAddressSpace;
};

class ConstantAddressSpace {
  typedef std::map<std::uint64_t, const MemoryObject *> OrderedObjectsContainer;

  struct Iterator {
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = const MemoryObject;
    using pointer = const MemoryObject *;
    using reference = const MemoryObject &;

    Iterator(OrderedObjectsContainer::const_iterator initial)
        : internal(initial) {}

    Iterator &operator++() {
      ++internal;
      return *this;
    }

    Iterator operator++(int) {
      Iterator result = *this;
      ++(*this);
      return result;
    }

    reference operator*() const { return *internal->second; }
    pointer operator->() { return internal->second; }

    bool operator==(const Iterator &rhs) { return internal == rhs.internal; }
    bool operator!=(const Iterator &rhs) { return internal != rhs.internal; }

  private:
    OrderedObjectsContainer::const_iterator internal;
  };

public:
  ConstantAddressSpace(const AddressSpace &addressSpace,
                       const Assignment &model);

  /*
   * Resolves the address to the corresponding object.
   */
  std::optional<ObjectPair> resolve(ref<ConstantPointerExpr> address) const;

  /*
   * Returns a list of objects referenced in the the given object.
   */
  ConstantResolutionList
  referencesInInitial(const ObjectPair &objectPair) const;

  /*
   * Returns a list of objects referenced in the the given object.
   */
  ConstantResolutionList referencesInFinal(const ObjectPair &objectPair) const;

  /*
   * Creates pointer graph owned by this address space. This
   * graph handles const reference to *this, therefore it cannot
   * be used after destruction of that address space.
   */
  ConstantPointerGraph createPointerGraph() const;

  std::uint64_t addressOf(const MemoryObject &object) const;
  std::uint64_t sizeOf(const MemoryObject &object) const;

  Iterator begin() const;
  Iterator end() const;

  std::size_t size() const { return objects.size(); }

private:
  void insert(const ObjectPair &objectPair);
  bool isResolution(ref<ConstantPointerExpr> address,
                    const MemoryObject &object) const;

private:
  const AddressSpace &addressSpace;
  const Assignment &model;
  OrderedObjectsContainer objects;
};
} // namespace klee

#endif
