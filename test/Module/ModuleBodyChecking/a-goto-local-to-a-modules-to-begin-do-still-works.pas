(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:landed
CHECK-NEXT:body
*)

(* Companion to a-procedure-cannot-goto-a-label-of-its-modules-to-begin-do.pas
   (issue #211): only a NON-local goto -- one reached through a procedure the
   module declares -- is rejected.  A goto directly inside 'to begin do',
   naming a label placed in that same statement, never leaves the module's
   initializer and stays a plain branch. *)
module M;
  label 1;
  var v: integer;
  to begin do begin
    v := 0;
    if v = 0 then goto 1;
    writeln('skipped');
    1: writeln('landed')
  end;
end.
program p;
  import M;
begin
  writeln('body')
end.
