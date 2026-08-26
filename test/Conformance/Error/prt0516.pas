(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 516: Misspelled 'set' on set type

}

program iso7185prt0516;

var a: ste of char;

begin

   a := []

end.
