(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 709: Missing first operand to '<='

}

program iso7185prt0709;

var b: boolean;

begin

   b := <= 1

end.
