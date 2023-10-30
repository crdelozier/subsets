set(LLVM_LINK_COMPONENTS support)

add_clang_executable(ironclad-scan
  IroncladScan.cpp
  )
target_link_libraries(ironclad-scan
  PRIVATE
  clangAST
  clangASTMatchers
  clangBasic
  clangFrontend
  clangSerialization
  clangTooling
  )
