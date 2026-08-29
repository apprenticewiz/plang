(*
Real Turbo Pascal idiom: ParamCount, like eof/eoln, is a zero-argument
builtin function most commonly written bare, with no parentheses at all.
Sema::checkIdent's generic SymbolKind::Builtin case already types a bare
read correctly; CGExprCore.cpp needed its own dedicated arm (mirroring
eof/eoln's) to route the READ itself to plang_tp_paramcount instead of
falling through to the ordinary variable table.  See
paramcount-and-paramstr-report-the-real-command-line-arguments.pas for the
WITH-parentheses form, exercised there instead.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t x y | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2
*)

program paramcountbare;
begin
  writeln(ParamCount);
end.
