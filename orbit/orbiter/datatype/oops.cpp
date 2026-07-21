// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>
#include <cmath>

#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/datatype/decimal.h>
#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/orstring.h>

using namespace orbiter::datatype;

bool Dispatch(orbiter::Isolate *isolate, const OObject *left, const OObject *right, OObject *&result,
              const char *op_sym, const size_t offset) noexcept {
#define GET_BINARY_OP(s, offset) *((const BinaryFn*)(((unsigned char *) s) + offset))

    if (O_IS_OBJECT(left)) {
        const auto ops = &O_GET_TYPE_OPS(left);
        const auto op = GET_BINARY_OP(ops, offset);

        if (op != nullptr) {
            if (op(left, right, result))
                return true;
        }
    }

    if (O_IS_OBJECT(right)) {
        const auto ops = &O_GET_TYPE_OPS(right);
        const auto op = GET_BINARY_OP(ops, offset);

        if (op != nullptr) {
            if (op(left, right, result))
                return true;
        }
    }

    char lname[24];
    char rname[24];

    GetTypeName(isolate, left, lname, sizeof(lname));
    GetTypeName(isolate, right, rname, sizeof(rname));

    ErrorSet(isolate,
             NotImplementedError::Details[NotImplementedError::Reason::ID],
             nullptr,
             NotImplementedError::Details[NotImplementedError::Reason::OPERATOR],
             op_sym,
             lname,
             rname);

    return false;

#undef GET_BINARY_OP
}

template<orbiter::OPCode opcode>
bool ObjectAddSubMul(orbiter::Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept {
    if (O_IS_SMI(left) && O_IS_SMI(right)) {
        const MSSize na = O_FROM_SMI(left);
        const MSSize nb = O_FROM_SMI(right);

        MSSize res;

        if constexpr (opcode == orbiter::OPCode::ADD)
            res = (MSSize) ((MSize) na + (MSize) nb);
        else if constexpr (opcode == orbiter::OPCode::SUB)
            res = (MSSize) ((MSize) na - (MSize) nb);
        else if constexpr (opcode == orbiter::OPCode::MUL)
            res = (MSSize) ((MSize) na * (MSize) nb);
        else
            assert(false);

        if (res < kSMIMinSize || res > kSMIMaxSize) [[unlikely]] {
            const auto tmp = IntNew(isolate, res);
            if (!tmp)
                return false;

            result = (OObject *) tmp.get();

            return true;
        }

        result = (OObject *) O_TO_SMI(res);

        return true;
    }

    if constexpr (opcode == orbiter::OPCode::ADD)
        return Dispatch(isolate, left, right, result, "+", offsetof(TypeOps, add));
    else if constexpr (opcode == orbiter::OPCode::SUB)
        return Dispatch(isolate, left, right, result, "-", offsetof(TypeOps, sub));
    else if constexpr (opcode == orbiter::OPCode::MUL)
        return Dispatch(isolate, left, right, result, "*", offsetof(TypeOps, mul));
    else
        assert(false);
}

template<orbiter::DivFlags flags, bool is_rem>
bool ObjectDivImpl(orbiter::Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept {
    constexpr bool is_float = (((U8) flags) & ((U8) orbiter::DivFlags::FLOAT)) != 0;

    if (O_IS_SMI(left) && O_IS_SMI(right)) {
        const MSSize na = O_FROM_SMI(left);
        const MSSize nb = O_FROM_SMI(right);

        if (nb == 0) [[unlikely]] {
            ErrorSet(isolate,
                     RuntimeError::Details[RuntimeError::Reason::ID],
                     nullptr,
                     RuntimeError::Details[RuntimeError::Reason::ZERO_DIVISION]);

            return false;
        }

        if constexpr (is_float) {
            const double res = is_rem
                                   ? std::fmod((double) na, (double) nb)
                                   : ((double) na / (double) nb);

            const auto tmp = DecimalNew(isolate, res);
            if (!tmp)
                return false;

            result = (OObject *) tmp.get();

            return true;
        } else {
            result = (OObject *) O_TO_SMI(is_rem ? na % nb : na / nb);

            return true;
        }
    }

    if constexpr (is_float && !is_rem)
        return Dispatch(isolate, left, right, result, "/", offsetof(TypeOps, div));
    else if constexpr (!is_float && !is_rem)
        return Dispatch(isolate, left, right, result, "//", offsetof(TypeOps, idiv));
    else
        return Dispatch(isolate, left, right, result, "%", offsetof(TypeOps, mod));
}

bool ObjectAdd(orbiter::Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept {
    return ObjectAddSubMul<orbiter::OPCode::ADD>(isolate, left, right, result);
}

bool ObjectSub(orbiter::Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept {
    return ObjectAddSubMul<orbiter::OPCode::SUB>(isolate, left, right, result);
}

bool ObjectMul(orbiter::Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept {
    return ObjectAddSubMul<orbiter::OPCode::MUL>(isolate, left, right, result);
}

bool ObjectDiv(orbiter::Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept {
    return ObjectDivImpl<orbiter::DivFlags::FLOAT, false>(isolate, left, right, result);
}

bool ObjectIDiv(orbiter::Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept {
    return ObjectDivImpl<orbiter::DivFlags::NONE, false>(isolate, left, right, result);
}

bool ObjectMod(orbiter::Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept {
    return ObjectDivImpl<orbiter::DivFlags::NONE, true>(isolate, left, right, result);
}

bool ObjectModR(orbiter::Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept {
    return ObjectDivImpl<orbiter::DivFlags::FLOAT, true>(isolate, left, right, result);
}

bool ObjectAnd(orbiter::Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept {
    if (O_IS_SMI(left) && O_IS_SMI(right)) {
        result = (OObject *) ((PtrSize) left & (PtrSize) right);

        return true;
    }

    return Dispatch(isolate, left, right, result, "&", offsetof(TypeOps, bit_and));
}

bool ObjectOr(orbiter::Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept {
    if (O_IS_SMI(left) && O_IS_SMI(right)) {
        result = (OObject *) ((PtrSize) left | (PtrSize) right);

        return true;
    }

    return Dispatch(isolate, left, right, result, "|",offsetof(TypeOps, bit_or));
}

bool ObjectXor(orbiter::Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept {
    if (O_IS_SMI(left) && O_IS_SMI(right)) {
        result = (OObject *) (((PtrSize) left ^ (PtrSize) right) | 0x01);

        return true;
    }

    return Dispatch(isolate, left, right, result, "^",offsetof(TypeOps, bit_xor));
}

bool ObjectLShift(orbiter::Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept {
    if (O_IS_SMI(left) && O_IS_SMI(right)) {
        const MSSize na = O_FROM_SMI(left);
        const MSSize nb = O_FROM_SMI(right);

        if (nb < 0) [[unlikely]] {
            ErrorSet(isolate,
                     RuntimeError::Details[RuntimeError::Reason::ID],
                     nullptr,
                     RuntimeError::Details[RuntimeError::Reason::NEGATIVE_SHIFT_COUNT]);

            return false;
        }

        if (nb >= (MSSize) (sizeof(MSize) * 8)) [[unlikely]] {
            result = (OObject *) O_TO_SMI(0);
            return true;
        }

        const auto res = (MSSize) ((MSize) na << nb);

        if (res < kSMIMinSize || res > kSMIMaxSize) [[unlikely]] {
            const auto tmp = IntNew(isolate, res);
            if (!tmp)
                return false;

            result = (OObject *) tmp.get();

            return true;
        }

        result = (OObject *) O_TO_SMI(res);

        return true;
    }

    return Dispatch(isolate, left, right, result, "<<", offsetof(TypeOps, lshift));
}

bool ObjectRShift(orbiter::Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept {
    if (O_IS_SMI(left) && O_IS_SMI(right)) {
        const MSSize na = O_FROM_SMI(left);
        const MSSize nb = O_FROM_SMI(right);

        if (nb < 0) [[unlikely]] {
            ErrorSet(isolate,
                     RuntimeError::Details[RuntimeError::Reason::ID],
                     nullptr,
                     RuntimeError::Details[RuntimeError::Reason::NEGATIVE_SHIFT_COUNT]);

            return false;
        }

        // Clamp shift amount: nb >= 64 would be UB (undefined behavior); 
        // shifting by 63 provides sign extension (yielding -1 or 0)
        const MSSize shift = nb < (MSSize) (sizeof(MSSize) * 8) ? nb : (MSSize) (sizeof(MSSize) * 8) - 1;

        result = (OObject *) O_TO_SMI(na >> shift);

        return true;
    }

    return Dispatch(isolate, left, right, result, ">>", offsetof(TypeOps, rshift));
}

bool ObjectNeg(orbiter::Isolate *isolate, const OObject *object, OObject *&result) noexcept {
    if (O_IS_SMI(object)) {
        const auto tmp = IntNew(isolate, -O_FROM_SMI(object));
        if (!tmp)
            return false;

        result = (OObject *) tmp.get();

        return true;
    }

    const auto &ops = O_GET_TYPE_OPS(object);
    if (ops.neg != nullptr)
        return ops.neg(object, result);

    ErrorSetWithObjType(isolate,
                        NotImplementedError::Details[NotImplementedError::Reason::ID],
                        NotImplementedError::Details[NotImplementedError::Reason::UNARY_OPERATOR],
                        "-",
                        object);

    return false;
}

bool ObjectMoveNot(orbiter::Isolate *isolate, const OObject *object, OObject *&result) noexcept {
    if (O_IS_SMI(object)) {
        result = (OObject *) O_TO_SMI(~O_FROM_SMI(((PtrSize)object)));

        return true;
    }

    const auto &ops = O_GET_TYPE_OPS(object);
    if (ops.bit_not != nullptr)
        return ops.bit_not(object, result);

    ErrorSetWithObjType(isolate,
                        NotImplementedError::Details[NotImplementedError::Reason::ID],
                        NotImplementedError::Details[NotImplementedError::Reason::UNARY_OPERATOR],
                        "~",
                        object);

    return false;
}

bool ObjectContains(orbiter::Isolate *isolate, const OObject *container, const OObject *value, bool &out) noexcept {
    if (O_IS_OBJECT(container)) {
        const auto &ops = O_GET_TYPE_OPS(container);
        if (ops.contains != nullptr)
            return ops.contains(container, value, out);
    }

    char lname[24];
    char rname[24];

    GetTypeName(isolate, container, lname, sizeof(lname));
    GetTypeName(isolate, value, rname, sizeof(rname));

    ErrorSet(isolate,
             NotImplementedError::Details[NotImplementedError::Reason::ID],
             nullptr,
             NotImplementedError::Details[NotImplementedError::Reason::OPERATOR],
             "in",
             lname,
             rname);

    return false;
}

// *********************************************************************************************************************
// PUBLIC
// *********************************************************************************************************************

int orbiter::datatype::Compare(const OObject *left, const OObject *right, const ComparisonMode mode) {
    auto real_mode = mode;
    auto delta = 0;
    auto ok = false;

    if (left == right && ENUMBITMASK_ISTRUE(mode, ComparisonMode::EQ))
        return 1;

    if (O_IS_OBJECT(left) && O_GET_RC(left).IsInstance()) {
        const auto &ops = O_GET_TYPE_OPS(left);
        if (ops.compare != nullptr)
            ok = ops.compare(left, right, delta);
    } else if (O_IS_OBJECT(right) && O_GET_RC(right).IsInstance()) {
        const auto &ops = O_GET_TYPE_OPS(right);
        if (ops.compare != nullptr) {
            if (ENUMBITMASK_ISTRUE(real_mode, ComparisonMode::LT))
                real_mode = (real_mode & (~ComparisonMode::LT)) | ComparisonMode::GT;
            else
                real_mode = (real_mode & (~ComparisonMode::GT)) | ComparisonMode::LT;

            ok = ops.compare(right, left, delta);
        }
    } else {
        delta = O_FROM_SMI(left) - O_FROM_SMI(right);

        ok = true;
    }

    if (!ok) {
        auto *fiber = Fiber::Current();
        if (fiber->IsPanicking())
            return -1;

        auto *isolate = fiber->isolate;

        // Operator symbol from the caller's point of view (`left op right`),
        // so we use `mode` rather than the internally-swapped `real_mode`.
        const char *op_sym = ENUMBITMASK_ISTRUE(mode, ComparisonMode::EQ) ? "<=" : "<";
        if (ENUMBITMASK_ISTRUE(mode, ComparisonMode::GT))
            op_sym = ENUMBITMASK_ISTRUE(mode, ComparisonMode::EQ) ? ">=" : ">";

        char lname[24];
        char rname[24];

        GetTypeName(isolate, left, lname, sizeof(lname));
        GetTypeName(isolate, right, rname, sizeof(rname));

        ErrorSet(isolate,
                 NotImplementedError::Details[NotImplementedError::Reason::ID],
                 nullptr,
                 NotImplementedError::Details[NotImplementedError::Reason::OPERATOR],
                 op_sym,
                 lname,
                 rname);

        return -1;
    }

    const bool eq = ENUMBITMASK_ISTRUE(real_mode, ComparisonMode::EQ);

    if (ENUMBITMASK_ISTRUE(real_mode, ComparisonMode::LT))
        return (eq ? delta <= 0 : delta < 0) ? 1 : 0;

    return (eq ? delta >= 0 : delta > 0) ? 1 : 0;
}

bool orbiter::datatype::Equal(const OObject *left, const OObject *right, bool &out) {
    if (left == right) {
        out = true;

        return true;
    }

    if (!O_IS_OBJECT(left) && !O_IS_OBJECT(right)) {
        out = false;

        return true;
    }

    if (left == kOddBallNIL || right == kOddBallNIL) {
        out = false;

        return true;
    }

    if (O_IS_OBJECT(left) && O_GET_RC(left).IsInstance()) {
        const auto &ops = O_GET_TYPE_OPS(left);
        if (ops.equal != nullptr)
            return ops.equal(left, right, out);
    }

    if (O_IS_OBJECT(right) && O_GET_RC(right).IsInstance()) {
        const auto &ops = O_GET_TYPE_OPS(right);
        if (ops.equal != nullptr)
            return ops.equal(right, left, out);
    }

    out = false;

    return true;
}

bool orbiter::datatype::EqualStrict(const OObject *left, const OObject *right, bool &out) {
    if (left == right) {
        out = true;

        return true;
    }

    if (O_IS_OBJECT(left) && O_IS_OBJECT(right)) {
        if (O_GET_TYPE(left) != O_GET_TYPE(right)) {
            out = false;

            return true;
        }

        return Equal(left, right, out);
    }

    out = false;

    return true;
}

bool orbiter::datatype::IsTrue(const OObject *object) {
    if (O_IS_SMI(object))
        return O_FROM_SMI(object);

    if (O_IS_FALSE(object) || O_IS_NIL(object))
        return false;

    if (O_IS_TRUE(object))
        return true;

    const auto &ops = O_GET_TYPE_OPS(object);
    if (ops.to_bool != nullptr)
        return ops.to_bool(object);

    return false;
}

HOObject orbiter::datatype::ObjectContains(Isolate *isolate, const OObject *container, const OObject *value) noexcept {
    bool result;

    ::ObjectContains(isolate, container, value, result);

    return HOObject((OObject *) BOOL_TO_OBOOL(result));
}

HOObject orbiter::datatype::ObjectAdd(Isolate *isolate, const OObject *left, const OObject *right) noexcept {
    OObject *result = nullptr;

    ::ObjectAdd(isolate, left, right, result);

    return HOObject(result);
}

HOObject orbiter::datatype::ObjectSub(Isolate *isolate, const OObject *left, const OObject *right) noexcept {
    OObject *result = nullptr;

    ::ObjectSub(isolate, left, right, result);

    return HOObject(result);
}

HOObject orbiter::datatype::ObjectMul(Isolate *isolate, const OObject *left, const OObject *right) noexcept {
    OObject *result = nullptr;

    ::ObjectMul(isolate, left, right, result);

    return HOObject(result);
}

HOObject orbiter::datatype::ObjectDiv(Isolate *isolate, const OObject *left, const OObject *right,
                                      const bool fdiv) noexcept {
    OObject *result = nullptr;

    if (fdiv)
        ::ObjectDiv(isolate, left, right, result);
    else
        ::ObjectIDiv(isolate, left, right, result);

    return HOObject(result);
}

HOObject orbiter::datatype::ObjectMod(Isolate *isolate, const OObject *left, const OObject *right,
                                      const bool fmod) noexcept {
    OObject *result = nullptr;

    if (fmod)
        ::ObjectModR(isolate, left, right, result);
    else
        ::ObjectMod(isolate, left, right, result);

    return HOObject(result);
}

HOObject orbiter::datatype::ObjectAnd(Isolate *isolate, const OObject *left, const OObject *right) noexcept {
    OObject *result = nullptr;

    ::ObjectAnd(isolate, left, right, result);

    return HOObject(result);
}

HOObject orbiter::datatype::ObjectOr(Isolate *isolate, const OObject *left, const OObject *right) noexcept {
    OObject *result = nullptr;

    ::ObjectOr(isolate, left, right, result);

    return HOObject(result);
}

HOObject orbiter::datatype::ObjectXor(Isolate *isolate, const OObject *left, const OObject *right) noexcept {
    OObject *result = nullptr;

    ::ObjectXor(isolate, left, right, result);

    return HOObject(result);
}

HOObject orbiter::datatype::ObjectLShift(Isolate *isolate, const OObject *left, const OObject *right) noexcept {
    OObject *result = nullptr;

    ::ObjectLShift(isolate, left, right, result);

    return HOObject(result);
}

HOObject orbiter::datatype::ObjectRShift(Isolate *isolate, const OObject *left, const OObject *right) noexcept {
    OObject *result = nullptr;

    ::ObjectRShift(isolate, left, right, result);

    return HOObject(result);
}

HOObject orbiter::datatype::ToString(Isolate *isolate, const OObject *object) noexcept {
    if (O_IS_SMI(object))
        return HOObject(ORStringFormat(isolate, "%lld", O_FROM_SMI(object)));

    if (O_IS_FALSE(object))
        return HOObject(ORStringIntern(isolate, "false"));

    if (O_IS_TRUE(object))
        return HOObject(ORStringIntern(isolate, "true"));

    if (O_IS_NIL(object))
        return HOObject(ORStringIntern(isolate, "nil"));

    if (O_IS_OBJECT(object) && O_GET_RC(object).IsInstance()) {
        const auto &ops = O_GET_TYPE_OPS(object);
        if (ops.to_string != nullptr) {
            auto result = ops.to_string(isolate, object);
            if (result == nullptr)
                return {};

            // Non-null but not a String: the type's to_string broke its contract.
            if (!O_IS_OBJECT(result) || !O_IS_TYPE(result, InstanceType::STRING)) {
                char src_name[24];
                char res_name[24];

                GetTypeName(isolate, object, src_name, sizeof(src_name));
                GetTypeName(isolate, result, res_name, sizeof(res_name));

                ErrorSet(isolate,
                         TypeError::Details[TypeError::Reason::ID],
                         nullptr,
                         "str() of '%s' returned non-string (type '%s')",
                         src_name, res_name);

                return {};
            }

            return HOObject(result);
        }
    }

    // Fallback: <TypeName at 0xADDR>
    char type_name[24];

    GetTypeName(isolate, object, type_name, sizeof(type_name));

    return HOObject(ORStringFormat(isolate, "<%s at %p>", type_name, object));
}

HOObject orbiter::datatype::Repr(Isolate *isolate, const OObject *object) noexcept {
    if (O_IS_OBJECT(object) && O_GET_RC(object).IsInstance()) {
        const auto &ops = O_GET_TYPE_OPS(object);
        if (ops.to_repr != nullptr) {
            auto result = ops.to_repr(isolate, object);
            if (result == nullptr)
                return {};

            // Non-null but not a String: the type's to_repr broke its contract.
            if (!O_IS_OBJECT(result) || !O_IS_TYPE(result, InstanceType::STRING)) {
                char src_name[24];
                char res_name[24];

                GetTypeName(isolate, object, src_name, sizeof(src_name));
                GetTypeName(isolate, result, res_name, sizeof(res_name));

                ErrorSet(isolate,
                         TypeError::Details[TypeError::Reason::ID],
                         nullptr,
                         "repr() of '%s' returned non-string (type '%s')",
                         src_name, res_name);

                return {};
            }

            return HOObject(result);
        }
    }

    return ToString(isolate, object);
}

MSize orbiter::datatype::Hash(const OObject *obj) {
    if (O_IS_SMI(obj) || O_IS_ODDBALL(obj))
        return O_FROM_SMI(obj);

    if (O_GET_RC(obj).IsInstance()) {
        const auto &ops = O_GET_TYPE_OPS(obj);
        if (ops.hash != nullptr) {
            const auto hash = ops.hash(obj);
            if (hash == HASH_ERROR) {
                ErrorSetWithObjType(O_GET_ISOLATE(obj),
                                    TypeError::Details[TypeError::Reason::ID],
                                    TypeError::Details[TypeError::Reason::UNHASHABLE],
                                    nullptr,
                                    obj);
            }

            return hash;
        }

        // No custom hash: identity-hash is safe only when equality is also identity.
        // A type that overrides `equal` without providing a compatible `hash` would
        // silently break the `a == b ⇒ hash(a) == hash(b)` invariant (e.g. a mutable
        // List with structural equality): mark it as unhashable.
        if (ops.equal != nullptr) {
            ErrorSetWithObjType(O_GET_ISOLATE(obj),
                                TypeError::Details[TypeError::Reason::ID],
                                TypeError::Details[TypeError::Reason::UNHASHABLE],
                                nullptr,
                                obj);

            return HASH_ERROR;
        }
    }

    return (MSSize) obj;
}
