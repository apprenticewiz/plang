(*
The other half of the ExitCode proof: a Turbo program that never touches
ExitCode still exits 0, the same as ISO 7185/Extended Pascal's main
always does -- plang_tp_exitcode's own zero-initializer
(runtime/plang_sys.cpp), not a coincidence of whatever happened to be on
the stack where ISO/EP's fixed-0 return used to be the only path.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t
*)

program exitcodedefaultzero;
begin
  writeln('never touches ExitCode');
end.
