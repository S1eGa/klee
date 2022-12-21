//===-- SolverImpl.cpp ----------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Expr/Expr.h"
#include "klee/Solver/Solver.h"
#include "klee/Solver/SolverImpl.h"
#include "klee/Support/ErrorHandling.h"

using namespace klee;

SolverImpl::~SolverImpl() {}

bool SolverImpl::computeValidity(const Query &query, Solver::Validity &result) {
  bool isTrue, isFalse;
  if (!computeTruth(query, isTrue))
    return false;
  if (isTrue) {
    result = Solver::True;
  } else {
    if (!computeTruth(query.negateExpr(), isFalse))
      return false;
    result = isFalse ? Solver::False : Solver::Unknown;
  }
  return true;
}

bool SolverImpl::computeValidity(const Query &query,
                                 ref<SolverResponse> &queryResult,
                                 ref<SolverResponse> &negatedQueryResult) {
  if (!check(query, queryResult))
    return false;
  if (!check(query.negateExpr(), negatedQueryResult))
    return false;
  return true;
}

bool SolverImpl::check(const Query &query, ref<SolverResponse> &result) {
  if (ProduceUnsatCore)
    klee_error("check is not implemented");
  return false;
}

bool SolverImpl::computeValidityCore(const Query &query,
                                     ValidityCore &validityCore,
                                     bool &isValid) {
  if (ProduceUnsatCore)
    klee_error("computeTruthCore is not implemented");
  return false;
}

bool SolverImpl::computeMinimalUnsignedValue(const Query &query,
                                             ref<ConstantExpr> &result) {
  bool mustBeTrue;

  // Fast path check
  if (!computeTruth(
          query.withExpr(EqExpr::createIsZero(query.expr)).negateExpr(),
          mustBeTrue)) {
    return false;
  }

  if (!mustBeTrue) {
    result = ConstantExpr::create(0, query.expr->getWidth());
    return true;
  }

  // At least one value must satisfy constraints
  ref<ConstantExpr> left = ConstantExpr::create(0, 64);
  ref<ConstantExpr> right = ConstantExpr::create(0, 64);

  // Compute the right border
  do {
    left = right;
    right = ConstantExpr::create(
        std::max((uint64_t)1, 2 * right->getZExtValue()), 64);
    if (!computeTruth(query.withExpr(UgtExpr::create(query.expr, right)),
                      mustBeTrue)) {
      return false;
    }
  } while (mustBeTrue);

  // Binary search the least value for expr from the given query
  while (left->getZExtValue() + 1 < right->getZExtValue()) {
    ref<ConstantExpr> mid = ConstantExpr::create(
        (left->getZExtValue() + right->getZExtValue()) / 2, 64);
    if (!computeTruth(query.withExpr(UgtExpr::create(query.expr, mid)),
                      mustBeTrue)) {
      return false;
    }

    if (mustBeTrue) {
      left = mid;
    } else {
      right = mid;
    }
  }

  result = right;
  return true;
}

const char *SolverImpl::getOperationStatusString(SolverRunStatus statusCode) {
  switch (statusCode) {
  case SOLVER_RUN_STATUS_SUCCESS_SOLVABLE:
    return "OPERATION SUCCESSFUL, QUERY IS SOLVABLE";
  case SOLVER_RUN_STATUS_SUCCESS_UNSOLVABLE:
    return "OPERATION SUCCESSFUL, QUERY IS UNSOLVABLE";
  case SOLVER_RUN_STATUS_FAILURE:
    return "OPERATION FAILED";
  case SOLVER_RUN_STATUS_TIMEOUT:
    return "SOLVER TIMEOUT";
  case SOLVER_RUN_STATUS_FORK_FAILED:
    return "FORK FAILED";
  case SOLVER_RUN_STATUS_INTERRUPTED:
    return "SOLVER PROCESS INTERRUPTED";
  case SOLVER_RUN_STATUS_UNEXPECTED_EXIT_CODE:
    return "UNEXPECTED SOLVER PROCESS EXIT CODE";
  case SOLVER_RUN_STATUS_WAITPID_FAILED:
    return "WAITPID FAILED FOR SOLVER PROCESS";
  default:
    return "UNRECOGNIZED OPERATION STATUS";
  }
}
