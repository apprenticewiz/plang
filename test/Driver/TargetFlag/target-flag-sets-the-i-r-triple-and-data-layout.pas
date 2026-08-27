(*
--target must reach the front end and change the module it emits, not just
the driver's own idea of which llc/linker to invoke (issue #243).  Before
this, -emit-llvm's IR carried the HOST triple and its data layout no matter
what --target asked for: makeFEArgs never forwarded --target to -pc1, and
-pc1 had no --target option to receive it even if it had, so codegen always
fell back to llvm::sys::getDefaultTargetTriple().

riscv64 is used because it can never be the CI host: this suite already
builds and runs on x86_64 Linux, aarch64 Linux and arm64 Darwin, so a
still-host-triple regression cannot pass by accident here the way it might
if the chosen --target happened to match whichever machine ran the test.

The compiled program lives in a split-file chunk below rather than directly
in this file: ISO 7185 6.1.8 lets a comment opened either way be closed by
either terminator, so this file's own CHECK lines could not otherwise use
FileCheck's regex-capture punctuation, or even quote LLVM's comment-closing
punctuation in prose the way this paragraph just did, without prematurely
ending the very comment they are written inside of.

RUN: split-file %s %t.dir
RUN: %plang_ir -emit-llvm --target=riscv64-unknown-linux-gnu %t.dir/test.pas -o %t.ll
RUN: FileCheck %s < %t.ll
*)

(*
CHECK-DAG: target datalayout = "{{.*}}n32:64{{.*}}"
CHECK-DAG: target triple = "riscv64-unknown-linux-gnu"
*)

//--- test.pas
program p;
begin end.
