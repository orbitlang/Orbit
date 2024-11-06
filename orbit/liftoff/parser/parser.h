// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_PARSER_PARSER_H_
#define ORBIT_LIFTOFF_PARSER_PARSER_H_

#include <orbit/liftoff/parser/ast.h>
#include <orbit/liftoff/scanner/scanner.h>

#include <orbit/orbiter/datatype/orstring.h>

namespace liftoff::parser {
    constexpr const char *kStandardError[] = {
        "Invalid syntax: unexpected token or expression",
        "Invalid assignment target: left side of '=' must be an identifier, index, slice, or member access",
        "Invalid increment/decrement: operator can only be applied to variables or member access",
        "Invalid subscript: index or slice definition cannot be empty",
        "Missing closing bracket: expected ']' to close index or slice definition",
        "Missing closing bracket: expected ']' to close list definition",
        "Type mismatch: started with dictionary syntax but found set element",
        "Type mismatch: started with set syntax but found key-value pair",
        "Missing closing brace: expected '}' to close dictionary definition",
        "Missing closing brace: expected '}' to close set definition",
        "Missing closing parenthesis: expected ')' to close tuple definition",
        "Invalid function declaration: expected '(' before parameter list",
        "Missing closing parenthesis: expected ')' to close function parameters",
        "Invalid parameter order: unexpected positional parameter after named parameter",
        "Invalid parameter order: named parameter found after rest or kwargs parameter",
        "Invalid function declaration: only one rest parameter (...args) allowed per function",
        "Missing identifier: expected name or identifier",
        "Invalid 'const' usage: can only be used within class or trait definitions",
        "Missing opening brace: expected '{' to start code block",
        "Missing closing brace: expected '}' to end code block",
        "Invalid named argument: only identifiers allowed before '=' in named arguments",
        "Invalid parameter order: must be [positional args][, named args][, spread args][, kwargs]",
        "Missing closing parenthesis: expected ')' to close function call",
        "Invalid walrus operator usage: expected identifier(s) before ':='",
        "Invalid let declaration: expected '=' after identifier(s)",
        "Invalid 'weak' usage: can only be used within class definitions",
        "Invalid defer statement: expected function or method call",
        "Invalid spawn statement: expected function or method call",
        "Missing semicolon: expected ';' after test expression",
        "Invalid for-in loop: expected 'in' keyword after loop variables",
        "Invalid yield statement: expected expression after 'yield'",
        "Invalid yield usage: 'yield' can only be used inside functions",
        "Invalid try block: must have at least one catch or finally clause",
        "Invalid catch clause: expected @atom after catch",
        "Invalid label placement: labels cannot be stacked. Each label must be followed by a valid loop statement",
        "Invalid label usage: labels can only precede 'for', 'for-in', or 'loop' statements. Other statements cannot be labeled",
        "Invalid switch syntax: expected '{' after switch condition to open switch block",
        "Invalid case/default syntax: expected ':' after case expression or default keyword",
        "Invalid 'fallthrough' placement: fallthrough statement must be the last statement in a case or default block",
        "Multiple default cases: switch statement can have at most one default case",
        "Invalid case syntax: multiple case expressions (case a;b;c:) are only allowed when switch has a value to match against.",
        "Unclosed switch statement: expected '}' at the end of switch block",
        "Invalid trait inheritance: traits cannot inherit from classes using ':'. Use 'impl TraitName' to extend other traits",
        "Invalid inheritance/implementation syntax: class extension and trait implementation can only use identifiers and member access (e.g., name or name.subname)",
        "Missing closing bracket: expected ']' to close decorator definition",
        "Invalid decorator placement: decorators must be immediately followed by a function definition",
        "Invalid import syntax: expected string literal or identifier after 'import' keyword",
        "Invalid alias syntax: expected identifier after 'as' keyword",
        "Invalid import syntax: expected 'from' keyword after import identifiers list",
        "Invalid module path: expected string literal after 'from' keyword",
        "Invalid native function declaration: expected function name identifier after 'native func' (e.g., 'native func read')",
        "Invalid native function syntax: expected opening parenthesis '(' after function name for parameter list",
        "Invalid native parameter declaration: expected parameter name identifier (e.g., 'value: i32')",
        "Invalid native parameter syntax: expected colon ':' after parameter name to specify type",
        "Invalid native parameter type: expected a valid native type (e.g., 'i32', 'f64', 'bool', etc.)",
        "Invalid native function syntax: expected closing parenthesis ')' to end parameter list",
        "Invalid native function syntax: expected colon ':' after parameter list to specify return type",
        "Invalid native return type: expected a valid native type (e.g., 'i32', 'f64', 'bool', etc.)",
        "Invalid native function alias: expected identifier after 'as' keyword (e.g., 'as print')",
        "Invalid native function import: expected string literal after 'from' keyword (e.g., 'from \"libc\"')",
        "Invalid native declaration: 'native' must be followed by 'func', 'var', or 'let' (e.g., 'native var' or 'native let')",
        "Invalid native variable declaration: expected variable name identifier after 'var' or 'let' (e.g., 'native var count')",
        "Invalid native variable syntax: expected colon ':' after variable name to specify type",
        "Invalid native variable type: expected a valid native type (e.g., 'i32', 'f64', 'bool', etc.)",
        "Invalid native variable alias: expected identifier after 'as' keyword (e.g., 'as counter')",
        "Invalid native variable import: expected string literal after 'from' keyword (e.g., 'from \"libc\"')"
    };

    class Context;

    class Parser {
        using LedMeth = ASTHandle<ASTNode *> (Parser::*)(ASTHandle<ASTNode *> &);
        using NudMeth = ASTHandle<ASTNode *> (Parser::*)();

        friend Context;

        const orbiter::Context *ctx_ = nullptr;

        const char *filename_ = nullptr;

        Context *context_ = nullptr;

        std::vector<orbiter::datatype::HORString> exports{};

        std::vector<orbiter::datatype::HORString> imports{};

        SymbolTable *sym_t_ = nullptr;

        scanner::Scanner &scanner_;

        scanner::Token tkcur_;

        scanner::Token doc_;

        [[nodiscard]] bool Match(scanner::TokenType type) const noexcept {
            return this->tkcur_.type == type;
        }

        template<typename... TokenTypes>
        [[nodiscard]] bool Match(scanner::TokenType type, TokenTypes... types) const {
            if (!this->Match(type))
                return this->Match(types...);

            return true;
        }

        bool MatchEat(scanner::TokenType type, bool ignore_nl) {
            if (ignore_nl && this->tkcur_.type == scanner::TokenType::END_OF_LINE)
                this->Eat(true);

            if (this->Match(type)) {
                this->Eat(ignore_nl);

                return true;
            }

            return false;
        }

        [[nodiscard]] bool TokenInRange(scanner::TokenType begin, scanner::TokenType end) const noexcept {
            return this->tkcur_.type > begin && this->tkcur_.type < end;
        }

        [[nodiscard]] ASTHandle<ASTNode *> ParseClassTrait();

        [[nodiscard]] ASTHandle<ASTNode *> ParseDecorator();

        [[nodiscard]] ASTHandle<ASTNode *> ParseExtImpl();

        [[nodiscard]] ASTHandle<ASTNode *> ParseDeferStatement();

        [[nodiscard]] ASTHandle<ASTNode *> ParseForInStatement();

        [[nodiscard]] ASTHandle<ASTNode *> ParseIfStatement();

        [[nodiscard]] ASTHandle<ASTNode *> ParseImportStatement();

        [[nodiscard]] ASTHandle<ASTNode *> ParseLoopStatement();

        [[nodiscard]] ASTHandle<ASTNode *> ParseNativeStatement();

        [[nodiscard]] ASTHandle<ASTNode *> ParseNativeFuncStatement(scanner::Position start);

        [[nodiscard]] ASTHandle<ASTNode *> ParseSwitchCase(bool as_if);

        [[nodiscard]] ASTHandle<ASTNode *> ParseSwitchStatement();

        [[nodiscard]] ASTHandle<ASTNode *> ParseSyncStatement();

        [[nodiscard]] ASTHandle<ASTNode *> ParseTryCatchFinally();

        [[nodiscard]] ASTHandle<ASTNode *> ParseVarDecl(const scanner::Position &start, bool pub, bool constant,
                                                        bool weak, bool decl_only);

        // *************************************************************************************************************
        // EXPRESSIONS
        // *************************************************************************************************************

        [[nodiscard]] ASTHandle<ASTNode *> ParseAPST();

        [[nodiscard]] ASTHandle<ASTNode *> ParseAssignment(ASTHandle<ASTNode *> &left);

        [[nodiscard]] ASTHandle<ASTNode *> ParseBlock(bool nested);

        [[nodiscard]] ASTHandle<ASTNode *> ParseDictSet();

        [[nodiscard]] ASTHandle<ASTNode *> ParseElvis(ASTHandle<ASTNode *> &left);

        [[nodiscard]] ASTHandle<ASTNode *> ParseExpression();

        [[nodiscard]] ASTHandle<ASTNode *> ParseExpression(int precedence);

        [[nodiscard]] ASTHandle<ASTNode *> ParseExpression(scanner::TokenType precedence);

        [[nodiscard]] ASTHandle<ASTNode *> ParseExpressionList(ASTHandle<ASTNode *> &left);

        [[nodiscard]] ASTHandle<ASTNode *> ParseExprOrTuple();

        [[nodiscard]] ASTHandle<ASTNode *> ParseFunc();

        [[nodiscard]] ASTHandle<ASTNode *> ParseFuncCall(ASTHandle<ASTNode *> &left);

        [[nodiscard]] ASTHandle<ASTNode *> ParseIdentifier();

        [[nodiscard]] ASTHandle<ASTNode *> ParseIndexing(ASTHandle<ASTNode *> &left);

        [[nodiscard]] ASTHandle<ASTNode *> ParseInfix(ASTHandle<ASTNode *> &left);

        [[nodiscard]] ASTHandle<ASTNode *> ParseInNotIn(ASTHandle<ASTNode *> &left);

        [[nodiscard]] ASTHandle<ASTNode *> ParseList();

        [[nodiscard]] ASTHandle<ASTNode *> ParseLiteral();

        [[nodiscard]] ASTHandle<ASTNode *> ParseMemberAccess(ASTHandle<ASTNode *> &left);

        [[nodiscard]] ASTHandle<ASTNode *> ParseNullCoalescing(ASTHandle<ASTNode *> &left);

        [[nodiscard]] ASTHandle<ASTNode *> ParsePipeline(ASTHandle<ASTNode *> &left);

        [[nodiscard]] ASTHandle<ASTNode *> ParsePostInc(ASTHandle<ASTNode *> &left);

        [[nodiscard]] ASTHandle<ASTNode *> ParsePrefix();

        [[nodiscard]] ASTHandle<ASTNode *> ParseStatement();

        [[nodiscard]] ASTHandle<ASTNode *> ParseTernary(ASTHandle<ASTNode *> &left);

        [[nodiscard]] ASTHandle<ASTNode *> ParseWalrus(ASTHandle<ASTNode *> &left);

        [[nodiscard]] ASTHandle<Function *> ParseFunction(bool inl);

        [[nodiscard]] ASTHandle<Parameter *> ParseParameter(const scanner::Position &start, NodeType type);

        [[nodiscard]] orbiter::datatype::HORString GetDocString();

        [[nodiscard]] std::vector<ASTHandle<ASTNode *> > ParseFuncParams();

        static LedMeth LookupLED(scanner::TokenType token) noexcept;

        static NudMeth LookupNUD(scanner::TokenType token) noexcept;

        [[nodiscard]] orbiter::datatype::HORString MakeFuncName() const;

        void AdjustInlineExport(const Assignment *decl, bool pub, bool weak);

        void Eat(bool ignore_nl);

        void EatNL();

        void IgnoreNewLineIF(scanner::TokenType type);

        /**
         * @brief Report a parsing error.
         *
         * @param message Error message.
         * @param location Location in the source where the error occurred.
         */
        //void ReportError(const std::string &message, const scanner::SourceLocation &location);

    public:
        /**
         * @brief Initialize the parser with a filename and the scanner.
         *
         * @param filename Source code name.
         * @param scanner Reference to Scanner.
         */
        Parser(orbiter::Context *ctx, const char *filename,
               scanner::Scanner &scanner) noexcept: ctx_(ctx), filename_(filename), scanner_(scanner) {
        }

        /**
         * @brief Parses the source code.
         *
         * @return A ASTHandle to the root node of the AST or nullptr in case of unrecoverable error.
         */
        ASTHandle<Module *> Parse() noexcept;
    };

    enum class ContextType {
        CLASS,
        FUNC,
        MODULE,
        TRAIT
    };

    class Context {
        Context *back_;
        Parser *parser_;

        ContextType type_;

    public:
        int anon_count = 0;

        [[nodiscard]] bool Check(ContextType type) const noexcept {
            return this->type_ == type;
        }

        [[nodiscard]] bool CheckExt(ContextType type) const noexcept {
            auto cursor = this->back_;

            while (cursor != nullptr) {
                if (cursor->type_ == type)
                    return true;

                cursor = cursor->back_;
            }

            return false;
        }

        explicit Context(Parser *parser, ContextType type) : back_(parser->context_), parser_(parser), type_(type) {
            parser->context_ = this;
        }

        ~Context() {
            this->parser_->context_ = this->back_;
        }
    };
}

#endif // !ORBIT_LIFTOFF_PARSER_PARSER_H_
