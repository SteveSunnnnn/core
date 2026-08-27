#include "core/scripting/CoreScriptParser.hpp"
#include <charconv>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>

namespace core {

const ScriptNode* ScriptNode::find(SymbolId wanted) const noexcept {
    for (const auto& child : children) if (child.key == wanted) return &child;
    return nullptr;
}

namespace {

enum class TokenKind : std::uint8_t { End, Word, Number, String, LBrace, RBrace, Equals, Invalid };

struct Token {
    TokenKind kind = TokenKind::End;
    std::string_view text;
    double number = 0.0;
    std::uint32_t line = 1;
    std::uint32_t column = 1;
};

class Lexer {
public:
    explicit Lexer(std::string_view source) : source_(source) {}

    Token next() {
        skip_space_and_comments();
        if (pos_ >= source_.size()) return {TokenKind::End, {}, 0.0, line_, column_};
        const auto line = line_;
        const auto column = column_;
        const char c = source_[pos_];
        if (c == '{') { advance(); return {TokenKind::LBrace, "{", 0.0, line, column}; }
        if (c == '}') { advance(); return {TokenKind::RBrace, "}", 0.0, line, column}; }
        if (c == '=') { advance(); return {TokenKind::Equals, "=", 0.0, line, column}; }
        if (c == '"') return string_token();
        if ((c >= '0' && c <= '9') || c == '-' || c == '+') {
            return number_or_word();
        }
        if (is_word_char(c)) return word_token();
        advance();
        return {TokenKind::Invalid, source_.substr(pos_ - 1u, 1u), 0.0, line, column};
    }

private:
    static bool is_word_char(char c) noexcept {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == ':' || c == '.' || c == '-' || c == '/';
    }

    void advance() {
        if (source_[pos_] == '\n') { ++line_; column_ = 1; } else { ++column_; }
        ++pos_;
    }

    void skip_space_and_comments() {
        for (;;) {
            while (pos_ < source_.size()) {
                const char c = source_[pos_];
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n') advance(); else break;
            }
            if (pos_ >= source_.size()) return;
            if (source_[pos_] == '#') {
                while (pos_ < source_.size() && source_[pos_] != '\n') advance();
                continue;
            }
            if (source_[pos_] == '/' && pos_ + 1u < source_.size() && source_[pos_ + 1u] == '/') {
                advance(); advance();
                while (pos_ < source_.size() && source_[pos_] != '\n') advance();
                continue;
            }
            return;
        }
    }

    Token word_token() {
        const auto start = pos_;
        const auto line = line_;
        const auto column = column_;
        while (pos_ < source_.size() && is_word_char(source_[pos_])) advance();
        return {TokenKind::Word, source_.substr(start, pos_ - start), 0.0, line, column};
    }

    Token number_or_word() {
        const auto start = pos_;
        const auto line = line_;
        const auto column = column_;
        while (pos_ < source_.size() && is_word_char(source_[pos_])) advance();
        const auto text = source_.substr(start, pos_ - start);
        // Dotted dates and identifiers beginning with digits are words, not numeric constants.
        if (text.find_first_of("_:/") != std::string_view::npos ||
            (text.find('.') != std::string_view::npos && text.find('.', text.find('.') + 1u) != std::string_view::npos)) {
            return {TokenKind::Word, text, 0.0, line, column};
        }
        std::string owned{text};
        char* end = nullptr;
        errno = 0;
        const double value = std::strtod(owned.c_str(), &end);
        if (end != owned.c_str() + static_cast<std::ptrdiff_t>(owned.size())) {
            return {TokenKind::Word, text, 0.0, line, column};
        }
        if (errno == ERANGE || !std::isfinite(value))
            return {TokenKind::Invalid, text, 0.0, line, column};
        return {TokenKind::Number, text, value, line, column};
    }

    Token string_token() {
        const auto line = line_;
        const auto column = column_;
        advance();
        const auto start = pos_;
        while (pos_ < source_.size() && source_[pos_] != '"') {
            if (source_[pos_] == '\n') return {TokenKind::Invalid, source_.substr(start, pos_ - start), 0.0, line, column};
            advance();
        }
        if (pos_ >= source_.size()) return {TokenKind::Invalid, source_.substr(start), 0.0, line, column};
        const auto text = source_.substr(start, pos_ - start);
        advance();
        return {TokenKind::String, text, 0.0, line, column};
    }

    std::string_view source_;
    std::size_t pos_ = 0;
    std::uint32_t line_ = 1;
    std::uint32_t column_ = 1;
};

class ParserImpl {
public:
    ParserImpl(SymbolTable& symbols, std::string_view source, std::string_view source_name)
        : symbols_(symbols), lexer_(source), source_name_(source_name) { advance(); }

    ScriptParseResult run() {
        while (current_.kind != TokenKind::End) {
            if (current_.kind != TokenKind::Word) {
                error("expected top-level object type");
                recover_top_level();
                continue;
            }
            ScriptObject object;
            object.type = symbols_.intern(current_.text);
            object.line = current_.line;
            advance();
            if (current_.kind != TokenKind::Word && current_.kind != TokenKind::String) {
                error("expected top-level object name");
                recover_top_level();
                continue;
            }
            object.name = symbols_.intern(current_.text);
            advance();
            if (!accept(TokenKind::LBrace)) {
                error("expected '{' after top-level object name");
                recover_top_level();
                continue;
            }
            object.fields = parse_block(1u);
            result_.objects.push_back(std::move(object));
            if (node_count_ >= max_ast_nodes) break;
        }
        return std::move(result_);
    }

private:
    void advance() { current_ = lexer_.next(); }
    bool accept(TokenKind kind) {
        if (current_.kind != kind) return false;
        advance();
        return true;
    }

    void skip_block_contents() {
        std::size_t depth = 0u;
        while (current_.kind != TokenKind::End) {
            if (current_.kind == TokenKind::LBrace) {
                ++depth;
            } else if (current_.kind == TokenKind::RBrace) {
                if (depth == 0u) {
                    advance();
                    return;
                }
                --depth;
            }
            advance();
        }
    }

    std::vector<ScriptNode> parse_block(std::size_t depth) {
        std::vector<ScriptNode> nodes;
        nodes.reserve(8u);
        if (depth > max_ast_depth) {
            error("maximum CoreScript block depth exceeded");
            skip_block_contents();
            return nodes;
        }
        while (current_.kind != TokenKind::End && current_.kind != TokenKind::RBrace) {
            if (current_.kind != TokenKind::Word) {
                error("expected property name");
                advance();
                continue;
            }
            ScriptNode node;
            ++node_count_;
            if (node_count_ > max_ast_nodes) {
                error("maximum CoreScript AST node count exceeded");
                skip_block_contents();
                return nodes;
            }
            node.key = symbols_.intern(current_.text);
            node.line = current_.line;
            node.column = current_.column;
            advance();
            if (!accept(TokenKind::Equals)) {
                error("expected '=' after property name");
                skip_to_property_boundary();
                continue;
            }
            if (current_.kind == TokenKind::LBrace) {
                node.kind = ScriptValueKind::Block;
                advance();
                node.children = parse_block(depth + 1u);
            } else if (current_.kind == TokenKind::Number) {
                node.kind = ScriptValueKind::Number;
                node.number = current_.number;
                advance();
            } else if (current_.kind == TokenKind::Word || current_.kind == TokenKind::String) {
                node.kind = ScriptValueKind::Symbol;
                node.symbol = symbols_.intern(current_.text);
                advance();
            } else {
                error("expected number, identifier, string or block value");
                advance();
                continue;
            }
            nodes.push_back(std::move(node));
        }
        if (current_.kind == TokenKind::RBrace) advance();
        else error("unterminated block");
        return nodes;
    }

    void skip_to_property_boundary() {
        while (current_.kind != TokenKind::End && current_.kind != TokenKind::RBrace && current_.kind != TokenKind::Word) advance();
    }

    void recover_top_level() {
        int depth = 0;
        while (current_.kind != TokenKind::End) {
            if (current_.kind == TokenKind::LBrace) ++depth;
            else if (current_.kind == TokenKind::RBrace) { if (depth == 0) { advance(); return; } --depth; }
            advance();
            if (depth == 0 && current_.kind == TokenKind::Word) return;
        }
    }

    void error(std::string message) {
        if (!source_name_.empty()) message = std::string{source_name_} + ": " + message;
        result_.diagnostics.push_back({std::move(message), current_.line, current_.column});
    }

    SymbolTable& symbols_;
    static constexpr std::size_t max_ast_depth = 128u;
    static constexpr std::size_t max_ast_nodes = 1'000'000u;
    Lexer lexer_;
    std::string_view source_name_;
    Token current_;
    ScriptParseResult result_;
    std::size_t node_count_ = 0u;
};

} // namespace

ScriptParseResult CoreScriptParser::parse(std::string_view source, std::string_view source_name) {
    return ParserImpl{symbols_, source, source_name}.run();
}

} // namespace core
