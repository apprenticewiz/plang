(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 1300: Misspelled 'nil'

}

program iso7185prt1300(output);

var a: ^integer;

begin

   a := nul  

end.
