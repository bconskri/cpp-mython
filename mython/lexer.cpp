#include "lexer.h"

#include <algorithm>
#include <charconv>
#include <unordered_map>

using namespace std;

namespace parse {

    bool operator==(const Token &lhs, const Token &rhs) {
        using namespace token_type;

        if (lhs.index() != rhs.index()) {
            return false;
        }
        if (lhs.Is<Char>()) {
            return lhs.As<Char>().value == rhs.As<Char>().value;
        }
        if (lhs.Is<Number>()) {
            return lhs.As<Number>().value == rhs.As<Number>().value;
        }
        if (lhs.Is<String>()) {
            return lhs.As<String>().value == rhs.As<String>().value;
        }
        if (lhs.Is<Id>()) {
            return lhs.As<Id>().value == rhs.As<Id>().value;
        }
        return true;
    }

    bool operator!=(const Token &lhs, const Token &rhs) {
        return !(lhs == rhs);
    }

    std::ostream &operator<<(std::ostream &os, const Token &rhs) {
        using namespace token_type;

#define VALUED_OUTPUT(type) \
    if (auto p = rhs.TryAs<type>()) return os << #type << '{' << p->value << '}';

        VALUED_OUTPUT(Number);
        VALUED_OUTPUT(Id);
        VALUED_OUTPUT(String);
        VALUED_OUTPUT(Char);

#undef VALUED_OUTPUT

#define UNVALUED_OUTPUT(type) \
    if (rhs.Is<type>()) return os << #type;

        UNVALUED_OUTPUT(Class);
        UNVALUED_OUTPUT(Return);
        UNVALUED_OUTPUT(If);
        UNVALUED_OUTPUT(Else);
        UNVALUED_OUTPUT(Def);
        UNVALUED_OUTPUT(Newline);
        UNVALUED_OUTPUT(Print);
        UNVALUED_OUTPUT(Indent);
        UNVALUED_OUTPUT(Dedent);
        UNVALUED_OUTPUT(And);
        UNVALUED_OUTPUT(Or);
        UNVALUED_OUTPUT(Not);
        UNVALUED_OUTPUT(Eq);
        UNVALUED_OUTPUT(NotEq);
        UNVALUED_OUTPUT(LessOrEq);
        UNVALUED_OUTPUT(GreaterOrEq);
        UNVALUED_OUTPUT(None);
        UNVALUED_OUTPUT(True);
        UNVALUED_OUTPUT(False);
        UNVALUED_OUTPUT(Eof);

#undef UNVALUED_OUTPUT

        return os << "Unknown token :("sv;
    }

    Lexer::Lexer(std::istream &in) : in_(in) {
        indent_ = 0U;
        new_line_ = true;
        current_token_ = NextToken();
    }

    const Token &Lexer::CurrentToken() const {
        return current_token_;
    }

    Token Lexer::NextToken() {
        current_token_ = ReadToken();
        return CurrentToken();
    }

    size_t Lexer::SkipCurIndent() {
        char c, cn;
        size_t i = 0;
        for (; i < indent_; ++i) {
            c = in_.get();
            if (c == -1) {//eof return
                return i;
            }
            if (c == '\n') { //empty string found
                return SkipCurIndent(); //preserve indents and skip it on next line
            }
            cn = in_.get();
            if (c == ' ' && cn != ' ') {
                throw LexerError("Indent incorrect"s);
            } else if (c != ' ') {//Dedent found
                in_.putback(cn);
                in_.putback(c);
                return i;
            }
        }
        return i;
    }

    Token Lexer::ReadNumber(char c) {
        std::string num{c};
        char cn = in_.get();
        while (std::isdigit(cn) && cn != -1) {
            num.push_back(cn);
            cn = in_.get();
        }
        in_.putback(cn);
        return token_type::Number{std::stoi(num)};
    }

    Token Lexer::ReadString(char c) {
        std::string str;
        char cn = in_.get();
        while (cn != c && cn != -1) { //идем до закрывающей кавычки, но помним что она может быть спец символом
            if (cn == '\\') { //спец символ
                cn = in_.get();
                switch (cn) {
                    case 't':
                        str.push_back('\t');
                        break;
                    case 'n':
                        str.push_back('\n');
                        break;
                    default:
                        str.push_back(cn);
                }
            } else {
                str.push_back(cn);
            }
            cn = in_.get();
        }
        if (cn != c) {
            throw LexerError("String error. Pair quote not found"s);
        }
        if (str.size() > 0) {
            return token_type::String{str};
        }
        return token_type::None();
    }

    Token Lexer::ReadId(char c) {
        std::string str{c};
        char cn = in_.get();
        while (cn != '=' && cn != '.' && cn != ',' && cn != '(' && cn != '+' && cn != '<' && cn != ')' &&
               cn != '!' && cn != '>' && cn != ' ' && cn != ':' && cn != '#' && cn != '\n' && cn != -1) {
            str.push_back(cn);
            cn = in_.get();
        }
        in_.putback(cn);
        new_line_ = false;
        if (declared_words_.count(str) > 0) {
            return declared_words_.at(str);
        } else {
            return token_type::Id{str};
        }
    }

    Token Lexer::ReadToken() {
        //skip indent if needed
        if (new_line_ && indent_ > 0) {
            size_t indent_skipped = SkipCurIndent();
            if (indent_skipped < indent_) {//Dedent found
                --indent_;
                for (size_t i = 0; i < indent_skipped; ++i) {
                    //вернем назад отступы, так как на следующем вызове NextToken
                    // надо будет снова считать есть ли еще Dedent
                    in_.putback(' ');
                    in_.putback(' ');
                }
                return token_type::Dedent{};
            }
        }

        char c = in_.get();
        //first check EOF
        if (c == -1 || in_.eof()) {
            //if newline needed
            if (!new_line_) {
                new_line_ = true;
                return token_type::Newline{};
            }
            return token_type::Eof{};
        }

        if (c == '\n') {
            if (!new_line_) {
                new_line_ = true;
                return token_type::Newline{};
            } else {
                return NextToken();
            }
        }

        if (c == ' ' && new_line_) {
            if (in_.get() == ' ') {// Лексема «увеличение отступа», соответствует двум пробелам
                ++indent_;
                new_line_ = false;
                return token_type::Indent{};
            } else {
                throw LexerError("Indent incorrect"s);
            }
        }

        //skip inner spaces
        while (c == ' ') {
            c = in_.get();
        }

        //comments ignored up to the end of the line
        if (c == '#') {
            std::string comment;
            std::getline(in_, comment, '\n');
            in_.putback('\n'); //вернем назад конец строки чтобы был Newline на следующем шаге
            return ReadToken();
        }

        if (std::isdigit(c)) { //это число
            new_line_ = false;
            return ReadNumber(c);
        }

        //операторы сравнения, состоящие из нескольких символов
        if (c == '=' || c == '!' || c == '<' || c == '>') {
            char cn = in_.get();
            if (cn == '=') {
                new_line_ = false;
                return comp_operators_.at(std::string{c, '='});
            }
            in_.putback(cn);
        }

        //это символ
        if (c == '=' || c == '+' || c == '-' || c == '*' || c == '/' || c == '>' || c == '<' || c == '.' || c == ',' ||
            c == '(' || c == ')' || c == ':') {
            new_line_ = false;
            return token_type::Char{c};
        }

        if (c == '\'' || c == '\"') { //это строка
            new_line_ = false;
            return ReadString(c);
        }

        return ReadId(c);
    }
}  // namespace parse