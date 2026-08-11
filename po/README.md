# Translations

Each file here is a translation of plang's diagnostic messages, in GNU gettext
`.po` format.  `plang -fdiagnostics-language=fr` uses `fr.po`; without the
option, `LC_ALL`, `LC_MESSAGES` and `LANG` are consulted in that order.

`en_US.po` is **not** here.  It is generated at build time from the
`Diagnostic*Kinds.def` files by `tools/plang-po`, because the English is
written in those files and a second copy could only drift from them.  Build
plang and it appears in `<build>/share/plang/locale/en_US.po`; that is the file
to start a new translation from, and the file to `msgmerge` an existing one
against when messages change.

## Starting one

    cmake --build build                    # writes en_US.po
    msginit -i build/share/plang/locale/en_US.po -l de -o po/de.po

## Keeping one current

    msgmerge -U po/de.po build/share/plang/locale/en_US.po

`msgmerge` marks anything whose English has changed `#, fuzzy`, which plang
then ignores — so a message whose meaning has shifted goes back to English
rather than keeping a translation that may no longer be true.

## The rules plang enforces

- An entry is found by its **`msgctxt`**, not by its English.  Rewording a
  message in English does not untranslate it.
- **`%0`..`%9`** are values plang substitutes.  Put them in whatever order the
  language needs, but use the same *set*: an entry that drops or invents one is
  refused and the English is used for it.  What appears inside a `%N` is a
  Pascal identifier, type name or operator, and is never translated.
- **`#, fuzzy` means not used.**  Read a draft with
  `-fdiagnostics-show-fuzzy`; clear the flag once it has been checked.
- An **empty `msgstr`** is untranslated, and falls back to English on its own.
  A translation is useful long before it is finished.
- The file must be **UTF-8**.  plang links no iconv and refuses anything else.
- Only `\\`, `\"`, `\n` and `\t` are accepted as escapes.

## Regional catalogs

`fr_CA.po`, `es_MX.po`, `en_GB.po` and `en_CA.po` are *deltas*: they name only
what differs regionally, and everything else falls through to the language
below them and then to English.  `es_MX` loads `es.po` first and lays itself
over it, so it need only carry the entries where Mexican usage differs from
peninsular Spanish.  `es_ES` has no file at all and simply resolves to `es.po`.

## Status

| File | State |
|---|---|
| `en_GB.po`, `en_CA.po` | in use — spelling only |
| `fr.po`, `fr_CA.po` | **drafted, unreviewed** — every entry fuzzy |
| `es.po`, `es_MX.po` | **drafted, unreviewed** — every entry fuzzy |

The French and Spanish were drafted without a native speaker and are marked
fuzzy throughout, so plang prints English for them.  A mistranslated diagnostic
sends someone hunting for the wrong mistake, which is worse than reading
English.  Clearing a fuzzy flag is how a reviewer puts an entry into use, one
at a time.
