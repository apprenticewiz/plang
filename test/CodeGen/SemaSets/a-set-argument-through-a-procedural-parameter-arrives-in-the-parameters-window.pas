(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(* Sibling to a-set-argument-arrives-in-the-parameters-window.pas, but called
   through a procedural parameter instead of directly.  Both direct paths
   (CGProcCall.cpp, CGFuncCall.cpp) route a plain-value set argument through
   Sets.alignSetArg so it lands in the callee's own window; the procedural-
   parameter call's plain-value branch (ClosureAndCallABI.cpp) used to skip
   that rebase entirely, so a set argument arrived with its bits carried
   across unmoved.  y holds [0, 5] in a window based at -5 (raw bits 5 and
   10); read back correctly through s's window based at 0 that is [0, 5]
   again, but the unrebased bug read it back as [5, 10]. *)
program p(output);
type a = set of -5..10; b = set of 0..10;
var y: a;
procedure show(s: b);
var j: integer;
begin
  for j := 0 to 10 do if j in s then write(j:3);
  writeln
end;
procedure callit(procedure f(s: b); v: a);
begin f(v) end;
begin y := [0, 5]; callit(show, y) end.

(*
CHECK:  0  5
*)
