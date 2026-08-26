(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 20: Missing "=" in const

}

program iso7185prt0020(output);

const one 1;

begin

   writeln(one)

end.
