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
        "Invalid yield usage: 'yield' can only be used inside function/method",
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
        "Invalid native variable import: expected string literal after 'from' keyword (e.g., 'from \"libc\"')",
        "Invalid break/continue: can only be used within a loop or switch statement",
        "Undefined label: break/continue refers to a non-existent or invalid label",
        "Invalid function declaration: function body is required. Only methods in traits and classes can be declared without a body (abstract methods). Regular functions must always have a body defined",
        "Invalid constant(let keyword) declaration: can only be used within class, trait, or module definitions",
        "Unsupported syntax: the current implementation does not support this construction",
        "Invalid syntax within class definition: expected method, var/let declaration, or class-level statement",
        "Invalid syntax within trait definition: expected method, let declaration, or trait-level statement",
        "Invalid init declaration: constructor body is required, expected '{' after parameter list",
        "Invalid init declaration: expected '(' before parameter list",
        "Invalid cleanup declaration: destructor body is required, expected '{' after 'cleanup'",
        "Invalid cleanup declaration: destructor cannot have parameters, unexpected '(' after 'cleanup'",
        "Invalid return statement: constructors and destructors cannot return a value",
        "Invalid new expression: expected call",
        "Invalid self/super usage: can only be used within class or trait methods",
        "Invalid constructor: first statement in derived class constructor must be super.init(...)",
        "Invalid 'prot' usage: can only be used within class/trait definitions",
        "Invalid assignment: cannot assign non-assignable expression (e.g., defer, panic, spawn)",
        "Invalid expression: non-value expression (e.g., defer, panic, spawn) cannot be used in value context (e.g., tuple, function argument, binary operation)",
        "Missing closing parenthesis: expected ')' after exception types",
        "Invalid import: module name is not a valid identifier. Use 'as' to provide an alias",
        "Invalid native function call arity",
        "Invalid call to a native variable",
        "Invalid function declaration: anonymous functions are not allowed in this context",
        "Invalid ternary operator syntax: expected ':' after true expression"
    };

    constexpr auto kInitMethodName = "init";
    constexpr auto kCleanupMethodName = "cleanup";

    class Context;

    enum class ParserErrorType {
        OK,

        GENERIC_ERROR,
        NOMEM,
        SYNTAX
    };

    class ParserError {
    public:
        /// @brief Holds the state of the parsing process represented by a `ParserErrorType` enumeration value.
        ParserErrorType type = ParserErrorType::OK;

        /**
         * @brief Pointer to a constant character string representing the error message.
         *
         * This variable is used to store a descriptive message related to parsing errors.
         * It is assigned a specific error message based on the type of error encountered
         * during the parsing process.
         */
        const char *message = nullptr;

        /// @brief Last processed token (if applicable)

        scanner::Token token;
    };

    class Parser {
        using LedMeth = ASTHandle<ASTNode *> (Parser::*)(ASTHandle<ASTNode *> &);
        using NudMeth = ASTHandle<ASTNode *> (Parser::*)();

        /**
         * @brief Pointer to an instance of orbiter::Isolate used for managing and isolating execution contexts.
         *
         * This variable is initialized to nullptr by default and serves as the Isolate context
         * for the execution of code, memory management, and other runtime tasks within the orbiter environment.
         * It must be properly initialized before usage to ensure correct functionality.
         */
        orbiter::Isolate *isolate_ = nullptr;

        /// @brief Stores the name of the source file being parsed.
        const char *filename_ = nullptr;

        /// @brief Context used during the parsing process to maintain state and enforce rules.
        Context *context_ = nullptr;

        /// @brief Pointer to the symbol table used by the parser.
        SymbolTable *sym_t_ = nullptr;

        /// @brief A collection of HORString objects representing exported definitions.
        std::vector<orbiter::datatype::HORString> exports{};

        /**
         * @brief Stores information about the most recent error encountered during parsing.
         *
         * This variable is an instance of the `ParserError` class and is used to capture details
         * about errors that occur during the parsing process, including the error type, associated message,
         * and the token where the error was identified. It is updated accordingly as errors are encountered
         * to facilitate error reporting and debugging.
         */
        ParserError error_;

        /// @brief Reference to a Scanner instance used for scanning tokens from the source code.
        scanner::Scanner &scanner_;

        /**
         * @brief Represents the current token being processed by the parser.
         *
         * This variable holds the token obtained from the scanner. It is used to track
         * and manage the parser's position within the source code, enabling token
         * matching, consumption, and validation during parsing operations.
         */
        scanner::Token tkcur_;

        /**
         * @brief Stores the current documentation comment token being processed by the parser.
         *
         * This token represents a documentation comment (e.g., docstring) encountered during parsing.
         * It is updated with the most recently parsed documentation comment, if applicable.
         */
        scanner::Token doc_;

        friend Context;

        [[nodiscard]] bool Match(scanner::TokenType type) const noexcept {
            return this->tkcur_.type == type;
        }

        template<typename... TokenTypes>
        [[nodiscard]] bool Match(scanner::TokenType type, TokenTypes... types) const {
            if (!this->Match(type))
                return this->Match(types...);

            return true;
        }

        [[nodiscard]] bool CheckAssignable(ASTNode *node) {
            do {
                if (node->node_type == NodeType::NIL_SAFE)
                    node = ((Unary *) node)->value;

                if (node->node_type == NodeType::DEFER
                    || node->node_type == NodeType::PANIC
                    || node->node_type == NodeType::SPAWN)
                    return false;
            } while (node->node_type == NodeType::NIL_SAFE);

            return true;
        }

        [[nodiscard]] bool MatchEat(const scanner::TokenType type, const bool ignore_nl) {
            if (ignore_nl && this->tkcur_.type == scanner::TokenType::END_OF_LINE)
                this->Eat(true);

            if (this->Match(type)) {
                this->Eat(ignore_nl);

                return true;
            }

            return false;
        }

        [[nodiscard]] bool TokenInRange(const scanner::TokenType begin, const scanner::TokenType end) const noexcept {
            return this->tkcur_.type > begin && this->tkcur_.type < end;
        }

        [[nodiscard]] ASTHandle<ASTNode *> InjectInit(scanner::Loc loc) const;

        [[nodiscard]] ASTHandle<ASTNode *> ParseClassTrait(AccessModifier access);

        [[nodiscard]] ASTHandle<ASTNode *> ParseDecorator();

        [[nodiscard]] ASTHandle<ASTNode *> ParseExtImpl();

        [[nodiscard]] ASTHandle<ASTNode *> ParseDeferStatement();

        [[nodiscard]] ASTHandle<ASTNode *> ParseForInStatement();

        [[nodiscard]] ASTHandle<ASTNode *> ParseIfStatement();

        [[nodiscard]] ASTHandle<ASTNode *> ParseImportStatement();

        [[nodiscard]] ASTHandle<ASTNode *> ParseLoopStatement();

        [[nodiscard]] ASTHandle<ASTNode *> ParseNativeStatement();

        [[nodiscard]] ASTHandle<ASTNode *> ParseNativeFuncStatement(const scanner::Position &start);

        [[nodiscard]] ASTHandle<ASTNode *> ParseSwitchCase(bool as_if);

        [[nodiscard]] ASTHandle<ASTNode *> ParseSwitchStatement();

        [[nodiscard]] ASTHandle<ASTNode *> ParseSyncStatement();

        [[nodiscard]] ASTHandle<ASTNode *> ParseTryCatchFinally();

        [[nodiscard]] ASTHandle<ASTNode *> ParseVarDecl(const scanner::Position &start, AccessModifier access,
                                                        bool constant,
                                                        bool weak, bool decl_only);

        // *************************************************************************************************************
        // EXPRESSIONS
        // *************************************************************************************************************

        [[nodiscard]] ASTHandle<ASTNode *> ParseANPST();

        [[nodiscard]] ASTHandle<ASTNode *> ParseAssignment(ASTHandle<ASTNode *> &left);

        [[nodiscard]] ASTHandle<ASTNode *> ParseBlock(bool nested);

        [[nodiscard]] ASTHandle<ASTNode *> ParseCleanupInit(const scanner::Position &start, AccessModifier access);

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

        [[nodiscard]] ASTHandle<Function *> ParseFunction(const scanner::Position &start, bool must_named,
                                                          AccessModifier access);

        [[nodiscard]] ASTHandle<Parameter *> ParseParameter(const scanner::Position &start, NodeType type);

        [[nodiscard]] ASTHandle<Parameter *> PushSelfParam(const scanner::Loc &loc) const;

        [[nodiscard]] orbiter::datatype::HORString GetDocString(bool module_doc);

        [[nodiscard]] std::vector<ASTHandle<ASTNode *> > ParseFuncParams(scanner::Loc &last_param);

        static LedMeth LookupLED(scanner::TokenType token) noexcept;

        static NudMeth LookupNUD(scanner::TokenType token) noexcept;

        [[nodiscard]] orbiter::datatype::HORString MakeFuncName() const;

        void AdjustInlineExport(const Assignment *decl, AccessModifier access, bool weak);

        void CheckSetImportAlias(orbiter::datatype::HORString alias, Import *imp) const;

        void ClassCheck(const Construct *clazz) const;

        void Eat(bool ignore_nl);

        void EatNL();

        void IgnoreNewLineIF(scanner::TokenType type);

    public:
        /**
         * @brief Initialize the parser with a filename and the scanner.
         * @param isolate Pointer to isolate.
         * @param filename Source code name.
         * @param scanner Reference to Scanner.
         */
        Parser(orbiter::Isolate *isolate, const char *filename, scanner::Scanner &scanner) noexcept : isolate_(isolate),
            filename_(filename), scanner_(scanner) {
        }

        ~Parser() {
            SymbolTable::Delete(this->sym_t_);
        }

        /**
         * @brief Parses the source code.
         *
         * @return A ASTHandle to the root node of the AST or nullptr in case of unrecoverable error.
         */
        ASTHandle<Module *> Parse() noexcept;

        ParserError GetLastError() noexcept;
    };
}

#endif // !ORBIT_LIFTOFF_PARSER_PARSER_H_
