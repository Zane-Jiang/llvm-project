#include "llvm/Transforms/Utils/HeapVarIDUtil.h" 
#include <algorithm>
#include <llvm/Support/raw_ostream.h>
#include "llvm/IR/DebugInfoMetadata.h"

namespace heapid {

constexpr size_t fnv_offset_basis = 14695981039346656037ull;
constexpr size_t fnv_prime = 1099511628211ull;
size_t fnv1a_hash(llvm::StringRef str) {
    size_t hash = fnv_offset_basis;
    for (char c : str) {
        hash ^= static_cast<size_t>(c);
        hash *= fnv_prime;
    }
    return hash;
}

constexpr size_t init_seed = 0xff51afd7ed558ccdULL;
size_t hash_combine(size_t seed, size_t value) {
    return seed ^ (value + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

size_t time_independent_hash_combine(size_t v1, size_t v2) {
    size_t seed = init_seed;
    seed = hash_combine(seed, v1);
    seed = hash_combine(seed, v2);
    return seed;
}

std::vector<std::string> getDominatorPath(llvm::BasicBlock *MBlock, llvm::DominatorTree &DT) {
    std::vector<std::string> path;
    llvm::BasicBlock *Current = MBlock;
    while (Current != nullptr) {
        path.push_back(Current->getName().str());
        auto *Node = DT.getNode(Current);
        if (!Node || !Node->getIDom()) break;
        Current = Node->getIDom()->getBlock();
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<std::string> getSortedPredecessors(llvm::BasicBlock *BB) {
    std::vector<std::string> preds;
    for (auto *Pred : predecessors(BB)) {
        preds.push_back(Pred->getName().str());
    }
    std::sort(preds.begin(), preds.end());
    return preds;
}

uint64_t generateAllocID(const llvm::DebugLoc &Loc, std::string &LocationStr, llvm::BasicBlock *BB, llvm::Function &F) {
    static uint64_t next_id = 1;
    LocationStr = "unknown:0:0";
    if (Loc) {
        LocationStr = Loc->getFilename().str() + ":" + std::to_string(Loc.getLine()) + ":" + std::to_string(Loc.getCol());
    }
    llvm::DominatorTree DT(F);
    auto domPath = getDominatorPath(BB, DT);
    auto preds = getSortedPredecessors(BB);

    // 哈希支配路径
    uint64_t domHash = 0;
    for (const auto &name : domPath)
        domHash = time_independent_hash_combine(domHash, fnv1a_hash(name));
    // 哈希前驱信息
    uint64_t predHash = 0;
    for (const auto &name : preds)
        predHash = time_independent_hash_combine(predHash, fnv1a_hash(name));
    // 哈希源码位置
    uint64_t locHash = fnv1a_hash(LocationStr);

    uint64_t id = domHash;
    id = time_independent_hash_combine(id, predHash);
    id = time_independent_hash_combine(id, locHash);

    
    // llvm::errs() << "[IDGen] DomPath: ";
    // for (auto &n : domPath) llvm::errs() << n << "->";
    // llvm::errs() << " | Preds: ";
    // for (auto &n : preds) llvm::errs() << n << ",";
    // llvm::errs() << " | Loc: " << LocationStr << " | ID: " << id << "\n";

    return id ? id : next_id++;
}

} // namespace heapid 