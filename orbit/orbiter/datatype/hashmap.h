// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_HASHMAP_H_
#define ORBIT_ORBITER_DATATYPE_HASHMAP_H_

#include <functional>
#include <cstring>

#include <orbit/orbiter/memory/iallocator.h>
#include <orbit/orbiter/datatype/oobject.h>

namespace orbiter::datatype {
    template<typename K, typename V>
    struct HEntry {
        std::atomic_int ref;

        HEntry *next;
        HEntry **prev;

        HEntry *iter_next;
        HEntry *iter_prev;

        K key;
        V value;
    };

    template<
        typename K, typename V,
        auto EqualFn, // bool EqualFn(const K, const K)
        auto HashFn, // size_t HashFn(const K)
        typename Allocator = memory::IsolateAllocator>
    class HashMap {
        static constexpr auto kHashMapInitialSize = 24;
        static constexpr auto kHashMapLoadFactor = 0.75f;
        static constexpr auto kHashMapMulFactor = 2;
        static constexpr auto kHashMapFreeNodeDefault = 1024;

        Allocator allocator_;

    public:
        static constexpr size_t HASH_ERROR = std::numeric_limits<size_t>::max();

        using HEntry = HEntry<K, V>;

        HEntry **map = nullptr;
        HEntry *free_node = nullptr;
        HEntry *iter_begin = nullptr;
        HEntry *iter_end = nullptr;

        size_t capacity = 0;
        size_t length = 0;

        size_t free_count = 0;
        size_t free_max = 0;

        explicit HashMap(Isolate *isolate): allocator_(isolate) {
        }

        bool Initialize(size_t capacity_, size_t free_nodes) {
            this->map = this->allocator_.template alloc<HEntry *>(capacity_ * sizeof(void *));
            if (this->map != nullptr) {
                this->free_node = nullptr;
                this->iter_begin = nullptr;
                this->iter_end = nullptr;

                this->capacity = capacity_;
                this->length = 0;
                this->free_count = 0;
                this->free_max = free_nodes;

                memset(this->map, 0, capacity_ * sizeof(void *));
            }

            return this->map != nullptr;
        }

        bool Initialize(size_t capacity_) {
            return this->Initialize(capacity_, kHashMapFreeNodeDefault);
        }

        bool Initialize() {
            return this->Initialize(kHashMapInitialSize, kHashMapFreeNodeDefault);
        }

        bool Insert(HEntry *entry) {
            if (!this->Resize())
                return false;

            auto index = HashFn(entry->key);
            if (index == HASH_ERROR)
                return false;

            index %= this->capacity;

            auto *slot = this->map[index];

            entry->next = slot;
            entry->prev = this->map + index;

            if (slot != nullptr)
                slot->prev = &entry->next;

            this->map[index] = entry;
            this->length += 1;

            this->AppendIterItem(entry);

            return true;
        }

        bool Lookup(K key, HEntry **entry) const {
            *entry = nullptr;

            auto index = HashFn(key);
            if (index == HASH_ERROR)
                return false;

            index %= this->capacity;

            for (HEntry *cur = this->map[index]; cur != nullptr; cur = cur->next) {
                if (EqualFn(key, cur->key)) {
                    *entry = cur;

                    return true;
                }
            }

            return false;
        }

        bool Remove(K key, HEntry **entry) {
            *entry = nullptr;

            auto index = HashFn(key);
            if (index == HASH_ERROR)
                return false;

            index %= this->capacity;

            for (HEntry *cur = this->map[index]; cur != nullptr; cur = cur->next) {
                if (EqualFn(key, cur->key)) {
                    (*cur->prev) = cur->next;

                    if (cur->next != nullptr)
                        cur->next->prev = cur->prev;

                    this->length -= 1;

                    this->RemoveIterItem(cur);

                    *entry = cur;

                    break;
                }
            }

            return true;
        }

        bool Resize() {
            if ((((float) this->length + 1) / ((float) this->capacity)) < kHashMapLoadFactor)
                return true;

            auto new_cap = this->capacity + ((this->capacity / kHashMapMulFactor) + 1);

            auto *new_map = this->allocator_.template realloc<HEntry *>(this->map, new_cap * sizeof(void *));
            if (new_map == nullptr)
                return false;

            memset(new_map + this->capacity, 0, (new_cap - this->capacity) * sizeof(void *));

            for (auto i = 0; i < this->capacity; i++) {
                for (HEntry *prev = nullptr, *cur = new_map[i], *next; cur != nullptr; cur = next) {
                    auto hash = HashFn(cur->key);

                    hash %= new_cap;
                    next = cur->next;

                    if (hash == i) {
                        prev = cur;
                        continue;
                    }

                    cur->next = new_map[hash];
                    new_map[hash] = cur;
                    if (prev != nullptr)
                        prev->next = next;
                    else
                        new_map[i] = next;
                }
            }

            this->map = new_map;
            this->capacity = new_cap;

            return true;
        }

        HEntry *AllocHEntry() {
            HEntry *ret;

            if (this->free_count > 0) {
                ret = this->free_node;

                this->free_node = ret->next;

                this->free_count -= 1;
            } else {
                ret = this->allocator_.template alloc<HEntry>(sizeof(HEntry));
                if (ret == nullptr)
                    return nullptr;

                memset(ret, 0, sizeof(HEntry));
            }

            ret->ref = 1;

            return ret;
        }

        void AppendIterItem(HEntry *entry) {
            if (this->iter_begin == nullptr) {
                this->iter_begin = entry;
                this->iter_end = entry;
                return;
            }

            entry->iter_next = nullptr;
            entry->iter_prev = this->iter_end;
            this->iter_end->iter_next = entry;
            this->iter_end = entry;
        }

        void Clear(std::function<void(HEntry *)> clear_fn) {
            HEntry *tmp;

            for (HEntry *cur = this->iter_begin; cur != nullptr; cur = tmp) {
                tmp = cur->iter_next;

                if (clear_fn != nullptr)
                    clear_fn(cur);

                this->RemoveIterItem(cur);
                this->FreeHEntry(cur);
            }

            this->length = 0;

            for (auto i = 0; i < this->capacity; i++)
                this->map[i] = nullptr;
        }

        void Finalize(std::function<void(HEntry *)> clear_fn) {
            HEntry *tmp;

            for (HEntry *cur = this->iter_begin; cur != nullptr; cur = tmp) {
                tmp = cur->iter_next;

                if (clear_fn != nullptr)
                    clear_fn(cur);

                this->allocator_.free((void *) cur);
            }

            for (HEntry *cur = this->free_node; cur != nullptr; cur = tmp) {
                tmp = cur->next;
                this->allocator_.free((void *) cur);
            }

            this->allocator_.free(this->map);
        }

        void FreeHEntry(HEntry *entry) {
            if (entry->ref.fetch_sub(1) != 1)
                return;

            entry->key = (K) 0;

            if (this->free_count + 1 > this->free_max) {
                this->allocator_.free((void *) entry);
                return;
            }

            entry->next = this->free_node;
            this->free_node = entry;
            this->free_count += 1;
        }

        void RemoveIterItem(HEntry *entry) {
            if (entry->iter_prev != nullptr)
                entry->iter_prev->iter_next = entry->iter_next;
            else
                this->iter_begin = entry->iter_next;

            if (entry->iter_next != nullptr)
                entry->iter_next->iter_prev = entry->iter_prev;
            else
                this->iter_end = entry->iter_prev;

            entry->iter_next = nullptr;
            entry->iter_prev = nullptr;
        }
    };

    using ORHEntry = HEntry<OObject *, OObject *>;
    using ORHMap = HashMap<
        OObject *,
        OObject *,
        Equal,
        Hash>;

    inline auto HASH_ERROR = ORHMap::HASH_ERROR;
}

#endif // !ORBIT_ORBITER_DATATYPE_HASHMAP_H_
