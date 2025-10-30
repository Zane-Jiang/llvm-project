#ifndef LLVM_CGHMP_ANALYSIS_VARIABLETYPEANALYSISPASS_H
#define LLVM_CGHMP_ANALYSIS_VARIABLETYPEANALYSISPASS_H

#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include <map>
#include <set>
#include <llvm/IR/Value.h>
   
namespace llvm {

class VariableTypeAnalysisPass : public PassInfoMixin<VariableTypeAnalysisPass> {
public:
    PreservedAnalyses run(Module &M, AnalysisManager<Module> &MAM);
    static bool isRequired() { return true; }

private:
    // 存储分析结果的数据结构
    enum class VarType {
        PointerTracking,  // 链表、树等需要指针追踪的类型
        ArrayAccess,      // 数组访问类型
        Other            // 其他类型
    };

    struct VarInfo {
        VarType type;            // 变量类型分类
        bool isShortLived;       // 是否是短生命周期
        Value *allocSite;        // 分配点
    };
    
    std::map<Value*, VarInfo> varInfoMap;
    
    // 分析变量的类型（指针追踪、数组访问或其他）
    VarType analyzeVarType(Type *Ty, std::set<Type*> &Visited);
    
    // 检查是否是链表或树等指针追踪类型
    bool isPointerTrackingType(Type *Ty, std::set<Type*> &Visited);
    
    // 检查是否是数组访问类型
    bool isArrayAccessType(Type *Ty);
    
    // 分析变量的生命周期
    bool isShortLifetime(Value *V);
};

} // namespace llvm

#endif // LLVM_CGHMP_ANALYSIS_VARIABLETYPEANALYSISPASS_H
