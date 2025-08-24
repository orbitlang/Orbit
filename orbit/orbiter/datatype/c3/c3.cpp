// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/datatype/c3/c3.h>

using namespace orbiter::datatype;
using namespace orbiter::datatype::linearization;

HList C3::BuildBasesList(TypeInfo **bases, const U16 length) const noexcept {
    // Convert base types into a list of MROs (Method Resolution Orders) for each base type
    // Format: [[base_type, types in its mro], ...]

    auto ret = ListNew(O_GET_ISOLATE(this->type_), length);
    if (!ret)
        return {};

    for (auto i = 0; i < length; i++) {
        const auto *base = bases[i];

        if (base->i_type != InstanceType::TRAIT)
            assert(false); // FIXME: error

        U16 capacity = 1;

        if (base->mro != nullptr)
            capacity += ((Tuple *) base->mro)->length;

        auto tmp = ListNew(O_GET_ISOLATE(this->type_), capacity);
        if (!tmp)
            return {};

        /*
         * MRO list should contain the trait itself as the first element,
         * this would cause a circular reference!
         * To avoid this, the trait itself is excluded from the MRO list.
         *
         * To perform the calculation, however, it must be included!
         * Therefore, it is added during the generation of the list of base traits.
         */

        ListAppend(tmp.get(), (OObject *) base);

        // ***

        if (base->mro != nullptr)
            ListExtend(tmp.get(), base->mro);

        ListAppend(ret.get(), (OObject *) tmp.get());
    }

    return ret;
}

HTuple C3::CalculateMRO(const List *bases) const noexcept {
    /*
     * Calculate MRO with C3 Linearization
     *
     * T1  T2  T3  T4  T5  T6  T7  T8  T9  ...  TN
     * ^  ^                                       ^
     * |  +---------------------------------------+
     * |                   Tail
     * +--Head
     */

    U32 bases_index = 0;

    auto output = ListNew(O_GET_ISOLATE(this->type_));
    if (!output)
        return {};

    while (bases_index < bases->length) {
        // Get head list
        const auto head_list = (List *) bases->objects[bases_index];

        if (head_list->length == 0) {
            bases_index++;

            continue;
        }

        auto *head = head_list->objects[0];
        bool found = false;

        // Check if head is in the tail of any other list
        for (auto i = 0; i < bases->length && !found; i++) {
            if (bases_index == i)
                continue;

            const auto *tail_list = (const List *) bases->objects[i];

            for (auto j = 1; j < tail_list->length; j++) {
                if (head == tail_list->objects[j]) {
                    found = true;

                    break;
                }
            }
        }

        if (found) {
            bases_index++;

            continue;
        }

        // If the current head is equal to head of another list, REMOVE IT!
        for (auto i = 0; i < bases->length; i++) {
            auto tail_list = ((List *) bases->objects[i]);

            if (bases_index != i && head == tail_list->objects[0])
                ListRemove(tail_list, 0);
        }

        if (!ListAppend(output.get(), head))
            return {};

        ListRemove(head_list, 0);

        bases_index = 0;
    }

    return TupleNewFromList(output);
}

bool C3::BuildMRO(TypeInfo **bases, U16 length) const noexcept {
    memory::IsolateAllocator allocator(O_GET_ISOLATE(this->type_));

    TypeInfo **merge = nullptr;
    const auto *mro = (Tuple *) this->type_->mro;

    if (length == 0)
        return true;

    if (mro != nullptr && mro->length > 0) {
        merge = allocator.alloc<TypeInfo *>((mro->length + length) * sizeof(void *));
        if (merge == nullptr)
            return false; // FIXME: memory error?!

        auto count = 0;
        for (auto i = 0; i < mro->length; i++)
            merge[count++] = (TypeInfo *) mro->objects[i];

        for (auto i = 0; i < length; i++)
            merge[count++] = bases[i];

        bases = merge;
        length = count;
    }

    const auto bases_list = this->BuildBasesList(bases, length);
    if (!bases_list)
        return false;

    auto ret = this->CalculateMRO(bases_list.get());

    if (merge != nullptr)
        allocator.free(merge);

    if (!ret) {
        // FIXME: error
        return false;
    }

    O_DECREF(this->type_->mro);

    this->type_->mro = (OObject *) ret.release();

    return true;
}
