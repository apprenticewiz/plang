/// MessageCatalog.cpp — the .po reader, and the lookup every diagnostic makes
///
/// The subset read here is the one plang writes: a header entry, then entries
/// of msgctxt / msgid / msgstr, each optionally continued over further quoted
/// lines.  What a translator's tools add around that — `#` comments, `#.`
/// extracted comments, `#:` references, `#|` previous strings, `#~` obsolete
/// entries — is understood well enough to be skipped, and `#,` flags are read
/// for `fuzzy` alone.
///
/// Three things are deliberately narrower than the format allows.
///
/// Only four escapes are decoded: \\ \" \n \t.  Everything else — \r \a \b \f
/// \v, octal, hex — is a malformed entry rather than a character.  This is not
/// laziness: getDiagFormat returns a const char* and formatDiagMsg measures it
/// with strlen, so an embedded NUL would silently truncate a message, and the
/// message text is written to stderr verbatim, so an embedded ESC would let a
/// catalog move the terminal cursor and rewrite what the user thinks they were
/// told.  Neither is representable if they cannot be escaped, and no catalog
/// Poedit or msgmerge produces needs them.  Raw control bytes are refused for
/// the same reason.
///
/// Plurals — msgid_plural and msgstr[N] — are refused rather than half-read.
/// plang has no diagnostic that needs one; the four that count something say
/// "argument(s)".  Reading only the singular would mean adding real plural
/// support later silently changed what an existing catalog meant.
///
/// A translation whose %N placeholders are not the same set as the English is
/// refused.  formatDiagMsg substitutes nothing for an index it has no argument
/// for, so a msgstr that dropped a %0 would produce a sentence with a hole in
/// it and nothing would report that anything was wrong.
///
/// A malformed *entry* costs that entry; the rest of the file is still read,
/// because one typo in a translation should lose one message and not 193.  A
/// malformed *file* — the wrong charset, or a catalog from a newer plang — is
/// refused whole and the previous contents are kept.

#include "plang/Basic/MessageCatalog.h"

#include "plang/Basic/Token.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace plang {

namespace {

/// The catalog ABI this reader understands.  A file declaring a higher one is
/// refused: it may use a construct that would otherwise be read as something
/// else, and guessing is how a catalog comes to mean two things.
constexpr unsigned CatalogAbi = 1;

std::string_view lstrip(std::string_view S) {
    std::size_t I = 0;
    while (I < S.size() && (S[I] == ' ' || S[I] == '\t')) ++I;
    return S.substr(I);
}

/// True if \p S starts with \p Word as a whole keyword, so that "msgid" does
/// not match the "msgid_plural" this reader refuses to guess at.
bool keyword(std::string_view S, std::string_view Word) {
    if (S.size() < Word.size() || S.compare(0, Word.size(), Word) != 0)
        return false;
    if (S.size() == Word.size()) return true;
    const char C = S[Word.size()];
    return C == ' ' || C == '\t' || C == '"';
}

/// Decodes one quoted .po string onto \p Out.  False if the line is not a
/// well-formed quoted string, or holds a byte this reader will not carry.
bool appendQuoted(std::string_view Line, std::string& Out) {
    const std::size_t Open = Line.find('"');
    if (Open == std::string_view::npos) return false;

    for (std::size_t I = Open + 1; I < Line.size(); ++I) {
        const auto C = static_cast<unsigned char>(Line[I]);
        if (C == '"') {
            // Only a comment may follow the closing quote.
            const std::string_view Rest = lstrip(Line.substr(I + 1));
            return Rest.empty() || Rest[0] == '#';
        }
        if (C == '\\') {
            if (++I >= Line.size()) return false;
            switch (Line[I]) {
            case 'n':  Out += '\n'; break;
            case 't':  Out += '\t'; break;
            case '"':  Out += '"';  break;
            case '\\': Out += '\\'; break;
            default:   return false; // see the note on escapes above
            }
            continue;
        }
        // A raw control byte cannot appear in a message: see above.  DEL is
        // included; \n and \t reach the string through their escapes, never
        // as themselves, because a line break ends the line.
        if (C < 0x20 || C == 0x7F) return false;
        Out += static_cast<char>(C);
    }
    return false; // ran off the end with the string still open
}

/// True if \p S is well-formed UTF-8.  Mojibake in a diagnostic is not a
/// crash, but it is not something to pass on either, and a catalog that
/// arrived through a tool that mangled its encoding is better refused than
/// half-shown.
bool validUtf8(std::string_view S) {
    for (std::size_t I = 0; I < S.size();) {
        const auto C = static_cast<unsigned char>(S[I]);
        std::size_t N = 0;
        unsigned Min = 0;
        unsigned Cp  = 0;
        if      (C < 0x80)            { ++I; continue; }
        else if ((C & 0xE0) == 0xC0)  { N = 1; Cp = C & 0x1Fu; Min = 0x80; }
        else if ((C & 0xF0) == 0xE0)  { N = 2; Cp = C & 0x0Fu; Min = 0x800; }
        else if ((C & 0xF8) == 0xF0)  { N = 3; Cp = C & 0x07u; Min = 0x10000; }
        else return false;

        if (I + N >= S.size()) return false;
        for (std::size_t K = 1; K <= N; ++K) {
            const auto Cont = static_cast<unsigned char>(S[I + K]);
            if ((Cont & 0xC0) != 0x80) return false;
            Cp = (Cp << 6) | (Cont & 0x3Fu);
        }
        // Overlong, surrogate, and beyond the last code point.
        if (Cp < Min || (Cp >= 0xD800 && Cp <= 0xDFFF) || Cp > 0x10FFFF)
            return false;
        I += N + 1;
    }
    return true;
}

/// The set of %N indices a format string uses, as a bitmask.  Position and
/// repetition do not matter — reordering is the point of positional
/// placeholders, and err_type_of_undefined already repeats %0 in English.
unsigned placeholderMask(std::string_view Fmt) {
    unsigned Mask = 0;
    for (std::size_t I = 0; I + 1 < Fmt.size(); ++I)
        if (Fmt[I] == '%' && Fmt[I + 1] >= '0' && Fmt[I + 1] <= '9') {
            Mask |= 1u << (Fmt[I + 1] - '0');
            ++I;
        }
    return Mask;
}

/// Spelling of a DiagID to the ID, built once from the same .def the messages
/// come from.  Lets the reader tell a translation of a message plang has from
/// one naming a message it does not, and find the English to validate against.
const std::unordered_map<std::string_view, DiagID>& diagIdsBySpelling() {
    static const auto* Map = [] {
        auto* M = new std::unordered_map<std::string_view, DiagID>();
#define DIAG(ID, LEVEL, MSG) M->emplace(#ID, DiagID::ID);
#include "plang/Basic/DiagnosticMessages.def"
#undef DIAG
        return M;
    }();
    return *Map;
}

/// One entry as it is read.
struct Pending {
    std::string Ctxt, Id, Str;
    bool HasCtxt{false};
    bool HasStr{false};
    bool Fuzzy{false};
    bool Bad{false};   ///< malformed, or a construct this reader will not guess at
    void reset() { *this = Pending{}; }
};

} // namespace

MessageCatalog& MessageCatalog::active() {
    static MessageCatalog C;
    return C;
}

CatalogReport MessageCatalog::loadText(std::string_view Text, bool Merge) {
    CatalogReport R;

    // Built locally and swapped in at the end, so that a file refused partway
    // leaves the catalog exactly as it was.  Merging starts from what is there
    // and lets this file win, which is what layers a region over its language.
    std::unordered_map<std::string, std::string> Built = Merge ? Entries
                                                               : decltype(Entries){};
    const auto& Ids = diagIdsBySpelling();

    if (Text.starts_with("\xEF\xBB\xBF")) Text.remove_prefix(3); // UTF-8 BOM

    Pending P;
    enum class Field { None, Ctxt, Id, Str } Cur = Field::None;

    // A `#,` flag line precedes the entry it describes, so it is read before
    // the msgctxt that starts that entry — and starting an entry flushes the
    // previous one, which resets everything.  Flags therefore wait here and
    // are adopted by the next entry rather than living in the one being
    // flushed away.  Without this every fuzzy entry loaded as if approved,
    // which is the failure this design exists to prevent.
    bool PendingFuzzy = false;
    const auto beginEntry = [&] {
        P.Fuzzy = PendingFuzzy;
        PendingFuzzy = false;
    };

    // The header entry has no msgctxt; its metadata is read here and it is not
    // stored.  Everything else needs a msgctxt to be found by.
    bool SawHeader = false; // the genuine header, once its entry has been read
    auto flush = [&]() -> bool {
        struct Reset { Pending& P; Field& F;
                       ~Reset() { P.reset(); F = Field::None; } } _{P, Cur};

        if (!P.HasCtxt) {
            if (!P.HasStr) return true;             // nothing at all: fine

            // gettext's header is the entry with an empty msgid, and only the
            // first one is it.  msgctxt is optional in gettext and most
            // real-world entries never carry one, so without the Id check
            // here, an ordinary "msgid / msgstr" pair -- ctxt-less, like the
            // header, but a message like any other -- would be misread as
            // the header the moment its msgstr happened to contain a line
            // starting "Content-Type:" or "X-Plang-Catalog-ABI:", and could
            // refuse the whole catalog over a charset that was never
            // actually declared.
            if (!P.Id.empty() || SawHeader) {
                ++R.Unknown; // msgctxt-less and not the header: no key to load it under
                return true;
            }
            SawHeader = true;

            // The header.  Only two fields can refuse the file.
            std::string_view H = P.Str;
            const auto find = [&](std::string_view Key) -> std::string_view {
                for (std::size_t I = 0; I < H.size();) {
                    const std::size_t E = H.find('\n', I);
                    std::string_view L = H.substr(I, E == std::string_view::npos
                                                        ? std::string_view::npos : E - I);
                    if (L.size() > Key.size() &&
                        L.compare(0, Key.size(), Key) == 0)
                        return lstrip(L.substr(Key.size()));
                    if (E == std::string_view::npos) break;
                    I = E + 1;
                }
                return {};
            };
            if (std::string_view CT = find("Content-Type:"); !CT.empty()) {
                const std::size_t Cs = CT.find("charset=");
                if (Cs != std::string_view::npos) {
                    std::string Set(CT.substr(Cs + 8));
                    while (!Set.empty() && (Set.back() == ' ' || Set.back() == '\n'))
                        Set.pop_back();
                    for (char& Ch : Set)
                        Ch = static_cast<char>(std::tolower(
                                 static_cast<unsigned char>(Ch)));
                    if (Set != "utf-8" && Set != "utf8" &&
                        Set != "ascii"  && Set != "us-ascii") {
                        R.FatalReason = "catalog charset '" + Set +
                                        "' is not UTF-8, and plang cannot transcode";
                        return false;
                    }
                }
            }
            if (std::string_view Abi = find("X-Plang-Catalog-ABI:"); !Abi.empty()) {
                // A saturating parse.  CatalogAbi is a small constant, so all
                // that matters past it is "bigger" -- but an accumulator that
                // wraps on overflow can come back around to something small
                // and *equal*, which is how 2**32 + 1 previously wrapped to 1
                // and a catalog claiming an ABI four billion newer than this
                // reader was silently accepted.  Clamping instead of wrapping
                // keeps an oversized or adversarial number reading as "newer
                // than this plang" -- refused -- rather than as anything a
                // real ABI could be.
                std::uint64_t V = 0;
                for (char Ch : Abi) {
                    if (Ch < '0' || Ch > '9') break;
                    const auto Digit = static_cast<std::uint64_t>(Ch - '0');
                    if (V > (std::numeric_limits<std::uint64_t>::max() - Digit) / 10) {
                        V = std::numeric_limits<std::uint64_t>::max();
                        break;
                    }
                    V = V * 10 + Digit;
                }
                if (V > CatalogAbi) {
                    R.FatalReason = "catalog needs a newer plang (ABI " +
                                    std::to_string(V) + ")";
                    return false;
                }
            }
            return true;
        }

        if (P.Bad)    { ++R.Malformed; return true; }
        if (!P.HasStr){ ++R.Malformed; return true; }

        const bool IsDiag  = P.Ctxt.starts_with(PoDiagPrefix);
        const bool IsToken = P.Ctxt.starts_with(PoTokenPrefix);
        const bool IsLabel = P.Ctxt.starts_with(PoLabelPrefix);
        if (!IsDiag && !IsToken && !IsLabel) { ++R.Unknown; return true; }

        const DiagID* Known = nullptr;
        if (IsDiag) {
            const auto It = Ids.find(
                std::string_view(P.Ctxt).substr(PoDiagPrefix.size()));
            if (It == Ids.end()) { ++R.Unknown; return true; }
            Known = &It->second;
        }
        if (P.Str.empty())     { ++R.Untranslated; return true; }
        if (P.Fuzzy && !HonourFuzzy) { ++R.Fuzzy; return true; }
        if (!validUtf8(P.Str)) { ++R.Malformed; return true; }

        // Only a message is a format string.  A token description and a
        // severity label are substituted *into* one, and formatDiagMsg makes a
        // single pass, so a %N inside either is text and not a placeholder.
        if (Known &&
            placeholderMask(P.Str) != placeholderMask(builtinDiagFormat(*Known))) {
            ++R.Malformed;  // a hole in the sentence; see the note above
            return true;
        }

        Built.insert_or_assign(P.Ctxt, P.Str);
        ++R.Loaded;
        return true;
    };

    std::size_t Pos = 0;
    while (Pos <= Text.size()) {
        const std::size_t Nl = Text.find('\n', Pos);
        std::string_view Raw =
            Text.substr(Pos, Nl == std::string_view::npos ? std::string_view::npos
                                                          : Nl - Pos);
        Pos = (Nl == std::string_view::npos) ? Text.size() + 1 : Nl + 1;
        if (!Raw.empty() && Raw.back() == '\r') Raw.remove_suffix(1); // CRLF

        const std::string_view Line = lstrip(Raw);
        if (Line.empty()) { if (!flush()) return R; continue; }

        if (Line[0] == '#') {
            // "#~" retires an entry.  Its lines look like live ones, so
            // skipping only the marker would read it straight back in.
            if (Line.size() > 1 && Line[1] == '~') { if (!flush()) return R; continue; }
            if (Line.size() > 1 && Line[1] == ',' &&
                Line.find("fuzzy") != std::string_view::npos)
                PendingFuzzy = true;
            continue;
        }

        if (keyword(Line, "msgctxt")) {
            if (!flush()) return R;
            beginEntry();
            P.HasCtxt = true;
            Cur = Field::Ctxt;
            if (!appendQuoted(Line, P.Ctxt)) { P.Bad = true; Cur = Field::None; }
            continue;
        }
        if (keyword(Line, "msgid_plural") || Line.starts_with("msgstr[")) {
            P.Bad = true;         // plurals: refused whole, see above
            Cur = Field::None;
            continue;
        }
        if (keyword(Line, "msgid")) {
            if (Cur == Field::Str) {
                if (!flush()) return R;
                beginEntry();   // an entry written without a msgctxt
            }
            Cur = Field::Id;
            if (!appendQuoted(Line, P.Id)) { P.Bad = true; Cur = Field::None; }
            continue;
        }
        if (keyword(Line, "msgstr")) {
            P.HasStr = true;
            Cur = Field::Str;
            if (!appendQuoted(Line, P.Str)) { P.Bad = true; Cur = Field::None; }
            continue;
        }
        if (Line[0] == '"') {
            std::string* Dst = Cur == Field::Ctxt ? &P.Ctxt
                             : Cur == Field::Id   ? &P.Id
                             : Cur == Field::Str  ? &P.Str
                                                  : nullptr;
            if (!Dst) { P.Bad = true; continue; } // continuation of something dropped
            if (!appendQuoted(Line, *Dst)) { P.Bad = true; Cur = Field::None; }
            continue;
        }

        P.Bad = true; // not a keyword, a string, a comment or blank
    }
    if (!flush()) return R;

    Entries = std::move(Built);
    return R;
}

CatalogReport MessageCatalog::loadFile(const std::string& Path, bool Merge) {
    CatalogReport R;

    // A catalog is a regular file.  Without this, pointing plang at a FIFO
    // would block it forever and pointing it at /dev/zero would read until the
    // machine gave out — neither of which a compiler should do because a path
    // was wrong.
    std::error_code Ec;
    const auto Status = std::filesystem::status(Path, Ec);
    if (Ec || !std::filesystem::is_regular_file(Status)) {
        R.FatalReason = "'" + Path + "' is not a readable file";
        return R;
    }
    const auto Size = std::filesystem::file_size(Path, Ec);
    if (Ec) {
        R.FatalReason = "cannot size '" + Path + "'";
        return R;
    }
    if (Size > MaxCatalogBytes) {
        R.FatalReason = "'" + Path + "' is larger than a message catalog can be";
        return R;
    }

    // stdio rather than a stream: plang is built -fno-exceptions and this has
    // to report failure by returning it.
    std::FILE* F = std::fopen(Path.c_str(), "rb");
    if (!F) {
        R.FatalReason = "cannot open '" + Path + "'";
        return R;
    }
    std::string Text;
    Text.reserve(static_cast<std::size_t>(Size));
    char Buf[8192];
    while (const std::size_t N = std::fread(Buf, 1, sizeof(Buf), F))
        Text.append(Buf, N);
    const bool ReadFailed = std::ferror(F) != 0;
    std::fclose(F);
    if (ReadFailed) {
        R.FatalReason = "cannot read '" + Path + "'";
        return R;
    }
    return loadText(Text, Merge);
}

const char* MessageCatalog::lookupDiag(DiagID ID) const {
    if (Entries.empty()) return nullptr; // the usual case: no catalog loaded
    const auto It = Entries.find(std::string(PoDiagPrefix)
                                 + std::string(getDiagSpelling(ID)));
    return It == Entries.end() ? nullptr : It->second.c_str();
}

const char* MessageCatalog::lookupToken(std::string_view KindName) const {
    if (Entries.empty()) return nullptr;
    const auto It = Entries.find(std::string(PoTokenPrefix) + std::string(KindName));
    return It == Entries.end() ? nullptr : It->second.c_str();
}

const char* MessageCatalog::lookupLabel(DiagSeverity Sev) const {
    if (Entries.empty()) return nullptr;
    const auto It = Entries.find(std::string(PoLabelPrefix)
                                 + builtinSeverityLabel(Sev));
    return It == Entries.end() ? nullptr : It->second.c_str();
}

// ---------------------------------------------------------------------------
// Choosing a language, and finding the file
// ---------------------------------------------------------------------------

namespace {

/// The path as a reader should see it.  Discovery composes <exedir>/../share,
/// so an installed plang would otherwise report a catalog at
/// ".../bin/../share/plang/locale/fr.po", which is correct, unreadable, and
/// impossible for a script to match against the prefix it installed to.
std::string tidyPath(const std::string& Path) {
    std::error_code Ec;
    const auto C = std::filesystem::weakly_canonical(Path, Ec);
    return Ec ? Path : C.string();
}

} // namespace


std::string normaliseLocale(std::string_view Spec) {
    // A POSIX locale is language[_TERRITORY][.codeset][@modifier].  The
    // codeset and the modifier say nothing about which translation to use --
    // plang reads UTF-8 and nothing else -- so both are dropped.
    std::size_t End = Spec.size();
    if (const std::size_t At = Spec.find('@'); At != std::string_view::npos)
        End = At;
    if (const std::size_t Dot = Spec.find('.'); Dot != std::string_view::npos)
        End = std::min(End, Dot);
    std::string Tag(Spec.substr(0, End));

    // "C" and "POSIX" are the absence of a locale, not a language.
    if (Tag == "C" || Tag == "POSIX") return {};
    return Tag;
}

std::string localeFromEnvironment() {
    // POSIX order: LC_ALL overrides everything, LC_MESSAGES is the category
    // this is about, LANG is the fallback for all of them.
    for (const char* Var : {"LC_ALL", "LC_MESSAGES", "LANG"})
        if (const char* V = std::getenv(Var); V && *V)
            return normaliseLocale(V);
    return {};
}

std::vector<std::string> catalogSearchOrder(std::string_view Tag) {
    if (Tag.empty()) return {};

    // English is what is compiled in.  Looking for a file to tell us the
    // English is English would be work for no result, and it is the common
    // case, so it is the case that does no I/O at all.  A regional English --
    // en_GB, en_CA -- is a real catalog and is not short-circuited.
    if (Tag == "en" || Tag == "en_US") return {};

    std::vector<std::string> Order{std::string(Tag)};
    if (const std::size_t Us = Tag.find('_'); Us != std::string_view::npos)
        Order.emplace_back(Tag.substr(0, Us)); // fr_CA -> fr
    return Order;
}

std::vector<std::string> catalogSearchPaths(const std::string& ExeDir) {
    std::vector<std::string> Paths;
    if (const char* Override = std::getenv("PLANG_LOCALE_DIR"); Override && *Override)
        Paths.emplace_back(Override);
    if (!ExeDir.empty())
        Paths.push_back(ExeDir + "/../share/plang/locale");
#ifdef PLANG_CATALOG_DIR
    Paths.emplace_back(PLANG_CATALOG_DIR);
#endif
    return Paths;
}

namespace {
/// What selectLocale last decided.  plang is single-threaded and resolves once
/// per process, so a plain static is the whole of the state this needs.
LocaleResolution& mutableCurrentLocale() {
    static LocaleResolution R;
    return R;
}
} // namespace

const LocaleResolution& currentLocale() { return mutableCurrentLocale(); }

LocaleResolution selectLocale(std::string_view Tag, const std::string& ExeDir,
                              bool HonourFuzzy) {
    LocaleResolution R;
    R.Requested = Tag.empty() ? localeFromEnvironment() : normaliseLocale(Tag);

    MessageCatalog::active().clear();
    MessageCatalog::active().setHonorFuzzy(HonourFuzzy);

    const auto Langs = catalogSearchOrder(R.Requested);
    const auto Dirs  = catalogSearchPaths(ExeDir);

    // Least specific first, each merged over the last, so that es_MX.po -- a
    // few dozen entries naming only what Mexico spells differently -- lands on
    // top of the whole of es.po rather than instead of it.  Loading only the
    // most specific file would leave every message it does not name in
    // English, which is the opposite of what a delta catalog is for.
    for (auto It = Langs.rbegin(); It != Langs.rend(); ++It) {
        const std::string& Lang = *It;
        for (const auto& Dir : Dirs) {
            const std::string Path = Dir + "/" + Lang + ".po";
            std::error_code Ec;
            if (!std::filesystem::is_regular_file(Path, Ec)) continue;
            auto Rep = MessageCatalog::active().loadFile(Path, /*Merge=*/true);
            if (!Rep.ok()) continue;   // refused; the others still stand
            R.Language = Lang;
            R.Path     = tidyPath(Path);
            R.Loaded.push_back(R.Path);
            R.Report.Loaded       += Rep.Loaded;
            R.Report.Fuzzy        += Rep.Fuzzy;
            R.Report.Untranslated += Rep.Untranslated;
            R.Report.Unknown      += Rep.Unknown;
            R.Report.Malformed    += Rep.Malformed;
            break;  // one directory wins per language
        }
    }
    mutableCurrentLocale() = R;
    return R; // nothing found leaves the compiled-in English, always there
}

std::string describeLocale() {
    const LocaleResolution& R = currentLocale();
    if (!R.Path.empty()) return R.Language + " (" + R.Path + ")";
    // Nothing was loaded.  Whether that is the answer or a disappointment
    // depends on whether a catalog was ever going to be looked for.
    if (catalogSearchOrder(R.Requested).empty())
        return (R.Requested.empty() ? std::string("en_US") : R.Requested)
             + " (built-in)";
    return R.Requested + " (no catalog found; using built-in en_US)";
}

// ---------------------------------------------------------------------------
// The hook Diagnostic.h declares.  A translation if there is one, English if
// there is not — which is every lookup until something loads a catalog.
// ---------------------------------------------------------------------------

const char* getDiagFormat(DiagID ID) {
    if (const char* Translated = MessageCatalog::active().lookupDiag(ID))
        return Translated;
    return builtinDiagFormat(ID);
}

const char* localizedTokenDescription(TokenKind K) {
    if (const char* T = MessageCatalog::active().lookupToken(kindName(K)))
        return T;
    return nullptr; // the caller falls back to the .def spelling
}

const char* localizedSeverityLabel(DiagSeverity Sev) {
    if (const char* T = MessageCatalog::active().lookupLabel(Sev)) return T;
    return builtinSeverityLabel(Sev);
}

} // namespace plang
