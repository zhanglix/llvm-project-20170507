# RUN: llvm-mc %s | FileCheck %s

# CHECK: TEST0:
# CHECK: .lsym bar,foo
# CHECK: .lsym baz,3
TEST0:  
        .lsym   bar, foo
        .lsym baz, 2+1
