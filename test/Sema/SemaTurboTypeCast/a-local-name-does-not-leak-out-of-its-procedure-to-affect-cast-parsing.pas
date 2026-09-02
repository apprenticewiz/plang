(*
Issue #599: Parser::TypeNames_/VarNames_ (Parser.h) are flat, parser-wide
sets that ParseExpr.cpp's cast-vs-call disambiguation consults, with no
scope chain of their own.  A procedure's local 'var T: Integer' used to add
'T' to VarNames_ and never remove it once parsing left the procedure, so
'T(x)' in the OUTER scope -- where T is a plain type name and should
therefore parse as a Turbo value typecast -- was permanently misparsed as
an ordinary call and rejected with 'T' is not callable, matching what a
local variable actually shadowing T would produce, even though that local
T had already gone out of scope.  parseProcDecl now snapshots and restores
TypeNames_/VarNames_ around a procedure/function's parameter list and body
(NameScopeGuard, ParseDecl.cpp), so this program runs and prints the
truncated byte value FPC 3.2.2 -Mtp also gives it.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:1
*)

program p;
type T = Byte;
var x: Word;
procedure q;
var T: Integer;
begin T := 0 end;
begin
  x := 257;
  writeln(T(x))
end.
