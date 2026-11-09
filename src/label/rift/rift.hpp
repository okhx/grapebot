#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "value.hpp"

namespace grape::rift {

struct Context {
    std::unordered_map<std::string, Value> m_variables;

    std::function<Value(const std::string&)> m_config;

    mutable uint64_t m_randomState = 0x9E3779B97F4A7C15ull;

    const Value* find(const std::string& name) const {
        const auto it = m_variables.find(name);
        return it == m_variables.end() ? nullptr : &it->second;
    }

    void set(std::string name, Value value) {
        m_variables[std::move(name)] = std::move(value);
    }

    double random() const {
        uint64_t x = m_randomState;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        m_randomState = x;
        return static_cast<double>((x * 0x2545F4914F6CDD1Dull) >> 11) /
               static_cast<double>(1ull << 53);
    }
};

namespace detail {

enum class Tok {
    End, Number, String, Ident,
    Plus, Minus, Star, Slash, Percent,
    Less, Greater, LessEq, GreaterEq, EqEq, NotEq,
    AndAnd, OrOr, Not,
    Question, Colon, Comma, Dot,
    LParen, RParen, LBracket, RBracket,
};

struct Token {
    Tok m_kind = Tok::End;
    std::string m_text;
    double m_number = 0.0;
    bool m_isInteger = false;
    size_t m_position = 0;
};

class Lexer {
   public:
    Lexer(std::string_view source) : m_source(source) {}

    bool run(std::vector<Token>& out, std::string& error) {
        while (true) {
            skipSpace();
            if (m_index >= m_source.size()) {
                out.push_back({Tok::End, {}, 0.0, false, m_index});
                return true;
            }
            const size_t start = m_index;
            const char c = m_source[m_index];

            if (std::isdigit(static_cast<unsigned char>(c)) ||
                (c == '.' && m_index + 1 < m_source.size() &&
                 std::isdigit(
                     static_cast<unsigned char>(m_source[m_index + 1])))) {
                out.push_back(number(start));
                continue;
            }
            if (c == '_' || std::isalpha(static_cast<unsigned char>(c))) {
                while (m_index < m_source.size() &&
                       (m_source[m_index] == '_' ||
                        std::isalnum(
                            static_cast<unsigned char>(m_source[m_index]))))
                    ++m_index;
                out.push_back({Tok::Ident,
                               std::string(m_source.substr(start,
                                                           m_index - start)),
                               0.0, false, start});
                continue;
            }
            if (c == '"' || c == '\'') {
                Token token;
                if (!string(c, token, error)) return false;
                out.push_back(token);
                continue;
            }

            const auto two = m_source.substr(m_index, 2);
            const auto push = [&](Tok kind, size_t length) {
                m_index += length;
                out.push_back({kind, std::string(m_source.substr(start, length)),
                               0.0, false, start});
            };
            if (two == "<=") { push(Tok::LessEq, 2); continue; }
            if (two == ">=") { push(Tok::GreaterEq, 2); continue; }
            if (two == "==") { push(Tok::EqEq, 2); continue; }
            if (two == "!=") { push(Tok::NotEq, 2); continue; }
            if (two == "&&") { push(Tok::AndAnd, 2); continue; }
            if (two == "||") { push(Tok::OrOr, 2); continue; }

            switch (c) {
                case '+': push(Tok::Plus, 1); continue;
                case '-': push(Tok::Minus, 1); continue;
                case '*': push(Tok::Star, 1); continue;
                case '/': push(Tok::Slash, 1); continue;
                case '%': push(Tok::Percent, 1); continue;
                case '<': push(Tok::Less, 1); continue;
                case '>': push(Tok::Greater, 1); continue;
                case '!': push(Tok::Not, 1); continue;
                case '?': push(Tok::Question, 1); continue;
                case ':': push(Tok::Colon, 1); continue;
                case ',': push(Tok::Comma, 1); continue;
                case '.': push(Tok::Dot, 1); continue;
                case '(': push(Tok::LParen, 1); continue;
                case ')': push(Tok::RParen, 1); continue;
                case '[': push(Tok::LBracket, 1); continue;
                case ']': push(Tok::RBracket, 1); continue;
                default: break;
            }
            error = std::string("unexpected character '") + c + "'";
            return false;
        }
    }

   private:
    void skipSpace() {
        while (m_index < m_source.size() &&
               std::isspace(static_cast<unsigned char>(m_source[m_index])))
            ++m_index;
    }

    Token number(size_t start) {
        bool isInteger = true;
        while (m_index < m_source.size() &&
               std::isdigit(static_cast<unsigned char>(m_source[m_index])))
            ++m_index;
        if (m_index < m_source.size() && m_source[m_index] == '.') {
            isInteger = false;
            ++m_index;
            while (m_index < m_source.size() &&
                   std::isdigit(static_cast<unsigned char>(m_source[m_index])))
                ++m_index;
        }
        const std::string text(m_source.substr(start, m_index - start));
        return {Tok::Number, text, std::strtod(text.c_str(), nullptr),
                isInteger, start};
    }

    bool string(char quote, Token& out, std::string& error) {
        const size_t start = m_index;
        ++m_index;
        std::string text;
        while (m_index < m_source.size() && m_source[m_index] != quote) {
            char c = m_source[m_index];
            if (c == '\\' && m_index + 1 < m_source.size()) {
                ++m_index;
                switch (m_source[m_index]) {
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'r': c = '\r'; break;
                    default: c = m_source[m_index]; break;
                }
            }
            text.push_back(c);
            ++m_index;
        }
        if (m_index >= m_source.size()) {
            error = "unterminated string";
            return false;
        }
        ++m_index;
        out = {Tok::String, std::move(text), 0.0, false, start};
        return true;
    }

    std::string_view m_source;
    size_t m_index = 0;
};

struct Node;
using NodePtr = std::shared_ptr<Node>;

enum class NodeKind {
    Literal, Variable, Unary, Binary, Ternary, Index, Member, Call,
};

struct Node {
    NodeKind m_kind = NodeKind::Literal;
    Value m_literal;
    std::string m_name;
    Tok m_op = Tok::End;
    std::vector<NodePtr> m_children;
};

}

class Script {
   public:
    bool valid() const { return m_valid; }
    const std::string& error() const { return m_error; }

    std::string run(const Context& context) const;

    const std::vector<std::string>& referenced() const { return m_referenced; }

    static Script compile(std::string_view source);

   private:
    struct Chunk {
        std::string m_text;
        detail::NodePtr m_expression;
    };

    std::vector<Chunk> m_chunks;
    std::vector<std::string> m_referenced;
    bool m_valid = true;
    std::string m_error;

    friend class Parser;
};

namespace detail {

class Parser {
   public:
    Parser(std::vector<Token> tokens) : m_tokens(std::move(tokens)) {}

    NodePtr parse(std::string& error) {
        NodePtr node = ternary(error);
        if (!node) return nullptr;
        if (peek().m_kind != Tok::End) {
            error = "unexpected '" + peek().m_text + "'";
            return nullptr;
        }
        return node;
    }

   private:
    const Token& peek() const { return m_tokens[m_index]; }
    const Token& advance() { return m_tokens[m_index++]; }
    bool match(Tok kind) {
        if (peek().m_kind != kind) return false;
        ++m_index;
        return true;
    }

    static NodePtr make(NodeKind kind) {
        auto node = std::make_shared<Node>();
        node->m_kind = kind;
        return node;
    }

    NodePtr binary(Tok op, NodePtr lhs, NodePtr rhs) {
        auto node = make(NodeKind::Binary);
        node->m_op = op;
        node->m_children = {std::move(lhs), std::move(rhs)};
        return node;
    }

    NodePtr ternary(std::string& error) {
        NodePtr condition = logicalOr(error);
        if (!condition) return nullptr;
        if (!match(Tok::Question)) return condition;

        NodePtr whenTrue = ternary(error);
        if (!whenTrue) return nullptr;
        if (!match(Tok::Colon)) {
            error = "expected ':' in conditional";
            return nullptr;
        }
        NodePtr whenFalse = ternary(error);
        if (!whenFalse) return nullptr;

        auto node = make(NodeKind::Ternary);
        node->m_children = {std::move(condition), std::move(whenTrue),
                            std::move(whenFalse)};
        return node;
    }

    NodePtr logicalOr(std::string& error) {
        NodePtr lhs = logicalAnd(error);
        while (lhs && peek().m_kind == Tok::OrOr) {
            advance();
            NodePtr rhs = logicalAnd(error);
            if (!rhs) return nullptr;
            lhs = binary(Tok::OrOr, lhs, rhs);
        }
        return lhs;
    }

    NodePtr logicalAnd(std::string& error) {
        NodePtr lhs = equality(error);
        while (lhs && peek().m_kind == Tok::AndAnd) {
            advance();
            NodePtr rhs = equality(error);
            if (!rhs) return nullptr;
            lhs = binary(Tok::AndAnd, lhs, rhs);
        }
        return lhs;
    }

    NodePtr equality(std::string& error) {
        NodePtr lhs = comparison(error);
        while (lhs && (peek().m_kind == Tok::EqEq ||
                       peek().m_kind == Tok::NotEq)) {
            const Tok op = advance().m_kind;
            NodePtr rhs = comparison(error);
            if (!rhs) return nullptr;
            lhs = binary(op, lhs, rhs);
        }
        return lhs;
    }

    NodePtr comparison(std::string& error) {
        NodePtr lhs = additive(error);
        while (lhs && (peek().m_kind == Tok::Less ||
                       peek().m_kind == Tok::Greater ||
                       peek().m_kind == Tok::LessEq ||
                       peek().m_kind == Tok::GreaterEq)) {
            const Tok op = advance().m_kind;
            NodePtr rhs = additive(error);
            if (!rhs) return nullptr;
            lhs = binary(op, lhs, rhs);
        }
        return lhs;
    }

    NodePtr additive(std::string& error) {
        NodePtr lhs = multiplicative(error);
        while (lhs && (peek().m_kind == Tok::Plus ||
                       peek().m_kind == Tok::Minus)) {
            const Tok op = advance().m_kind;
            NodePtr rhs = multiplicative(error);
            if (!rhs) return nullptr;
            lhs = binary(op, lhs, rhs);
        }
        return lhs;
    }

    NodePtr multiplicative(std::string& error) {
        NodePtr lhs = unary(error);
        while (lhs && (peek().m_kind == Tok::Star ||
                       peek().m_kind == Tok::Slash ||
                       peek().m_kind == Tok::Percent)) {
            const Tok op = advance().m_kind;
            NodePtr rhs = unary(error);
            if (!rhs) return nullptr;
            lhs = binary(op, lhs, rhs);
        }
        return lhs;
    }

    NodePtr unary(std::string& error) {
        if (peek().m_kind == Tok::Minus || peek().m_kind == Tok::Not) {
            const Tok op = advance().m_kind;
            NodePtr operand = unary(error);
            if (!operand) return nullptr;
            auto node = make(NodeKind::Unary);
            node->m_op = op;
            node->m_children = {std::move(operand)};
            return node;
        }
        return postfix(error);
    }

    NodePtr postfix(std::string& error) {
        NodePtr node = primary(error);
        while (node) {
            if (match(Tok::LBracket)) {
                NodePtr index = ternary(error);
                if (!index) return nullptr;
                if (!match(Tok::RBracket)) {
                    error = "expected ']'";
                    return nullptr;
                }
                auto access = make(NodeKind::Index);
                access->m_children = {node, index};
                node = access;
                continue;
            }
            if (peek().m_kind == Tok::Dot) {
                advance();
                if (peek().m_kind != Tok::Ident) {
                    error = "expected a name after '.'";
                    return nullptr;
                }
                auto access = make(NodeKind::Member);
                access->m_name = advance().m_text;
                access->m_children = {node};
                node = access;
                continue;
            }
            if (node->m_kind == NodeKind::Variable &&
                peek().m_kind == Tok::LParen) {
                advance();
                auto call = make(NodeKind::Call);
                call->m_name = node->m_name;
                if (!match(Tok::RParen)) {
                    while (true) {
                        NodePtr argument = ternary(error);
                        if (!argument) return nullptr;
                        call->m_children.push_back(argument);
                        if (match(Tok::Comma)) continue;
                        if (match(Tok::RParen)) break;
                        error = "expected ',' or ')' in arguments";
                        return nullptr;
                    }
                }
                node = call;
                continue;
            }
            break;
        }
        return node;
    }

    NodePtr primary(std::string& error) {
        const Token& token = peek();
        switch (token.m_kind) {
            case Tok::Number: {
                advance();
                auto node = make(NodeKind::Literal);
                node->m_literal = token.m_isInteger
                    ? Value(static_cast<int64_t>(token.m_number))
                    : Value(token.m_number);
                return node;
            }
            case Tok::String: {
                advance();
                auto node = make(NodeKind::Literal);
                node->m_literal = Value(token.m_text);
                return node;
            }
            case Tok::Ident: {
                advance();
                if (token.m_text == "true" || token.m_text == "false") {
                    auto node = make(NodeKind::Literal);
                    node->m_literal = Value(token.m_text == "true");
                    return node;
                }
                if (token.m_text == "null") return make(NodeKind::Literal);
                auto node = make(NodeKind::Variable);
                node->m_name = token.m_text;
                return node;
            }
            case Tok::LParen: {
                advance();
                NodePtr inner = ternary(error);
                if (!inner) return nullptr;
                if (!match(Tok::RParen)) {
                    error = "expected ')'";
                    return nullptr;
                }
                return inner;
            }
            default:
                error = token.m_kind == Tok::End
                            ? "unexpected end of expression"
                            : "unexpected '" + token.m_text + "'";
                return nullptr;
        }
    }

    std::vector<Token> m_tokens;
    size_t m_index = 0;
};

inline Value callFunction(const std::string& name,
                          const std::vector<Value>& args,
                          const Context& context, bool& known);

inline Value evaluate(const NodePtr& node, const Context& context);

inline std::string padTo(const std::string& text, size_t width, char fill,
                         int side) {
    if (text.size() >= width) return text;
    const size_t missing = width - text.size();
    if (side < 0) return std::string(missing, fill) + text;
    if (side > 0) return text + std::string(missing, fill);
    const size_t left = missing / 2;
    return std::string(left, fill) + text +
           std::string(missing - left, fill);
}

inline char fillChar(const std::vector<Value>& args, size_t index) {
    if (args.size() <= index) return ' ';
    const std::string text = args[index].toString();
    return text.empty() ? ' ' : text[0];
}

inline Value add(const Value& lhs, const Value& rhs) {
    if (lhs.isString() || rhs.isString())
        return Value(lhs.toString() + rhs.toString());
    if (lhs.type() == Type::Int && rhs.type() == Type::Int)
        return Value(lhs.asInt() + rhs.asInt());
    return Value(lhs.asNumber() + rhs.asNumber());
}

inline Value multiply(const Value& lhs, const Value& rhs) {
    if (lhs.isString() && rhs.isNumber()) {
        const int64_t times = rhs.asInt();
        if (times <= 0) return Value(std::string{});
        std::string out;
        const int64_t limit = std::min<int64_t>(times, 4096);
        out.reserve(lhs.stringRef().size() * static_cast<size_t>(limit));
        for (int64_t i = 0; i < limit; ++i) out += lhs.stringRef();
        return Value(out);
    }
    if (rhs.isString() && lhs.isNumber()) return multiply(rhs, lhs);
    if (lhs.type() == Type::Int && rhs.type() == Type::Int)
        return Value(lhs.asInt() * rhs.asInt());
    return Value(lhs.asNumber() * rhs.asNumber());
}

inline int compare(const Value& lhs, const Value& rhs) {
    if (lhs.isString() && rhs.isString()) {
        const int result = lhs.stringRef().compare(rhs.stringRef());
        return result < 0 ? -1 : result > 0 ? 1 : 0;
    }
    const double a = lhs.asNumber();
    const double b = rhs.asNumber();
    return a < b ? -1 : a > b ? 1 : 0;
}

inline Value evaluateBinary(Tok op, const NodePtr& lhsNode,
                            const NodePtr& rhsNode, const Context& context) {
    if (op == Tok::AndAnd) {
        const Value lhs = evaluate(lhsNode, context);
        if (!lhs.asBool()) return Value(false);
        return Value(evaluate(rhsNode, context).asBool());
    }
    if (op == Tok::OrOr) {
        const Value lhs = evaluate(lhsNode, context);
        if (lhs.asBool()) return Value(true);
        return Value(evaluate(rhsNode, context).asBool());
    }

    const Value lhs = evaluate(lhsNode, context);
    const Value rhs = evaluate(rhsNode, context);
    switch (op) {
        case Tok::Plus: return add(lhs, rhs);
        case Tok::Minus:
            if (lhs.type() == Type::Int && rhs.type() == Type::Int)
                return Value(lhs.asInt() - rhs.asInt());
            return Value(lhs.asNumber() - rhs.asNumber());
        case Tok::Star: return multiply(lhs, rhs);
        case Tok::Slash: {
            const double divisor = rhs.asNumber();
            if (divisor == 0.0) return Value(0);
            return Value(lhs.asNumber() / divisor);
        }
        case Tok::Percent: {
            const int64_t divisor = rhs.asInt();
            if (divisor == 0) return Value(0);
            if (lhs.type() == Type::Int && rhs.type() == Type::Int)
                return Value(lhs.asInt() % divisor);
            return Value(std::fmod(lhs.asNumber(), rhs.asNumber()));
        }
        case Tok::EqEq: return Value(lhs.equals(rhs));
        case Tok::NotEq: return Value(!lhs.equals(rhs));
        case Tok::Less: return Value(compare(lhs, rhs) < 0);
        case Tok::Greater: return Value(compare(lhs, rhs) > 0);
        case Tok::LessEq: return Value(compare(lhs, rhs) <= 0);
        case Tok::GreaterEq: return Value(compare(lhs, rhs) >= 0);
        default: return {};
    }
}

inline Value evaluate(const NodePtr& node, const Context& context) {
    if (!node) return {};
    switch (node->m_kind) {
        case NodeKind::Literal:
            return node->m_literal;
        case NodeKind::Variable: {
            const Value* found = context.find(node->m_name);
            return found ? *found : Value{};
        }
        case NodeKind::Unary: {
            const Value operand = evaluate(node->m_children[0], context);
            if (node->m_op == Tok::Not) return Value(!operand.asBool());
            if (operand.type() == Type::Int) return Value(-operand.asInt());
            return Value(-operand.asNumber());
        }
        case NodeKind::Binary:
            return evaluateBinary(node->m_op, node->m_children[0],
                                  node->m_children[1], context);
        case NodeKind::Ternary:
            return evaluate(node->m_children[0], context).asBool()
                       ? evaluate(node->m_children[1], context)
                       : evaluate(node->m_children[2], context);
        case NodeKind::Index: {
            const Value target = evaluate(node->m_children[0], context);
            const Value key = evaluate(node->m_children[1], context);
            if (target.isArray()) {
                const Array& items = target.arrayRef();
                const int64_t index = key.asInt();
                if (index < 0 || index >= static_cast<int64_t>(items.size()))
                    return {};
                return items[static_cast<size_t>(index)];
            }
            if (target.isDict()) {
                const Dict& items = target.dictRef();
                const auto found = items.find(key.toString());
                return found == items.end() ? Value{} : found->second;
            }
            if (target.isString()) {
                const std::string& text = target.stringRef();
                const int64_t index = key.asInt();
                if (index < 0 || index >= static_cast<int64_t>(text.size()))
                    return {};
                return Value(std::string(1, text[static_cast<size_t>(index)]));
            }
            return {};
        }
        case NodeKind::Member: {
            const Value target = evaluate(node->m_children[0], context);
            if (!target.isDict()) return {};
            const Dict& items = target.dictRef();
            const auto found = items.find(node->m_name);
            return found == items.end() ? Value{} : found->second;
        }
        case NodeKind::Call: {
            std::vector<Value> args;
            args.reserve(node->m_children.size());
            for (const auto& child : node->m_children)
                args.push_back(evaluate(child, context));
            bool known = true;
            Value result = callFunction(node->m_name, args, context, known);
            return known ? result : Value{};
        }
    }
    return {};
}

inline Value callFunction(const std::string& name,
                          const std::vector<Value>& args,
                          const Context& context, bool& known) {
    known = true;
    const auto arg = [&](size_t index) -> Value {
        return index < args.size() ? args[index] : Value{};
    };
    const auto number = [&](size_t index) { return arg(index).asNumber(); };

    if (name == "len") {
        const Value value = arg(0);
        if (value.isArray())
            return Value(static_cast<int64_t>(value.arrayRef().size()));
        if (value.isDict())
            return Value(static_cast<int64_t>(value.dictRef().size()));
        return Value(static_cast<int64_t>(value.toString().size()));
    }
    if (name == "substr") {
        const std::string text = arg(0).toString();
        const int64_t size = static_cast<int64_t>(text.size());
        int64_t from = args.size() > 1 ? arg(1).asInt() : 0;
        int64_t to = args.size() > 2 ? arg(2).asInt() : size;
        from = std::clamp<int64_t>(from, 0, size);
        to = std::clamp<int64_t>(to, from, size);
        return Value(text.substr(static_cast<size_t>(from),
                                 static_cast<size_t>(to - from)));
    }
    if (name == "toUpper" || name == "toLower") {
        std::string text = arg(0).toString();
        const bool upper = name == "toUpper";
        for (char& c : text)
            c = static_cast<char>(
                upper ? std::toupper(static_cast<unsigned char>(c))
                      : std::tolower(static_cast<unsigned char>(c)));
        return Value(text);
    }
    if (name == "trim") {
        std::string text = arg(0).toString();
        const auto space = [](unsigned char c) { return std::isspace(c) != 0; };
        size_t first = 0;
        while (first < text.size() &&
               space(static_cast<unsigned char>(text[first])))
            ++first;
        size_t last = text.size();
        while (last > first &&
               space(static_cast<unsigned char>(text[last - 1])))
            --last;
        return Value(text.substr(first, last - first));
    }
    if (name == "replace") {
        std::string text = arg(0).toString();
        const std::string from = arg(1).toString();
        const std::string to = arg(2).toString();
        if (from.empty()) return Value(text);
        std::string out;
        size_t at = 0;
        while (true) {
            const size_t found = text.find(from, at);
            if (found == std::string::npos) break;
            out += text.substr(at, found - at) + to;
            at = found + from.size();
        }
        out += text.substr(at);
        return Value(out);
    }
    if (name == "leftPad" || name == "rightPad" || name == "middlePad") {
        const std::string text = arg(0).toString();
        const int64_t width = arg(1).asInt();
        if (width <= 0) return Value(text);
        const int side = name == "leftPad" ? -1 : name == "rightPad" ? 1 : 0;
        return Value(padTo(text, static_cast<size_t>(std::min<int64_t>(
                                     width, 4096)),
                           fillChar(args, 2), side));
    }

    if (name == "random") {
        const double low = args.empty() ? 0.0 : number(0);
        const double high = args.size() > 1 ? number(1) : 1.0;
        return Value(low + context.random() * (high - low));
    }
    if (name == "round") return Value(static_cast<int64_t>(std::llround(number(0))));
    if (name == "floor") return Value(static_cast<int64_t>(std::floor(number(0))));
    if (name == "ceil") return Value(static_cast<int64_t>(std::ceil(number(0))));
    if (name == "abs") {
        const Value value = arg(0);
        if (value.type() == Type::Int) return Value(std::abs(value.asInt()));
        return Value(std::fabs(value.asNumber()));
    }
    if (name == "sqrt") {
        const double value = number(0);
        return Value(value < 0.0 ? 0.0 : std::sqrt(value));
    }
    if (name == "pow") return Value(std::pow(number(0), number(1)));
    if (name == "sin") return Value(std::sin(number(0)));
    if (name == "cos") return Value(std::cos(number(0)));
    if (name == "tan") return Value(std::tan(number(0)));
    if (name == "precision") {
        const double value = number(0);
        const int digits = static_cast<int>(
            std::clamp<int64_t>(args.size() > 1 ? arg(1).asInt() : 0, 0, 17));
        if (!std::isfinite(value)) return Value(formatNumber(value));
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
        return Value(std::string(buffer));
    }

    if (name == "min" || name == "max" || name == "sum" || name == "avg") {
        std::vector<double> numbers;
        const auto collect = [&numbers](const Value& value,
                                        auto&& self) -> void {
            if (value.isArray()) {
                for (const Value& item : value.arrayRef()) self(item, self);
                return;
            }
            numbers.push_back(value.asNumber());
        };
        for (const Value& value : args) collect(value, collect);
        if (numbers.empty()) return Value(0);
        if (name == "min")
            return Value(*std::min_element(numbers.begin(), numbers.end()));
        if (name == "max")
            return Value(*std::max_element(numbers.begin(), numbers.end()));
        double total = 0.0;
        for (double value : numbers) total += value;
        if (name == "sum") return Value(total);
        return Value(total / static_cast<double>(numbers.size()));
    }

    if (name == "cfg")
        return context.m_config ? context.m_config(arg(0).toString()) : Value{};

    known = false;
    return {};
}

inline void collectNames(const NodePtr& node, std::vector<std::string>& out) {
    if (!node) return;
    if (node->m_kind == NodeKind::Variable &&
        std::find(out.begin(), out.end(), node->m_name) == out.end())
        out.push_back(node->m_name);
    for (const auto& child : node->m_children) collectNames(child, out);
}

}

inline Script Script::compile(std::string_view source) {
    Script script;
    std::string literal;

    for (size_t i = 0; i < source.size();) {
        const char c = source[i];
        if (c == '\\' && i + 1 < source.size() &&
            (source[i + 1] == '{' || source[i + 1] == '}')) {
            literal.push_back(source[i + 1]);
            i += 2;
            continue;
        }
        if (c != '{') {
            literal.push_back(c);
            ++i;
            continue;
        }

        size_t depth = 1;
        size_t j = i + 1;
        char quote = 0;
        for (; j < source.size(); ++j) {
            const char d = source[j];
            if (quote) {
                if (d == '\\') ++j;
                else if (d == quote) quote = 0;
                continue;
            }
            if (d == '"' || d == '\'') { quote = d; continue; }
            if (d == '{') ++depth;
            else if (d == '}' && --depth == 0) break;
        }
        if (j >= source.size()) {
            script.m_valid = false;
            script.m_error = "unmatched '{'";
            return script;
        }

        if (!literal.empty()) {
            script.m_chunks.push_back({literal, nullptr});
            literal.clear();
        }

        const std::string_view body = source.substr(i + 1, j - i - 1);
        if (body.find_first_not_of(" \t\r\n") == std::string_view::npos) {
            i = j + 1;
            continue;
        }

        std::vector<detail::Token> tokens;
        std::string error;
        detail::Lexer lexer(body);
        if (!lexer.run(tokens, error)) {
            script.m_valid = false;
            script.m_error = error;
            return script;
        }
        detail::Parser parser(std::move(tokens));
        detail::NodePtr node = parser.parse(error);
        if (!node) {
            script.m_valid = false;
            script.m_error = error;
            return script;
        }
        detail::collectNames(node, script.m_referenced);
        script.m_chunks.push_back({{}, node});
        i = j + 1;
    }

    if (!literal.empty()) script.m_chunks.push_back({literal, nullptr});
    return script;
}

inline std::string Script::run(const Context& context) const {
    if (!m_valid) return m_error;
    std::string out;
    for (const Chunk& chunk : m_chunks) {
        if (!chunk.m_expression) {
            out += chunk.m_text;
            continue;
        }
        out += detail::evaluate(chunk.m_expression, context).toString();
    }
    return out;
}

}
