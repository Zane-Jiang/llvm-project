#include "llvm/CGHMP/Analysis/VariableTypeAnalysisPass.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

template class llvm::PassInfoMixin<VariableTypeAnalysisPass>;


PreservedAnalyses VariableTypeAnalysisPass::run(Module &M,
                                                AnalysisManager<Module> &MAM) {
    std::vector<CallBase*> allocCalls, freeCalls;
    std::vector<Instruction*> accessInsts;

    // 收集所有内存分配和释放的调用点
    for (Function &F : M) {
        for (BasicBlock &BB : F) {
            for (Instruction &I : BB) {
                if (auto *CI = dyn_cast<CallBase>(&I)) {
                    if (Function *Callee = CI->getCalledFunction()) {
                        StringRef Name = Callee->getName();
                        if (Name.contains("malloc") || Name.contains("calloc") || Name.contains("realloc")) {
                            allocCalls.push_back(CI);

                            // 分析分配的变量类型：遍历所有 users，寻找 BitCastInst（更可靠）
                            for (User *U : CI->users()) {
                                if (auto *BC = dyn_cast<BitCastInst>(U)) {
                                    Type *AllocatedType = BC->getDestTy();
                                    if (PointerType *PtrTy = dyn_cast<PointerType>(AllocatedType)) {
                                        Type *ElementTy = nullptr;
                                        if (PtrTy->getNumContainedTypes() > 0)
                                            ElementTy = PtrTy->getContainedType(0);
                                        else {
                                            // Opaque pointer: contained type not available.
                                            // Skip this user and continue searching for other uses
                                            // that reveal the element type (e.g., loads/stores).
                                            continue;
                                        }
                                        std::set<Type*> Visited;
                                        VarType varType = analyzeVarType(ElementTy, Visited);

                                        // 记录变量信息
                                        VarInfo Info;
                                        Info.type = varType;
                                        Info.allocSite = CI;
                                        Info.isShortLived = false; // 稍后分析
                                        varInfoMap[CI] = Info;

                                        dbgs() << "Allocation at " << *CI
                                              << "\nType: " << *ElementTy
                                              << "\nVariable type: "
                                              << (varType == VarType::PointerTracking ? "Pointer Tracking"
                                                                                 : varType == VarType::ArrayAccess ? "Array Access"
                                                                                                                   : "Other")
                                              << "\n";
                                        break; // 已找到相关 cast，退出 users 循环
                                    }
                                }
                            }
                        } else if (Name == "free") {
                            freeCalls.push_back(CI);
                        }
                    }
                } else if (isa<LoadInst>(I) || isa<StoreInst>(I)) {
                    accessInsts.push_back(&I);
                }
            }
        }
    }

    // 分析变量的生命周期
    for (auto *AllocCall : allocCalls) {
        if (varInfoMap.find(AllocCall) == varInfoMap.end())
            continue;

        unsigned InstructionCount = 0;
        unsigned ControlFlowComplexity = 0;
        std::set<BasicBlock*> VisitedBlocks;
        BasicBlock *AllocBB = AllocCall->getParent();
        Function *F = AllocBB->getParent();

        // 从分配点开始遍历所有基本块
        for (BasicBlock &BB : *F) {
            // 如果还没到分配点所在的基本块，跳过
            if (&BB != AllocBB && !VisitedBlocks.count(&BB))
                continue;

            VisitedBlocks.insert(&BB);
            
            // 计算控制流复杂度（分支数量）
            if (BranchInst *BI = dyn_cast<BranchInst>(BB.getTerminator())) {
                if (BI->isConditional())
                    ControlFlowComplexity++;
            }

            // 计算指令数量
            for (Instruction &I : BB) {
                InstructionCount++;
                
                // 如果遇到free调用，检查是否是释放当前变量
                if (auto *CI = dyn_cast<CallBase>(&I)) {
                    if (Function *Callee = CI->getCalledFunction()) {
                        if (Callee->getName() == "free") {
                            // 简单检查：如果free的参数可能是我们跟踪的变量
                            if (CI->getArgOperand(0) == AllocCall) {
                                goto LifetimeAnalysisDone;
                            }
                        }
                    }
                }
            }
        }

LifetimeAnalysisDone:
        // 根据指令数量和控制流复杂度判断生命周期
        bool isShortLived = InstructionCount < 100 && ControlFlowComplexity < 3;
        varInfoMap[AllocCall].isShortLived = isShortLived;
        
        dbgs() << "Variable allocated at " << *AllocCall 
               << "\nInstruction count: " << InstructionCount
               << "\nControl flow complexity: " << ControlFlowComplexity
               << "\nIs short-lived: " << (isShortLived ? "yes" : "no") << "\n";
    }

    // 汇总分析结果
    unsigned totalVars = 0;
    unsigned pointerTrackingVars = 0;
    unsigned arrayAccessVars = 0;
    unsigned otherVars = 0;
    unsigned shortLivedVars = 0;
    unsigned shortLivedPointerTracking = 0;
    unsigned shortLivedArray = 0;

    for (const auto &[Value, Info] : varInfoMap) {
        totalVars++;
        
        switch (Info.type) {
            case VarType::PointerTracking:
                pointerTrackingVars++;
                if (Info.isShortLived) shortLivedPointerTracking++;
                break;
            case VarType::ArrayAccess:
                arrayAccessVars++;
                if (Info.isShortLived) shortLivedArray++;
                break;
            case VarType::Other:
                otherVars++;
                break;
        }
        
        if (Info.isShortLived) shortLivedVars++;
    }

    // 打印汇总信息
    dbgs() << "\n=== Variable Analysis Summary ===\n"
           << "Total variables analyzed: " << totalVars << "\n"
           << "By Type:\n"
           << "  Pointer tracking types (链表/树): " << pointerTrackingVars << "\n"
           << "  Array access types (数组): " << arrayAccessVars << "\n"
           << "  Other types: " << otherVars << "\n"
           << "\nLifetime Analysis:\n"
           << "  Total short-lived variables: " << shortLivedVars << "\n"
           << "  Short-lived pointer tracking variables: " << shortLivedPointerTracking << "\n"
           << "  Short-lived array variables: " << shortLivedArray << "\n"
           << "===========================\n";

    return PreservedAnalyses::all();
}

// 检查是否是链表或树等指针追踪类型
bool VariableTypeAnalysisPass::isPointerTrackingType(Type *Ty, std::set<Type*> &Visited) {
    if (!Ty) return false;
    
    // // 避免循环引用
    // if (!Visited.insert(Ty).second)
    //     return false;

    // // 如果是结构体类型，检查是否包含自引用指针（链表/树的特征）
    // if (StructType *StructTy = dyn_cast<StructType>(Ty)) {
    //     bool hasSelfReference = false;
        
    //     // 检查结构体的每个成员
    //     for (unsigned i = 0; i < StructTy->getNumElements(); i++) {
    //         Type *ElementTy = StructTy->getElementType(i);
            
    //         // 如果成员是指针类型
    //         if (PointerType *PtrTy = dyn_cast<PointerType>(ElementTy)) {
    //             Type *PointeeTy = nullptr;
    //             if(!PtrTy->isOpaque()){
    //                 PointeeTy = PtrTy->getNonOpaquePointerElementType();
    //             }else{
    //                 errs() << "Encountered opaque pointer type in isPointerTrackingType\n";
    //             }
    //             // 检查是否指向相同类型（自引用）或包含相同类型的结构体
    //             if (PointeeTy == Ty || 
    //                 (isa<StructType>(PointeeTy) && isPointerTrackingType(PointeeTy, Visited))) {
    //                 hasSelfReference = true;
    //                 break;
    //             }
    //         }
    //     }
    //     return hasSelfReference;
    // }
    
    return false;
}

// 检查是否是数组访问类型
bool VariableTypeAnalysisPass::isArrayAccessType(Type *Ty) {
    // 直接的数组类型
    if (isa<ArrayType>(Ty))
        return true;
        
    // // 指向数组的指针
    // if (PointerType *PtrTy = dyn_cast<PointerType>(Ty)) {
    //     Type *ElementTy = PtrTy->getElementType();
    //     // 如果指针指向基本类型或结构体，且不是字符串（char*），可能是数组
    //     if (!isa<PointerType>(ElementTy) && !ElementTy->isIntegerTy(8))
    //         return true;
    // }
    
    return false;
}

// 分析变量的类型
VariableTypeAnalysisPass::VarType VariableTypeAnalysisPass::analyzeVarType(Type *Ty, std::set<Type*> &Visited) {
    if (!Ty)
        return VariableTypeAnalysisPass::VarType::Other;

    // 首先检查是否是指针追踪类型（如链表、树）
    if (isPointerTrackingType(Ty, Visited))
        return VariableTypeAnalysisPass::VarType::PointerTracking;
        
    // 然后检查是否是数组访问类型
    if (isArrayAccessType(Ty))
        return VariableTypeAnalysisPass::VarType::ArrayAccess;
        
    return VariableTypeAnalysisPass::VarType::Other;
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "VariableTypeAnalysisPass", "v0.1",
                    [](PassBuilder &PB) {
                        PB.registerPipelineParsingCallback(
                                [](StringRef Name, ModulePassManager &MPM,
                                     ArrayRef<PassBuilder::PipelineElement>) {
                                    errs() << "addPass(VariableTypeAnalysisPass())\n";
                                    MPM.addPass(VariableTypeAnalysisPass());
                                    return true;
                                });
                    }};
}