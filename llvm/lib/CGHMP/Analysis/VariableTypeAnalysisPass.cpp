#include "llvm/CGHMP/Analysis/VariableTypeAnalysisPass.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/TypeFinder.h"
#include <unordered_map>
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

template class llvm::PassInfoMixin<VariableTypeAnalysisPass>;

// 全局（文件内）提示映射：不透明指针类型 -> 推断的 pointee type
static std::unordered_map<const Type*, Type*> PointeeHints;

// 在模块中扫描常见指令模式，为不透明指针建立 pointee 类型提示。
static void buildPointeeHints(Module &M) {
    PointeeHints.clear();

    for (Function &F : M) {
        for (BasicBlock &BB : F) {
            for (Instruction &I : BB) {
                // bitcast: 如果 src pointer 不含 contained type，而 dest pointer 含有 contained type
                if (auto *BC = dyn_cast<BitCastInst>(&I)) {
                    Type *SrcT = BC->getSrcTy();
                    Type *DstT = BC->getDestTy();
                    if (PointerType *SP = dyn_cast<PointerType>(SrcT)) {
                        if (SP->getNumContainedTypes() == 0) {
                            if (PointerType *DP = dyn_cast<PointerType>(DstT)) {
                                if (DP->getNumContainedTypes() > 0) {
                                    PointeeHints[SP] = DP->getContainedType(0);
                                }
                            }
                        }
                    }
                }

                // gep: 如果 gep 的 source element type 可知，则把指针类型映射为该 element
                if (auto *GEP = dyn_cast<GetElementPtrInst>(&I)) {
                    Type *SrcElem = GEP->getSourceElementType();
                    Value *PtrOp = GEP->getPointerOperand();
                    if (PointerType *PT = dyn_cast<PointerType>(PtrOp->getType())) {
                        if (PT->getNumContainedTypes() == 0 && SrcElem) {
                            PointeeHints[PT] = SrcElem;
                        }
                    }
                }

                // store: 如果把某个具有具体类型的值存到一个不透明指针地址上
                if (auto *SI = dyn_cast<StoreInst>(&I)) {
                    Value *Ptr = SI->getPointerOperand();
                    Value *Val = SI->getValueOperand();
                    if (PointerType *PT = dyn_cast<PointerType>(Ptr->getType())) {
                        if (PT->getNumContainedTypes() == 0) {
                            if (PointerType *VPT = dyn_cast<PointerType>(Val->getType())) {
                                if (VPT->getNumContainedTypes() > 0) {
                                    PointeeHints[PT] = VPT->getContainedType(0);
                                }
                            } else {
                                // 如果存的是非指针值，其类型本身可能就是 element
                                PointeeHints[PT] = Val->getType();
                            }
                        }
                    }
                }
            }
        }
    }
}

// 尝试从某个 Value（或其 users 链）中推断出指针的元素类型。
// 会递归搜索 GEP/Load/BitCast/Store 等指令，有限深度以避免循环。
static Type *inferElementTypeFromValue(Value *V, unsigned MaxDepth = 6) {
    if (!V || MaxDepth == 0) return nullptr;
    std::set<Value*> Visited;
    std::function<Type*(Value*, unsigned)> dfs = [&](Value *Cur, unsigned Depth) -> Type* {
        if (!Cur || Depth == 0) return nullptr;
        if (!Visited.insert(Cur).second) return nullptr;

        // 如果当前 value 本身是指针并且含有 contained type（非完全不透明），直接返回
        if (Type *T = Cur->getType()) {
            if (PointerType *PT = dyn_cast<PointerType>(T)) {
                if (PT->getNumContainedTypes() > 0)
                    return PT->getContainedType(0);
            }
        }

        // 遍历 uses，尝试从常见指令中推断
        for (User *U : Cur->users()) {
            if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
                return GEP->getSourceElementType();
            }
            if (auto *LI = dyn_cast<LoadInst>(U)) {
                // load 返回的类型可能就是我们要找的元素类型
                return LI->getType();
            }
            if (auto *BC = dyn_cast<BitCastInst>(U)) {
                if (Type *DT = BC->getDestTy()) {
                    if (PointerType *PT = dyn_cast<PointerType>(DT)) {
                        if (PT->getNumContainedTypes() > 0)
                            return PT->getContainedType(0);
                    }
                }
                // 继续沿 bitcast 的用户递归
                if (Type *R = dfs(BC, Depth - 1))
                    return R;
            }
            if (auto *SI = dyn_cast<StoreInst>(U)) {
                // store: 如果 Cur 是被存储的值，检查目标地址的类型或它的 users
                if (SI->getValueOperand() == Cur) {
                    if (Type *PTy = SI->getPointerOperand()->getType()) {
                        if (PointerType *PPT = dyn_cast<PointerType>(PTy)) {
                            if (PPT->getNumContainedTypes() > 0)
                                return PPT->getContainedType(0);
                        }
                    }
                    if (Type *R = dfs(SI->getPointerOperand(), Depth - 1))
                        return R;
                }
                // 如果 Cur 是存储地址，则存储值的类型可能包含线索
                if (SI->getPointerOperand() == Cur) {
                    if (Type *VT = SI->getValueOperand()->getType()) {
                        if (PointerType *PVT = dyn_cast<PointerType>(VT)) {
                            if (PVT->getNumContainedTypes() > 0)
                                return PVT->getContainedType(0);
                        }
                        return VT;
                    }
                }
            }

            // 递归检查该 user 本身的 users（广度优先样式）
            if (Type *R = dfs(U, Depth - 1))
                return R;
        }

        return nullptr;
    };

    return dfs(V, MaxDepth);
}

// 尝试通过 malloc 大小匹配模块中的结构体类型（基于 DataLayout）
static Type *findElementTypeByMallocSize(Module &M, uint64_t Size) {
    if (Size == 0) return nullptr;
    TypeFinder TF;
    TF.run(M, /*OnlyNamed*/ true);

    const DataLayout &DL = M.getDataLayout();

    // 优先精确匹配结构体大小，其次匹配可被整除的情况
    StructType *bestExact = nullptr;
    StructType *bestDivisible = nullptr;

    for (Type *T : TF) {
        if (StructType *ST = dyn_cast<StructType>(T)) {
            if (ST->isOpaque())
                continue;
            uint64_t STSize = DL.getTypeAllocSize(ST);
            if (STSize == Size) {
                bestExact = ST;
                break;
            }
            if (STSize > 0 && Size % STSize == 0) {
                bestDivisible = ST;
            }
        }
    }

    if (bestExact) return bestExact;
    if (bestDivisible) return bestDivisible;
    return nullptr;
}


PreservedAnalyses VariableTypeAnalysisPass::run(Module &M,
                                                AnalysisManager<Module> &MAM) {
    // 在分析前构建模块范围的 pointee hints，用于不透明指针的后备推断
    buildPointeeHints(M);
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

                            // 分析分配的变量类型：遍历所有 users
                            for (User *U : CI->users()) {
                                errs()<<"Analyzing user: "<<*U<<"\n";
                                Type *AllocatedType = nullptr;
                                
                                // 处理BitCast指令
                                if (auto *BC = dyn_cast<BitCastInst>(U)) {
                                    errs()<<"Analyzing BitCastInst: "<<*BC<<"\n";
                                    AllocatedType = BC->getDestTy();
                                }
                                // 处理Store指令
                                else if (auto *SI = dyn_cast<StoreInst>(U)) {
                                    errs()<<"Analyzing StoreInst: "<<*SI<<"\n";
                                    
                                    // 在Store指令中，我们关心的是CI是作为源值还是目标地址
                                    if (SI->getValueOperand() == CI) {
                                        // 如果CI是源值，那么目标地址的类型告诉我们存储的类型
                                        AllocatedType = SI->getPointerOperand()->getType();
                                        errs()<<"Found store from malloc result to: "<<*AllocatedType<<"\n";
                                    } else if (SI->getPointerOperand() == CI) {
                                        // 如果CI是目标地址，那么源值的类型告诉我们存储的内容
                                        AllocatedType = PointerType::get(SI->getValueOperand()->getType(), 0);
                                        errs()<<"Found store to malloc result, storing: "<<*SI->getValueOperand()->getType()<<"\n";
                                    }
                                }
                                
                                // 如果还是没有找到类型，尝试从其他用途推断
                                if (!AllocatedType) {
                                    errs()<<"Could not determine type from current instruction, continuing search...\n";
                                    continue;
                                }
                                    // 处理指针类型
                                if (PointerType *PtrTy = dyn_cast<PointerType>(AllocatedType)) {
                                        Type *ElementTy = nullptr;
                                        
                                        // 1. 首先尝试直接获取类型
                                        if (PtrTy->getNumContainedTypes() > 0) {
                                            ElementTy = PtrTy->getContainedType(0);
                                        }
                                        
                                        // 2. 如果是不透明指针，尝试递归从 users 链中推断类型
                                        if (!ElementTy) {
                                            errs() << "Trying to infer type from users...\n";
                                            ElementTy = inferElementTypeFromValue(U, 8);
                                            if (!ElementTy) {
                                                // 如果 malloc 的大小是常量，尝试基于 DataLayout 在模块中匹配 struct 类型
                                                if (auto *CInt = dyn_cast<ConstantInt>(CI->getArgOperand(0))) {
                                                    uint64_t MSize = CInt->getZExtValue();
                                                    if (Type *T = findElementTypeByMallocSize(*CI->getModule(), MSize)) {
                                                        ElementTy = T;
                                                        errs() << "Inferred element type by malloc size: " << *ElementTy << "\n";
                                                    }
                                                }
                                            }

                                            if (!ElementTy) {
                                                // 最后退化为 i8（原始内存）以保证分析继续
                                                ElementTy = Type::getInt8Ty(PtrTy->getContext());
                                                errs() << "Fallback: treating malloc result as raw memory (i8*)\n";
                                            } else {
                                                errs() << "Inferred element type: " << *ElementTy << "\n";
                                            }
                                        }
                                        
                                        if (!ElementTy) {
                                            errs() << "Warning: Could not determine element type for allocation at "
                                                  << *CI << ". Skipping type analysis for this user.\n";
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

                                        errs() << "Allocation at " << *CI   <<   "\n";

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
                        } else if (Name == "free") {
                            freeCalls.push_back(CI);
                        }
                    }
                }
                else if (isa<LoadInst>(I) || isa<StoreInst>(I)) {
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
           << "  Pointer tracking types : " << pointerTrackingVars << "\n"
           << "  Array access types : " << arrayAccessVars << "\n"
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

    // 避免循环引用
    if (!Visited.insert(Ty).second)
        return false;

    // 如果是指针类型，尝试获取其指向的元素类型并继续检测
    if (PointerType *PT = dyn_cast<PointerType>(Ty)) {
        if (PT->getNumContainedTypes() > 0) {
            Type *Pointee = PT->getContainedType(0);
            return isPointerTrackingType(Pointee, Visited);
        }
        // 对于不含 contained type 的不透明指针，无法通过 type 层面判断
        return false;
    }

    // 如果是结构体类型，检查其成员是否包含指向自身或包含自身的指针（自引用）
    if (StructType *ST = dyn_cast<StructType>(Ty)) {
        if (ST->isOpaque())
            return false;

        for (unsigned i = 0; i < ST->getNumElements(); ++i) {
            Type *Elem = ST->getElementType(i);

            // 如果成员是指针，检查指向的类型
            if (PointerType *ElemPtr = dyn_cast<PointerType>(Elem)) {
                Type *Pointee = nullptr;
                if (ElemPtr->getNumContainedTypes() > 0) {
                    Pointee = ElemPtr->getContainedType(0);
                } else {
                    // 对不透明指针，尝试从全模块提示映射中查找 pointee
                    auto It = PointeeHints.find(ElemPtr);
                    if (It != PointeeHints.end())
                        Pointee = It->second;
                    else
                        Pointee = nullptr; // 继续后续处理（不会直接跳过）
                }
                if (!Pointee) {
                    // 无法确定 pointee，继续检查下一个成员
                    continue;
                }
                if (Pointee == Ty)
                    return true; // 直接自引用

                if (isa<StructType>(Pointee) && isPointerTrackingType(Pointee, Visited))
                    return true;
            }

            // 否则如果成员本身是结构体，递归检查
            if (StructType *NestedST = dyn_cast<StructType>(Elem)) {
                if (!NestedST->isOpaque() && isPointerTrackingType(NestedST, Visited))
                    return true;
            }
        }
    }

    return false;
}

// 检查是否是数组访问类型
bool VariableTypeAnalysisPass::isArrayAccessType(Type *Ty) {
    // 直接的数组类型
    if (isa<ArrayType>(Ty))
        return true;

    // 指向元素的指针：如果 contained type 可用，且不是 i8（字符串），则可能是数组指针
    if (PointerType *PT = dyn_cast<PointerType>(Ty)) {
        if (PT->getNumContainedTypes() == 0)
            return false; // 不透明指针，无法从 type 层面判断

        Type *Elem = PT->getContainedType(0);
        if (!isa<PointerType>(Elem) && !Elem->isIntegerTy(8))
            return true;
    }

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