(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 1501: Missing digit after '.'

}

program iso7185prt1501(output);

var a: integer;

begin

   a := 5.

end.
