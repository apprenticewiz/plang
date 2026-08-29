(*
-std=turbo: the bare, no-parentheses `eof`/`eoln` idiom (CGExprCore.cpp's
own IdentExpr arm) has to follow Input's OWN storage, exactly like an
explicit `eof(input)`/`eoln(input)` call already does (CGFuncCall.cpp) --
before this item, both bare and explicit forms without their own file
argument read the real stdin directly (plang_eof_stdin/plang_eoln_stdin),
which could never see a redirection performed through Assign(Input, ...).
This drives Input all the way to its own end-of-file entirely through a
redirected file, checking eof/eoln at each line.

RUN: printf 'one\ntwo' > bare-eof-and-eoln-follow-a-redirected-input.txt
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:before: eof=FALSE
CHECK-NEXT:line: one
CHECK-NEXT:before: eof=FALSE
CHECK-NEXT:line: two
CHECK-NEXT:after: eof=TRUE
*)

var s: string;
begin
  Assign(Input, 'bare-eof-and-eoln-follow-a-redirected-input.txt');
  Reset(Input);
  while not eof do begin
    Writeln('before: eof=', eof);
    Readln(s);
    Writeln('line: ', s);
  end;
  Writeln('after: eof=', eof);
  Close(Input);
end.
