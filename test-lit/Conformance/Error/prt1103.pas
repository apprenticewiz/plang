(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 1103: Missing second operand to '/'

}

program iso7185prt1103(output);

var a: real;
    b: integer;

begin

   a := b/

end.
