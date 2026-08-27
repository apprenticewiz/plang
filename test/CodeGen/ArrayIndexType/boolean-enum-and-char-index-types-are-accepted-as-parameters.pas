(*
Issue #258: array[boolean]/array[Color]/array[char] (and other non-integer
ordinal index types) parsed fine as a `type` or `var` denoter but were
mis-parsed as a formal parameter's array type -- parseConformantOrRegular's
own lo..hi speculation, used only in parameter position, assumed an integer
subrange and never fell back to parseArrayIndexType the way every other
array-type site does.  This exercises the fix past parsing, through Sema and
CodeGen, by passing each index shape as a `var` parameter and reading the
values back out.
*)

(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program p(output);
type Color = (red, green, blue);
var b: array[boolean] of integer;
    c: array[Color] of integer;
    ch: array[char] of integer;

procedure fillBool(var a: array[boolean] of integer);
begin
  a[false] := 10; a[true] := 20;
end;

procedure fillColor(var a: array[Color] of integer);
begin
  a[red] := 1; a[green] := 2; a[blue] := 3;
end;

procedure fillChar(var a: array[char] of integer);
begin
  a['a'] := 97; a['z'] := 122;
end;

begin
  fillBool(b);
  fillColor(c);
  fillChar(ch);
  writeln(b[false], ' ', b[true], ' ', c[red], ' ', c[green], ' ', c[blue], ' ', ch['a'], ' ', ch['z'])
end.

(*
CHECK:10 20 1 2 3 97 122
*)
