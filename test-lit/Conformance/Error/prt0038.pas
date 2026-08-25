(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 38: Incomplete second in var

}

program iso7185prt0038(output);

var  one: char;
     two;

begin

   writeln(one, two)

end.
