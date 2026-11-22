#pragma once
// Tiny JSON value with parser and pretty printer (UTF-8, no dependencies).
// Objects keep insertion order so the saved config stays readable.
#include <string>
#include <utility>
#include <vector>

class Json {
public:
    enum Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(bool b) : t_(Bool), b_(b) {}
    Json(int n) : t_(Number), n_(n) {}
    Json(double n) : t_(Number), n_(n) {}
    Json(const char* s) : t_(String), s_(s) {}
    Json(const std::string& s) : t_(String), s_(s) {}
    static Json Arr() { Json j; j.t_ = Array; return j; }
    static Json Obj() { Json j; j.t_ = Object; return j; }

    Type type() const { return t_; }
    bool isNull() const { return t_ == Null; }
    bool isObject() const { return t_ == Object; }
    bool isArray() const { return t_ == Array; }

    bool has(const std::string& key) const;
    const Json& operator[](const std::string& key) const;   // null value if missing
    Json& operator[](const std::string& key);               // creates the member (and the object)
    Json& push(const Json& v);

    size_t size() const { return t_ == Array ? a_.size() : t_ == Object ? o_.size() : 0; }
    const Json& at(size_t i) const { return a_[i]; }
    const std::vector<std::pair<std::string, Json>>& items() const { return o_; }

    bool asBool(bool def = false) const { return t_ == Bool ? b_ : def; }
    double asNum(double def = 0) const { return t_ == Number ? n_ : def; }
    int asInt(int def = 0) const { return t_ == Number ? (int)n_ : def; }
    std::string asStr(const std::string& def = "") const { return t_ == String ? s_ : def; }

    std::string dump(int indent = 2) const;
    static bool parse(const std::string& text, Json& out, std::string* error = nullptr);

private:
    void dumpTo(std::string& out, int indent, int level) const;
    Type t_ = Null;
    bool b_ = false;
    double n_ = 0;
    std::string s_;
    std::vector<Json> a_;
    std::vector<std::pair<std::string, Json>> o_;
    friend struct JsonParser;
};
