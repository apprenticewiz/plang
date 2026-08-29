(*
Companion to shortstring-declares-with-a-packed-one-byte-header-layout-not-
varstrings.pas (test/CodeGen/Turbo): that test checks the LLVM IR struct
shape directly, but this project has already been burned once by a class of
debug-info bug an IR-text check alone did not catch (see
dash-g-turbo-integer-prints-correctly-under-a-real-debugger.pas, test/
Driver/Driver, and lib/CodeGen/CGDebugInfo.h's own notes) -- so this runs a
real gdb session instead, the same as that test does.

CGDebugInfo::debugTypeOfSemaType has no NumSemaTypeKinds compile-time
sentinel tying it to every TypeKind the way the four switches that DO have
one are guarded (CGTypes::canLowerSemaType/llvmTypeOfSemaTypeImpl among
them): a missed `case TypeKind::ShortString:` there would compile clean and
silently give a string[N] variable no DIType at all (or, worse, mis-type it
as VarString's eight-byte-header shape) rather than fail the build -- this
is the only check in this feature's test suite that would have caught that.

sizeof(s) reporting 11 here is also this feature's ONE working substitute
for a `SizeOf` builtin check: SizeOf itself is not implemented as of this
commit (a possibly-concurrent, separate work item), so gdb's own sizeof is
what verifies "SizeOf(string[N]) == N+1" empirically instead.
*)

(*
REQUIRES: gdb-binary
*)

(*
RUN: split-file %s %t.dir
RUN: %plang -std=turbo -g %t.dir/test.pas -o %t
RUN: gdb -q -batch -ex "break %t.dir/test.pas:5" -ex run -ex "print sizeof(s)" -ex "print s" -ex "ptype s" %t < %t.dir/stdin.txt 2>&1 | FileCheck %s
*)

//--- test.pas
program p(input, output);
var s: string[10];
begin
  readln(s);
  writeln(s)
end.

//--- stdin.txt
hello

(*
CHECK: $1 = 11
CHECK: $2 = {length = 5, data = 'hello'
CHECK: type = string = record
CHECK-NEXT: length : byte;
CHECK-NEXT: data : array [0..9] of char;
CHECK-NEXT: end
*)
