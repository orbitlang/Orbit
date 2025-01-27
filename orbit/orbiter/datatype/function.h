// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_FUNCTION_H_
#define ORBIT_ORBITER_DATATYPE_FUNCTION_H_

#include <atomic>

#include <orbit/orbiter/isolate.h>

#include <orbit/orbiter/datatype/oobject.h>
#include <orbit/orbiter/datatype/orstring.h>

namespace orbiter::datatype {
    enum class FunctionKind : U8 {
        ASYNC = 0x1,

        METHOD = 0x1 << 1,
        NATIVE = 0x1 << 2
    };
}

ENUMBITMASK_ENABLE(orbiter::datatype::FunctionKind);

namespace orbiter::datatype {
    /**
     * @brief Structure representing shared function data
     */
    struct FuncShared {
        /// Reference count for this shared data
        std::atomic_uint refs;

        /// Name of the function
        ORString *name;

        /// Documentation string for the function
        ORString *doc;

        union {
            /// Pointer to Orbit code.
            OObject *code; // TODO: Code obj

            /// Pointer to native code.
            FunctionPtr func;
        };

        /// Arity of the function, how many args accepts in input?!.
        U16 arity;

        /// Kind of the function (async, method, native, etc.)
        FunctionKind kind;

        /**
         * @brief Check if the function is interpreted
         *
         * @return true if the function is interpreted, false otherwise
         */
        [[nodiscard]] bool IsInterpreted() const {
            return ENUMBITMASK_ISFALSE(this->kind, FunctionKind::NATIVE);
        }
    };

    /**
     * @brief Structure representing a function
     */
    struct Function {
        OROBJ_HEAD;

        /// Pointer to shared function data
        FuncShared *shared;
    };

    /**
     * @brief Set up additional features and properties for the specified type
     *
     * This function enriches the previously created type with various functionalities.
     * It typically performs the following tasks:
     * - Adds default methods to the type
     * - Adds required properties to the type
     *
     * This function is called immediately after the type's Init function to complete its setup.
     *
     * @param self Pointer to TypeInfo created by %type%Init call
     *
     * @return true if setup was successful, false otherwise
     */
    bool FunctionTypeSetup(TypeInfo *self);

    /**
     * @brief Create a new Function object
     *
     * @param isolate Pointer to the Isolate
     * @param def Pointer to the FunctionDef
     *
     * @return Pointer to the newly created Function
     */
    Function *FunctionNew(Isolate *isolate, const FunctionDef *def);

    /**
     * @brief Initialize the Function type
     *
     * @param isolate Pointer to the Isolate
     *
     * @return Pointer to the TypeInfo for the Function type
     */
    TypeInfo *FunctionTypeInit(Isolate *isolate);
}

#define RUNTIME_FUNCTION(name, exported_name, doc, params)                                          \
OObject *name##_fn(orbiter::datatype::Function *_func, OObject **argv, OObject *kwargs, U16 argc);  \
const FunctionDef name = {#exported_name, doc, name##_fn, params, false};                           \
OObject *name##_fn(orbiter::datatype::Function *_func, OObject **argv, OObject *kwargs, U16 argc)

#define RUNTIME_METHOD(name, exported_name, doc, params)                                            \
OObject *name##_fn(orbiter::datatype::Function *_func, OObject **argv, OObject *kwargs, U16 argc);  \
const FunctionDef name = {#exported_name, doc, name##_fn, params, true};                            \
OObject *name##_fn(orbiter::datatype::Function *_func, OObject **argv, OObject *kwargs, U16 argc)

#endif // !ORBIT_ORBITER_DATATYPE_FUNCTION_H_
