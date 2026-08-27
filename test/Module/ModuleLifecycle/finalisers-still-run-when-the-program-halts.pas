(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:init
CHECK-NEXT:body
CHECK-NEXT:fini
*)

(* EP §6.11.2: a module's 'to end do' is required to run as the program
   terminates, and halt (EP §6.7.5.7) is one of the two ways a program can
   terminate -- not just falling off the end of the program block.  Issue
   #242: halt's runtime implementation used to call exit() directly, which
   never reached the explicit plang_module_finals_run() call CodeGenProcs.cpp
   emits at the end of a normally-completing main, so 'fini' below never
   printed. *)
module M;
  to begin do writeln('init');
  to end do writeln('fini');
end.
program p(output); import M;
begin
  writeln('body');
  halt
end.
