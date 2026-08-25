(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 134: Missing expression on repeat statement

}

program iso7185prt0134;

var a, b: integer;

begin

   repeat a := b until

end.
