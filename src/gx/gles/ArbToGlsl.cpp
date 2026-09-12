#include "gx/gles/ArbToGlsl.hpp"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

struct ParamEntry {
    bool literal = false;
    float value[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    int32_t localIndex = -1;
};

struct Translator {
    bool fragment = false;
    ArbProgramInfo* info = nullptr;
    std::string body;
    std::map<std::string, std::vector<ParamEntry>> arrays;
    std::map<std::string, std::string> aliases;
    std::vector<std::string> temps;
    std::vector<std::string> addresses;
    bool usesPointSize = false;
    bool usesFragDepth = false;
    bool usesSecondaryColor = false;
    std::string error;

    bool Fail(const std::string& message) {
        if (this->error.empty()) {
            this->error = message;
        }

        return false;
    }

    static std::string Trim(const std::string& s) {
        size_t start = 0;
        size_t end = s.size();

        while (start < end && isspace(static_cast<unsigned char>(s[start]))) {
            start++;
        }

        while (end > start && isspace(static_cast<unsigned char>(s[end - 1]))) {
            end--;
        }

        return s.substr(start, end - start);
    }

    // Splits on a separator that is not nested inside braces or brackets
    static std::vector<std::string> SplitTopLevel(const std::string& s, char sep) {
        std::vector<std::string> parts;
        int32_t depth = 0;
        std::string current;

        for (char c : s) {
            if (c == '{' || c == '[' || c == '(') {
                depth++;
            } else if (c == '}' || c == ']' || c == ')') {
                depth--;
            }

            if (c == sep && depth == 0) {
                parts.push_back(Trim(current));
                current.clear();
            } else {
                current += c;
            }
        }

        if (!Trim(current).empty()) {
            parts.push_back(Trim(current));
        }

        return parts;
    }

    static std::string FormatFloat(float v) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.9g", v);

        if (!strchr(buf, '.') && !strchr(buf, 'e') && !strchr(buf, 'n') && !strchr(buf, 'i')) {
            strcat(buf, ".0");
        }

        return buf;
    }

    static std::string Vec4(const float* v) {
        return "vec4(" + FormatFloat(v[0]) + ", " + FormatFloat(v[1]) + ", " + FormatFloat(v[2]) + ", " + FormatFloat(v[3]) + ")";
    }

    static bool IsSwizzleChar(char c) {
        return c == 'x' || c == 'y' || c == 'z' || c == 'w' || c == 'r' || c == 'g' || c == 'b' || c == 'a';
    }

    static char NormalizeComponent(char c) {
        switch (c) {
        case 'r': return 'x';
        case 'g': return 'y';
        case 'b': return 'z';
        case 'a': return 'w';
        default: return c;
        }
    }

    static bool IsSwizzle(const std::string& s) {
        if (s.empty() || s.size() > 4) {
            return false;
        }

        for (char c : s) {
            if (!IsSwizzleChar(c)) {
                return false;
            }
        }

        return true;
    }

    // Splits an operand like "fragment.texcoord[1].zwzw" into its base and swizzle
    static void SplitSwizzle(const std::string& operand, std::string& base, std::string& swizzle) {
        int32_t depth = 0;
        size_t lastDot = std::string::npos;

        for (size_t i = 0; i < operand.size(); i++) {
            char c = operand[i];

            if (c == '[' || c == '{') {
                depth++;
            } else if (c == ']' || c == '}') {
                depth--;
            } else if (c == '.' && depth == 0) {
                lastDot = i;
            }
        }

        base = operand;
        swizzle.clear();

        if (lastDot != std::string::npos) {
            std::string tail = operand.substr(lastDot + 1);

            if (IsSwizzle(tail)) {
                base = operand.substr(0, lastDot);

                for (char c : tail) {
                    swizzle += NormalizeComponent(c);
                }
            }
        }
    }

    bool ParseLiteral(const std::string& text, ParamEntry& entry) {
        std::string inner = Trim(text);

        if (inner.size() >= 2 && inner.front() == '{' && inner.back() == '}') {
            inner = inner.substr(1, inner.size() - 2);
        }

        auto parts = SplitTopLevel(inner, ',');

        if (parts.empty() || parts.size() > 4) {
            return this->Fail("bad literal " + text);
        }

        entry.literal = true;
        entry.value[0] = 0.0f;
        entry.value[1] = 0.0f;
        entry.value[2] = 0.0f;
        entry.value[3] = 1.0f;

        for (size_t i = 0; i < parts.size(); i++) {
            entry.value[i] = static_cast<float>(strtod(parts[i].c_str(), nullptr));
        }

        return true;
    }

    static bool ParseIndexRange(const std::string& text, int32_t& first, int32_t& last) {
        // "[3]" or "[1..33]"
        size_t open = text.find('[');
        size_t close = text.find(']');

        if (open == std::string::npos || close == std::string::npos || close < open) {
            return false;
        }

        std::string inner = Trim(text.substr(open + 1, close - open - 1));
        size_t dots = inner.find("..");

        if (dots == std::string::npos) {
            first = last = atoi(inner.c_str());
        } else {
            first = atoi(inner.substr(0, dots).c_str());
            last = atoi(inner.substr(dots + 2).c_str());
        }

        return last >= first;
    }

    bool ParseParamEntries(const std::string& text, std::vector<ParamEntry>& entries) {
        std::string inner = Trim(text);

        // A single literal vector or a single binding is written without an outer brace list
        bool list = false;

        if (inner.size() >= 2 && inner.front() == '{' && inner.back() == '}') {
            // Distinguish "{ 0, 1 }" from "{ program.local[0..3], { 0, 1 } }"
            std::string content = inner.substr(1, inner.size() - 2);
            auto parts = SplitTopLevel(content, ',');

            for (auto& part : parts) {
                if (!part.empty() && (part.front() == '{' || part.find("program.") == 0 || part.find("state.") == 0)) {
                    list = true;
                }
            }

            if (list) {
                inner = content;
            }
        }

        auto parts = list ? SplitTopLevel(inner, ',') : std::vector<std::string>{ inner };

        for (auto& part : parts) {
            if (part.empty()) {
                continue;
            }

            if (part.front() == '{') {
                ParamEntry entry;

                if (!this->ParseLiteral(part, entry)) {
                    return false;
                }

                entries.push_back(entry);
            } else if (part.find("program.local") == 0 || part.find("program.env") == 0) {
                int32_t first, last;

                if (!ParseIndexRange(part, first, last)) {
                    return this->Fail("bad binding " + part);
                }

                for (int32_t i = first; i <= last; i++) {
                    ParamEntry entry;
                    entry.literal = false;
                    entry.localIndex = i;
                    entries.push_back(entry);
                }
            } else if (isdigit(static_cast<unsigned char>(part.front())) || part.front() == '-' || part.front() == '.') {
                ParamEntry entry;
                entry.literal = true;
                float v = static_cast<float>(strtod(part.c_str(), nullptr));
                entry.value[0] = entry.value[1] = entry.value[2] = entry.value[3] = v;
                entries.push_back(entry);
            } else {
                return this->Fail("unsupported parameter binding " + part);
            }
        }

        return true;
    }

    bool Declare(const std::string& keyword, const std::string& rest) {
        if (keyword == "TEMP") {
            for (auto& name : SplitTopLevel(rest, ',')) {
                this->temps.push_back(name);
            }

            return true;
        }

        if (keyword == "ADDRESS") {
            for (auto& name : SplitTopLevel(rest, ',')) {
                this->addresses.push_back(name);
            }

            return true;
        }

        if (keyword == "OPTION") {
            if (rest == "ARB_fog_linear") {
                this->info->fogLinear = true;
            } else if (rest == "ARB_fog_exp" || rest == "ARB_fog_exp2") {
                // Only linear fog is emulated; treat the others as linear
                this->info->fogLinear = true;
            }

            // ARB_fragment_program_shadow, ARB_position_invariant, and precision hints need nothing
            return true;
        }

        if (keyword == "ATTRIB" || keyword == "OUTPUT") {
            size_t eq = rest.find('=');

            if (eq == std::string::npos) {
                return this->Fail("bad alias " + rest);
            }

            this->aliases[Trim(rest.substr(0, eq))] = Trim(rest.substr(eq + 1));
            return true;
        }

        if (keyword == "PARAM") {
            size_t eq = rest.find('=');

            if (eq == std::string::npos) {
                return this->Fail("bad parameter " + rest);
            }

            std::string lhs = Trim(rest.substr(0, eq));
            std::string rhs = Trim(rest.substr(eq + 1));

            size_t bracket = lhs.find('[');
            std::string name = bracket == std::string::npos ? lhs : Trim(lhs.substr(0, bracket));

            if (rhs.find("program.") == 0 && bracket == std::string::npos) {
                // PARAM name = program.local[n];
                this->aliases[name] = rhs;
                return true;
            }

            std::vector<ParamEntry> entries;

            if (!this->ParseParamEntries(rhs, entries)) {
                return false;
            }

            this->arrays[name] = entries;
            return true;
        }

        return this->Fail("unknown declaration " + keyword);
    }

    std::string ConstantArray() const {
        return this->fragment ? "pc" : "vc";
    }

    bool IsAddressRegister(const std::string& name) const {
        for (auto& a : this->addresses) {
            if (a == name) {
                return true;
            }
        }

        return name == "A0";
    }

    // Resolves an operand base (no swizzle) into a vec4 GLSL expression
    bool ResolveSource(const std::string& baseIn, std::string& expr) {
        std::string base = Trim(baseIn);

        if (base.empty()) {
            return this->Fail("empty operand");
        }

        if (base.front() == '{') {
            ParamEntry entry;

            if (!this->ParseLiteral(base, entry)) {
                return false;
            }

            expr = Vec4(entry.value);
            return true;
        }

        auto alias = this->aliases.find(base);

        if (alias != this->aliases.end()) {
            return this->ResolveSource(alias->second, expr);
        }

        for (auto& t : this->temps) {
            if (t == base) {
                expr = base;
                return true;
            }
        }

        // Parameter arrays: c[4], c[A0.x + 31]
        size_t bracket = base.find('[');
        std::string name = bracket == std::string::npos ? base : Trim(base.substr(0, bracket));

        auto array = this->arrays.find(name);

        if (array != this->arrays.end()) {
            auto& entries = array->second;
            std::string index = "0";

            if (bracket != std::string::npos) {
                size_t close = base.rfind(']');
                index = Trim(base.substr(bracket + 1, close - bracket - 1));
            }

            std::string relative;
            int32_t offset = 0;

            if (!index.empty() && !isdigit(static_cast<unsigned char>(index.front()))) {
                // Relative addressing: A0.x, A0.x + 31, A0.x - 1
                std::string reg = index;
                size_t plus = index.find_first_of("+-");

                if (plus != std::string::npos) {
                    reg = Trim(index.substr(0, plus));
                    offset = atoi(Trim(index.substr(plus + 1)).c_str());

                    if (index[plus] == '-') {
                        offset = -offset;
                    }
                }

                size_t dot = reg.find('.');

                if (dot != std::string::npos) {
                    reg = reg.substr(0, dot);
                }

                if (!this->IsAddressRegister(reg)) {
                    return this->Fail("bad index " + index);
                }

                relative = reg;
            } else {
                offset = atoi(index.c_str());
            }

            if (offset < 0 || static_cast<size_t>(offset) >= entries.size()) {
                return this->Fail("index out of range in " + base);
            }

            auto& entry = entries[offset];

            if (relative.empty()) {
                if (entry.literal) {
                    expr = Vec4(entry.value);
                } else {
                    expr = this->ConstantArray() + "[" + std::to_string(entry.localIndex) + "]";
                }
            } else {
                if (entry.literal) {
                    return this->Fail("relative addressing into a literal in " + base);
                }

                expr = this->ConstantArray() + "[" + relative + " + " + std::to_string(entry.localIndex) + "]";
            }

            return true;
        }

        if (base.find("program.local") == 0 || base.find("program.env") == 0) {
            int32_t first, last;

            if (!ParseIndexRange(base, first, last)) {
                return this->Fail("bad binding " + base);
            }

            expr = this->ConstantArray() + "[" + std::to_string(first) + "]";
            return true;
        }

        if (base.find("vertex.attrib") == 0) {
            int32_t first, last;

            if (!ParseIndexRange(base, first, last) || first > 15) {
                return this->Fail("bad attribute " + base);
            }

            this->info->attribMask |= 1 << first;
            expr = "a" + std::to_string(first);
            return true;
        }

        if (base == "vertex.position") {
            this->info->attribMask |= 1;
            expr = "a0";
            return true;
        }

        if (base == "vertex.normal") {
            this->info->attribMask |= 1 << 3;
            expr = "a3";
            return true;
        }

        if (base == "vertex.color" || base == "vertex.color.primary") {
            this->info->attribMask |= 1 << 4;
            expr = "a4";
            return true;
        }

        if (base.find("vertex.texcoord") == 0) {
            int32_t first = 0, last = 0;

            if (base.find('[') != std::string::npos && !ParseIndexRange(base, first, last)) {
                return this->Fail("bad texcoord " + base);
            }

            this->info->attribMask |= 1 << (6 + first);
            expr = "a" + std::to_string(6 + first);
            return true;
        }

        if (base == "fragment.color" || base == "fragment.color.primary") {
            expr = "v_color";
            return true;
        }

        if (base == "fragment.color.secondary") {
            expr = "vec4(0.0, 0.0, 0.0, 1.0)";
            return true;
        }

        if (base.find("fragment.texcoord") == 0) {
            int32_t first = 0, last = 0;

            if (base.find('[') != std::string::npos && !ParseIndexRange(base, first, last)) {
                return this->Fail("bad texcoord " + base);
            }

            if (first > 7) {
                return this->Fail("texcoord out of range " + base);
            }

            expr = "v_tc" + std::to_string(first);
            return true;
        }

        if (base == "fragment.fogcoord") {
            expr = "v_fog";
            return true;
        }

        if (base == "fragment.position") {
            expr = "gl_FragCoord";
            return true;
        }

        return this->Fail("unknown operand " + base);
    }

    // Full source operand: optional negation, base, swizzle
    bool Source(const std::string& operandIn, std::string& expr) {
        std::string operand = Trim(operandIn);
        bool negate = false;

        if (!operand.empty() && operand.front() == '-') {
            negate = true;
            operand = Trim(operand.substr(1));
        } else if (!operand.empty() && operand.front() == '+') {
            operand = Trim(operand.substr(1));
        }

        bool absolute = false;

        if (operand.size() >= 2 && operand.front() == '|' && operand.back() == '|') {
            absolute = true;
            operand = Trim(operand.substr(1, operand.size() - 2));
        }

        std::string base, swizzle;
        SplitSwizzle(operand, base, swizzle);

        std::string baseExpr;

        if (!this->ResolveSource(base, baseExpr)) {
            return false;
        }

        expr = baseExpr;

        if (!swizzle.empty()) {
            if (swizzle.size() == 1) {
                swizzle = std::string(4, swizzle[0]);
            } else if (swizzle.size() != 4) {
                // Pad short swizzles the way the assembler would reject; keep it defined
                while (swizzle.size() < 4) {
                    swizzle += swizzle.back();
                }
            }

            expr = "(" + expr + ")." + swizzle;
        }

        if (absolute) {
            expr = "abs(" + expr + ")";
        }

        if (negate) {
            expr = "(-" + expr + ")";
        }

        return true;
    }

    // Resolves a destination register into a GLSL lvalue; scalar reports float targets
    bool ResolveDest(const std::string& baseIn, std::string& name, bool& scalar) {
        std::string base = Trim(baseIn);
        scalar = false;

        auto alias = this->aliases.find(base);

        if (alias != this->aliases.end()) {
            return this->ResolveDest(alias->second, name, scalar);
        }

        for (auto& t : this->temps) {
            if (t == base) {
                name = base;
                return true;
            }
        }

        if (this->fragment) {
            if (base == "result.color" || base == "result.color.primary") {
                name = "o_col";
                return true;
            }

            if (base == "result.depth") {
                this->usesFragDepth = true;
                name = "gl_FragDepth";
                scalar = true;
                return true;
            }
        } else {
            if (base == "result.position") {
                name = "gl_Position";
                return true;
            }

            if (base == "result.color" || base == "result.color.primary" || base == "result.color.front" || base == "result.color.front.primary") {
                name = "v_color";
                return true;
            }

            if (base.find("result.color") == 0) {
                // Secondary or back colors are not used by the client; keep them in a scratch register
                this->usesSecondaryColor = true;
                name = "o_color1";
                return true;
            }

            if (base.find("result.texcoord") == 0) {
                int32_t first = 0, last = 0;

                if (base.find('[') != std::string::npos && !ParseIndexRange(base, first, last)) {
                    return this->Fail("bad texcoord " + base);
                }

                if (first > 7) {
                    return this->Fail("texcoord out of range " + base);
                }

                name = "v_tc" + std::to_string(first);
                return true;
            }

            if (base == "result.fogcoord") {
                name = "v_fog";
                return true;
            }

            if (base == "result.pointsize") {
                this->usesPointSize = true;
                name = "gl_PointSize";
                scalar = true;
                return true;
            }
        }

        return this->Fail("unknown destination " + base);
    }

    bool Assign(const std::string& operandIn, const std::string& expr, bool saturate) {
        std::string operand = Trim(operandIn);
        std::string base, mask;
        SplitSwizzle(operand, base, mask);

        std::string name;
        bool scalar;

        if (!this->ResolveDest(base, name, scalar)) {
            return false;
        }

        std::string value = saturate ? "clamp(" + expr + ", 0.0, 1.0)" : expr;

        if (scalar) {
            char component = mask.empty() ? 'x' : mask[0];
            this->body += "    " + name + " = (" + value + ")." + component + ";\n";
            return true;
        }

        if (mask.empty() || mask == "xyzw") {
            this->body += "    " + name + " = " + value + ";\n";
        } else {
            this->body += "    " + name + "." + mask + " = (" + value + ")." + mask + ";\n";
        }

        return true;
    }

    bool Instruction(const std::string& opcodeIn, const std::string& rest) {
        std::string opcode = opcodeIn;
        bool saturate = false;

        if (opcode.size() > 4 && opcode.compare(opcode.size() - 4, 4, "_SAT") == 0) {
            saturate = true;
            opcode = opcode.substr(0, opcode.size() - 4);
        }

        auto operands = SplitTopLevel(rest, ',');

        auto need = [&](size_t count) -> bool {
            if (operands.size() != count) {
                return this->Fail("wrong operand count for " + opcode);
            }

            return true;
        };

        std::string s[3];

        auto sources = [&](size_t count) -> bool {
            for (size_t i = 0; i < count; i++) {
                if (!this->Source(operands[i + 1], s[i])) {
                    return false;
                }
            }

            return true;
        };

        if (opcode == "ARL") {
            if (!need(2) || !sources(1)) {
                return false;
            }

            std::string base, mask;
            SplitSwizzle(operands[0], base, mask);

            if (!this->IsAddressRegister(Trim(base))) {
                return this->Fail("ARL into a non address register");
            }

            this->body += "    " + Trim(base) + " = int(floor((" + s[0] + ").x));\n";
            return true;
        }

        if (opcode == "KIL") {
            if (!need(1) || !this->Source(operands[0], s[0])) {
                return false;
            }

            this->body += "    if (any(lessThan(" + s[0] + ", vec4(0.0)))) discard;\n";
            return true;
        }

        if (opcode == "TEX" || opcode == "TXP" || opcode == "TXB") {
            if (!need(4) || !sources(1)) {
                return false;
            }

            int32_t first, last;

            if (!ParseIndexRange(operands[2], first, last) || first > 15) {
                return this->Fail("bad texture unit " + operands[2]);
            }

            std::string target = Trim(operands[3]);
            std::string unit = std::to_string(first);
            std::string expr;

            if (target == "SHADOW2D" || target == "SHADOWRECT") {
                this->info->shadowSamplerMask |= 1 << first;
                expr = "vec4(texture(sh" + unit + ", (" + s[0] + ").xyz))";
            } else if (target == "CUBE") {
                this->info->cubeSamplerMask |= 1 << first;
                expr = "texture(sc" + unit + ", (" + s[0] + ").xyz)";
            } else {
                this->info->samplerMask |= 1 << first;

                if (opcode == "TXP") {
                    expr = "textureProj(s" + unit + ", (" + s[0] + ").xyw)";
                } else if (opcode == "TXB") {
                    expr = "texture(s" + unit + ", (" + s[0] + ").xy, (" + s[0] + ").w)";
                } else {
                    expr = "texture(s" + unit + ", (" + s[0] + ").xy)";
                }
            }

            return this->Assign(operands[0], expr, saturate);
        }

        std::string expr;

        if (opcode == "MOV") {
            if (!need(2) || !sources(1)) return false;
            expr = s[0];
        } else if (opcode == "ABS") {
            if (!need(2) || !sources(1)) return false;
            expr = "abs(" + s[0] + ")";
        } else if (opcode == "FLR") {
            if (!need(2) || !sources(1)) return false;
            expr = "floor(" + s[0] + ")";
        } else if (opcode == "FRC") {
            if (!need(2) || !sources(1)) return false;
            expr = "fract(" + s[0] + ")";
        } else if (opcode == "RCP") {
            if (!need(2) || !sources(1)) return false;
            expr = "vec4(1.0 / (" + s[0] + ").x)";
        } else if (opcode == "RSQ") {
            if (!need(2) || !sources(1)) return false;
            expr = "vec4(inversesqrt(abs((" + s[0] + ").x)))";
        } else if (opcode == "EX2") {
            if (!need(2) || !sources(1)) return false;
            expr = "vec4(exp2((" + s[0] + ").x))";
        } else if (opcode == "LG2") {
            if (!need(2) || !sources(1)) return false;
            expr = "vec4(log2(abs((" + s[0] + ").x)))";
        } else if (opcode == "SIN") {
            if (!need(2) || !sources(1)) return false;
            expr = "vec4(sin((" + s[0] + ").x))";
        } else if (opcode == "COS") {
            if (!need(2) || !sources(1)) return false;
            expr = "vec4(cos((" + s[0] + ").x))";
        } else if (opcode == "SCS") {
            if (!need(2) || !sources(1)) return false;
            expr = "vec4(cos((" + s[0] + ").x), sin((" + s[0] + ").x), 0.0, 0.0)";
        } else if (opcode == "LIT") {
            if (!need(2) || !sources(1)) return false;
            expr = "vec4(1.0, max((" + s[0] + ").x, 0.0), ((" + s[0] + ").x > 0.0) ? pow(max((" + s[0] + ").y, 0.0), clamp((" + s[0] + ").w, -128.0, 128.0)) : 0.0, 1.0)";
        } else if (opcode == "ADD") {
            if (!need(3) || !sources(2)) return false;
            expr = "(" + s[0] + " + " + s[1] + ")";
        } else if (opcode == "SUB") {
            if (!need(3) || !sources(2)) return false;
            expr = "(" + s[0] + " - " + s[1] + ")";
        } else if (opcode == "MUL") {
            if (!need(3) || !sources(2)) return false;
            expr = "(" + s[0] + " * " + s[1] + ")";
        } else if (opcode == "MIN") {
            if (!need(3) || !sources(2)) return false;
            expr = "min(" + s[0] + ", " + s[1] + ")";
        } else if (opcode == "MAX") {
            if (!need(3) || !sources(2)) return false;
            expr = "max(" + s[0] + ", " + s[1] + ")";
        } else if (opcode == "DP3") {
            if (!need(3) || !sources(2)) return false;
            expr = "vec4(dot((" + s[0] + ").xyz, (" + s[1] + ").xyz))";
        } else if (opcode == "DP4") {
            if (!need(3) || !sources(2)) return false;
            expr = "vec4(dot(" + s[0] + ", " + s[1] + "))";
        } else if (opcode == "DPH") {
            if (!need(3) || !sources(2)) return false;
            expr = "vec4(dot((" + s[0] + ").xyz, (" + s[1] + ").xyz) + (" + s[1] + ").w)";
        } else if (opcode == "DST") {
            if (!need(3) || !sources(2)) return false;
            expr = "vec4(1.0, (" + s[0] + ").y * (" + s[1] + ").y, (" + s[0] + ").z, (" + s[1] + ").w)";
        } else if (opcode == "SGE") {
            if (!need(3) || !sources(2)) return false;
            expr = "vec4(greaterThanEqual(" + s[0] + ", " + s[1] + "))";
        } else if (opcode == "SLT") {
            if (!need(3) || !sources(2)) return false;
            expr = "vec4(lessThan(" + s[0] + ", " + s[1] + "))";
        } else if (opcode == "POW") {
            if (!need(3) || !sources(2)) return false;
            expr = "vec4(pow(max((" + s[0] + ").x, 0.0), (" + s[1] + ").x))";
        } else if (opcode == "XPD") {
            if (!need(3) || !sources(2)) return false;
            expr = "vec4(cross((" + s[0] + ").xyz, (" + s[1] + ").xyz), 1.0)";
        } else if (opcode == "MAD") {
            if (!need(4) || !sources(3)) return false;
            expr = "(" + s[0] + " * " + s[1] + " + " + s[2] + ")";
        } else if (opcode == "LRP") {
            if (!need(4) || !sources(3)) return false;
            expr = "mix(" + s[2] + ", " + s[1] + ", " + s[0] + ")";
        } else if (opcode == "CMP") {
            if (!need(4) || !sources(3)) return false;
            expr = "mix(" + s[2] + ", " + s[1] + ", vec4(lessThan(" + s[0] + ", vec4(0.0))))";
        } else {
            return this->Fail("unsupported instruction " + opcode);
        }

        return this->Assign(operands[0], expr, saturate);
    }

    bool Translate(const std::string& text) {
        // Header
        size_t lineEnd = text.find('\n');
        std::string header = Trim(text.substr(0, lineEnd));

        if (header.find("!!ARBfp") == 0) {
            this->fragment = true;
        } else if (header.find("!!ARBvp") == 0) {
            this->fragment = false;
        } else {
            return this->Fail("not an ARB program");
        }

        this->info->fragment = this->fragment;

        // Strip comments
        std::string code;
        bool comment = false;

        for (size_t i = lineEnd == std::string::npos ? text.size() : lineEnd; i < text.size(); i++) {
            char c = text[i];

            if (c == '#') {
                comment = true;
            } else if (c == '\n') {
                comment = false;
            }

            if (!comment) {
                code += c == '\n' || c == '\r' || c == '\t' ? ' ' : c;
            }
        }

        // Statements
        for (auto& statement : SplitTopLevel(code, ';')) {
            if (statement.empty()) {
                continue;
            }

            if (statement == "END") {
                break;
            }

            size_t space = statement.find_first_of(" \t");
            std::string keyword = space == std::string::npos ? statement : statement.substr(0, space);
            std::string rest = space == std::string::npos ? "" : Trim(statement.substr(space + 1));

            bool ok;

            if (keyword == "TEMP" || keyword == "ADDRESS" || keyword == "OPTION" || keyword == "ATTRIB" || keyword == "OUTPUT" || keyword == "PARAM" || keyword == "ALIAS") {
                ok = keyword == "ALIAS" ? true : this->Declare(keyword, rest);
            } else {
                ok = this->Instruction(keyword, rest);
            }

            if (!ok) {
                return false;
            }
        }

        return true;
    }

    std::string Emit() const {
        std::string out;

        out += "#version 300 es\n";
        out += "precision highp float;\n";
        out += "precision highp int;\n";

        if (this->fragment) {
            out += "precision highp sampler2D;\n";
            out += "precision highp sampler2DShadow;\n";
            out += "precision highp samplerCube;\n";
            out += "layout(std140) uniform PsConstants { vec4 pc[256]; };\n";

            for (int32_t i = 0; i < 16; i++) {
                if (this->info->samplerMask & (1 << i)) {
                    out += "uniform sampler2D s" + std::to_string(i) + ";\n";
                }

                if (this->info->cubeSamplerMask & (1 << i)) {
                    out += "uniform samplerCube sc" + std::to_string(i) + ";\n";
                }

                if (this->info->shadowSamplerMask & (1 << i)) {
                    out += "uniform sampler2DShadow sh" + std::to_string(i) + ";\n";
                }
            }

            out += "uniform float u_alphaRef;\n";
            out += "uniform vec4 u_fogParams;\n";
            out += "uniform vec4 u_fogColor;\n";
            out += "in vec4 v_color;\n";

            for (int32_t i = 0; i < 8; i++) {
                out += "in vec4 v_tc" + std::to_string(i) + ";\n";
            }

            out += "in vec4 v_fog;\n";
            out += "out vec4 o_fragColor;\n";
        } else {
            out += "layout(std140) uniform VsConstants { vec4 vc[256]; };\n";

            for (int32_t i = 0; i < 16; i++) {
                if (this->info->attribMask & (1 << i)) {
                    out += "layout(location = " + std::to_string(i) + ") in vec4 a" + std::to_string(i) + ";\n";
                }
            }

            out += "out vec4 v_color;\n";

            for (int32_t i = 0; i < 8; i++) {
                out += "out vec4 v_tc" + std::to_string(i) + ";\n";
            }

            out += "out vec4 v_fog;\n";
        }

        out += "void main() {\n";

        if (this->fragment) {
            out += "    vec4 o_col = vec4(0.0, 0.0, 0.0, 1.0);\n";
        } else {
            out += "    v_color = vec4(1.0, 1.0, 1.0, 1.0);\n";

            for (int32_t i = 0; i < 8; i++) {
                out += "    v_tc" + std::to_string(i) + " = vec4(0.0, 0.0, 0.0, 1.0);\n";
            }

            out += "    v_fog = vec4(0.0);\n";
            out += "    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);\n";

            if (this->usesSecondaryColor) {
                out += "    vec4 o_color1 = vec4(0.0);\n";
            }
        }

        for (auto& t : this->temps) {
            out += "    vec4 " + t + " = vec4(0.0);\n";
        }

        for (auto& a : this->addresses) {
            out += "    int " + a + " = 0;\n";
        }

        out += this->body;

        if (this->fragment) {
            out += "    o_col = clamp(o_col, 0.0, 1.0);\n";
            out += "    if (u_alphaRef >= 0.0 && o_col.w < u_alphaRef) discard;\n";

            if (this->info->fogLinear) {
                out += "    if (u_fogParams.z > 0.5) {\n";
                out += "        float fogFactor = clamp((u_fogParams.y - v_fog.x) / max(u_fogParams.y - u_fogParams.x, 0.0001), 0.0, 1.0);\n";
                out += "        o_col.xyz = mix(u_fogColor.xyz, o_col.xyz, fogFactor);\n";
                out += "    }\n";
            }

            out += "    o_fragColor = o_col;\n";
        }

        out += "}\n";

        return out;
    }
};

} // namespace

bool ArbToGlsl(const char* source, size_t length, std::string& glsl, ArbProgramInfo& info) {
    // The shader containers keep a trailing NUL inside the code buffer
    while (length && source[length - 1] == 0) {
        length--;
    }

    Translator translator;
    translator.info = &info;

    info = ArbProgramInfo();
    translator.info = &info;

    if (!translator.Translate(std::string(source, length))) {
        info.error = translator.error;
        return false;
    }

    glsl = translator.Emit();
    return true;
}
