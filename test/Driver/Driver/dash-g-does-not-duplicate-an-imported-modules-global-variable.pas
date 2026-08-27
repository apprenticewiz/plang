(*
CGDebugInfo::declareLocal is the single choke point every named Pascal
variable passes through to get its DIGlobalVariableExpression built --
including, twice, an EP module-level variable that another module
compiled in the same run imports: once when the OWNING module declares
it (Codegen::Impl::init's ordinary defVar), and a second time when
Codegen::Impl::resolveImportedVar's "compiled alongside this one"
branch binds the SAME llvm::GlobalVariable into the IMPORTING module's
own symbol table, under whatever local/qualified name the importer
spells it, and calls defVar again.

This project builds one llvm::Module (and so one DICompileUnit) for a
whole program, not one per Pascal module -- confirmed by grep of
CGDebugInfo's own constructor, called exactly once per Codegen::Impl --
so there is no "declaration in the importing CU referencing a
definition in the owning one" split (the shape an extern variable
declared in one TU and used in another gets from Clang) to give a
second DW_TAG_variable a legitimate reason to exist here.  Before the
fix, the second defVar call built a second, spurious
DIGlobalVariableExpression under the IMPORTER's own alias ("A.v" for
`import A qualified;`, not the variable's real declared name "v"),
confirmed with llvm-dwarfdump on a compiled two-module program to
produce two DW_TAG_variable entries at the identical DW_AT_location for
one storage cell.

RUN: split-file %s %t.dir
RUN: %plang_ir -std=iso10206 -g -emit-llvm %t.dir/case.pas -o %t.ll
RUN: FileCheck %s < %t.ll
*)

(*
CHECK: @"pasg_a$v" = global i64 0, !dbg [[GVE:![0-9]+]]{{$}}
CHECK: [[GVE]] = !DIGlobalVariableExpression(var: [[GV:![0-9]+]]
CHECK: [[GV]] = distinct !DIGlobalVariable(name: "v"
*)

//--- case.pas
module A; var v: integer; to begin do v := 1; end.
program p(output); import A qualified;
begin writeln(A.v) end.
