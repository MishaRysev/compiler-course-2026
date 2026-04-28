#include "mlir/Pass/Pass.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Support/TypeID.h"
#include "mlir/Tools/Plugins/PassPlugin.h"
#include "llvm/Support/Compiler.h"

using namespace mlir;

class TraceConditionPass
    : public PassWrapper<TraceConditionPass, OperationPass<ModuleOp>> {
public:
  StringRef getArgument() const final { return "trace-condition-pass"; }
  StringRef getDescription() const final {
    return "Insert trace_condition_* calls into scf.if and affine.if";
  }

  void runOnOperation() override;

private:
  void declareTraceFunctions(ModuleOp module);
  void insertBeginEnd(Region &region, StringRef beginFunc, StringRef endFunc);
  void instrumentIfOp(scf::IfOp ifOp);
  void instrumentAffineIfOp(affine::AffineIfOp ifOp);
};

void TraceConditionPass::runOnOperation() {
  ModuleOp module = getOperation();
  declareTraceFunctions(module);
  module.walk([&](Operation *op) {
    if (auto ifOp = dyn_cast<scf::IfOp>(op)) {
      instrumentIfOp(ifOp);
    } else if (auto affineIfOp = dyn_cast<affine::AffineIfOp>(op)) {
      instrumentAffineIfOp(affineIfOp);
    }
  });
}

void TraceConditionPass::declareTraceFunctions(ModuleOp module) {
  MLIRContext *ctx = module.getContext();
  OpBuilder builder(module.getBodyRegion());
  auto addFunc = [&](StringRef name) {
    if (!module.lookupSymbol<func::FuncOp>(name)) {
      auto funcType = FunctionType::get(ctx, TypeRange(), TypeRange());
      auto func = builder.create<func::FuncOp>(module.getLoc(), name, funcType);
      func.setVisibility(mlir::SymbolTable::Visibility::Private);
    }
  };
  addFunc("trace_condition_then_begin");
  addFunc("trace_condition_then_end");
  addFunc("trace_condition_else_begin");
  addFunc("trace_condition_else_end");
}

void TraceConditionPass::insertBeginEnd(Region &region, StringRef beginFunc,
                                        StringRef endFunc) {
  if (region.empty())
    return;
  for (Block &block : region.getBlocks()) {
    OpBuilder beginBuilder(&block, block.begin());
    beginBuilder.create<func::CallOp>(beginBuilder.getUnknownLoc(), beginFunc,
                                      TypeRange());
    auto *terminator = block.getTerminator();
    if (terminator) {
      OpBuilder endBuilder(terminator);
      endBuilder.create<func::CallOp>(endBuilder.getUnknownLoc(), endFunc,
                                      TypeRange());
    } else {
      OpBuilder endBuilder(&block, block.end());
      endBuilder.create<func::CallOp>(endBuilder.getUnknownLoc(), endFunc,
                                      TypeRange());
    }
  }
}

void TraceConditionPass::instrumentIfOp(scf::IfOp ifOp) {
  insertBeginEnd(ifOp.getThenRegion(), "trace_condition_then_begin",
                 "trace_condition_then_end");
  if (!ifOp.getElseRegion().empty())
    insertBeginEnd(ifOp.getElseRegion(), "trace_condition_else_begin",
                   "trace_condition_else_end");
}

void TraceConditionPass::instrumentAffineIfOp(affine::AffineIfOp ifOp) {
  insertBeginEnd(ifOp.getThenRegion(), "trace_condition_then_begin",
                 "trace_condition_then_end");
  if (!ifOp.getElseRegion().empty())
    insertBeginEnd(ifOp.getElseRegion(), "trace_condition_else_begin",
                   "trace_condition_else_end");
}

MLIR_DECLARE_EXPLICIT_TYPE_ID(TraceConditionPass)
MLIR_DEFINE_EXPLICIT_TYPE_ID(TraceConditionPass)

static mlir::PassPluginLibraryInfo getTraceConditionPassPluginInfo() {
  return {MLIR_PLUGIN_API_VERSION, "TraceConditionPass", "1.0",
          []() { mlir::PassRegistration<TraceConditionPass>(); }};
}

extern "C" LLVM_ATTRIBUTE_WEAK mlir::PassPluginLibraryInfo
mlirGetPassPluginInfo() {
  return getTraceConditionPassPluginInfo();
}