(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 703: Missing first operand to '>'

}

program iso7185prt0703;

var b: boolean;

begin

   b := > 1

end.
