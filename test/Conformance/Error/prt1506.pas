(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 1506: Missing digits in exponent after '+'

}

program iso7185prt1506(output);

var a: integer;

begin

   a := 5e+

end.
