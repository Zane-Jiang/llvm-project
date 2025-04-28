# CMakeLists.txt

add_llvm_component_library(LLVMHeapAllocOptimizer 
  HeapAllocOptimizer.cpp

  ADDITIONAL_HEADER_DIRS ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/HeapAllocOptimizer

  LINK_COMPONENTS
  Core
  Support
  Demangle      
  IR  
)
