#ifndef ARRAYS_H
#define ARRAYS_H

#include "SolverTheory.h"
#include "SolverAdapter.h"

#include "klee/Expr/Expr.h"

#include <map>
#include <vector>

namespace klee {

/* Arrays theory */
struct Arrays : public SolverTheory {
private:

  ref<TheoryResponse> array(const ref<ReadExpr> &readExpr) {
    ref<ExprHandle> result = nullptr;
    // ref<ExprHandle> result =< solverAdapter->array();

    ref<UpdateNode> node = readExpr->updates.head;
    
    std::vector<ref<Expr>> required;
    required.reserve(readExpr->updates.getSize() * 2);

    while (node != nullptr) {
      required.push_back(node->index);
      required.push_back(node->value);
      node = node->next;
    }

    std::function<ref<ExprHandle>(const ExprHashMap<ref<ExprHandle>> &)>
        completer = [*this, result,
                     readExpr](const ExprHashMap<ref<ExprHandle>> &map) mutable
        -> ref<ExprHandle> {
      ref<UpdateNode> node = readExpr->updates.head;
      while (!node.isNull()) {
        result = write(result, map.at(node->index), map.at(node->value));
        node = node->next;
      }
      return result;
    };


    assert(0 && "Not implemented");
    // /*
    //  * Optimization on values on constant indices.
    //  * Forms expression in form of 
    //  * - ReadExpr Idx Array == ConstantExpr 
    //  */
    // const Array *array = readExpr->updates.root;
    // if (ref<ConstantSource> constantSource =
    //         dyn_cast<ConstantSource>(array->source)) {
    //   const std::vector<ref<ConstantExpr>> &constantValues =
    //       constantSource->constantValues;
    //   for (unsigned idx = 0; idx < constantValues.size(); ++idx) {
    //     assert(0);
    //     ref<SolverBuilder> parentBuilder = nullptr;
    //     ref<ExprHandle> constantAssertion =
    //         parentBuilder->build(constantValues.at(idx));

    //   }
    // }

    return new IncompleteResponse(completer, required);
  }

protected:
  ref<TheoryResponse> translate(const ref<Expr> &expr,
                                const ExprHandleList &args) override {
    switch (expr->getKind()) {
    case Expr::Kind::Read: {
      ref<ReadExpr> readExpr = cast<ReadExpr>(expr);

      ref<ExprHandle> indexExprHandle = args[0];
      ref<TheoryResponse> arrayResponse = array(readExpr);

      if (ref<IncompleteResponse> arrayIncompleteResponse =
              dyn_cast<IncompleteResponse>(arrayResponse)) {

        IncompleteResponse::completer_t completer =
            [*this, arrayIncompleteResponse, indexExprHandle](
                const ExprHashMap<ref<ExprHandle>> &required) mutable
            -> ref<ExprHandle> {
          ref<ExprHandle> completerSubResponse =
              arrayIncompleteResponse->completer(required);
          return read(completerSubResponse, indexExprHandle);
        };

        return new IncompleteResponse(completer,
                                      arrayIncompleteResponse->toBuild);
      } else {
        ref<CompleteResponse> arrayCompleteResponse =
            cast<CompleteResponse>(arrayResponse);
        return new CompleteResponse(
            read(arrayCompleteResponse->expr(), indexExprHandle));
      }
    }
    default: {
      return nullptr;
    }
    }
  }

public:
  Arrays(const ref<SolverAdapter> &solverAdapter)
      : SolverTheory(solverAdapter) {}

  ref<ExprHandle> sort(const ref<ExprHandle> &domainSort,
                       const ref<ExprHandle> &rangeSort) {
    return solverAdapter->array(domainSort, rangeSort);
  }

  ref<ExprHandle> read(const ref<ExprHandle> &array,
                       const ref<ExprHandle> &index) {
    return solverAdapter->read(array, index);
  }

  ref<ExprHandle> write(const ref<ExprHandle> &array,
                        const ref<ExprHandle> &index,
                        const ref<ExprHandle> &value) {
    return solverAdapter->write(array, index, value);
  }
};

} // namespace klee

#endif
