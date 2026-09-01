(*
The ABI parse accumulates digit by digit. If that accumulator wraps at 32
bits, an ABI far beyond what this reader understands can wrap back around to
a small, accepted value -- 2**32 + 1 wrapping to 1, which is CatalogAbi
itself, and being waved through as if it were exactly the ABI this plang
supports. It must be refused exactly as a plain "2" is.

RUN: split-file %s %t.dir
RUN: env PLANG_LOCALE_DIR=%t.dir not %plang -fdiagnostics-language=xx 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: no input files
*)

//--- xx.po
msgid ""
msgstr ""
"Content-Type: text/plain; charset=UTF-8\n"
"X-Plang-Catalog-ABI: 4294967297\n"

msgctxt "diag/err_no_input_files"
msgid "no input files"
msgstr "quelque chose"
