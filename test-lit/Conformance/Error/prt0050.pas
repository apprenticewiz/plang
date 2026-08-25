(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 50: Misspelled function

}

program iso7185prt0050(output);

funktion x: integer; begin end;

begin

   writeln(x)

end.
