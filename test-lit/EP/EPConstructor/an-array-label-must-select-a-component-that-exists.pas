(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: outside the array's index type
*)

program p(output);
type arr = array[1..4] of integer;
var a: arr;
begin a := arr[1: 1; 2: 2; 3: 3; 4: 4; 5: 555]; writeln(a[1]:1) end.
