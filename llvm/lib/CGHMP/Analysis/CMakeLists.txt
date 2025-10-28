#CMakeLists.txt

add_llvm_component_library(LLVMVariableTypeAnalysis
  LLVMVariableTypeAnalysisPass.cpp

  ADDITIONAL_HEADER_DIRS ${LLVM_MAIN_INCLUDE_DIR}/llvm/CGHMP/Analysis

  LINK_COMPONENTS
  Core
  Support
  IR  
)