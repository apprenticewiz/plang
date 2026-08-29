(*
-std=turbo: the read-side complement of bare-writeln-with-no-file-argument-
writes-to-a-redirected-output.pas -- `Assign(Input, name); Reset(Input);`
redirects where a BARE `Readln` (no file argument at all) actually reads
from, exactly the way real Turbo Pascal's own Input redirection idiom
works.

RUN: printf 'first redirected line\nsecond redirected line\n' > bare-readln-with-no-file-argument-reads-from-a-redirected-input.txt
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:read: first redirected line
CHECK-NEXT:read: second redirected line
*)

var s: string;
begin
  Assign(Input, 'bare-readln-with-no-file-argument-reads-from-a-redirected-input.txt');
  Reset(Input);
  Readln(s);
  Writeln('read: ', s);
  Readln(s);
  Writeln('read: ', s);
  Close(Input);
end.
