// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/orstring.h>
#include <orbit/orbiter/datatype/pcheck.h>
#include <orbit/orbiter/datatype/type.h>

using namespace orbiter::datatype;

RUNTIME_METHOD(type_hash, hash,
               R"DOC(
@brief Return the hash value of the object.

For numeric values the hash equals the integer itself.  For heap objects a
type-specific hash function is used when available; otherwise the object's
identity address is used as a fallback.

@return An integer hash value.

@see id

@example
    "hello".hash()   // deterministic hash of the string
    (42).hash()      // 42
)DOC", 1, false, false) {
    auto n = UIntNew(O_GET_ISOLATE(_func), Hash(argv[0]));
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(type_id, id,
               R"DOC(
@brief Return a unique integer identifier for the object.

The identifier is derived from the object's memory address and remains
constant throughout its lifetime.  Two distinct live objects will always
have different identifiers; an identifier may be reused after the object
has been collected.

@return An integer that uniquely identifies the object for its lifetime.

@see hash

@example
    let a = SomeClass()
    let b = SomeClass()
    a.id() == b.id()   // false — distinct objects
    a.id() == a.id()   // true  — same object
)DOC", 1, false, false) {
    auto n = UIntNew(O_GET_ISOLATE(_func), (PtrSize) argv[0]);
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(type_str, str,
               R"DOC(
@brief Return a human-readable string representation of the object.

Calls the type's to_string operation when defined.  Falls back to a default
representation of the form "<TypeName at 0xADDR>" when no to_string is
registered for the type.

@return A String describing the object.

@see repr

@example
    (42).str()         // "42"
    true.str()         // "true"
    [1, 2, 3].str()    // "[1, 2, 3]"
)DOC", 1, false, false) {
    return ToString(O_GET_ISOLATE(_func), argv[0]);
}

RUNTIME_METHOD(type_repr, repr,
               R"DOC(
@brief Return a developer-oriented representation of the object.

Calls the type's to_repr operation when defined.  Falls back to str() when
no to_repr is registered, and ultimately to the default "<TypeName at 0xADDR>"
form.  The repr string is intended for debugging and should ideally be valid
Orbit syntax that reconstructs the value.

@return A String with the debug representation.

@see str

@example
    "hello".repr()    // '"hello"'  (includes quotes)
    [1, 2].repr()     // "[1, 2]"
)DOC", 1, false, false) {
    return Repr(O_GET_ISOLATE(_func), argv[0]);
}

RUNTIME_METHOD(type_type, type,
               R"DOC(
@brief Return the name of the object's runtime type.

@return A String containing the type name (e.g. "String", "List", "Number").

@see is

@example
    "hi".type()        // "String"
    (3.14).type()      // "Decimal"
    [].type()          // "List"
)DOC", 1, false, false) {
    auto *isolate = O_GET_ISOLATE(_func);

    char name[64];
    GetTypeName(isolate, argv[0], name, sizeof(name));

    auto s = ORStringNew(isolate, name);
    if (!s)
        return {};

    return HOObject(std::move(s));
}

RUNTIME_METHOD(type_is, is,
               R"DOC(
@brief Return true if the object is an instance of the given type.

Returns true when the object's type is exactly `t` or when the object's type
extends `t` (i.e. the check is inheritance-aware).  Primitive values such as
integers, booleans, and nil that are not heap-allocated always return false.

@param t  The type to check against.

@return true if the object is an instance of `t`, false otherwise.

@see type

@example
    "hi".is(String)         // true
    (1).is(String)          // false
    MySubClass().is(Base)   // true  (subclass satisfies base check)
)DOC", 2, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("t", false, InstanceType::TYPE));

    PCHECK_CHECK(params);

    // SMIs and oddballs are not heap objects and cannot be class instances.
    if (!O_IS_OBJECT(argv[0]))
        return HOObject((OObject *) kOddBallFALSE);

    const auto *self_type = O_GET_TYPE(argv[0]);
    const auto *target = (TypeInfo *) argv[1];

    return HOObject((OObject *) BOOL_TO_OBOOL(self_type == target || IsTypeExtends(self_type, target)));
}

constexpr FunctionDef type_methods[] = {
    type_hash,
    type_id,
    type_str,
    type_repr,
    type_type,
    type_is,

    FUNCTIONDEF_SENTINEL
};

HOType orbiter::datatype::TypeInit(Isolate *isolate) {
    auto type = MakeType(isolate, nullptr, "Type", InstanceType::TYPE, 0, 6, 0);
    return type;
}

bool orbiter::datatype::TypeSetup(TypeInfo *self) {
    assert(self != nullptr);

    return TIPropertyAdd(self, type_methods, PropertyFlag::IS_PUBLIC);
}
