(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 711: Missing first operand to '>='

}

program iso7185prt0711;

var b: boolean;

begin

   b := >= 1

end.
