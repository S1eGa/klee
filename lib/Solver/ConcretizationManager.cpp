#include "klee/Solver/ConcretizationManager.h"
#include "klee/Expr/Assignment.h"
#include "klee/Expr/Constraints.h"
#include "klee/Expr/IndependentSet.h"
#include "klee/Solver/Solver.h"
#include <set>

using namespace klee;

Assignment ConcretizationManager::get(const ConstraintSet &set) {
  Assignment assign(true);
  auto independent = getAllIndependentConstraintsSets(
      Query(set, ConstantExpr::alloc(0, Expr::Bool)));
  for (auto i : *independent) {
    if (auto a = concretizations.lookup(ConstraintSet(i.exprs).asSet())) {
      for (auto i : a->bindings) {
        assign.bindings.insert(i);
      }
    }
  }
  delete independent;

  return assign;
}

void ConcretizationManager::add(const ConstraintSet &oldCS,
                                const ConstraintSet &newCS,
                                const Assignment &assign) {
  Assignment newAssign(true);
  ref<Expr> newExpr = ConstantExpr::alloc(1, Expr::Bool);
  for (auto i : newCS) {
    newExpr = AndExpr::alloc(newExpr, i);
  }
  std::vector<ref<Expr>> dependent;
  auto dependency = getIndependentConstraints(Query(oldCS, newExpr), dependent);
  auto independent = getAllIndependentConstraintsSets(
      Query(oldCS, ConstantExpr::alloc(0, Expr::Bool)));
  
  for (auto i : *independent) {
    if (i.intersects(dependency)) {
      if (auto a = concretizations.lookup(ConstraintSet(i.exprs).asSet())) {
        for (auto i : a->bindings) {
          newAssign.bindings.insert(i);
        }
      }
    }
  }
  delete independent;

  ConstraintSet dependentWithNew;
  ConstraintManager cm(dependentWithNew);
  
  for (auto i : dependent) {
    cm.addConstraint(i);
  }
  
  for (auto i : newCS) {
    cm.addConstraint(i);
  }

  for (auto i : assign.bindings) {
    newAssign.bindings[i.first] = i.second;
  }

  independent = getAllIndependentConstraintsSets(
      Query(dependentWithNew, ConstantExpr::alloc(0, Expr::Bool)));

  for (auto i : *independent) {
    Assignment independentAssignment(true);
    for (const Array *array : i.wholeObjects) {
      if (newAssign.bindings.count(array)) {
        independentAssignment.bindings[array] = newAssign.bindings.at(array);
      }
    }
    concretizations.insert(ConstraintSet(i.exprs).asSet(),
                           independentAssignment);
  }

  delete independent;
}

void ConcretizationManager::add(const Query &q, const Assignment &assign) {
  add(q.constraints, ConstraintSet(std::vector<ref<Expr>>{q.expr}), assign);
}

ref<Expr>
ConcretizationManager::simplifyExprWithSymcretes(const ConstraintSet &cs,
                                                 ref<Expr> e) {
  Assignment assign = get(cs);
  ConstraintSet newCS = assign.createConstraintsFromAssignment();
  ConstraintManager cm(newCS);
  for (ref<Expr> constraint : cs) {
    cm.addConstraint(assign.evaluate(constraint));
  }
  return ConstraintManager::simplifyExpr(newCS, e);
}