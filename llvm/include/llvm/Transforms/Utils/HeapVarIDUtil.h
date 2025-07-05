#ifndef HEAP_VAR_ID_UTIL_H
#define HEAP_VAR_ID_UTIL_H

#include <cstddef>
#include <string>
#include <vector>
#include <llvm/ADT/StringRef.h>
#include <llvm/IR/DebugLoc.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Dominators.h>

namespace heapid {

size_t fnv1a_hash(llvm::StringRef str);
size_t hash_combine(size_t seed, size_t value);
size_t time_independent_hash_combine(size_t v1, size_t v2);

std::vector<std::string> getDominatorPath(llvm::BasicBlock *MBlock, llvm::DominatorTree &DT);
std::vector<std::string> getSortedPredecessors(llvm::BasicBlock *BB);

uint64_t generateAllocID(const llvm::DebugLoc &Loc, std::string &LocationStr, llvm::BasicBlock *BB, llvm::Function &F);

} // namespace heapid

#endif 