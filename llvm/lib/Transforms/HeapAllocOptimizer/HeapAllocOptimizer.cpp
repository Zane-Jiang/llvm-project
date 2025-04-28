#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Transforms/HeapAllocOptimizer/HeapAllocOptimizer.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/ProfileData/InstrProfReader.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
using namespace llvm;

PreservedAnalyses HeapAllocOptimizer::run(Module &M, ModuleAnalysisManager &AM) {
  DenseMap<uint64_t, uint64_t> AccessMap;
  if (!loadProfileData(M, AccessMap)) {
    return PreservedAnalyses::all();
  }

  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  
  for (Function &F : M) {
    BlockFrequencyInfo &BFI = FAM.getResult<BlockFrequencyAnalysis>(F);
    
    for (BasicBlock &BB : F) {
      auto Freq = BFI.getBlockFreq(&BB).getFrequency();
      
      for (Instruction &I : BB) {
        if (auto *CI = dyn_cast<CallBase>(&I)) {
          if (Function *Callee = CI->getCalledFunction()) {
            if (Callee->getName().contains("malloc")) {
              DebugLoc Loc = CI->getDebugLoc();
              uint64_t ID = hash_combine(
                  hash_value(Loc->getFilename()),
                  Loc.getLine(),
                  Loc.getCol());
              
              uint64_t Hotness = AccessMap.lookup(ID) * Freq;
              StringRef NewAlloc = selectAllocator(Hotness);
              
              if (!NewAlloc.empty()) {
                replaceAllocator(CI, NewAlloc, M);
              }
            }
          }
        }
      }
    }
  }
  return PreservedAnalyses::none();
}

bool HeapAllocOptimizer::loadProfileData(Module &M, 
                                       DenseMap<uint64_t, uint64_t> &AccessMap) {
  auto BufferOrErr = MemoryBuffer::getFile("heap.prof");
  if (auto EC = BufferOrErr.getError()) {
    M.getContext().emitError("无法打开profile文件: " + EC.message());
    return false;
  }

  auto ReaderOrErr = InstrProfReader::create(std::move(BufferOrErr.get()));
  if (Error E = ReaderOrErr.takeError()) {
    M.getContext().emitError(toString(std::move(E)));
    return false;
  }
  
  std::unique_ptr<InstrProfReader> Reader = std::move(ReaderOrErr.get());

  NamedInstrProfRecord Record;
  while (true) {
    Expected<InstrProfRecord> RecordOrErr = Reader->readNextRecord(Record);
    
    if (!RecordOrErr) {
      Error E = RecordOrErr.takeError();
      bool IsEOF = false;
      handleAllErrors(
          std::move(E),
          [&](const InstrProfError &IPE) {
            if (IPE.get() == instrprof_error::eof) {
              IsEOF = true;
            } else {
              M.getContext().emitError("Profile data error: " + IPE.message());
            }
          },
          [&](const ErrorInfoBase &EI) {
            M.getContext().emitError("unknown error: " + EI.message());
          });
      
      if (IsEOF) break;
      return false;
    }

    if (Record.Counts.size() > 0) {
      AccessMap[Record.Hash] = Record.Counts[0];
    }
  }
  return true;
}

StringRef HeapAllocOptimizer::selectAllocator(uint64_t hotness) const {
  // 阈值根据实际硬件调整
  const uint64_t CXLThreshold = 100000; 
  return hotness > CXLThreshold ? "cxl_malloc" : "local_malloc";
}

void HeapAllocOptimizer::replaceAllocator(CallBase *CI, StringRef NewAlloc, Module &M) const {
  IRBuilder<> Builder(CI);
  FunctionType *FTy = CI->getFunctionType();
  
  Function *NewFunc = M.getFunction(NewAlloc);
  if (!NewFunc) {
    NewFunc = Function::Create(FTy, GlobalValue::ExternalLinkage, NewAlloc, M);
    NewFunc->setAttributes(CI->getCalledFunction()->getAttributes());
  }
  
  SmallVector<Value *, 4> Args;
  for (unsigned i = 0; i < CI->getNumOperands(); ++i) {
    Args.push_back(CI->getArgOperand(i));
  }
  CallInst *NewCall = Builder.CreateCall(FTy, NewFunc, Args);
  CI->replaceAllUsesWith(NewCall);
  CI->eraseFromParent();
}



extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {
      LLVM_PLUGIN_API_VERSION, "HeapAllocOptimizer", "v0.1",
      [](PassBuilder &PB) {
        PB.registerPipelineParsingCallback(
            [](StringRef Name, ModulePassManager &MPM,
               ArrayRef<PassBuilder::PipelineElement>) {
              if (Name == "heap-optimizer") {
                MPM.addPass(HeapAllocOptimizer());
                return true;
              }
              return false;
            });
      }};
}