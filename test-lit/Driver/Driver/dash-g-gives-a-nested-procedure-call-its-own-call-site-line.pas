(*
RUN: %plang_ir -g -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p(output);
var x: integer;

procedure outer;
  var y: integer;

  procedure inner;
  begin
    y := y + 1
  end;

begin
  y := 5;
  inner;
  writeln(y)
end;

begin
  x := 1;
  outer;
  writeln(x)
end.

(*
createEntryAlloca hoists an alloca to the entry block via
saveIP/SetInsertPoint/restoreIP; SetInsertPoint at an EXISTING
instruction (entry.begin(), since the prologue's own allocas are
already there) also adopts that instruction's own debug location, and
plain restoreIP does not restore the debug location back afterward.
outer's static-link frame for its call to inner is built this way, so
the call inherited outer's own prologue line (4) instead of its real
call site (the line above where "inner;" is written) -- confirmed with
a real gdb backtrace showing the wrong line before the fix, the right
one after.  This CHECK block pins an exact source line number, so keep
the RUN block above minimal -- reflowing it shifts every line below.

CHECK: call void @"pas_outer$inner"([[ARGS:.*]]), !dbg [[CALLLOC:![0-9]+]]
CHECK: [[CALLLOC]] = !DILocation(line: 19,
*)
