//===-- TimingSolver.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TimingSolver.h"

#include "ExecutionState.h"

#include "klee/Config/Version.h"
#include "klee/Expr/Expr.h"
#include "klee/Expr/ExprUtil.h"
#include "klee/Statistics/Statistics.h"
#include "klee/Statistics/TimerStatIncrementer.h"
#include "klee/Solver/Solver.h"

#include "CoreStats.h"

#include <vector>

using namespace klee;
using namespace llvm;

/***/

bool TimingSolver::evaluate(const ConstraintSet &constraints, ref<Expr> expr,
                            Solver::Validity &result,
                            SolverQueryMetaData &metaData,
                            bool produceValidityCore) {
  // Fast path, to avoid timer and OS overhead.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
    result = CE->isTrue() ? Solver::True : Solver::False;
    return true;
  }

  TimerStatIncrementer timer(stats::solverTime);

  if (simplifyExprs)
    expr = ConstraintManager::simplifyExpr(constraints, expr);

  bool success =
      solver->evaluate(Query(constraints, expr, produceValidityCore), result);

  metaData.queryCost += timer.delta();

  return success;
}

bool TimingSolver::mustBeTrue(ExecutionState &state, const ConstraintSet &constraints, ref<Expr> expr,
                              bool &result, SolverQueryMetaData &metaData,
                              Assignment &symcretesCex,
                              bool minimizeModel) {
  // Fast path, to avoid timer and OS overhead.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
    result = CE->isTrue() ? true : false;
    return true;
  }

  TimerStatIncrementer timer(stats::solverTime);

  // if (simplifyExprs)
  //   expr = ConstraintManager::simplifyExpr(constraints, expr);
  bool success =
      solver->mustBeTrue(Query(constraints, expr, true), result);

  if (success && result) {
    ValidityCore core;
    bool hasSolution;
    success = solver->getValidityCore(Query(constraints, expr, true), core,
                                      hasSolution);
    assert(success && hasSolution);
    Assignment newAssignment(true);
    success = getValidAssignment(
        state.constraints, expr, core, state.symcretes, state.symsizesToMO,
        state.symcreteToConstraints, hasSolution, newAssignment, metaData, minimizeModel);
    if (success && hasSolution) {
      result = false;
    }
    symcretesCex = newAssignment;
  }

  metaData.queryCost += timer.delta();

  return success;
}

bool TimingSolver::mustBeFalse(ExecutionState &state, const ConstraintSet &constraints, ref<Expr> expr,
                               bool &result, SolverQueryMetaData &metaData, Assignment &symcretesCex,
                               bool minimizeModel) {
  return mustBeTrue(state, constraints, Expr::createIsZero(expr), result, metaData, symcretesCex,
                    minimizeModel);
}

bool TimingSolver::mayBeTrue(ExecutionState &state, const ConstraintSet &constraints, ref<Expr> expr,
                             bool &result, SolverQueryMetaData &metaData, Assignment &symcretesEx,
                             bool minimizeModel) {
  bool res;
  if (!mustBeFalse(state, constraints, expr, res, metaData, symcretesEx, minimizeModel))
    return false;
  result = !res;
  return true;
}

bool TimingSolver::mayBeFalse(ExecutionState &state, const ConstraintSet &constraints, ref<Expr> expr,
                              bool &result, SolverQueryMetaData &metaData, Assignment &symcretesEx,
                              bool minimizeModel) {
  bool res;
  if (!mustBeTrue(state, constraints, expr, res, metaData, symcretesEx, minimizeModel))
    return false;
  result = !res;
  return true;
}

bool TimingSolver::getValue(const ConstraintSet &constraints, ref<Expr> expr,
                            ref<ConstantExpr> &result,
                            SolverQueryMetaData &metaData) {
  // Fast path, to avoid timer and OS overhead.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
    result = CE;
    return true;
  }
  
  TimerStatIncrementer timer(stats::solverTime);

  if (simplifyExprs)
    expr = ConstraintManager::simplifyExpr(constraints, expr);

  bool success = solver->getValue(Query(constraints, expr), result);

  metaData.queryCost += timer.delta();

  return success;
}

bool TimingSolver::getInitialValues(
    const ConstraintSet &constraints, const std::vector<const Array *> &objects,
    std::vector<std::vector<unsigned char>> &result,
    SolverQueryMetaData &metaData, bool produceValidityCore) {
  if (objects.empty())
    return true;

  TimerStatIncrementer timer(stats::solverTime);

  bool success = solver->getInitialValues(
      Query(constraints, ConstantExpr::alloc(0, Expr::Bool),
            produceValidityCore),
      objects, result);

  metaData.queryCost += timer.delta();

  return success;
}

bool TimingSolver::evaluate(const ConstraintSet &constraints, ref<Expr> expr,
                            ref<SolverResponse> &queryResult,
                            ref<SolverResponse> &negatedQueryResult,
                            SolverQueryMetaData &metaData) {
  TimerStatIncrementer timer(stats::solverTime);

  // if (simplifyExprs)
  //   expr = ConstraintManager::simplifyExpr(constraints, expr);

  bool success = solver->evaluate(Query(constraints, expr, true), queryResult,
                                  negatedQueryResult);

  metaData.queryCost += timer.delta();

  return success;
}

bool TimingSolver::getValidityCore(const ConstraintSet &constraints,
                                   ref<Expr> expr, ValidityCore &validityCore,
                                   bool &result,
                                   SolverQueryMetaData &metaData) {
  // Fast path, to avoid timer and OS overhead.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
    result = CE->isTrue() ? true : false;
    return true;
  }

  TimerStatIncrementer timer(stats::solverTime);

  // if (simplifyExprs)
  //   expr = ConstraintManager::simplifyExpr(constraints, expr);

  bool success =
      solver->getValidityCore(Query(constraints, expr, true), validityCore, result);

  metaData.queryCost += timer.delta();

  return success;
}

static uint64_t bytesToAddress(const std::vector<uint8_t> &bytes) {
  uint64_t value = 0; 
  assert(bytes.size() == Context::get().getPointerWidth() / CHAR_BIT &&
          "Symcrete must be a 64-bit value");
  
  for (unsigned bit = 0; bit < bytes.size(); ++bit) {
    value |= (static_cast<uint64_t>(bytes[bit]) << (CHAR_BIT * bit));
  }
  return value;
} 

bool TimingSolver::getValidAssignment(
    const ConstraintSet &constraints, ref<Expr> expr,
    /* FIXME: full copy */ ValidityCore validityCore,
    /* FIXME: full copy */ Assignment symcretes,
    const std::unordered_map<const Array *, MemoryObject *> &symsizes,
    ExprHashMap<std::set<const Array *>> &exprToSymcretes,
    bool &hasResult, Assignment &result, SolverQueryMetaData &metaData, bool minimizeModel) const {
  
  /// Received core for SAT query 
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(validityCore.expr)) {
    if (validityCore.constraints.empty() && CE->isTrue()) {
      hasResult = false;
      return true;
    }
  }

  bool foundSymcreteDependendentConstraint = false;
  std::vector<const Array *> requestedSizeSymcretes;

  ref<SolverResponse> solverResponse;
  ConstraintSet constraintsWithSymcretes;


  /// FIXME: MOVE THIS TO EXECUTION STATE
  ref<Expr> optimizationRead = ConstantExpr::create(0, Expr::Int128);

  std::vector<const Array *> arrays;
  findObjects(expr, arrays);
  for (const auto *array : arrays) {
    if (symcretes.bindings.count(array)) {
      exprToSymcretes[validityCore.expr].insert(array);
    }
  }

  do {
    foundSymcreteDependendentConstraint = false;
    constraintsWithSymcretes = ConstraintSet(); 
    ConstraintManager cs(constraintsWithSymcretes);

    std::vector<ref<Expr>> unsatConstraints(validityCore.constraints.begin(),
                                            validityCore.constraints.end());
    unsatConstraints.push_back(validityCore.expr);

    for (const auto &brokenConstraint : unsatConstraints) {
      if (!exprToSymcretes.count(brokenConstraint)) {
        /// We can't fix it as it does not have symcrete.
        /// But we can not halt the process as we do not know,
        /// if we get minimal unsat core.
        continue;
      }

      const std::set<const Array *> &dependentSymcretes =
          exprToSymcretes.at(brokenConstraint);
      for (const auto &brokenSymcrete : dependentSymcretes) {
        if (symcretes.bindings.count(brokenSymcrete)) {
          foundSymcreteDependendentConstraint = true;
          symcretes.bindings.erase(brokenSymcrete);

          /// FIXME: MOVE THIS TO EXECUTION STATE
          if (symsizes.count(brokenSymcrete)) {
            requestedSizeSymcretes.emplace_back(brokenSymcrete);
                        
            ref<Expr> readFromSizeSymcrete = Expr::createTempRead(brokenSymcrete, Context::get().getPointerWidth());
            
            optimizationRead = AddExpr::create(optimizationRead, ZExtExpr::create(readFromSizeSymcrete, Expr::Int128));
          }
          /// SUM MINIMIZATION 
        
        }
      }
    }
    
    if (!foundSymcreteDependendentConstraint) {
      break;
    }

    /// Add constraints for remaining symcretes
    for (const auto &symcrete : symcretes.bindings) {
      ref<Expr> eqSymcreteExpr =
          EqExpr::create(Expr::createTempRead(symcrete.first,
                                              Context::get().getPointerWidth()),
                         Expr::createPointer(bytesToAddress(symcrete.second)));
      ref<Expr> evaluatedEqSymcreteExpr =
          cs.addConstraint(symcretes.evaluate(eqSymcreteExpr));
      exprToSymcretes[evaluatedEqSymcreteExpr].insert(symcrete.first);
    }

    for (const auto &constraint : constraints) {
      /// We can reuse set from previous iteration
      ref<Expr> evaluatedConstraint =
          cs.addConstraint(symcretes.evaluate(constraint));
      std::vector<const Array *> arraysInConstraint;
      findObjects(constraint, arraysInConstraint);
      exprToSymcretes[evaluatedConstraint].insert(arraysInConstraint.begin(),
                                                  arraysInConstraint.end());
    }

    TimerStatIncrementer timer(stats::solverTime);
    bool success = solver->check(
        Query(constraintsWithSymcretes, expr, true),
        solverResponse);
    metaData.queryCost += timer.delta();

    if (!success) {
      return false;
    }
  } while (foundSymcreteDependendentConstraint && solverResponse->getValidityCore(validityCore));

  /// Query is still have unsat core, but we did not remove any
  /// symcrete dependent constraints. 
  if (!foundSymcreteDependendentConstraint) {
    // Indeed must be true
    hasResult = false;
    return true;
  }

  if (requestedSizeSymcretes.empty()) {
    hasResult = false;
    return true;
  }

  /* ============================================================ */
  /*                FIXME: sum size minimization                  */
  /* ============================================================ */
  
  std::vector<std::vector<uint8_t>> requestedSymcretesConcretization;

  /// In the beggining we will take solution from model.
  if (!solverResponse->getInitialValuesFor(requestedSizeSymcretes, requestedSymcretesConcretization)) {
    hasResult = false;
    return true;
  }

  if (minimizeModel) {
    /// Solution only for symcrete sizes: we want to minimize the sum.
    /// So, we will binary search on minimum sum of objects sizes.
    uint64_t minSumModel = 0, maxSumModel = 0;

    /// "Bound" to prevent overflow during binary search
    static const uint64_t maxSumModelValue =
        ((uint64_t)1 << (sizeof(maxSumModel) * CHAR_BIT - 1)) - 1;

    for (const auto &concretization: requestedSymcretesConcretization) {
      uint64_t value = bytesToAddress(concretization);

      /// Overflow check. It is better to use builtin functions.
      /// But some versions of clang do not support them (?)
      if (maxSumModel + value < maxSumModel ||
          maxSumModel + value >= maxSumModelValue) {
        maxSumModel = maxSumModelValue;
        break;
      }
      maxSumModel += value;
    }

    maxSumModel += 1;
    uint64_t maxSumModelInit = maxSumModel;

    ConstraintManager cs(constraintsWithSymcretes);
    cs.addConstraint(NotExpr::create(symcretes.evaluate(expr)));

    /// TODO: this formula is too complex. Maybe we should use another way?
    while (maxSumModel && minSumModel < maxSumModel - 1) {
      uint64_t middleSumModel = (minSumModel + maxSumModel) / 2;
      ref<Expr> ask = UleExpr::create(
          optimizationRead, ConstantExpr::create(middleSumModel, Expr::Int128));
      ref<SolverResponse> newSolverRespone;

      TimerStatIncrementer timer(stats::solverTime);
      bool success =
          solver->check(Query(constraintsWithSymcretes, ask, true).negateExpr(),
                        newSolverRespone);
      metaData.queryCost += timer.delta();
      
      if (!success) {
        return false;
      }

      if (isa<InvalidResponse>(newSolverRespone)) {
        solverResponse = newSolverRespone;
        maxSumModel = middleSumModel;
      } else {
        minSumModel = middleSumModel;
      }
    }

    if (maxSumModelInit == maxSumModel) {
      hasResult = false;
      return true;
    }

    requestedSymcretesConcretization.clear();
    solverResponse->getInitialValuesFor(requestedSizeSymcretes, requestedSymcretesConcretization);
  }

  hasResult = true;
  
  /* Get concrete solution */
  result = Assignment(true);
  /* Here we do not have cocretizations for sym addresses */
  for (unsigned idx = 0; idx < requestedSizeSymcretes.size(); ++idx) {
    result.bindings[requestedSizeSymcretes[idx]] = requestedSymcretesConcretization[idx];
  }

  return true;
}

std::pair<ref<Expr>, ref<Expr>>
TimingSolver::getRange(const ConstraintSet &constraints, ref<Expr> expr,
                       SolverQueryMetaData &metaData, time::Span timeout) {
  TimerStatIncrementer timer(stats::solverTime);
  auto query = Query(constraints, expr);
  auto result = solver->getRange(query, timeout);
  metaData.queryCost += timer.delta();
  return result;
}
