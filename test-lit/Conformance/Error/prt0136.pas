(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 136: Misspelled "for" on for statement

}

program iso7185prt0136;

var i, a, b: integer;

begin

   fro i := 1 to 10 do a := b

end.
