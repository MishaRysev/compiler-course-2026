#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include <map>
#include <vector>

using namespace clang;

namespace {

enum class ResourceKind { Memory, File };

struct AllocationInfo {
  const Expr *expr;
  SourceLocation loc;
  ResourceKind kind;
  bool freed;
};

struct FunctionContext {
  std::vector<AllocationInfo> allocations;
  llvm::DenseMap<const Expr *, unsigned> exprToIdx;
  llvm::DenseMap<const VarDecl *, unsigned> varToAllocIdx;
};

class ResourceLeakVisitor final
    : public RecursiveASTVisitor<ResourceLeakVisitor> {
public:
  explicit ResourceLeakVisitor(ASTContext *context, DiagnosticsEngine &diags)
      : m_diags(diags) {}

  bool TraverseFunctionDecl(FunctionDecl *FD) {
    if (!FD || !FD->hasBody())
      return true;

    m_contexts.push_back(FunctionContext());
    RecursiveASTVisitor::TraverseFunctionDecl(FD);
    FunctionContext &ctx = m_contexts.back();
    for (const AllocationInfo &AI : ctx.allocations) {
      if (!AI.freed) {
        std::string kindStr =
            (AI.kind == ResourceKind::Memory) ? "memory" : "file";
        m_diags.Report(AI.loc, m_warnID) << kindStr;
      }
    }
    m_contexts.pop_back();
    return true;
  }

  bool VisitCXXNewExpr(CXXNewExpr *NE) {
    if (!m_contexts.empty()) {
      addAllocation(NE, ResourceKind::Memory, NE->getExprLoc());
    }
    return true;
  }

  bool VisitCallExpr(CallExpr *CE) {
    if (m_contexts.empty())
      return true;

    FunctionDecl *callee = CE->getDirectCallee();
    if (!callee)
      return true;

    StringRef funcName = callee->getName();

    if (funcName == "malloc" || funcName == "calloc" || funcName == "realloc") {
      addAllocation(CE, ResourceKind::Memory, CE->getExprLoc());
    } else if (funcName == "fopen") {
      addAllocation(CE, ResourceKind::File, CE->getExprLoc());
    } else if (funcName == "free") {
      handleFreeCall(CE);
    } else if (funcName == "fclose") {
      handleFcloseCall(CE);
    }
    return true;
  }

  bool VisitCXXDeleteExpr(CXXDeleteExpr *DE) {
    if (m_contexts.empty())
      return true;
    handleDeleteExpr(DE);
    return true;
  }

  bool VisitVarDecl(VarDecl *VD) {
    if (m_contexts.empty() || !VD->hasInit())
      return true;

    Expr *init = VD->getInit()->IgnoreParenCasts();
    if (isAllocationExpr(init)) {
      unsigned idx =
          addAllocation(init, getKindForExpr(init), init->getExprLoc());
      FunctionContext &ctx = m_contexts.back();
      auto it = ctx.varToAllocIdx.find(VD);
      if (it != ctx.varToAllocIdx.end())
        ctx.varToAllocIdx.erase(it);
      ctx.varToAllocIdx[VD] = idx;
    }
    return true;
  }

  bool VisitBinaryOperator(BinaryOperator *BO) {
    if (m_contexts.empty() || !BO->isAssignmentOp())
      return true;

    Expr *lhs = BO->getLHS()->IgnoreParenCasts();
    Expr *rhs = BO->getRHS()->IgnoreParenCasts();

    DeclRefExpr *lhsDRE = dyn_cast<DeclRefExpr>(lhs);
    if (!lhsDRE)
      return true;

    VarDecl *VD = dyn_cast<VarDecl>(lhsDRE->getDecl());
    if (!VD)
      return true;

    FunctionContext &ctx = m_contexts.back();

    if (isAllocationExpr(rhs)) {
      unsigned idx = addAllocation(rhs, getKindForExpr(rhs), rhs->getExprLoc());
      auto it = ctx.varToAllocIdx.find(VD);
      if (it != ctx.varToAllocIdx.end())
        ctx.varToAllocIdx.erase(it);
      ctx.varToAllocIdx[VD] = idx;
    } else {
      auto it = ctx.varToAllocIdx.find(VD);
      if (it != ctx.varToAllocIdx.end())
        ctx.varToAllocIdx.erase(it);
    }
    return true;
  }

  void setWarnID(unsigned id) { m_warnID = id; }

private:
  DiagnosticsEngine &m_diags;
  unsigned m_warnID;
  std::vector<FunctionContext> m_contexts;

  bool isAllocationExpr(Expr *E) {
    if (isa<CXXNewExpr>(E))
      return true;
    CallExpr *CE = dyn_cast<CallExpr>(E);
    if (!CE)
      return false;
    FunctionDecl *callee = CE->getDirectCallee();
    if (!callee)
      return false;
    StringRef name = callee->getName();
    return name == "malloc" || name == "calloc" || name == "realloc" ||
           name == "fopen";
  }

  ResourceKind getKindForExpr(Expr *E) {
    if (isa<CXXNewExpr>(E))
      return ResourceKind::Memory;
    CallExpr *CE = cast<CallExpr>(E);
    FunctionDecl *callee = CE->getDirectCallee();
    StringRef name = callee->getName();
    if (name == "fopen")
      return ResourceKind::File;
    return ResourceKind::Memory;
  }

  unsigned addAllocation(Expr *E, ResourceKind kind, SourceLocation loc) {
    FunctionContext &ctx = m_contexts.back();
    auto it = ctx.exprToIdx.find(E);
    if (it != ctx.exprToIdx.end())
      return it->second;

    unsigned idx = ctx.allocations.size();
    ctx.allocations.push_back({E, loc, kind, false});
    ctx.exprToIdx[E] = idx;
    return idx;
  }

  void handleFreeCall(CallExpr *CE) {
    if (CE->getNumArgs() == 0)
      return;
    Expr *arg = CE->getArg(0)->IgnoreParenCasts();
    handleFreeLike(arg, ResourceKind::Memory);
  }

  void handleFcloseCall(CallExpr *CE) {
    if (CE->getNumArgs() == 0)
      return;
    Expr *arg = CE->getArg(0)->IgnoreParenCasts();
    handleFreeLike(arg, ResourceKind::File);
  }

  void handleDeleteExpr(CXXDeleteExpr *DE) {
    Expr *arg = DE->getArgument()->IgnoreParenCasts();
    handleFreeLike(arg, ResourceKind::Memory);
  }

  void handleFreeLike(Expr *arg, ResourceKind kind) {
    FunctionContext &ctx = m_contexts.back();

    if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(arg)) {
      if (VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
        auto it = ctx.varToAllocIdx.find(VD);
        if (it != ctx.varToAllocIdx.end()) {
          unsigned idx = it->second;
          if (ctx.allocations[idx].kind == kind) {
            ctx.allocations[idx].freed = true;
            ctx.varToAllocIdx.erase(it);
          }
        }
      }
    } else {
      auto it = ctx.exprToIdx.find(arg);
      if (it != ctx.exprToIdx.end()) {
        unsigned idx = it->second;
        if (ctx.allocations[idx].kind == kind) {
          ctx.allocations[idx].freed = true;
          auto it2 = ctx.varToAllocIdx.begin();
          while (it2 != ctx.varToAllocIdx.end()) {
            if (it2->second == idx)
              ctx.varToAllocIdx.erase(it2++);
            else
              ++it2;
          }
        }
      }
    }
  }
};

class ResourceLeakConsumer final : public ASTConsumer {
public:
  explicit ResourceLeakConsumer(ASTContext *context, DiagnosticsEngine &diags,
                                unsigned warnID)
      : m_visitor(context, diags) {
    m_visitor.setWarnID(warnID);
  }

  void HandleTranslationUnit(ASTContext &context) override {
    m_visitor.TraverseDecl(context.getTranslationUnitDecl());
  }

private:
  ResourceLeakVisitor m_visitor;
};

class ResourceLeakAction final : public PluginASTAction {
public:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &ci,
                                                 llvm::StringRef) override {
    return std::make_unique<ResourceLeakConsumer>(
        &ci.getASTContext(), ci.getDiagnostics(), m_warnID);
  }

  bool ParseArgs(const CompilerInstance &ci,
                 const std::vector<std::string> &args) override {
    DiagnosticsEngine &diags = ci.getDiagnostics();
    m_warnID = diags.getCustomDiagID(DiagnosticsEngine::Warning,
                                     "resource leak: %0 allocated here");
    return true;
  }

private:
  unsigned m_warnID;
};

} // namespace

static FrontendPluginRegistry::Add<ResourceLeakAction>
    X("rysev_m_lab_1",
      "Detects resource leaks (memory from new/malloc, files from fopen)");