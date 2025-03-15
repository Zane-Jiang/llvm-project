# CMakeLists.txt

add_llvm_component_library(LLVMHeapProfiler
  HeapProfiler.cpp

  ADDITIONAL_HEADER_DIRS ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/HeapProfiler

  LINK_COMPONENTS
  Core
  Support
  IR  
)
