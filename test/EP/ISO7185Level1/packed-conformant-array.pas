(*
RUN: %plang -std=iso7185 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:hello
CHECK-NEXT:hello world
*)

program p(output);
var a: packed array [1..5] of char;
    b: packed array [1..11] of char;
procedure show(s: packed array [lo..hi: integer] of char);
var i: integer;
begin for i := lo to hi do write(s[i]); writeln end;
begin a := 'hello'; b := 'hello world'; show(a); show(b) end.
