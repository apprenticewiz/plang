(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 407: Missing 2nd constant on subrange

}

program iso7185prt0155;

var a: 1..;

begin

   a := 1

end.
