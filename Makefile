LLVM_BUILD_DIR = build
LLVM_BIN_DIR = $(LLVM_BUILD_DIR)/bin
CXX = clang++
CC = clang
CXXFLAGS = -std=c++11
OPT = $(LLVM_BIN_DIR)/opt
CLANG = $(LLVM_BIN_DIR)/clang
CLANGXX = $(LLVM_BIN_DIR)/clang++
CMAKE = cmake
MAKE = make
NPROC = $(shell nproc)

TEST_SRC = test.c
BASE_LL = test_base.ll
TEST_INSTRU_LL = test_after_instru.ll
RUNTIME_SRC = runtime.cpp
RUNTIME_O = runtime.o
TEST_BIN = test

TEST_OPTIMIZE_LL = test_after_opt.ll
OPTIMIZED_TEST_BIN = optimized_test
HMALLOC_SRC = hmalloc
HMSDK_EXAMPLE = hmsdk_example
PROFILE_FILE = heap.prof

all: profile-test

build:
	mkdir -p $(LLVM_BUILD_DIR)
	cd $(LLVM_BUILD_DIR) && $(CMAKE) -G "Unix Makefiles" -DLLVM_ENABLE_PROJECTS="clang" -DCMAKE_BUILD_TYPE=Debug  ../llvm
	$(MAKE) -C $(LLVM_BUILD_DIR) -j$(NPROC)

base-ll: $(TEST_SRC) 
	$(CLANG) -S -g -emit-llvm $(TEST_SRC) -o $(BASE_LL)


runtime: $(RUNTIME_O)

$(RUNTIME_O): $(RUNTIME_SRC)
	$(CXX) $(CXXFLAGS) -c $(RUNTIME_SRC) -o $(RUNTIME_O)


instru-opt: base-ll
	$(OPT) -S -passes=heap-profiler $(BASE_LL) -o $(TEST_INSTRU_LL)


instru-test: instru-opt runtime
	$(CLANGXX) $(TEST_INSTRU_LL) $(RUNTIME_O) -o $(TEST_BIN)


optimize-opt: base-ll
	$(OPT) -S -passes=heap-optimizer $(BASE_LL) -o $(TEST_OPTIMIZE_LL)
	
optimize-test: optimize-opt 
	$(CLANGXX) $(TEST_OPTIMIZE_LL) -L$(HMALLOC_SRC) -lhmalloc -I$(HMALLOC_SRC)/include  -o $(OPTIMIZED_TEST_BIN)

make-optimized-bin-run:
	LD_LIBRARY_PATH=./hmalloc ./$(OPTIMIZED_TEST_BIN)
	
$(HMSDK_EXAMPLE):
	$(CC) $(HMALLOC_SRC)/example.c -o $(HMSDK_EXAMPLE) -L$(HMALLOC_SRC) -lhmalloc -I$(HMALLOC_SRC)/include 


clean-profile:
	rm -f $(PROFILE_FILE)

clean:
	rm -f $(BASE_LL) $(TEST_INSTRU_LL) $(RUNTIME_O) $(TEST_BIN) $(TEST_OPTIMIZE_LL) $(OPTIMIZED_TEST_BIN) $(PROFILE_FILE)

.PHONY: all clean clean-profile instru-opt instru-test build base-ll runtime optimize-opt optimize-test 

