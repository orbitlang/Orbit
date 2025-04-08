// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_FUNCTION_H_
#define ORBIT_ORBITER_DATATYPE_FUNCTION_H_

#include <atomic>

#include <orbit/orbiter/isolate.h>

#include <orbit/orbiter/datatype/context.h>
#include <orbit/orbiter/datatype/code.h>
#include <orbit/orbiter/datatype/dict.h>
#include <orbit/orbiter/datatype/module.h>

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

        /// Pointer to the context associated with the function.
        Context *context;

        /// Pointer to the module associated with the function (if any).
        Module *module;

        /// Pointer to default values for function parameters.
        Dict *defaults;

        /// Name of the function
        ORString *name;

        /// Documentation string for the function
        ORString *doc;

        union {
            /// Pointer to Orbit code.
            Code *code;

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

    using HFunction = Handle<Function>;

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
    HFunction FunctionNew(Isolate *isolate, const FunctionDef *def);

    /**
     * @brief Creates a new Function object with the specified code, defaults, and function kind.
     *
     * This constructor initializes a new shared function data object and associates
     * it with the provided code and default values. It also sets up the function's
     * execution context and module information.
     *
     * @param code A pointer to the Code object representing the code for the function.
     * @param defaults A pointer to the Dict object containing default argument values for the function.
     * @param kind The type of function, defined by the FunctionKind enumeration.
     * @return An HFunction handle representing the created Function object.
     */
    HFunction FunctionNew(Code *code, Dict *defaults, FunctionKind kind);

    /**
     * @brief Initialize and create the specified type
     *
     * This function creates a new TypeInfo object representing the specific type.
     * It sets up the basic structure and core properties of the type.
     *
     * @param isolate Pointer to the Isolate in which the type is being created
     *
     * @return Handle to the newly created TypeInfo for the type, or an empty handle if creation failed
     */
    HOType FunctionTypeInit(Isolate *isolate);
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
