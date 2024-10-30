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
        "invalid syntax",
        "only identifier(s)/index/slice/access expression are allowed before the '=' sign",
        "unexpected update operator",
        "subscript definition (index | slice) cannot be empty",
        "expected ']' after (index | slice) definition",
        "expected ']' after list definition",
        "you started defining a dict, not a set",
        "you started defining a set, not a dict",
        "expected '}' after dict definition",
        "expected '}' after set definition",
        "expected ')' after tuple definition",
        "expected '(' to start function parameters definition",
        "expected ')' after function params",
        "unexpected named/positional parameter",
        "unexpected named parameter",
        "only one rest-param is allowed per function declaration",
        "expected identifier",
        "const is unexpected outside a class/trait",
        "expected '{' to start a code block",
        "expected '}' to end a code block",
        "only identifiers are allowed before the '=' sign",
        "function parameters must be passed in the order: [positional][, named param][, spread][, kwargs]",
        "expected ')' after arguments in function call",
        "expected identifier(s) before warlus':=' operator",
        "expected '=' after identifier(s) in let declaration",
        "'weak' can only be used in the context of a class",
        "defer expected call expression"
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

        [[nodiscard]] ASTHandle<ASTNode *> ParseDeferStatement();

        [[nodiscard]] ASTHandle<ASTNode *> ParseIfStatement();

        [[nodiscard]] ASTHandle<ASTNode *> ParseSyncStatement();

        [[nodiscard]] ASTHandle<ASTNode *> ParseVarDecl(const scanner::Position &start, bool pub, bool constant,
                                                        bool weak);

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
