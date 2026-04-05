// ============================================================================
// FoundationKitCxxAbi — Itanium ABI Demangler Implementation
// ============================================================================
// Recursive-descent parser for the Itanium C++ ABI name mangling grammar.
// All state is on the call stack (DemangleState). Zero heap allocations.
//
// GRAMMAR COVERAGE:
//   - Builtin types (v, b, c, a, h, s, t, i, j, l, m, x, y, n, o, f, d, e)
//   - Source names: <length><identifier>
//   - Nested names: N [cv-quals] <component>* E
//   - Unscoped names: <source-name> | St <source-name>
//   - Template args: I <type>* E
//   - Substitutions: S_, S<n>_, St, Sa, Sb, Ss, Si, So, Sd
//   - CV-qualifiers: r (restrict), V (volatile), K (const)
//   - Ref-qualifiers: R (&), O (&&)
//   - Pointer/reference declarators: P, R, O, C, G
//   - Constructor/destructor names: C1/C2/C3, D0/D1/D2
//   - Operator names: nw (new), dl (delete), as (=), eq (==), etc.
//   - Function types: F <return-type> <param-type>* [v] E
//   - Local names: Z <function> E <entity>
//   - Vendor extended types: u <source-name>
//
// SUBSTITUTION SEMANTICS:
//   Every "nameable" parsed prefix is recorded in the substitution table.
//   S_  resolves to subs[0], S0_ to subs[1], S1_ to subs[2], etc.
//   Named substitutions: St=std::, Sa=std::allocator, Ss=std::string, etc.
// ============================================================================

#include <FoundationKitCxxAbi/Demangle/Demangler.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/CharType.hpp>

using namespace FoundationKitCxxAbi::Demangle;
using namespace FoundationKitCxxStl;

namespace {

void ParseEncoding(DemangleState& s) noexcept;
void ParseName(DemangleState& s) noexcept;
void ParseNestedName(DemangleState& s) noexcept;
void ParseUnscopedName(DemangleState& s) noexcept;
void ParseUnqualifiedName(DemangleState& s) noexcept;
void ParseSourceName(DemangleState& s) noexcept;
void ParseType(DemangleState& s) noexcept;
void ParseBuiltinType(DemangleState& s) noexcept;
void ParseTemplateArgs(DemangleState& s) noexcept;
void ParseSubstitution(DemangleState& s) noexcept;
void ParseBareFunctionType(DemangleState& s) noexcept;
void ParseOperatorName(DemangleState& s) noexcept;
void ParseCtorDtorName(DemangleState& s) noexcept;
void ParseLocalName(DemangleState& s) noexcept;
void ParseCvQualifiers(DemangleState& s) noexcept;

/// @brief Record the current output extent as a substitution.
void RecordSubstitution(DemangleState& s, usize start) noexcept {
    if (s.nsubs < k_max_substitutions) {
        s.subs[s.nsubs++] = {start, s.output.written};
    } else {
        FK_LOG_WARN("__cxa_demangle: substitution table overflow (max={}). "
                    "Demangling may be incomplete.", k_max_substitutions);
    }
}

/// @brief Resolve a substitution index into the output buffer and re-emit.
void EmitSubstitution(DemangleState& s, usize index) noexcept {
    if (index >= s.nsubs) {
        FK_LOG_WARN("__cxa_demangle: invalid substitution index {} (table has {} entries).",
                    index, s.nsubs);
        s.output.Append("?S?");
        s.SetError();
        return;
    }
    const SubstitutionEntry& e = s.subs[index];
    // Re-emit the recorded output range.
    for (usize i = e.output_start; i < e.output_end; ++i) {
        s.output.Append(s.output.buf[i]);
    }
}

[[nodiscard]] usize ParseNumber(DemangleInput& in) noexcept {
    usize n = 0;
    while (CharType::IsDigit(in.Peek())) {
        n = n * 10 + static_cast<usize>(in.Consume() - '0');
    }
    return n;
}

[[nodiscard]] usize ParseSeqId(DemangleInput& in) noexcept {
    usize n = 0;
    while (in.Peek() != '_' && in.HasMore()) {
        char c = in.Consume();
        if (CharType::IsDigit(c)) {
            n = n * 36 + static_cast<usize>(c - '0');
        } else if (CharType::IsUpper(c)) {
            n = n * 36 + static_cast<usize>(c - 'A' + 10);
        } else {
            break;
        }
    }
    return n;
}

void ParseSubstitution(DemangleState& s) noexcept {
    DemangleInput& in = s.input;
    if (!in.Expect('S')) { s.SetError(); return; }

    char c = in.Peek();

    // Named substitutions (std:: aliases)
    if (c == 't') { in.Consume(); s.output.Append("std"); return; }
    if (c == 'a') { in.Consume(); s.output.Append("std::allocator"); return; }
    if (c == 'b') { in.Consume(); s.output.Append("std::basic_string"); return; }
    if (c == 's') { in.Consume(); s.output.Append("std::string"); return; }
    if (c == 'i') { in.Consume(); s.output.Append("std::istream"); return; }
    if (c == 'o') { in.Consume(); s.output.Append("std::ostream"); return; }
    if (c == 'd') { in.Consume(); s.output.Append("std::iostream"); return; }

    // Numbered substitutions: S_ = subs[0], S0_ = subs[1], S1_ = subs[2], ...
    if (c == '_') {
        in.Consume();
        EmitSubstitution(s, 0);
        return;
    }

    // S <seq-id> _
    // seq-id is 1-based: S0_ → subs[1], S1_ → subs[2], ...
    usize seq = ParseSeqId(in) + 1;
    if (!in.Expect('_')) { s.SetError(); return; }
    EmitSubstitution(s, seq);
}

void ParseBuiltinType(DemangleState& s) noexcept {
    char c = s.input.Consume();
    switch (c) {
        case 'v': s.output.Append("void");                  return;
        case 'b': s.output.Append("bool");                  return;
        case 'c': s.output.Append("char");                  return;
        case 'a': s.output.Append("signed char");           return;
        case 'h': s.output.Append("unsigned char");         return;
        case 's': s.output.Append("short");                 return;
        case 't': s.output.Append("unsigned short");        return;
        case 'i': s.output.Append("int");                   return;
        case 'j': s.output.Append("unsigned int");          return;
        case 'l': s.output.Append("long");                  return;
        case 'm': s.output.Append("unsigned long");         return;
        case 'x': s.output.Append("long long");             return;
        case 'y': s.output.Append("unsigned long long");    return;
        case 'n': s.output.Append("__int128");              return;
        case 'o': s.output.Append("unsigned __int128");     return;
        case 'f': s.output.Append("float");                 return;
        case 'd': s.output.Append("double");                return;
        case 'e': s.output.Append("long double");           return;
        case 'g': s.output.Append("__float128");            return;
        case 'z': s.output.Append("...");                   return;
        case 'D':
            // Extended types: Di=char32_t, Ds=char16_t, Da=auto, Dc=decltype(auto)
            switch (s.input.Consume()) {
                case 'i': s.output.Append("char32_t");      return;
                case 's': s.output.Append("char16_t");      return;
                case 'a': s.output.Append("auto");          return;
                case 'c': s.output.Append("decltype(auto)");return;
                case 'n': s.output.Append("std::nullptr_t");return;
                default:  s.output.Append("?D?");           return;
            }
        default:
            s.output.Append("?T?");
            s.SetError();
            return;
    }
}

void ParseCvQualifiers(DemangleState& s) noexcept {
    bool has_const    = false;
    bool has_volatile = false;
    bool has_restrict = false;

    while (s.input.HasMore()) {
        char c = s.input.Peek();
        if      (c == 'K') { s.input.Consume(); has_const    = true; }
        else if (c == 'V') { s.input.Consume(); has_volatile = true; }
        else if (c == 'r') { s.input.Consume(); has_restrict = true; }
        else break;
    }

    // Emit the qualified-base-type by recursing, then append qualifiers.
    ParseType(s);

    if (has_const)    s.output.Append(" const");
    if (has_volatile) s.output.Append(" volatile");
    if (has_restrict) s.output.Append(" restrict");
}

void ParseType(DemangleState& s) noexcept {
    if (!s.input.HasMore() || s.HasError()) return;

    const char c = s.input.Peek();

    // Pointer
    if (c == 'P') {
        s.input.Consume();
        ParseType(s);
        s.output.Append('*');
        return;
    }
    // L-value reference
    if (c == 'R') {
        s.input.Consume();
        ParseType(s);
        s.output.Append('&');
        return;
    }
    // R-value reference
    if (c == 'O') {
        s.input.Consume();
        ParseType(s);
        s.output.Append("&&");
        return;
    }
    // Complex pair
    if (c == 'C') {
        s.input.Consume();
        s.output.Append("_Complex ");
        ParseType(s);
        return;
    }
    // Imaginary
    if (c == 'G') {
        s.input.Consume();
        s.output.Append("_Imaginary ");
        ParseType(s);
        return;
    }
    // CV qualifiers
    if (c == 'K' || c == 'V' || c == 'r') {
        ParseCvQualifiers(s);
        return;
    }
    // Substitution
    if (c == 'S') {
        ParseSubstitution(s);
        return;
    }
    // Nested or unscoped name (a named type)
    if (c == 'N') {
        usize start = s.output.written;
        ParseNestedName(s);
        RecordSubstitution(s, start);
        return;
    }
    // Template instantiation
    if (c == 'I') {
        ParseTemplateArgs(s);
        return;
    }
    // Function type: F <return-type> <params>* [v] E
    if (c == 'F') {
        s.input.Consume();
        ParseType(s); // return type
        s.output.Append('(');
        bool first = true;
        while (s.input.Peek() != 'E' && s.input.HasMore() && !s.HasError()) {
            if (!first) s.output.Append(", ");
            first = false;
            if (s.input.Peek() == 'v' && s.input.PeekAt(1) == 'E') {
                s.input.Consume(); // void params
                break;
            }
            ParseType(s);
        }
        s.input.Expect('E');
        s.output.Append(')');
        return;
    }
    // Vendor extended type: u <source-name>
    if (c == 'u') {
        s.input.Consume();
        ParseSourceName(s);
        return;
    }

    // Builtin type (single char or two-char D-prefix)
    ParseBuiltinType(s);
}

void ParseSourceName(DemangleState& s) noexcept {
    if (!CharType::IsDigit(s.input.Peek())) { s.SetError(); return; }
    const usize len = ParseNumber(s.input);
    if (len == 0 || s.input.ptr + len > s.input.end) {
        s.SetError();
        return;
    }
    s.output.Append(s.input.ptr, len);
    s.input.ptr += len;
}

void ParseOperatorName(DemangleState& s) noexcept {
    char a = s.input.Consume();
    char b = s.input.Consume();

    struct OpEntry { char a, b; const char* name; };
    static constexpr OpEntry k_ops[] = {
        {'n','w',"operator new"},        {'n','a',"operator new[]"},
        {'d','l',"operator delete"},     {'d','a',"operator delete[]"},
        {'p','s',"operator+"},           {'n','g',"operator-"},
        {'a','d',"operator&"},           {'d','e',"operator*"},
        {'c','o',"operator~"},           {'p','l',"operator+"},
        {'m','i',"operator-"},           {'m','l',"operator*"},
        {'d','v',"operator/"},           {'r','m',"operator%"},
        {'a','n',"operator&"},           {'o','r',"operator|"},
        {'e','o',"operator^"},           {'a','S',"operator="},
        {'p','L',"operator+="},          {'m','I',"operator-="},
        {'m','L',"operator*="},          {'d','V',"operator/="},
        {'r','M',"operator%="},          {'a','N',"operator&="},
        {'o','R',"operator|="},          {'e','O',"operator^="},
        {'l','s',"operator<<"},          {'r','s',"operator>>"},
        {'l','S',"operator<<="},         {'r','S',"operator>>="},
        {'e','q',"operator=="},          {'n','e',"operator!="},
        {'l','t',"operator<"},           {'g','t',"operator>"},
        {'l','e',"operator<="},          {'g','e',"operator>="},
        {'s','s',"operator<=>"},         {'n','t',"operator!"},
        {'a','a',"operator&&"},          {'o','o',"operator||"},
        {'p','p',"operator++"},          {'m','m',"operator--"},
        {'c','m',"operator,"},           {'p','m',"operator->*"},
        {'p','t',"operator->"},          {'c','l',"operator()"},
        {'i','x',"operator[]"},          {'q','u',"operator?"},
        {'c','v',"operator "},           {'l','i',"operator\"\""},
    };

    for (const auto& op : k_ops) {
        if (op.a == a && op.b == b) {
            s.output.Append(op.name);
            // 'cv' is a cast operator: emit the target type
            if (a == 'c' && b == 'v') ParseType(s);
            return;
        }
    }
    s.output.Append("operator?");
}

void ParseCtorDtorName(DemangleState& s) noexcept {
    char kind = s.input.Consume();
    char num  = s.input.Consume();

    if (kind == 'C') {
        switch (num) {
            case '1': s.output.Append("<ctor-complete>");  return;
            case '2': s.output.Append("<ctor-base>");      return;
            case '3': s.output.Append("<ctor-allocating>"); return;
            default:  s.output.Append("<ctor-?>"); return;
        }
    }
    if (kind == 'D') {
        switch (num) {
            case '0': s.output.Append("<dtor-deleting>");  return;
            case '1': s.output.Append("<dtor-complete>");  return;
            case '2': s.output.Append("<dtor-base>");      return;
            default:  s.output.Append("<dtor-?>"); return;
        }
    }
    s.output.Append("<?cdtor?>");
    s.SetError();
}

void ParseUnqualifiedName(DemangleState& s) noexcept {
    char c = s.input.Peek();
    if (CharType::IsDigit(c)) {
        ParseSourceName(s);
        return;
    }
    if (c == 'o' || c == 'n' || c == 'd' || c == 'p' || c == 'm' ||
        c == 'a' || c == 'e' || c == 'r' || c == 'l' || c == 'g' ||
        c == 's' || c == 'c' || c == 'q' || c == 'i') {
        ParseOperatorName(s);
        return;
    }
    if (c == 'C' || c == 'D') {
        ParseCtorDtorName(s);
        return;
    }
    // L-prefix: language-specific name (ignore)
    if (c == 'L') {
        s.input.Consume();
        ParseUnqualifiedName(s);
        return;
    }
    s.output.Append("?uqname?");
    s.SetError();
}

void ParseTemplateArgs(DemangleState& s) noexcept {
    if (!s.input.Expect('I')) { s.SetError(); return; }
    s.output.Append('<');
    bool first = true;
    while (s.input.Peek() != 'E' && s.input.HasMore() && !s.HasError()) {
        if (!first) s.output.Append(", ");
        first = false;
        if (s.input.Peek() == 'L') {
            s.input.Consume();
            ParseType(s);
            s.output.Append('(');
            bool neg = s.input.Peek() == 'n';
            if (neg) { s.input.Consume(); s.output.Append('-'); }
            while (CharType::IsDigit(s.input.Peek())) {
                s.output.Append(s.input.Consume());
            }
            s.output.Append(')');
            s.input.Expect('E');
        } else if (s.input.Peek() == 'X') {
            s.input.Consume();
            s.output.Append("<expr>");
            while (s.input.Peek() != 'E' && s.input.HasMore()) s.input.Consume();
            s.input.Expect('E');
        } else {
            ParseType(s);
        }
    }
    s.input.Expect('E');
    if (s.output.written > 0 && s.output.buf[s.output.written - 1] == '>') {
        s.output.Append(' ');
    }
    s.output.Append('>');
}

void ParseNestedName(DemangleState& s) noexcept {
    if (!s.input.Expect('N')) { s.SetError(); return; }

    // Consume optional CV-qualifiers (appear before nested-name in lambdas)
    while (s.input.Peek() == 'K' || s.input.Peek() == 'V' || s.input.Peek() == 'r') {
        s.input.Consume();
    }
    // Consume optional ref-qualifier
    if (s.input.Peek() == 'R' || s.input.Peek() == 'O') {
        s.input.Consume();
    }

    bool first = true;
    while (s.input.Peek() != 'E' && s.input.HasMore() && !s.HasError()) {
        if (!first) s.output.Append("::");
        first = false;

        const char c = s.input.Peek();
        usize seg_start = s.output.written;

        if (c == 'S') {
            ParseSubstitution(s);
        } else if (c == 'T') {
            // Template parameter: T_ T0_ T1_ ...
            s.input.Consume();
            if (s.input.Peek() == '_') {
                s.input.Consume();
                s.output.Append("T0");
            } else {
                usize n = ParseNumber(s.input);
                s.input.Expect('_');
                s.output.Append('T');
                // Emit n+1 as the param index
                char tmp[20];
                usize ti = 0;
                usize val = n + 1;
                do {
                    tmp[ti++] = static_cast<char>('0' + (val % 10));
                    val /= 10;
                } while (val);
                for (usize ri = ti; ri > 0; --ri) s.output.Append(tmp[ri-1]);
            }
        } else if (CharType::IsDigit(c)) {
            ParseSourceName(s);
        } else if (c == 'C' || c == 'D') {
            ParseCtorDtorName(s);
        } else if (c == 'I') {
            ParseTemplateArgs(s);
        } else {
            ParseUnqualifiedName(s);
        }

        if (s.input.Peek() == 'I' && !s.HasError()) {
            RecordSubstitution(s, seg_start);
            ParseTemplateArgs(s);
        }
        RecordSubstitution(s, seg_start);
    }
    s.input.Expect('E');
}

void ParseUnscopedName(DemangleState& s) noexcept {
    if (s.input.Peek() == 'S' && s.input.PeekAt(1) == 't') {
        s.input.Consume(); s.input.Consume();
        s.output.Append("std::");
    }
    ParseUnqualifiedName(s);
}

void ParseName(DemangleState& s) noexcept {
    char c = s.input.Peek();
    if (c == 'N') {
        ParseNestedName(s);
        return;
    }
    if (c == 'Z') {
        ParseLocalName(s);
        return;
    }
    // Unscoped: might be followed by template args.
    usize start = s.output.written;
    ParseUnscopedName(s);
    if (s.input.Peek() == 'I') {
        RecordSubstitution(s, start);
        ParseTemplateArgs(s);
    }
    RecordSubstitution(s, start);
}

void ParseLocalName(DemangleState& s) noexcept {
    if (!s.input.Expect('Z')) { s.SetError(); return; }
    ParseEncoding(s);
    if (!s.input.Expect('E')) { s.SetError(); return; }
    s.output.Append("::");
    if (s.input.Peek() == 's') {
        s.input.Consume();
        s.output.Append("std::string literal");
    } else {
        ParseName(s);
    }
    // Consume optional discriminator _ <number>
    if (s.input.Peek() == '_' && !CharType::IsDigit(s.input.PeekAt(1))) {
        // Not a discriminator, leave it.
    } else if (s.input.Peek() == '_') {
        s.input.Consume();
        while (CharType::IsDigit(s.input.Peek())) s.input.Consume();
    }
}

void ParseBareFunctionType(DemangleState& s) noexcept {
    s.output.Append('(');
    bool first = true;

    while (s.input.HasMore() && !s.HasError()) {
        // 'v' alone means "void parameters" (no arguments)
        if (s.input.Peek() == 'v' &&
            (!s.input.HasMore() || !CharType::IsDigit(s.input.PeekAt(1)))) {
            s.input.Consume();
            break;
        }
        if (!first) s.output.Append(", ");
        first = false;
        ParseType(s);
    }
    s.output.Append(')');
}

void ParseEncoding(DemangleState& s) noexcept {
    if (s.input.Peek() == 'T') {
        s.input.Consume();
        char sub = s.input.Peek();
        if (sub == 'V') { s.input.Consume(); s.output.Append("vtable for "); ParseType(s); return; }
        if (sub == 'T') { s.input.Consume(); s.output.Append("VTT for "); ParseType(s); return; }
        if (sub == 'I') { s.input.Consume(); s.output.Append("typeinfo for "); ParseType(s); return; }
        if (sub == 'S') { s.input.Consume(); s.output.Append("typeinfo name for "); ParseType(s); return; }
        if (sub == 'h') { s.input.Consume(); s.output.Append("non-virtual thunk to "); ParseEncoding(s); return; }
        if (sub == 'v') { s.input.Consume(); s.output.Append("virtual thunk to "); ParseEncoding(s); return; }
        s.SetError();
        return;
    }

    usize name_start = s.output.written;
    (void)name_start;
    ParseName(s);
    if (s.HasError()) return;

    if (s.input.HasMore()) {
        ParseBareFunctionType(s);
    }
}

} // anonymous namespace

namespace FoundationKitCxxAbi::Demangle {

void ParseMangledName(DemangleState& s) noexcept {
    // All Itanium-mangled names begin with "_Z".
    if (!s.input.Expect('_') || !s.input.Expect('Z')) {
        s.output.Append(s.input.begin); // pass-through: not mangled
        return;
    }
    ParseEncoding(s);
}

usize Demangle(StringView mangled, Span<char> out, DemangleStatus& status) noexcept {
    if (mangled.Data() == nullptr) {
        status = DemangleStatus::InvalidArg;
        return 0;
    }
    if (out.Data() == nullptr || out.Size() < 2) {
        status = DemangleStatus::OomOrNullBuf;
        return 0;
    }

    DemangleState state{};
    state.input  = {mangled.Data(), mangled.Data(), mangled.Data() + mangled.Size()};
    state.output = {out.Data(), out.Size(), 0, false};
    state.nsubs  = 0;
    state.error  = false;

    ParseMangledName(state);
    state.output.Finalize();

    if (state.HasError()) {
        status = DemangleStatus::InvalidMangle;
        return 0;
    }
    if (state.output.truncated) {
        // We still return what we could produce.
        status = DemangleStatus::OomOrNullBuf;
        return state.output.written;
    }
    status = DemangleStatus::Success;
    return state.output.written;
}

} // namespace FoundationKitCxxAbi::Demangle

extern "C" {

char* __cxa_demangle(const char* mangled_name, char* buf,
                     unsigned long* n, int* status) {
    using namespace FoundationKitCxxAbi::Demangle;
    using namespace FoundationKitCxxStl;

    // Guard against null inputs — both are programming errors, not OOM.
    FK_BUG_ON(mangled_name == nullptr,
        "__cxa_demangle: mangled_name is null. "
        "The caller passed a null pointer as the symbol to demangle.");

    if (buf == nullptr || n == nullptr || *n < 2) {
        // Standard: *status = -1 if malloc would be needed (we have no malloc).
        if (status) *status = -1;
        return nullptr;
    }

    auto ds = DemangleStatus::Success;
    const StringView sv{mangled_name};  // StringView from null-terminated
    const Span sp{buf, (*n)};

    const usize written = Demangle(sv, sp, ds);

    if (status) *status = static_cast<int>(ds);
    *n = static_cast<unsigned long>(written);

    return (ds == DemangleStatus::Success || written > 0) ? buf : nullptr;
}

} // extern "C"
