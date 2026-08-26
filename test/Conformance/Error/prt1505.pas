(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 1505: Missing digits in exponent

}

program iso7185prt1505(output);

var a: integer;

begin

   a := 5e

end.
