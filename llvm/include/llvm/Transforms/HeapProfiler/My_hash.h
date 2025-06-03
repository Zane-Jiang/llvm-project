#ifndef MY_HASH_H

#define MY_HASH_H
constexpr std::size_t fnv_offset_basis = 14695981039346656037ull;
constexpr std::size_t fnv_prime = 1099511628211ull;
static  inline std::size_t fnv1a_hash(llvm::StringRef str) {
    std::size_t hash = fnv_offset_basis;
    for (char c : str) { 
        hash ^= static_cast<std::size_t>(c);
        hash *= fnv_prime;
    }
    return hash;
}

constexpr std::size_t init_seed = 0xff51afd7ed558ccdULL;
static   inline size_t hash_combine(size_t seed, size_t value) {
    return seed ^ (value + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}


static  inline size_t my_hash_combine(size_t  v1,size_t  v2, size_t v3) {
    size_t seed = init_seed;
    seed =  hash_combine(init_seed,v1);
    seed = hash_combine(seed, v2);
    seed = hash_combine(seed, v3);
    return seed;
}

#endif