(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 717: Missing first operand to 'in'

}

program iso7185prt0717;

var b: boolean;

begin

   b := in [1..6]

end.
