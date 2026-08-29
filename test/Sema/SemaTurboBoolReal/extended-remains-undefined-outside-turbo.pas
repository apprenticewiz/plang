(*
Extended/Comp's explicit refusal (extended-is-refused-under-turbo.pas, this
directory) is gated on Opts.turbo() the OPPOSITE way EP's complex/string
keyword-rejection is: complex/string are recognized under every dialect and
refused outside EP, while Extended/Comp are recognized ONLY under Turbo --
ISO 7185 and Extended Pascal never had either name as a keyword at all.  So
outside -std=turbo this stays the plain, generic "undefined type" a program
would get for any other unknown identifier, exactly as it did before this
feature existed; it must not turn into err_turbo_unsupported_float_type's
more specific wording where Turbo's own rules do not even apply.

RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
RUN: not %plang_ep %s -o %t2 2> %t2.err
RUN: FileCheck %s < %t2.err
*)

(*
CHECK: undefined type 'Extended'
CHECK-NOT: plang implements Turbo's Real and Single
*)

program p;
var
  x: Extended;
begin
end.
