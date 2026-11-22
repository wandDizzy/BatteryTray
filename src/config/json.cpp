#include "json.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static const Json kNull;

bool Json::has(const std::string& key) const {
    if (t_ != Object) return false;
    for (auto& kv : o_) if (kv.first == key) return true;
    return false;
}

const Json& Json::operator[](const std::string& key) const {
    if (t_ != Object) return kNull;
    for (auto& kv : o_) if (kv.first == key) return kv.second;
    return kNull;
}

Json& Json::operator[](const std::string& key) {
    if (t_ != Object) { *this = Obj(); }
    for (auto& kv : o_) if (kv.first == key) return kv.second;
    o_.emplace_back(key, Json());
    return o_.back().second;
}

Json& Json::push(const Json& v) {
    if (t_ != Array) { *this = Arr(); }
    a_.push_back(v);
    return a_.back();
}

static void Escape(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
            else out += (char)c;
        }
    }
    out += '"';
}

void Json::dumpTo(std::string& out, int indent, int level) const {
    auto nl = [&](int lv) {
        if (indent <= 0) return;
        out += '\n';
        out.append((size_t)(lv * indent), ' ');
    };
    switch (t_) {
    case Null: out += "null"; break;
    case Bool: out += b_ ? "true" : "false"; break;
    case Number: {
        char b[32];
        if (std::fabs(n_ - std::round(n_)) < 1e-9 && std::fabs(n_) < 1e15) snprintf(b, sizeof(b), "%lld", (long long)std::llround(n_));
        else snprintf(b, sizeof(b), "%.6g", n_);
        out += b;
        break;
    }
    case String: Escape(out, s_); break;
    case Array: {
        if (a_.empty()) { out += "[]"; break; }
        out += '[';
        for (size_t i = 0; i < a_.size(); i++) {
            nl(level + 1);
            a_[i].dumpTo(out, indent, level + 1);
            if (i + 1 < a_.size()) out += ',';
        }
        nl(level);
        out += ']';
        break;
    }
    case Object: {
        if (o_.empty()) { out += "{}"; break; }
        out += '{';
        for (size_t i = 0; i < o_.size(); i++) {
            nl(level + 1);
            Escape(out, o_[i].first);
            out += indent > 0 ? ": " : ":";
            o_[i].second.dumpTo(out, indent, level + 1);
            if (i + 1 < o_.size()) out += ',';
        }
        nl(level);
        out += '}';
        break;
    }
    }
}

std::string Json::dump(int indent) const {
    std::string s;
    dumpTo(s, indent, 0);
    return s;
}

struct JsonParser {
    const std::string& s;
    size_t i = 0;
    std::string err;

    void ws() {
        for (;;) {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
            // allow // comments, handy for hand-edited configs
            if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') {
                while (i < s.size() && s[i] != '\n') i++;
                continue;
            }
            break;
        }
    }
    bool fail(const char* m) { if (err.empty()) { err = m; err += " at offset " + std::to_string(i); } return false; }

    static void AppendUtf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }

    bool str(std::string& out) {
        if (s[i] != '"') return fail("expected string");
        i++;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) return fail("bad escape");
            char e = s[i++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                if (i + 4 > s.size()) return fail("bad \\u");
                unsigned cp = (unsigned)strtoul(s.substr(i, 4).c_str(), nullptr, 16);
                i += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                    unsigned lo = (unsigned)strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16);
                    i += 6;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                AppendUtf8(out, cp);
                break;
            }
            default: out += e;
            }
        }
        if (i >= s.size()) return fail("unterminated string");
        i++;
        return true;
    }

    bool value(Json& v) {
        ws();
        if (i >= s.size()) return fail("unexpected end");
        char c = s[i];
        if (c == '{') {
            v = Json::Obj();
            i++;
            ws();
            if (i < s.size() && s[i] == '}') { i++; return true; }
            for (;;) {
                ws();
                std::string k;
                if (!str(k)) return false;
                ws();
                if (i >= s.size() || s[i] != ':') return fail("expected ':'");
                i++;
                Json child;
                if (!value(child)) return false;
                v.o_.emplace_back(k, child);
                ws();
                if (i < s.size() && s[i] == ',') { i++; ws(); if (i < s.size() && s[i] == '}') { i++; return true; } continue; }
                if (i < s.size() && s[i] == '}') { i++; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            v = Json::Arr();
            i++;
            ws();
            if (i < s.size() && s[i] == ']') { i++; return true; }
            for (;;) {
                Json child;
                if (!value(child)) return false;
                v.a_.push_back(child);
                ws();
                if (i < s.size() && s[i] == ',') { i++; ws(); if (i < s.size() && s[i] == ']') { i++; return true; } continue; }
                if (i < s.size() && s[i] == ']') { i++; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { std::string t; if (!str(t)) return false; v = Json(t); return true; }
        if (s.compare(i, 4, "true") == 0) { i += 4; v = Json(true); return true; }
        if (s.compare(i, 5, "false") == 0) { i += 5; v = Json(false); return true; }
        if (s.compare(i, 4, "null") == 0) { i += 4; v = Json(); return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            char* end = nullptr;
            double d = strtod(s.c_str() + i, &end);
            i = (size_t)(end - s.c_str());
            v = Json(d);
            return true;
        }
        return fail("unexpected character");
    }
};

bool Json::parse(const std::string& text, Json& out, std::string* error) {
    JsonParser p{text};
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        p.i = 3;   // UTF-8 BOM
    Json v;
    bool ok = p.value(v);
    if (ok) { out = v; return true; }
    if (error) *error = p.err;
    return false;
}
