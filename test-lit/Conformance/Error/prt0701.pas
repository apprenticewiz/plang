(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 701: Missing first operand to '='

}

program iso7185prt0701;

var b: boolean;

begin

   b := = 1

end.
