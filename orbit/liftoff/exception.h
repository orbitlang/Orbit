// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_EXCEPTION_H_
#define ORBIT_LIFTOFF_EXCEPTION_H_

#include <stdexcept>

namespace liftoff {
    class DatatypeException final : public std::exception {
    };

    class ParserException : public std::exception {
    public:
        int err_idx;

        explicit ParserException(int err_idx) : err_idx(err_idx) {
        }
    };

    class ScannerException final : public std::exception {
    };

    class SymbolTableException final : public std::exception {
    };
}

#endif // !ORBIT_LIFTOFF_EXCEPTION_H_
