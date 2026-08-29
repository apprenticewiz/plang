(*
ParamCount/ParamStr(n) (Builtins.def, CodeGenProcs.cpp's emitMain +
runtime/plang_sys.cpp's plang_set_args) -- confirms real argv reaches the
compiled program: ParamStr(0) is the running program's own path (not
checked byte-for-byte, since %t's own path varies by test run -- only that
it is non-empty), and ParamStr(1)..ParamStr(ParamCount) are exactly the
arguments %run passed after %t, in order.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t alpha beta gamma | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:count=3
CHECK-NEXT:arg1=alpha
CHECK-NEXT:arg2=beta
CHECK-NEXT:arg3=gamma
CHECK-NEXT:arg0 nonempty=TRUE
*)

program paramcountstr;
var i: Integer;
begin
  writeln('count=', ParamCount);
  for i := 1 to ParamCount do
    writeln('arg', i, '=', ParamStr(i));
  writeln('arg0 nonempty=', Length(ParamStr(0)) > 0);
end.
