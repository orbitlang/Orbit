// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/datatype/decimal.h>
#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/orstring.h>
#include <orbit/orbiter/datatype/rawptr.h>

#define __FFI_INTERNAL

#include <orbit/orbiter/native/ffi_internal.h>

using namespace orbiter::datatype;
using namespace orbiter::native;

bool orbiter::native::PrepareCall(Isolate *isolate, const NativeFunc *func, ParamInfo *dst, double *fp_dst,
                                  U16 *out_fp_length, OObject **args, const U16 argc) {
    if (argc != func->arity) {
        ErrorSet(isolate,
                 FFIError::Details[FFIError::Reason::ID],
                 nullptr,
                 FFIError::Details[FFIError::Reason::ARITY_MISMATCH],
                 ORSTRING_TO_CSTR(func->name),
                 func->arity,
                 argc
        );

        return false;
    }

    int d_index = 0;
    int fp_index = 0;

    const auto *params = func->params;
    for (auto i = 0; i < func->arity; i++) {
        const auto *param = params + i;

        switch (param->type) {
            case NativeType::BOOL:
            case NativeType::BYTE:
            case NativeType::I8:
            case NativeType::I16:
            case NativeType::I32:
            case NativeType::I64:
            case NativeType::ISIZE:
            case NativeType::U8:
            case NativeType::U16:
            case NativeType::U32:
            case NativeType::U64:
            case NativeType::USIZE:
                if (O_IS_SMI(args[i])) {
                    dst[d_index++].value = (void *) (O_FROM_SMI((PtrSize) args[i]));
                    continue;
                }
            case NativeType::F32:
                if (O_IS_TYPE(args[i], InstanceType::DECIMAL)) {
                    if (fp_dst != nullptr && out_fp_length != nullptr) {
                        if (fp_index + 1 >= *out_fp_length)
                            goto FP_ERROR;

                        *((float *) (fp_dst + fp_index++)) = (float) ((Decimal *) args[i])->value;
                    } else {
                        *((float *) (dst + d_index)->value) = ((float) ((Decimal *) args[i])->value);
                        dst[d_index++].fp_reg = (void *) 1;
                    }
                    continue;
                }
                break;
            case NativeType::F64:
                if (O_IS_TYPE(args[i], InstanceType::DECIMAL)) {
                    if (fp_dst != nullptr && out_fp_length != nullptr) {
                        if (fp_index + 1 >= *out_fp_length)
                            goto FP_ERROR;

                        *(fp_dst + fp_index++) = ((Decimal *) args[i])->value;
                    } else {
                        *((double *) (dst + d_index)->value) = ((Decimal *) args[i])->value;
                        dst[d_index++].fp_reg = (void *) 1;
                    }
                    continue;
                }
                break;
            default:
                break;
        }

        if (param->type == NativeType::PTR && args[i] == nullptr) {
            dst[d_index++].value = nullptr;

            continue;
        }

        const auto to_native = O_IS_OBJECT(args[i]) ? O_GET_TYPE_OPS(args[i]).to_native : nullptr;
        if (to_native == nullptr || !to_native(args[i], &dst[d_index++].value, param->type)) {
            char error[24]{};

            GetTypeName(isolate, args[i], error, sizeof(error));

            ErrorSet(isolate,
                     FFIError::Details[FFIError::Reason::ID],
                     nullptr,
                     FFIError::Details[FFIError::Reason::UNSUPPORTED_TYPE],
                     error,
                     NativeTypeNames[(int) param->type]
            );

            return false;
        }
    }

    if (out_fp_length != nullptr)
        *out_fp_length = fp_index;

    return true;

FP_ERROR:
    ErrorSet(isolate,
             FFIError::Details[FFIError::Reason::ID],
             nullptr,
             FFIError::Details[FFIError::Reason::INVALID_FP_ARITY],
             8
    );

    return false;
}

HOObject orbiter::native::ConvertToOrbitObject(Isolate *isolate, void **result, const NativeType type) {
    switch (type) {
        case NativeType::BOOL:
            return HOObject((OObject *) BOOL_TO_OBOOL((*(int*)result)));
        case NativeType::BYTE:
        case NativeType::I8:
            return HOObject(IntNew(isolate, *((char *) result)));
        case NativeType::I16:
            return HOObject(IntNew(isolate, *((I16 *) result)));
        case NativeType::I32:
            return HOObject(IntNew(isolate, *((I32 *) result)));
        case NativeType::I64:
        case NativeType::ISIZE:
            return HOObject(IntNew(isolate, *((MSSize *) result)));
        case NativeType::U8:
            return HOObject(UIntNew(isolate, *((U8 *) result)));
        case NativeType::U16:
            return HOObject(UIntNew(isolate, *((U16 *) result)));
        case NativeType::U32:
            return HOObject(UIntNew(isolate, *((U32 *) result)));
        case NativeType::U64:
        case NativeType::USIZE:
            return HOObject(UIntNew(isolate, *((MSize *) result)));
        case NativeType::UNIT:
            return HOObject(kOddBallNIL);
        case NativeType::PTR:
            return HOObject(RawPtrNew(isolate, result));
        case NativeType::F32:
            return HOObject(DecimalNew(isolate, *(float *) result));
        case NativeType::F64:
            return HOObject(DecimalNew(isolate, *(double *) result));
    }

    return {};
}
