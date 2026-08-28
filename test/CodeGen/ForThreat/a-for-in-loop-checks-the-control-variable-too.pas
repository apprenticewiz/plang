(*
Issue #405: checkForIn never called checkForBody, so none of the
control-variable threat checks #265/#291 (PR #342) added for the to/downto
form -- reassignment, var-parameter aliasing, use as a read/readln target --
were ever applied to `for v in set-expr do`.  This mirrors
expression-position-var-param-and-readstr-still-threaten.pas, but drives the
same threats through a for-in loop instead.

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: must not be assigned
ERR: must not be passed as a 'var' parameter
*)

program p(output);
var c: char; y: integer; s: set of char;

function f(var v: char): integer;
begin f := ord(v) end;

begin
  s := ['a', 'b', 'c'];
  for c in s do c := 'z';
  for c in s do y := f(c);
  writeln(y)
end.
