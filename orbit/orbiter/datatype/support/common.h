// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_SUPPORT_COMMON_H_
#define ORBIT_ORBITER_DATATYPE_SUPPORT_COMMON_H_

#include <orbit/datatype.h>

#include <orbit/orbiter/datatype/list.h>

#include <orbit/orbiter/datatype/support/byteops.h>

namespace orbiter::datatype::support {

    /**
     * @brief Factory-function pointer used by the generic splitters to create per-chunk objects.
     *
     * @tparam T  Concrete datatype produced for each split segment (e.g. ORString).
     *
     * Called with a slice of the source buffer; the factory is responsible for owning the bytes
     * (copying into a fresh container, interning, whatever is appropriate for @p T).
     * Must return an empty handle on failure and set the isolate panic state.
     */
    template<typename T>
    using ChunkFn = Handle<T> (*)(Isolate *isolate, const unsigned char *buffer, MSize length);

    /**
     * @brief Generic split-on-separator: produce a @c List of @p T chunks.
     *
     * The pattern match is exact (non-overlapping), so "aaaa" / "aa" yields ["", "", ""].
     * The result always contains at least one element (the tail remainder), matching
     * Python-style semantics.
     *
     * The list capacity is sized exactly by a pre-count pass, so the append loop never
     * reallocates. When @p maxsplit is negative the split is unbounded; when 0, the whole
     * buffer is returned as a single chunk.
     *
     * @tparam T  Chunk type; combined with @p chunk_new this determines the element type
     *            of the returned list.
     *
     * @param isolate    Owning isolate; passed through to @p chunk_new and to ListNew.
     * @param buffer     Source buffer.
     * @param blen       Length of @p buffer in bytes.
     * @param pattern    Separator pattern. Must not be null.
     * @param plen       Length of @p pattern. Must be > 0 — the caller is expected to
     *                   validate and raise the appropriate error otherwise.
     * @param chunk_new  Factory invoked for every segment, including empty leading/trailing
     *                   segments and the final tail.
     * @param maxsplit   Maximum number of splits to perform (list will contain at most
     *                   @p maxsplit + 1 elements). Negative = unlimited.
     *
     * @return A new HList, empty on allocation/factory failure.
     */
    template<typename T>
    HList Split(Isolate *isolate,
                const unsigned char *buffer, const MSize blen,
                const unsigned char *pattern, const MSize plen,
                const ChunkFn<T> chunk_new,
                const MSSize maxsplit = -1) {
        assert(pattern != nullptr && plen > 0);

        // Pre-count occurrences (capped at maxsplit) so the list is sized exactly
        // and the append loop never reallocates.
        const MSize occurrences = Count(buffer, blen, pattern, plen, maxsplit);

        auto list = ListNew(isolate, occurrences + 1);
        if (!list)
            return {};

        MSize pos = 0;

        for (MSize n = 0; n < occurrences; n++) {
            const MSSize idx = Search(buffer + pos, blen - pos, pattern, plen);
            assert(idx >= 0); // guaranteed by the pre-count above

            const auto part_len = (MSize) idx;

            auto part = chunk_new(isolate, buffer + pos, part_len);
            if (!part)
                return {};

            if (!ListAppend(list.get(), (OObject *) part.get()))
                return {};

            pos += part_len + plen;
        }

        // Tail remainder (always appended — may be empty if buffer ends with the separator)
        auto tail = chunk_new(isolate, buffer + pos, blen - pos);
        if (!tail)
            return {};

        if (!ListAppend(list.get(), (OObject *) tail.get()))
            return {};

        return list;
    }

} // namespace orbiter::datatype::support

#endif // !ORBIT_ORBITER_DATATYPE_SUPPORT_COMMON_H_
