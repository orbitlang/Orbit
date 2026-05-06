// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_ITERATOR_H_
#define ORBIT_ORBITER_DATATYPE_ITERATOR_H_

#include <orbit/orbiter/datatype/oobject.h>

namespace orbiter::datatype {
    /**
     * @brief Source-specific step function for an Iterator.
     *
     * Reads the iterator's `state` and `source`, optionally compares
     * `snapshot_length` against the source's current length for fail-fast
     * detection of structural changes, and produces the next element.
     *
     * @param self  The iterator instance.
     * @param out   Receives the next element on `CallResult::CONTINUE`.
     *              Untouched on EXHAUST or ERROR.
     *
     * @return CallResult::CONTINUE — `out` holds the next element.
     *         CallResult::EXHAUST  — no more elements.
     *         CallResult::ERROR    — panic state set (e.g. concurrent
     *                                modification of the source).
     */
    using IteratorStepFn = CallResult (*)(struct Iterator *self, OObject **out);

    /**
     * @brief Generic native iterator wrapping a container.
     *
     * Allocated by a container's `get_iter` and consumed via `iter_next` in
     * its TypeOps. The struct is intentionally small and source-agnostic:
     *   - `source`  is a strong reference to the container being iterated;
     *               traced by the GC so the container survives a parked iter.
     *   - `state`   is an inline union whose layout is decided by the source's
     *               `get_iter`; only the matching `step` knows how to read it.
     *   - `snapshot_length` records the source's length at iteration start
     *               for fail-fast detection: if the source resizes, the next
     *               step raises `RuntimeError::CONCURRENT_MODIFICATION`.
     *               Sources that are immutable (Tuple, ORString) leave it 0.
     *   - `step`    is the per-source body. The Iterator's TypeOps.iter_next
     *               simply forwards to it.
     */
    struct Iterator {
        OROBJ_HEAD;

        /// Container being iterated. Strong reference, traced by the GC.
        OObject *source;

        /// Source-specific step function. Set by the source's `get_iter`.
        IteratorStepFn step;

        /// Inline state. Layout chosen by the source's `get_iter` and read
        /// by the matching `step`.
        union {
            /// Linear cursor: List, Tuple element index, or ORString byte offset.
            MSize index;

            /// Chain pointer: Dict / Set HEntry currently in iter list.
            const void *entry;

            /// ORString cursor split between byte position and codepoint count.
            struct {
                MSize byte;
                MSize cp;
            } str;
        } state;

        /// Length of `source` captured at iteration start. Compared by `step`
        /// against the source's current length to detect structural changes.
        /// Set to 0 for immutable sources where the check is unnecessary.
        MSize snapshot_length;

        CallResult last_result;

        /// True if the iterator walks the source in reverse direction.
        /// Set by the source's `get_riter`; left false by `IteratorNew` so
        /// `get_iter` paths inherit the default. Used purely for diagnostics.
        bool reverse;
    };

    using HIterator = Handle<Iterator>;

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
    bool IteratorTypeSetup(TypeInfo *self);

    /**
     * @brief Allocate a new Iterator over the given source.
     *
     * The returned iterator has `source` and `step` set; `state` is zeroed and
     * `snapshot_length` is 0. The caller is expected to populate `state` and
     * `snapshot_length` to match @p step's expectations before exposing the
     * iterator (typically inside the source's `get_iter`).
     *
     * @param isolate  Owning isolate.
     * @param source   The container being iterated. The Iterator keeps a
     *                 strong reference; the GC traces it.
     * @param step     Source-specific step function.
     *
     * @return Handle to the new Iterator, or an empty handle on allocation failure.
     */
    HIterator IteratorNew(Isolate *isolate, OObject *source, IteratorStepFn step);

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
    HOType IteratorTypeInit(Isolate *isolate);
}

#endif // !ORBIT_ORBITER_DATATYPE_ITERATOR_H_
