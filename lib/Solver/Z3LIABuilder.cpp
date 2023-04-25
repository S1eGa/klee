#include "Z3LIABuilder.h"

#include "z3.h"
#include "llvm/ADT/APInt.h"

using namespace klee;

Z3SortHandle Z3LIABuilder::liaSort() { return {Z3_mk_int_sort(ctx), ctx}; }

Z3ASTHandleLIA
Z3LIABuilder::handleUnsignedOverflow(const Z3ASTHandleLIA &expr) {
  assert(!expr.sign());

  // Unsigned overflow
  // assumes: expr \in [-(2**w)+1, 2**(w+1)-2]
  // gives:   sum = (a + b >= 2**w) ? (a + b - 2**w) : (a + b);
  Z3ASTHandleLIA maxUnsignedInt =
      liaUnsignedConst(llvm::APInt::getMaxValue(expr.getWidth() + 1));
  Z3_ast condition = Z3_mk_ge(ctx, expr, maxUnsignedInt);

  Z3_ast subArgs[] = {expr, maxUnsignedInt};
  Z3_ast ite = Z3_mk_ite(ctx, condition, Z3_mk_sub(ctx, 2, subArgs), expr);

  return {ite, ctx, expr.getWidth(), false};
}

Z3ASTHandleLIA
Z3LIABuilder::handleUnsignedUnderflow(const Z3ASTHandleLIA &expr) {
  assert(!expr.sign());

  // Unsigned underflow
  // assumes: expr \in [-(2**w)+1, 2**(w+1)-2]
  // gives:   sum = (a+b < 0) ? (a+b+2**w) : (a+b);
  Z3ASTHandleLIA maxUnsignedInt =
      liaUnsignedConst(llvm::APInt::getHighBitsSet(expr.getWidth() + 1, 1));
  llvm::APInt nullValue = llvm::APInt::getNullValue(expr.getWidth());

  Z3_ast condition = Z3_mk_lt(ctx, expr, liaUnsignedConst(nullValue));

  Z3_ast addArgs[] = {expr, maxUnsignedInt};
  Z3_ast ite = Z3_mk_ite(ctx, condition, Z3_mk_add(ctx, 2, addArgs), expr);

  return {ite, ctx, expr.getWidth(), false};
}

Z3ASTHandleLIA Z3LIABuilder::handleSignedOverflow(const Z3ASTHandleLIA &expr) {
  assert(expr.sign());

  // Signed overflow:
  // assumes: expr \in [-(2**w), 2**w-2]
  // gives:   sum = (a+b > 2**(w-1)-1) ? (2**(w-1)-1-(a+b)) : (a+b)
  Z3ASTHandleLIA maxSignedInt =
      liaSignedConst(llvm::APInt::getMaxValue(expr.getWidth()));
  Z3_ast condition = Z3_mk_gt(ctx, expr, maxSignedInt);

  Z3_ast overflowASTArgs[] = {maxSignedInt, expr};
  Z3_ast preparedExpr = Z3_mk_sub(ctx, 2, overflowASTArgs);

  return {Z3_mk_ite(ctx, condition, preparedExpr, expr), ctx, expr.getWidth(),
          expr.sign()};
}

Z3ASTHandleLIA Z3LIABuilder::handleSignedUnderflow(const Z3ASTHandleLIA &expr) {
  assert(expr.sign());

  // Signed underflow:
  // assumes: expr \in [-(2**w), 2**w-2]
  // gives:   sum = (a+b < -2**(w-1) ? -2**(w-1)-(a+b) : a+b)
  Z3ASTHandleLIA minSignedInt =
      liaSignedConst(llvm::APInt::getSignedMinValue(expr.getWidth()));
  Z3_ast condition = Z3_mk_lt(ctx, expr, minSignedInt);

  Z3_ast overflowASTArgs[] = {minSignedInt, expr};

  Z3_ast preparedExpr =
      Z3_mk_unary_minus(ctx, Z3_mk_sub(ctx, 2, overflowASTArgs));

  return {Z3_mk_ite(ctx, condition, preparedExpr, expr), ctx, expr.getWidth(),
          expr.sign()};
}

Z3ASTHandleLIA Z3LIABuilder::castToSigned(const Z3ASTHandleLIA &expr) {
  if (expr.sign()) {
    return expr;
  }

  Z3ASTHandleLIA signedExpr = {expr, ctx, expr.getWidth(), true};
  return handleSignedOverflow(signedExpr);
}

Z3ASTHandleLIA Z3LIABuilder::castToUnsigned(const Z3ASTHandleLIA &expr) {
  if (!expr.sign()) {
    return expr;
  }

  Z3ASTHandleLIA unsignedExpr = {expr, ctx, expr.getWidth(), false};
  return handleUnsignedUnderflow(expr);
}

Z3ASTHandleLIA Z3LIABuilder::liaUnsignedConst(const llvm::APInt &value) {
  std::string valueString = value.toString(10, true);
  Z3_string s = valueString.c_str();
  return {Z3_mk_numeral(ctx, s, liaSort()), ctx, value.getBitWidth(), false};
}

Z3ASTHandleLIA Z3LIABuilder::liaSignedConst(const llvm::APInt &value) {
  std::string valueString = value.toString(10, true);
  Z3_string s = valueString.c_str();
  return {Z3_mk_numeral(ctx, s, liaSort()), ctx, value.getBitWidth(), true};
}

Z3ASTHandleLIA Z3LIABuilder::liaUleExpr(const Z3ASTHandleLIA &lhs,
                                        const Z3ASTHandleLIA &rhs) {
  Z3ASTHandleLIA unsignedLhs = castToUnsigned(lhs);
  Z3ASTHandleLIA unsignedRhs = castToUnsigned(rhs);
  return {Z3_mk_le(ctx, unsignedLhs, unsignedRhs), ctx, lhs.getWidth(), false};
}

Z3ASTHandleLIA Z3LIABuilder::liaUltExpr(const Z3ASTHandleLIA &lhs,
                                        const Z3ASTHandleLIA &rhs) {
  Z3ASTHandleLIA signedLhs = castToSigned(lhs);
  Z3ASTHandleLIA signedRhs = castToSigned(rhs);
  return {Z3_mk_le(ctx, signedLhs, signedRhs), ctx, lhs.getWidth(), false};
}

Z3ASTHandleLIA Z3LIABuilder::liaSleExpr(const Z3ASTHandleLIA &lhs,
                                        const Z3ASTHandleLIA &rhs) {
  Z3ASTHandleLIA signedLhs = castToSigned(lhs);
  Z3ASTHandleLIA signedRhs = castToSigned(rhs);
  return {Z3_mk_le(ctx, signedLhs, signedRhs), ctx, lhs.getWidth(), false};
}

Z3ASTHandleLIA Z3LIABuilder::liaSltExpr(const Z3ASTHandleLIA &lhs,
                                        const Z3ASTHandleLIA &rhs) {
  Z3ASTHandleLIA signedLhs = castToSigned(lhs);
  Z3ASTHandleLIA signedRhs = castToSigned(rhs);
  return {Z3_mk_lt(ctx, signedLhs, signedRhs), ctx, lhs.getWidth(), false};
}

Z3ASTHandleLIA Z3LIABuilder::liaAddExpr(const Z3ASTHandleLIA &lhs,
                                        const Z3ASTHandleLIA &rhs) {
  if (!lhs.sign() || !rhs.sign()) {
    // signed + unsigned
    // unsigned + unsigned
    const Z3_ast args[] = {castToUnsigned(lhs), castToUnsigned(rhs)};
    Z3ASTHandleLIA sumExpr(Z3_mk_add(ctx, 2, args), ctx, lhs.getWidth(),
                           lhs.sign());

    return handleUnsignedOverflow(sumExpr);
  } else {
    // signed + signed
    // overflow or underflow?
    // is this way better or make a cast to unsigned?
    const Z3_ast args[] = {lhs, rhs};
    Z3ASTHandleLIA sumExpr(Z3_mk_add(ctx, 2, args), ctx, lhs.getWidth(),
                           lhs.sign());
    return handleSignedUnderflow(handleSignedOverflow(sumExpr));
  }
}

Z3ASTHandleLIA Z3LIABuilder::liaSubExpr(const Z3ASTHandleLIA &lhs,
                                        const Z3ASTHandleLIA &rhs) {
  if (!lhs.sign() || !rhs.sign()) {
    // signed + unsigned
    // unsigned + unsigned
    const Z3_ast args[] = {castToUnsigned(lhs), castToUnsigned(rhs)};
    Z3ASTHandleLIA sumExpr(Z3_mk_sub(ctx, 2, args), ctx, lhs.getWidth(),
                           lhs.sign());

    return handleUnsignedUnderflow(sumExpr);
  } else {
    // signed + signed
    // overflow or underflow?
    // is this way better or make a cast to unsigned?
    const Z3_ast args[] = {lhs, rhs};
    Z3ASTHandleLIA sumExpr(Z3_mk_sub(ctx, 2, args), ctx, lhs.getWidth(),
                           lhs.sign());
    return handleSignedUnderflow(handleSignedOverflow(sumExpr));
  }
}

Z3ASTHandleLIA Z3LIABuilder::liaMulExpr(const Z3ASTHandleLIA &lhs,
                                        const Z3ASTHandleLIA &rhs) {
  const Z3_ast args[] = {lhs, rhs};
  return {Z3_mk_mul(ctx, 2, args), ctx, lhs.getWidth(), lhs.sign()};
}

Z3ASTHandleLIA Z3LIABuilder::liaUdivExpr(const Z3ASTHandleLIA &lhs,
                                         const Z3ASTHandleLIA &rhs) {
  Z3ASTHandleLIA unsignedLhs = castToUnsigned(lhs);
  Z3ASTHandleLIA unsignedRhs = castToUnsigned(rhs);
  return {Z3_mk_div(ctx, unsignedLhs, unsignedRhs), ctx, lhs.getWidth(), false};
}

Z3ASTHandleLIA Z3LIABuilder::liaSdivExpr(const Z3ASTHandleLIA &lhs,
                                         const Z3ASTHandleLIA &rhs) {
  Z3ASTHandleLIA signedLhs = castToSigned(lhs);
  Z3ASTHandleLIA signedRhs = castToSigned(rhs);
  return {Z3_mk_div(ctx, signedLhs, signedRhs), ctx, lhs.getWidth(), true};
}

Z3ASTHandleLIA Z3LIABuilder::liaZextExpr(const Z3ASTHandleLIA &expr,
                                         unsigned width) {
  return {expr, ctx, expr.getWidth(), expr.sign()};
}

Z3ASTHandleLIA Z3LIABuilder::liaSextExpr(const Z3ASTHandleLIA &expr,
                                         unsigned width) {
  return {castToSigned(expr), ctx, width, true};
}

Z3ASTHandleLIA Z3LIABuilder::liaAnd(const Z3ASTHandleLIA &lhs,
                                    const Z3ASTHandleLIA &rhs) {
  assert(lhs.getWidth() == rhs.getWidth() && lhs.getWidth() == 1);
  Z3_ast args[] = {lhs, rhs};
  return {Z3_mk_and(ctx, 2, args), ctx, 1, false};
}

Z3ASTHandleLIA Z3LIABuilder::liaOr(const Z3ASTHandleLIA &lhs,
                                   const Z3ASTHandleLIA &rhs) {
  assert(lhs.getWidth() == rhs.getWidth() && lhs.getWidth() == 1);
  Z3_ast args[] = {lhs, rhs};
  return {Z3_mk_or(ctx, 2, args), ctx, 1, false};
}

Z3ASTHandleLIA Z3LIABuilder::liaXor(const Z3ASTHandleLIA &lhs,
                                    const Z3ASTHandleLIA &rhs) {
  assert(lhs.getWidth() == rhs.getWidth() && lhs.getWidth() == 1);
  return {Z3_mk_xor(ctx, lhs, rhs), ctx, 1, false};
}

Z3ASTHandleLIA Z3LIABuilder::liaNot(const Z3ASTHandleLIA &expr) {
  assert(expr.getWidth() == 1);
  return {Z3_mk_not(ctx, expr), ctx, 1, false};
}

Z3ASTHandleLIA Z3LIABuilder::liaEq(const Z3ASTHandleLIA &lhs,
                                   const Z3ASTHandleLIA &rhs) {
  return {Z3_mk_eq(ctx, lhs, rhs), ctx, 1, false};
}

Z3ASTHandleLIA Z3LIABuilder::liaIte(const Z3ASTHandleLIA &condition,
                                    const Z3ASTHandleLIA &whenTrue,
                                    const Z3ASTHandleLIA &whenFalse) {

  if (whenTrue.sign() != whenFalse.sign()) {
    return {Z3_mk_ite(ctx, condition, castToUnsigned(whenTrue),
                      castToUnsigned(whenFalse)),
            ctx, whenTrue.getWidth(), false};
  }
  return {Z3_mk_ite(ctx, condition, whenTrue, whenFalse), ctx,
          whenTrue.getWidth(), whenTrue.sign()};
}

Z3ASTHandleLIA Z3LIABuilder::liaConcatExpr(const Z3ASTHandleLIA &lhs,
                                           const Z3ASTHandleLIA &rhs) {
  Z3ASTHandleLIA shift =
      liaUnsignedConst(llvm::APInt::getHighBitsSet(rhs.getWidth() + 1, 1));
  Z3_ast args[] = {lhs, shift};

  Z3ASTHandleLIA shiftedLhs = {Z3_mk_mul(ctx, 2, args), ctx,
                               lhs.getWidth() + rhs.getWidth(), false};

  return liaAddExpr(shiftedLhs, castToUnsigned(rhs));
}

Z3ASTHandleLIA Z3LIABuilder::liaGetInitialArray(const Array *root) {
  assert(root);
  Z3ASTHandleLIA array_expr;
  bool hashed = arrHashLIA.lookupArrayExpr(root, array_expr);

  if (!hashed) {
    // Unique arrays by name, so we make sure the name is unique by
    // using the size of the array hash as a counter.
    std::string unique_id = llvm::utostr(arrHashLIA._array_hash.size());
    std::string unique_name = root->name + unique_id;
    if (ref<ConstantWithSymbolicSizeSource> constantWithSymbolicSizeSource =
            dyn_cast<ConstantWithSymbolicSizeSource>(root->source)) {
      array_expr = liaBuildConstantArray(
          unique_name.c_str(),
          llvm::APInt(root->getDomain(),
                      constantWithSymbolicSizeSource->defaultValue));
    } else {
      array_expr =
          liaBuildArray(unique_name.c_str(), root->getDomain());
    }

    if (root->isConstantArray() && constant_array_assertions.count(root) == 0) {
      std::vector<Z3ASTHandle> array_assertions;
      for (unsigned i = 0, e = root->constantValues.size(); i != e; ++i) {
        // construct(= (select i root) root->value[i]) to be asserted in
        // Z3Solver.cpp
        Z3ASTHandleLIA array_value =
            constructLIA(root->constantValues[i]);
        array_assertions.push_back(liaEq(
            liaReadExpr(array_expr,
                        liaUnsignedConst(llvm::APInt(root->getDomain(), i))),
            array_value));
      }
      constant_array_assertions[root] = std::move(array_assertions);
    }

    arrHashLIA.hashArrayExpr(root, array_expr);
  }

  return array_expr;
}

Z3ASTHandleLIA Z3LIABuilder::liaGetArrayForUpdate(const Array *root,
                                                  const UpdateNode *un) {
  if (!un) {
    return liaGetInitialArray(root);
  } else {
    // FIXME: This really needs to be non-recursive.
    Z3ASTHandleLIA un_expr;
    bool hashed = arrHashLIA.lookupUpdateNodeExpr(un, un_expr);

    if (!hashed) {
      un_expr = liaWriteExpr(liaGetArrayForUpdate(root, un->next.get()),
                             constructLIA(un->index),
                             constructLIA(un->value));

      arrHashLIA.hashUpdateNodeExpr(un, un_expr);
    }

    return un_expr;
  }
}

Z3ASTHandleLIA Z3LIABuilder::liaBuildArray(const char *name, unsigned width) {
  Z3SortHandle t = getArraySort(liaSort(), liaSort());
  Z3_symbol s = Z3_mk_string_symbol(ctx, const_cast<char *>(name));
  return { Z3_mk_const(ctx, s, t), ctx, width, false };
}

Z3ASTHandleLIA
Z3LIABuilder::liaBuildConstantArray(const char *name,
                                    const llvm::APInt &defaultValue) {
  Z3ASTHandleLIA liaDefaultValue = liaUnsignedConst(defaultValue);
  return {Z3_mk_const_array(ctx, liaSort(), liaDefaultValue), ctx,
          liaDefaultValue.getWidth(), false};
}

Z3ASTHandleLIA Z3LIABuilder::liaWriteExpr(const Z3ASTHandleLIA &array,
                                          const Z3ASTHandleLIA &index,
                                          const Z3ASTHandleLIA &value) {
  return {Z3_mk_store(ctx, array, index, castToUnsigned(value)), ctx,
          array.getWidth(), false};
}

Z3ASTHandleLIA Z3LIABuilder::liaReadExpr(const Z3ASTHandleLIA &array,
                                         const Z3ASTHandleLIA &index) {
  return {Z3_mk_select(ctx, array, index), ctx, array.getWidth(), false};
}

Z3ASTHandleLIA Z3LIABuilder::constructLIA(const ref<Expr> &e) {
  if (!Z3HashConfig::UseConstructHashZ3 || isa<ConstantExpr>(e)) {
    return constructActualLIA(e);
  } else {
    auto it = constructedLIA.find(e);
    if (it != constructedLIA.end()) {
      return it->second;
    } else {
      Z3ASTHandleLIA res = constructActualLIA(e);
      constructedLIA.insert(std::make_pair(e, res));
      return res;
    }
  }
}

Z3ASTHandle Z3LIABuilder::construct(ref<Expr> e, int *width_out) {
  isBroken = false;
  Z3ASTHandleLIA result = constructLIA(e);
  if (width_out) {
    *width_out = result.getWidth();
  }
  return result;
}


/** if *width_out!=1 then result is a bitvector,
    otherwise it is a bool */
Z3ASTHandleLIA Z3LIABuilder::constructActualLIA(const ref<Expr> &e) {
  ++stats::queryConstructs;

  switch (e->getKind()) {
  case Expr::Constant: {
    ref<ConstantExpr> CE = cast<ConstantExpr>(e);
    // Coerce to bool if necessary.
    if (CE->getWidth() == 1)
      return CE->isTrue() ? Z3ASTHandleLIA{Z3_mk_true(ctx), ctx, 1, false}
                          : Z3ASTHandleLIA{Z3_mk_true(ctx), ctx, 0, false};

    return liaUnsignedConst(CE->getAPValue());
  }

  // Special
  case Expr::NotOptimized: {
    ref<NotOptimizedExpr> noe = cast<NotOptimizedExpr>(e);
    return constructLIA(noe->src);
  }

  case Expr::Read: {
    ref<ReadExpr> re = cast<ReadExpr>(e);
    assert(re && re->updates.root);
    return liaReadExpr(
        liaGetArrayForUpdate(re->updates.root, re->updates.head.get()),
        constructLIA(re->index));
  }

  case Expr::Select: {
    ref<SelectExpr> se = cast<SelectExpr>(e);
    Z3ASTHandleLIA cond = constructLIA(se->cond);
    Z3ASTHandleLIA tExpr = constructLIA(se->trueExpr);
    Z3ASTHandleLIA fExpr = constructLIA(se->falseExpr);
    return liaIte(cond, tExpr, fExpr);
  }

  case Expr::Concat: {
    ref<ConcatExpr> ce = cast<ConcatExpr>(e);
    int numKids = static_cast<int>(ce->getNumKids());
    Z3ASTHandleLIA res = constructLIA(ce->getKid(numKids - 1));

    for (int i = numKids - 2; i >= 0; i--) {
      Z3ASTHandleLIA kidExpr = constructLIA(ce->getKid(i));
      res = liaConcatExpr(kidExpr, res);
    }
    return res;
  }

    // Casting

  case Expr::ZExt: {
    ref<CastExpr> ce = cast<CastExpr>(e);
    Z3ASTHandleLIA src = constructLIA(ce->src);
    if (ce->getWidth() == 1) {
      return liaIte(src, liaUnsignedConst(llvm::APInt(1, 1)),
                    liaUnsignedConst(llvm::APInt(1, 0)));
    } else {
      assert(ce->getWidth() < ce->getWidth());
      return liaZextExpr(src, ce->getWidth());
    }
  }

  case Expr::SExt: {
    ref<CastExpr> ce = cast<CastExpr>(e);
    Z3ASTHandleLIA src = constructLIA(ce->src);
    if (ce->getWidth() == 1) {
      return liaIte(src, liaSignedConst(llvm::APInt(1, -1)),
                    liaSignedConst(llvm::APInt(1, 0)));
    } else {
      return liaSextExpr(src, ce->getWidth());
    }
  }

  // Arithmetic
  case Expr::Add: {
    ref<AddExpr> ae = cast<AddExpr>(e);
    Z3ASTHandleLIA left = constructLIA(ae->left);
    Z3ASTHandleLIA right = constructLIA(ae->right);
    return liaAddExpr(left, right);
  }

  case Expr::Sub: {
    ref<SubExpr> se = cast<SubExpr>(e);
    Z3ASTHandleLIA left = constructLIA(se->left);
    Z3ASTHandleLIA right = constructLIA(se->right);
    return liaSubExpr(left, right);
  }

  // Bitwise
  case Expr::Not: {
    ref<NotExpr> ne = cast<NotExpr>(e);
    Z3ASTHandleLIA expr = constructLIA(ne->expr);
    if (expr.getWidth() == 1) {
      return liaNot(expr);
    } else {
      isBroken = true;
      return liaUnsignedConst(llvm::APInt(e->getWidth(), 0));
    }
  }

  case Expr::And: {
    ref<AndExpr> ae = cast<AndExpr>(e);
    Z3ASTHandleLIA left = constructLIA(ae->left);
    Z3ASTHandleLIA right = constructLIA(ae->right);
    if (left.getWidth() == 1) {
      return liaAnd(left, right);
    } else {
      isBroken = true;
      return liaUnsignedConst(llvm::APInt(e->getWidth(), 0));
    }
  }

  case Expr::Or: {
    ref<OrExpr> oe = cast<OrExpr>(e);
    Z3ASTHandleLIA left = constructLIA(oe->left);
    Z3ASTHandleLIA right = constructLIA(oe->right);
    if (left.getWidth() == 1) {
      return liaOr(left, right);
    } else {
      isBroken = true;
      return liaUnsignedConst(llvm::APInt(e->getWidth(), 0));
    }
  }

  case Expr::Xor: {
    ref<XorExpr> xe = cast<XorExpr>(e);
    Z3ASTHandleLIA left = constructLIA(xe->left);
    Z3ASTHandleLIA right = constructLIA(xe->right);

    if (left.getWidth() == 1) {
      // XXX check for most efficient?
      return liaXor(left, right);
    } else {
      isBroken = true;
      return liaUnsignedConst(llvm::APInt(e->getWidth(), 0));
    }
  }
    // Comparison

  case Expr::Eq: {
    ref<EqExpr> ee = cast<EqExpr>(e);
    Z3ASTHandleLIA left = constructLIA(ee->left);
    Z3ASTHandleLIA right = constructLIA(ee->right);
    if (left.getWidth() == 1) {
      if (ref<ConstantExpr> CE = dyn_cast<ConstantExpr>(ee->left)) {
        if (CE->isTrue())
          return right;
        return liaNot(right);
      } else {
        return liaEq(left, right);
      }
    } else {
      return liaEq(left, right);
    }
  }

  case Expr::Ult: {
    ref<UltExpr> ue = cast<UltExpr>(e);
    Z3ASTHandleLIA left = constructLIA(ue->left);
    Z3ASTHandleLIA right = constructLIA(ue->right);
    return liaUltExpr(left, right);
  }

  case Expr::Ule: {
    ref<UleExpr> ue = cast<UleExpr>(e);
    Z3ASTHandleLIA left = constructLIA(ue->left);
    Z3ASTHandleLIA right = constructLIA(ue->right);
    return liaUleExpr(left, right);
  }

  case Expr::Slt: {
    ref<SltExpr> se = cast<SltExpr>(e);
    Z3ASTHandleLIA left = constructLIA(se->left);
    Z3ASTHandleLIA right = constructLIA(se->right);
    return liaSltExpr(left, right);
  }

  case Expr::Sle: {
    ref<SleExpr> se = cast<SleExpr>(e);
    Z3ASTHandleLIA left = constructLIA(se->left);
    Z3ASTHandleLIA right = constructLIA(se->right);
    return liaSleExpr(left, right);
  }

  case Expr::Mul:
  case Expr::UDiv:
  case Expr::SDiv:
  case Expr::URem:
  case Expr::SRem:
  case Expr::Shl:
  case Expr::LShr:
  case Expr::Extract:
  case Expr::AShr:
    isBroken = true;
    return liaUnsignedConst(llvm::APInt(e->getWidth(), 0));
// unused due to canonicalization
#if 0
  case Expr::Ne:
  case Expr::Ugt:
  case Expr::Uge:
  case Expr::Sgt:
  case Expr::Sge:
#endif

  default:
    assert(0 && "unhandled Expr type");
  }
}
