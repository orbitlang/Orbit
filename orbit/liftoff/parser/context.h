// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_PARSER_CONTEXT_H_
#define ORBIT_LIFTOFF_PARSER_CONTEXT_H_

#include <orbit/liftoff/parser/parser.h>

namespace liftoff::parser {
    enum class ContextType {
        CDTOR,
        CLASS,
        FUNC,
        LOOP,
        MODULE,
        TRAIT,
        SWITCH
    };

    class Context {
        Context *back_;
        Parser *parser_;

        ContextType type_;

    public:
        int anon_count = 0;

        [[nodiscard]] bool Check(const ContextType type) const noexcept {
            return this->type_ == type;
        }

        [[nodiscard]] bool CheckBack(const ContextType type) const noexcept {
            return this->back_ != nullptr && this->back_->type_ == type;
        }

        [[nodiscard]] bool CheckExt(const ContextType type) const noexcept {
            auto cursor = this;

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

#endif // !ORBIT_LIFTOFF_PARSER_CONTEXT_H_
