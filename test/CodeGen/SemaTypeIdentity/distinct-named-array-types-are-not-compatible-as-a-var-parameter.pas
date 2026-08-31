(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR-DAG: type mismatch for 'var' parameter
ERR-DAG: 'TArr1'
ERR-DAG: 'TArr2'
*)

program p;
type TArr1 = array[1..5] of Integer; TArr2 = array[1..5] of Integer;
var a: TArr1;
procedure Touch(var x: TArr2); begin x[1] := 99 end;
begin Touch(a) end.
