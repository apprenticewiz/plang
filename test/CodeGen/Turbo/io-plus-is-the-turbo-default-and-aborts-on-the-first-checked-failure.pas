(*
CompilerSwitches.def's own IOChecks entry (`SWITCH(IOChecks, 'i',
"iochecks", true, true)`) has TurboDefault=true -- real Turbo Pascal ships
with `{$I+}` ON, unlike RangeChecks's own TurboDefault=false.  This program
writes no `{$I}` directive of any kind, so the very first failing I/O
statement (Reset against a file that does not exist) must abort the
process right there, through the same plang_tp_runerror(Code) reporter
every other Turbo runtime check already uses (RangeCheckGuards.cpp) --
"Runtime error 2 at $<addr>", exit status 2 itself (InOutRes's own code for
"file not found"), not a silent continuation.

RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 2 %run %t %t.does-not-exist.txt 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: Runtime error 2 at $
*)

var f: text;
begin
  assign(f, ParamStr(1));
  reset(f);
  writeln('unreachable: {$I+} is Turbo''s default and should have aborted above');
end.
