// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/rawptr.h>

using namespace orbiter::datatype;

// TODO methods:
/*
*
      func read_i8(): i8
      func read_i32(): i32
      func read_i64(): i64
      func read_f64(): f64
      func read_ptr(): ptr

      func read_bytes(size: i64): Bytes

      func write_i32(value: i32)
      func write_f64(value: f64)

      func offset(bytes: i64): ptr

      func address(): i64      # <--> to_uSize()
      func to_uSize(): uSize

      func is_null(): bool
 */

bool orbiter::datatype::RawPtrTypeSetup(TypeInfo *self) {
    return true;
}

HOType orbiter::datatype::RawPtrTypeInit(Isolate *isolate) {
    auto rawptr = MakeType(isolate, InstanceType::RAWPTR, sizeof(RawPtr) - sizeof(OObject), 0, 0);
    return rawptr;
}

HRawPtr orbiter::datatype::RawPtrNew(Isolate *isolate, void *ptr) {
    auto *rawptr = MakeObject<RawPtr>(isolate, InstanceType::RAWPTR);
    if (rawptr != nullptr)
        rawptr->ptr = ptr;

    O_GC_TRACK_RETURN(isolate, rawptr, false);
}
