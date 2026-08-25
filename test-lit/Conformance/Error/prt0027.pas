(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 27: Missing "=" in type

}

program iso7185prt0027(output);

type  integer char;

var i: integer;

begin

   writeln(i)

end.
