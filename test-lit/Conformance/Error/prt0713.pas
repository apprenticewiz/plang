(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 713: Missing second operand to 'in'

}

program iso7185prt0713;

var b: boolean;

begin

   b := 1 in

end.
