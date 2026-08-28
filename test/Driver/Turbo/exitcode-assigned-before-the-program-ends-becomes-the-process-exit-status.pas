(*
TP `ExitCode: Integer` (Sema::registerBuiltins, -std=turbo only) -- the
first predefined identifier this project registers as a mutable Var
rather than a Const/Builtin.  emitMain (CodeGenProcs.cpp) returns whatever
the program last assigned to it, instead of the fixed 0 ISO 7185/Extended
Pascal's main always returns, once the program block ends NORMALLY --
falling off the end, not through Halt (which takes its own exit status
and never touches ExitCode).
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 5 %run %t
*)

program exitcodeassigned;
begin
  writeln('setting exit code');
  ExitCode := 5;
end.
