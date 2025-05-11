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
BASE_LL = test_before_instru.ll
TEST_INSTRU_LL = test_after_instru.ll
RUNTIME_SRC = runtime.cpp
RUNTIME_O = runtime.o
TEST_BIN = test

HMALLOC_SRC = hmalloc
HMSDK_EXAMPLE = hmsdk_example

all: instru-test



$(HMSDK_EXAMPLE):
	$(CC) $(HMALLOC_SRC)/example.c -o $(HMSDK_EXAMPLE) -L$(HMALLOC_SRC) -lhmalloc -I$(HMALLOC_SRC)/include


build:
	mkdir -p $(LLVM_BUILD_DIR)
	cd $(LLVM_BUILD_DIR) && $(CMAKE) -G "Unix Makefiles" -DLLVM_ENABLE_PROJECTS="clang" -DCMAKE_BUILD_TYPE=Release ../llvm
	$(MAKE) -C $(LLVM_BUILD_DIR) -j$(NPROC)

base-ll: $(TEST_SRC) 
	$(CLANG) -S -emit-llvm $(TEST_SRC) -o $(BASE_LL)

instru-opt: base-ll
	$(OPT) -S -passes=heap-profiler $(BASE_LL) -o $(TEST_INSTRU_LL)

runtime: $(RUNTIME_O)

$(RUNTIME_O): $(RUNTIME_SRC)
	$(CXX) $(CXXFLAGS) -c $(RUNTIME_SRC) -o $(RUNTIME_O)

instru-test: instru-opt runtime
	$(CLANGXX) $(TEST_INSTRU_LL) $(RUNTIME_O) -o $(TEST_BIN)

clean:
	rm -f $(BASE_LL) $(TEST_INSTRU_LL) $(RUNTIME_O) $(TEST_BIN)

.PHONY: all clean instru-opt instru-test build base-ll runtime

