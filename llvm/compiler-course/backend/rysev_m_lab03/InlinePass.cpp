#include "X86.h"
#include "X86InstrInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include <map>

using namespace llvm;

#define DEBUG_TYPE "example-x86-inline"

namespace {

class RysevInlining : public ModulePass {
public:
  static char ID;
  RysevInlining() : ModulePass(ID) {}

  bool runOnModule(Module &M) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineModuleInfoWrapperPass>();
    ModulePass::getAnalysisUsage(AU);
  }

  StringRef getPassName() const override {
    return "Rysev Inlining Pass (ModulePass)";
  }

private:
  static const unsigned MAX_INSTR = 15;
  static const unsigned MAX_REC_DEPTH = 3;

  using DepthMap = std::map<const Function *, unsigned>;
  DepthMap recursionDepth;

  bool canBeInlined(const MachineFunction &MF) const;
  bool tryInline(MachineFunction &caller, MachineBasicBlock &block,
                 MachineInstr &callMI, DepthMap &depthMap);
};

char RysevInlining::ID = 0;

bool RysevInlining::canBeInlined(const MachineFunction &MF) const {
  if (MF.size() != 1)
    return false;

  unsigned cnt = 0;
  for (const MachineBasicBlock &BB : MF)
    for (const MachineInstr &MI : BB)
      if (!MI.isDebugInstr() && ++cnt > MAX_INSTR)
        return false;
  return true;
}

bool RysevInlining::tryInline(MachineFunction &caller,
                              MachineBasicBlock &block,
                              MachineInstr &callMI,
                              DepthMap &depthMap) {
  if (callMI.getOpcode() != X86::CALL64pcrel32)
    return false;
  if (callMI.getNumOperands() == 0)
    return false;

  MachineOperand &op0 = callMI.getOperand(0);
  if (!op0.isGlobal())
    return false;

  const Function *targetFn = dyn_cast<Function>(op0.getGlobal());
  if (!targetFn)
    return false;

  if (depthMap[targetFn] >= MAX_REC_DEPTH)
    return false;

  bool isRecursive = (targetFn == &caller.getFunction());

  MachineFunction *calleeMF = nullptr;
  if (isRecursive) {
    calleeMF = &caller;
  } else {
    auto &MMI = getAnalysis<MachineModuleInfoWrapperPass>().getMMI();
    calleeMF = MMI.getMachineFunction(*targetFn);
    if (!calleeMF)
      return false;
  }

  if (!canBeInlined(*calleeMF))
    return false;

  depthMap[targetFn]++;

  MachineRegisterInfo &callerMRI = caller.getRegInfo();
  MachineBasicBlock &calleeEntry = calleeMF->front();

  std::map<Register, Register> regMap;
  SmallVector<MachineInstr *, 16> instrsToClone;

  for (MachineInstr &mi : calleeEntry)
    if (!mi.isReturn())
      instrsToClone.push_back(&mi);

  for (MachineInstr *origMI : instrsToClone) {
    MachineInstr *clonedMI = caller.CloneMachineInstr(origMI);

    for (MachineOperand &mo : clonedMI->operands()) {
      if (!mo.isReg())
        continue;
      Register oldReg = mo.getReg();
      if (!oldReg.isVirtual())
        continue;

      auto it = regMap.find(oldReg);
      if (it == regMap.end()) {
        const TargetRegisterClass *rc =
            calleeMF->getRegInfo().getRegClass(oldReg);
        Register newReg = callerMRI.createVirtualRegister(rc);
        it = regMap.insert({oldReg, newReg}).first;
      }
      mo.setReg(it->second);
    }

    block.insert(callMI.getIterator(), clonedMI);
  }

  callMI.eraseFromParent();

  if (!isRecursive)
    depthMap[targetFn]--;

  return true;
}

bool RysevInlining::runOnModule(Module &M) {
  bool changed = false;
  bool again;

  do {
    again = false;

    auto &MMI = getAnalysis<MachineModuleInfoWrapperPass>().getMMI();

    struct CallSite {
      MachineFunction *MF;
      MachineBasicBlock *MBB;
      MachineInstr *MI;
    };
    SmallVector<CallSite, 64> calls;

    for (Function &F : M) {
      if (F.isDeclaration())
        continue;
      MachineFunction *MF = MMI.getMachineFunction(F);
      if (!MF)
        continue;
      for (MachineBasicBlock &MBB : *MF) {
        for (MachineInstr &MI : MBB) {
          if (MI.getOpcode() == X86::CALL64pcrel32) {
            calls.push_back({MF, &MBB, &MI});
          }
        }
      }
    }

    for (CallSite &cs : calls) {
      if (tryInline(*cs.MF, *cs.MBB, *cs.MI, recursionDepth)) {
        again = true;
        changed = true;
      }
    }
  } while (again);

  return changed;
}

} // namespace

static RegisterPass<RysevInlining>
    X("example-x86-inline",
      "Rysev Inlining Pass (small funcs, recursion depth <=3)", false, false);