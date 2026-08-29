(*
PChar/PAnsiChar's own landing gave them pointer arithmetic, p[i] indexing,
and array-to-pointer decay, but write()/writeln() rejected a PChar argument
outright ("'PChar' cannot be written; a write-parameter is an integer, real,
boolean, char or string value") -- checkCallStmt's write-parameter switch
(SemaStmt.cpp) never grew a case for isCharPointerType, even though CodeGen's
emitWriteValue already handles a bare pointer value correctly by falling
through to its string-writer arm (nothing about that arm is string(N)-
specific -- a raw char* reaches plang_write(ln)_str/plang_write_str_w exactly
as it needs to).  Sema's whitelist was the only thing standing between a
PChar and working output.  Checked directly against `fpc -Mtp`:
`writeln(p)`/`write(p:w)` print the bytes p points to up to the NUL
terminator, exactly the string-writer behavior this now reaches.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:Hi
CHECK-NEXT:        Hi|
*)

var
  p: PChar;
  buf: array[0..20] of AnsiChar;
begin
  buf[0] := 'H'; buf[1] := 'i'; buf[2] := #0;
  p := @buf[0];
  writeln(p);
  write(p:10);
  writeln('|');
end.
