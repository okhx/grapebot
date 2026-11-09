#pragma once

#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace grape::rift {

class Value;

using Array = std::vector<Value>;
using Dict = std::map<std::string, Value>;

enum class Type {
    Null,
    Bool,
    Int,
    Float,
    String,
    Array,
    Dict,
};

class Value {
   public:
    Value() = default;
    Value(bool value) : m_type(Type::Bool), m_bool(value) {}
    Value(int value) : m_type(Type::Int), m_int(value) {}
    Value(int64_t value) : m_type(Type::Int), m_int(value) {}
    Value(uint64_t value)
        : m_type(Type::Int), m_int(static_cast<int64_t>(value)) {}
    Value(unsigned value)
        : m_type(Type::Int), m_int(static_cast<int64_t>(value)) {}
    Value(double value) : m_type(Type::Float), m_float(value) {}
    Value(float value)
        : m_type(Type::Float), m_float(static_cast<double>(value)) {}
    Value(std::string value)
        : m_type(Type::String), m_string(std::move(value)) {}
    Value(const char* value) : m_type(Type::String), m_string(value) {}
    Value(Array value)
        : m_type(Type::Array),
          m_array(std::make_shared<Array>(std::move(value))) {}
    Value(Dict value)
        : m_type(Type::Dict),
          m_dict(std::make_shared<Dict>(std::move(value))) {}

    Type type() const { return m_type; }
    bool isNull() const { return m_type == Type::Null; }
    bool isNumber() const {
        return m_type == Type::Int || m_type == Type::Float ||
               m_type == Type::Bool;
    }
    bool isString() const { return m_type == Type::String; }
    bool isArray() const { return m_type == Type::Array; }
    bool isDict() const { return m_type == Type::Dict; }

    bool asBool() const {
        switch (m_type) {
            case Type::Null: return false;
            case Type::Bool: return m_bool;
            case Type::Int: return m_int != 0;
            case Type::Float: return m_float != 0.0;
            case Type::String: return !m_string.empty();
            case Type::Array: return m_array && !m_array->empty();
            case Type::Dict: return m_dict && !m_dict->empty();
        }
        return false;
    }

    double asNumber() const {
        switch (m_type) {
            case Type::Bool: return m_bool ? 1.0 : 0.0;
            case Type::Int: return static_cast<double>(m_int);
            case Type::Float: return m_float;
            default: return 0.0;
        }
    }

    int64_t asInt() const {
        switch (m_type) {
            case Type::Bool: return m_bool ? 1 : 0;
            case Type::Int: return m_int;
            case Type::Float:
                return std::isfinite(m_float)
                           ? static_cast<int64_t>(m_float)
                           : 0;
            default: return 0;
        }
    }

    const std::string& stringRef() const { return m_string; }
    const Array& arrayRef() const {
        static const Array empty;
        return m_array ? *m_array : empty;
    }
    const Dict& dictRef() const {
        static const Dict empty;
        return m_dict ? *m_dict : empty;
    }

    std::string toString() const;

    bool equals(const Value& other) const;

   private:
    Type m_type = Type::Null;
    bool m_bool = false;
    int64_t m_int = 0;
    double m_float = 0.0;
    std::string m_string;
    std::shared_ptr<Array> m_array;
    std::shared_ptr<Dict> m_dict;
};

inline std::string formatNumber(double value) {
    if (!std::isfinite(value)) return std::isnan(value) ? "nan" : "inf";
    if (value == static_cast<double>(static_cast<int64_t>(value)) &&
        std::fabs(value) < 1e15) {
        return std::to_string(static_cast<int64_t>(value));
    }
    std::string out = std::to_string(value);
    const size_t dot = out.find('.');
    if (dot == std::string::npos) return out;
    size_t last = out.find_last_not_of('0');
    if (last == dot) last = dot - 1;
    return out.substr(0, last + 1);
}

inline std::string Value::toString() const {
    switch (m_type) {
        case Type::Null: return "null";
        case Type::Bool: return m_bool ? "true" : "false";
        case Type::Int: return std::to_string(m_int);
        case Type::Float: return formatNumber(m_float);
        case Type::String: return m_string;
        case Type::Array: {
            std::string out = "[";
            const Array& items = arrayRef();
            for (size_t i = 0; i < items.size(); ++i) {
                if (i) out += ", ";
                out += items[i].toString();
            }
            return out + "]";
        }
        case Type::Dict: {
            std::string out = "{";
            bool first = true;
            for (const auto& [key, item] : dictRef()) {
                if (!first) out += ", ";
                first = false;
                out += key + ": " + item.toString();
            }
            return out + "}";
        }
    }
    return {};
}

inline bool Value::equals(const Value& other) const {
    if (isString() && other.isString()) return m_string == other.m_string;
    if (isNumber() && other.isNumber()) return asNumber() == other.asNumber();
    if (isNull() || other.isNull()) return isNull() && other.isNull();
    if (isArray() && other.isArray()) {
        const Array& a = arrayRef();
        const Array& b = other.arrayRef();
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (!a[i].equals(b[i])) return false;
        return true;
    }
    if (isDict() && other.isDict()) {
        const Dict& a = dictRef();
        const Dict& b = other.dictRef();
        if (a.size() != b.size()) return false;
        for (const auto& [key, item] : a) {
            const auto found = b.find(key);
            if (found == b.end() || !item.equals(found->second)) return false;
        }
        return true;
    }
    return toString() == other.toString();
}

}
