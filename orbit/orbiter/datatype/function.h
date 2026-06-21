// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_FUNCTION_H_
#define ORBIT_ORBITER_DATATYPE_FUNCTION_H_

#include <atomic>

#include <orbit/orbiter/isolate.h>

#include <orbit/orbiter/datatype/closure.h>
#include <orbit/orbiter/datatype/context.h>
#include <orbit/orbiter/datatype/code.h>
#include <orbit/orbiter/datatype/dict.h>
#include <orbit/orbiter/datatype/module.h>
#include <orbit/orbiter/datatype/tuple.h>

namespace orbiter::datatype {
    enum class FunctionKind : U8 {
        SIMPLE = 0x0,

        ASYNC = 0x1,
        CLOSURE = 1 << 1,
        GENERATOR = 1 << 2,
        INIT = 0x1 << 3,
        METHOD = 0x1 << 4,
        NATIVE = 0x1 << 5,
        KWARGS = 0x1 << 6,
        REST = 0x1 << 7
    };
}

ENUMBITMASK_ENABLE(orbiter::datatype::FunctionKind);

namespace orbiter::datatype {
    using FunctionPtr = HOObject (*)(struct Function *, OObject **argv, OObject *rest, OObject *kwargs, U16 argc);

    struct FunctionDef {
        /// Name of native function (this name will be exposed to Orbit)
        const char *name;

        /// Documentation of native function (this doc will be exposed to Orbit)
        const char *doc;

        /// List of optional parameters equivalent to Orbit's defaults in the form: param1, param2.
        /// Unlike managed functions, it is not possible to define the default value, which will always be nil.
        /// Value management and validation are handled by the called function.
        const char *defaults;

        /// Pointer to native code
        FunctionPtr func;

        /// Arity
        U16 params;

        /// Export as a method or a static function?
        bool method;

        /// Accepts variadic arguments?
        bool varargs;

        /// Accepts keyword arguments?
        bool kwargs;
    };

#define FUNCTIONDEF_SENTINEL {nullptr, nullptr, nullptr, 0, false}

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
        Tuple *defaults;

        /// Pointer to the TypeInfo structure that represents the owning type of the function.
        TypeInfo *owner_type;

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
         * @brief Checks whether the function has default arguments defined.
         *
         * @return true if the function has default arguments, false otherwise.
         */
        [[nodiscard]] bool HasDefaultArgs() const {
            return this->defaults != nullptr;
        }

        [[nodiscard]] bool IsAsync() const {
            return ENUMBITMASK_ISTRUE(this->kind, FunctionKind::ASYNC);
        }

        /**
         * @brief Check if the function is a generator
         *
         * @return true if the function is a generator, false otherwise
         */
        [[nodiscard]] bool IsGenerator() const {
            return ENUMBITMASK_ISTRUE(this->kind, FunctionKind::GENERATOR);
        }

        /**
         * @brief Check if the function is a class constructor
         *
         * @return true if the function is a constructor, false otherwise
         */
        [[nodiscard]] bool IsInit() const {
            return ENUMBITMASK_ISTRUE(this->kind, FunctionKind::INIT)
                   && ENUMBITMASK_ISTRUE(this->kind, FunctionKind::METHOD);
        }

        /**
         * @brief Check if the function is interpreted
         *
         * @return true if the function is interpreted, false otherwise
         */
        [[nodiscard]] bool IsInterpreted() const {
            return ENUMBITMASK_ISFALSE(this->kind, FunctionKind::NATIVE);
        }

        /**
         * @brief Checks whether the function accepts keyword arguments.
         *
         * @return true if the function accepts keyword arguments, false otherwise.
         */
        [[nodiscard]] bool IsKWargs() const {
            return ENUMBITMASK_ISTRUE(this->kind, FunctionKind::KWARGS);
        }

        /**
         * @brief Checks whether the function is categorized as a method.
         *
         * @return true if the function is a method, false otherwise.
         */
        [[nodiscard]] bool IsMethod() const {
            return ENUMBITMASK_ISTRUE(this->kind, FunctionKind::METHOD);
        }

        /**
         * @brief Determines if the function is variadic.
         *
         * @return true if the function is variadic, false otherwise.
         */
        [[nodiscard]] bool IsVariadic() const {
            return ENUMBITMASK_ISTRUE(this->kind, FunctionKind::REST);
        }

        /**
         * @brief Increments the reference count for the shared function data
         * and returns the current instance.
         *
         * @return A pointer to the current `FuncShared` instance with the incremented reference count.
         */
        [[nodiscard]] FuncShared *GetRef() {
            this->refs.fetch_add(1);

            return this;
        }
    };

    /**
     * @brief Structure representing a function
     */
    struct Function {
        OROBJ_HEAD;

        /// Pointer to shared function data
        FuncShared *shared;

        /// Pointer to the closure associated with the function.
        Closure *closure;

        /// Tuple that contains values for partial application.
        Tuple *currying;
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
     * @brief Look up the Function instance registered on a type from its definition.
     *
     * Scans the type's properties for a native Function whose underlying
     * implementation matches `def.func` and returns the first match.
     *
     * Intended to be called after the type's methods have been registered via
     * `TIPropertyAdd`, when you need to reference one of those Function objects
     * directly — for example to bind it as the type's constructor (`ctor`)
     * without allocating a second Function for the same definition.
     *
     * @param type Pointer to the TypeInfo whose properties will be searched.
     * @param def  Pointer to the native function definition to match against.
     *
     * @return Handle to the matching Function, or an empty handle if no
     *         registered Function wraps `def.func`.
     */
    HFunction FunctionFromDef(const TypeInfo *type, const FunctionDef &def);

    /**
     * @brief Create a new native Function object from a function definition.
     *
     * This factory builds the shared runtime metadata for a native function,
     * including its name, documentation string, arity and flags
     * (method / variadic / keyword arguments).
     *
     * When `def->method` is true, the function is marked as a method and associated with `owner`.
     *
     * @param isolate Pointer to the Isolate used to allocate the function.
     * @param owner Pointer to the owning type, used only when the function
     *        is exported as a method.
     * @param def Pointer to the native function definition.
     *
     * @return Handle to the newly created Function, or an empty handle on failure.
     */
    HFunction FunctionNew(Isolate *isolate, TypeInfo *owner, const FunctionDef *def);

    /**
     * @brief Creates a new Function object with the specified code, closure, defaults and function kind.
     *
     * This constructor initializes a new shared function data object and associates
     * it with the provided code and default values. It also sets up the function's
     * execution context, closure environment and module information.
     *
     * @param code A pointer to the Code object representing the code for the function.
     * @param closure A pointer to the Closure object containing the captured variables.
     * @param defaults A pointer to the Tuple object containing default argument values.
     * @param flags The type of function, defined by the LoadFuncFlags enumeration.
     * @return An HFunction handle representing the created Function object.
     */
    HFunction FunctionNew(Code *code, Closure *closure, Tuple *defaults, LoadFuncFlags flags);

    /**
     * @brief Creates a new function object with provided arguments and curried values.
     *
     * This function initializes a new function object by taking an existing function
     * and an array of arguments. It applies argument currying, which is the process
     * of partially applying a function by storing the provided arguments.
     * The new function object shares the same shared data as the input function
     * and increments its reference count.
     *
     * @param func Pointer to the existing function object to be used as a base.
     * @param args Array of pointers to objects representing the arguments to be curried.
     * @param argc The number of arguments provided in the `args` array.
     * @return A handle to the newly created function object, or an empty handle if the creation fails.
     */
    HFunction FunctionNew(const Function *func, OObject **args, U16 argc);

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

#define RUNTIME_FUNCTION(name, exported_name, doc, params, defaults, varargs, _kwargs)                              \
HOObject name##_fn(orbiter::datatype::Function *_func, OObject **argv, OObject *rest, OObject *kwargs, U16 argc);   \
constexpr FunctionDef name = {#exported_name, doc, defaults, name##_fn, params, false, varargs, _kwargs};           \
HOObject name##_fn(orbiter::datatype::Function *_func, OObject **argv, OObject *rest, OObject *kwargs, U16 argc)

#define RUNTIME_METHOD(name, exported_name, doc, params, defaults, varargs, _kwargs)                                \
HOObject name##_fn(orbiter::datatype::Function *_func, OObject **argv, OObject *rest, OObject *kwargs, U16 argc);   \
constexpr FunctionDef name = {#exported_name, doc, defaults, name##_fn, params, true, varargs, _kwargs};            \
HOObject name##_fn(orbiter::datatype::Function *_func, OObject **argv, OObject *rest, OObject *kwargs, U16 argc)

#endif // !ORBIT_ORBITER_DATATYPE_FUNCTION_H_
