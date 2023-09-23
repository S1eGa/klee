#include "SolverBuilder.h"

#include "SolverAdapter.h"
#include "SolverTheory.h"

#include "klee/ADT/Ref.h"
#include "klee/Expr/Expr.h"
#include "klee/util/EDM.h"

#include "klee/Support/ErrorHandling.h"

#include <cstdio>
#include <queue>

using namespace klee;

SolverBuilder::SolverBuilder(const std::vector<ref<SolverTheory>> &theories) {
  if (theories.empty()) {
    klee_error("no theories specified for the builder");
  }

  for (size_t pos = 0; pos < theories.size(); ++pos) {
    if (!orderOfTheories.put(pos, theories.at(pos))) {
      klee_error("same theory appeared twice in theories sequence : %s",
                 theories.at(pos)->toString().c_str());
    }
  }
}

void SolverBuilder::onNotify(
    const std::pair<ref<Expr>, ref<TheoryHandle>> &completed) {
  cache[completed.first] = completed.second;
}

ref<TheoryHandle>
SolverBuilder::buildWithTheory(const ref<SolverTheory> &theory,
                               const ref<Expr> &expr) {
  /*
   * Translates `klee::Expr` using given theory.
   * Firstly, translates all children of given expression.
   * Then, strategy may differ:
   * - we may choose the lowest common sort from constructed
   *   expressions, and make a casts to it
   * - we may rebuild children in the sort of the lowest
   *   common sort
   */
  TheoryHandleList kidsHandles;
  kidsHandles.reserve(expr->getNumKids());

  if (expr->getNumKids() == 0) {
    return theory->translate(expr, kidsHandles);
  }

  uint64_t positionOfLeastCommonSort = orderOfTheories.size();

  /* Figure out the least common sort for kid handles. */
  for (const auto &child : expr->kids()) {
    ref<TheoryHandle> kidHandle = build(child);
    if (ref<BrokenTheoryHandle> brokenKidHandle =
            dyn_cast<BrokenTheoryHandle>(kidHandle)) {
      return kidHandle;
    }
    positionOfLeastCommonSort =
        std::min(orderOfTheories.getByValue(kidHandle->parent),
                 positionOfLeastCommonSort);
    kidsHandles.push_back(kidHandle);
  }

  SolverTheory::Sort leastCommonSort =
      orderOfTheories.getByKey(positionOfLeastCommonSort)->getSort();
  for (auto &kid : kidsHandles) {
    kid = castToTheory(kid, leastCommonSort);
    if (isa<BrokenTheoryHandle>(kid)) {
      return new BrokenTheoryHandle(expr);
    }
  }

  return theory->translate(expr, kidsHandles);
}

/*
 * Translates KLEE's inner representation of expression
 * to the expression for the solver, specified in solverAdapter.
 */
ref<TheoryHandle> SolverBuilder::build(const ref<Expr> &expr) {
  if (cache.count(expr)) {
    return cache.at(expr);
  }

  for (const auto &it : orderOfTheories) {
    ref<TheoryHandle> exprHandle = buildWithTheory(it.second, expr);

    /*
     * If handle is broken, then expression can not be built
     * in that theory. Try another one.
     */
    if (isa<BrokenTheoryHandle>(exprHandle)) {
      continue;
    }

    /*
     * If handle is incomplete, then we should subscribe
     * on it in order to cache it as soon as we will be able to construct it.
     */
    if (ref<IncompleteResponse> incompleteExprHandle =
            dyn_cast<IncompleteResponse>(exprHandle)) {
      incompleteExprHandle->listen(this);
    }

    cache.emplace(expr, exprHandle);
    return exprHandle;
  }

  /*
   * All theories can not be use to translate current expression
   * in solver's expression. Memoize that we can not build it
   * and return broken handle.
   */
  cache.emplace(expr, new BrokenTheoryHandle(expr));
  return cache.at(expr);
}

/*
 * Casts an expression to specified theory.
 *
 * Inside implements BFS on theories in order to
 * reach target theory in least possible steps.
 * We may consider transitions between theories as
 * weighted and use graph algorithms in order to
 * find the less expensive path to specified theory.
 */
ref<TheoryHandle> SolverBuilder::castToTheory(const ref<TheoryHandle> &arg,
                                              SolverTheory::Sort sort) {
  std::queue<ref<TheoryHandle>> castedHandlesQueue;
  castedHandlesQueue.push(arg);

  std::unordered_set<SolverTheory::Sort> visited = {arg->parent->getSort()};

  while (!castedHandlesQueue.empty()) {
    const ref<TheoryHandle> topCastedHandle = castedHandlesQueue.front();
    castedHandlesQueue.pop();

    if (topCastedHandle->parent->getSort() == sort) {
      return topCastedHandle;
    }

    const ref<SolverTheory> &theoryOfTopCastedHandle = topCastedHandle->parent;
    for (const auto &it : theoryOfTopCastedHandle->castMapping) {
      SolverTheory::Sort toSort = it.first;
      if (visited.count(toSort)) {
        continue;
      }

      if (ref<TheoryHandle> nextCastedHandle =
              topCastedHandle->parent->castTo(toSort, topCastedHandle)) {
        castedHandlesQueue.push(nextCastedHandle);
        visited.insert(toSort);
      }
    }
  }

  return new BrokenTheoryHandle(arg->source);
}
