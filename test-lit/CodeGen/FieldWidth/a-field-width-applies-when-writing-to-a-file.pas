(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:hi th
CHECK-NEXT:  hi there !
*)

program p(output);
type s10 = packed array [1..10] of char;
var f: text; s: s10; c: char;
begin
  s := 'hi there !';
  rewrite(f); writeln(f, s:5); writeln(f, s:12);
  reset(f);
  while not eof(f) do begin
    if eoln(f) then begin readln(f); writeln end
    else begin read(f, c); write(c) end
  end
end.
