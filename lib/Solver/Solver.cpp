//===-- Solver.cpp --------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Solver/Solver.h"

#include "klee/Expr/Constraints.h"
#include "klee/Expr/ExprUtil.h"
#include "klee/Expr/SymbolicSource.h"
#include "klee/Solver/SolverImpl.h"

using namespace klee;

const char *Solver::validity_to_str(Validity v) {
  switch (v) {
  default:
    return "Unknown";
  case True:
    return "True";
  case False:
    return "False";
  }
}

Solver::~Solver() { delete impl; }

char *Solver::getConstraintLog(const Query &query) {
  return impl->getConstraintLog(query);
}

void Solver::setCoreSolverTimeout(time::Span timeout) {
  impl->setCoreSolverTimeout(timeout);
}

bool Solver::evaluate(const Query &query, Validity &result) {
  assert(query.expr->getWidth() == Expr::Bool && "Invalid expression type!");

  // Maintain invariants implementations expect.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(query.expr)) {
    result = CE->isTrue() ? True : False;
    return true;
  }

  return impl->computeValidity(query, result);
}

bool Solver::mustBeTrue(const Query &query, bool &result) {
  assert(query.expr->getWidth() == Expr::Bool && "Invalid expression type!");

  // Maintain invariants implementations expect.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(query.expr)) {
    result = CE->isTrue() ? true : false;
    return true;
  }

  return impl->computeTruth(query, result);
}

bool Solver::mustBeFalse(const Query &query, bool &result) {
  return mustBeTrue(query.negateExpr(), result);
}

bool Solver::mayBeTrue(const Query &query, bool &result) {
  bool res;
  if (!mustBeFalse(query, res))
    return false;
  result = !res;
  return true;
}

bool Solver::mayBeFalse(const Query &query, bool &result) {
  bool res;
  if (!mustBeTrue(query, res))
    return false;
  result = !res;
  return true;
}

bool Solver::getValue(const Query &query, ref<ConstantExpr> &result) {
  // Maintain invariants implementation expect.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(query.expr)) {
    result = CE;
    return true;
  }

  // FIXME: Push ConstantExpr requirement down.
  ref<Expr> tmp;
  if (!impl->computeValue(query, tmp))
    return false;

  result = cast<ConstantExpr>(tmp);
  return true;
}

bool Solver::getMinimalUnsignedValue(const Query &query,
                                     ref<ConstantExpr> &result) {
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(query.expr)) {
    result = CE;
    return true;
  }

  return impl->computeMinimalUnsignedValue(query, result);
}

bool Solver::evaluate(const Query &query, ref<SolverResponse> &queryResult,
                      ref<SolverResponse> &negateQueryResult) {
  assert(query.expr->getWidth() == Expr::Bool && "Invalid expression type!");

  // Maintain invariants implementations expect.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(query.expr)) {
    if (CE->isTrue()) {
      queryResult = new ValidResponse(ValidityCore());
      return impl->check(query.negateExpr(), negateQueryResult);
    } else {
      negateQueryResult = new ValidResponse(ValidityCore());
      return impl->check(query, queryResult);
    }
  }

  return impl->computeValidity(query, queryResult, negateQueryResult);
}

bool Solver::getValidityCore(const Query &query, ValidityCore &validityCore,
                             bool &result) {
  assert(query.expr->getWidth() == Expr::Bool && "Invalid expression type!");

  // Maintain invariants implementations expect.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(query.expr)) {
    result = CE->isTrue() ? true : false;
    if (result) {
      validityCore = ValidityCore();
    }
    return true;
  }

  return impl->computeValidityCore(query, validityCore, result);
}

bool Solver::getInitialValues(
    const Query &query, const std::vector<const Array *> &objects,
    std::vector<SparseStorage<unsigned char>> &values) {
  bool hasSolution;
  bool success =
      impl->computeInitialValues(query, objects, values, hasSolution);
  // FIXME: Propogate this out.
  if (!hasSolution)
    return false;

  return success;
}

bool Solver::check(const Query &query, ref<SolverResponse> &queryResult) {
  return impl->check(query, queryResult);
}

std::pair<ref<Expr>, ref<Expr>> Solver::getRange(const Query &query) {
  ref<Expr> e = query.expr;
  Expr::Width width = e->getWidth();
  uint64_t min, max;

  if (width == 1) {
    Solver::Validity result;
    if (!evaluate(query, result))
      assert(0 && "computeValidity failed");
    switch (result) {
    case Solver::True:
      min = max = 1;
      break;
    case Solver::False:
      min = max = 0;
      break;
    default:
      min = 0, max = 1;
      break;
    }
  } else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(e)) {
    min = max = CE->getZExtValue();
  } else {
    // binary search for # of useful bits
    uint64_t lo = 0, hi = width, mid, bits = 0;
    while (lo < hi) {
      mid = lo + (hi - lo) / 2;
      bool res;
      bool success =
          mustBeTrue(query.withExpr(EqExpr::create(
                         LShrExpr::create(e, ConstantExpr::create(mid, width)),
                         ConstantExpr::create(0, width))),
                     res);

      assert(success && "FIXME: Unhandled solver failure");
      (void)success;

      if (res) {
        hi = mid;
      } else {
        lo = mid + 1;
      }

      bits = lo;
    }

    // could binary search for training zeros and offset
    // min max but unlikely to be very useful

    // check common case
    bool res = false;
    bool success = mayBeTrue(
        query.withExpr(EqExpr::create(e, ConstantExpr::create(0, width))), res);

    assert(success && "FIXME: Unhandled solver failure");
    (void)success;

    if (res) {
      min = 0;
    } else {
      // binary search for min
      lo = 0, hi = bits64::maxValueOfNBits(bits);
      while (lo < hi) {
        mid = lo + (hi - lo) / 2;
        bool res = false;
        bool success = mayBeTrue(query.withExpr(UleExpr::create(
                                     e, ConstantExpr::create(mid, width))),
                                 res);

        assert(success && "FIXME: Unhandled solver failure");
        (void)success;

        if (res) {
          hi = mid;
        } else {
          lo = mid + 1;
        }
      }

      min = lo;
    }

    res = false;
    success = mayBeTrue(
        query.withExpr(EqExpr::create(
            e, ConstantExpr::create(bits64::maxValueOfNBits(bits), width))),
        res);

    assert(success && "FIXME: Unhandled solver failure");
    (void)success;

    if (res) {
      max = bits64::maxValueOfNBits(bits);
    } else {
      // binary search for max
      lo = min, hi = bits64::maxValueOfNBits(bits);
      while (lo < hi) {
        mid = lo + (hi - lo) / 2;
        bool res;
        bool success = mustBeTrue(query.withExpr(UleExpr::create(
                                      e, ConstantExpr::create(mid, width))),
                                  res);

        assert(success && "FIXME: Unhandled solver failure");
        (void)success;

        if (res) {
          hi = mid;
        } else {
          lo = mid + 1;
        }
      }

      max = lo;
    }
  }

  return std::make_pair(ConstantExpr::create(min, width),
                        ConstantExpr::create(max, width));
}

std::vector<const Array *> Query::gatherArrays() const {
  std::vector<const Array *> arrays = constraints.gatherArrays();
  findObjects(expr, arrays);
  return arrays;
}

std::vector<const Array *> Query::gatherSymcreteArrays() const {
  std::unordered_set<const Array *> arrays;
  for (const Array *array :
       ConstraintSet(std::vector<ref<Expr>>{expr}).gatherSymcreteArrays()) {
    arrays.insert(array);
  }
  for (const Array *array : constraints.gatherSymcreteArrays()) {
    arrays.insert(array);
  }
  return std::vector<const Array *>(arrays.begin(), arrays.end());
}

bool Query::containsSymcretes() const {
  return !gatherSymcreteArrays().empty();
}

void Query::dump() const {
  llvm::errs() << "Constraints [\n";
  for (const auto &constraint : constraints)
    constraint->dump();

  llvm::errs() << "]\n";
  llvm::errs() << "Query [\n";
  expr->dump();
  llvm::errs() << "]\n";
}

void ValidityCore::dump() const {
  Query(ConstraintSet(constraints), expr).dump();
}
