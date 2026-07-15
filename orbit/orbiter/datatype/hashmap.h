// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_HASHMAP_H_
#define ORBIT_ORBITER_DATATYPE_HASHMAP_H_

#include <functional>
#include <type_traits>

#include <orbit/util/hash.h>

#include <orbit/orbiter/memory/iallocator.h>
#include <orbit/orbiter/datatype/oobject.h>

namespace orbiter::datatype {
    enum class LookupResult : U8 {
        OK,
        NOT_FOUND,
        ERROR,
    };

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
        auto EqualFn, // bool EqualFn(const K, const K)  — infallible key equality
                      // bool EqualFn(const K, const K, bool &out) — fallible: the
                      // return value reports success (false ⇒ panic set), the
                      // equality lands in `out`.
        auto HashFn, // size_t HashFn(const K)
        typename Allocator = memory::IsolateAllocator>
    class HashMap {
        /// Adapts both EqualFn signatures to the fallible convention.
        static bool KeyEqual(const K a, const K b, bool &out) {
            if constexpr (std::is_invocable_r_v<bool, decltype(EqualFn), const K, const K, bool &>) {
                return EqualFn(a, b, out);
            } else {
                out = EqualFn(a, b);

                return true;
            }
        }

        static constexpr auto kHashMapInitialSize = 24;
        static constexpr auto kHashMapLoadFactor = 0.75f;
        static constexpr auto kHashMapMulFactor = 2;
        static constexpr auto kHashMapFreeNodeDefault = 1024;

        Allocator allocator_;

    public:
        using HEntry = HEntry<K, V>;

        HEntry **map = nullptr;
        HEntry *free_node = nullptr;
        HEntry *iter_begin = nullptr;
        HEntry *iter_end = nullptr;

        size_t capacity = 0;
        size_t length = 0;

        size_t free_count = 0;
        size_t free_max = 0;

        explicit HashMap(Isolate *isolate) : allocator_(isolate) {
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

        LookupResult Insert(HEntry *entry) {
            if (!this->Resize())
                return LookupResult::ERROR;

            auto index = HashFn(entry->key);
            if (index == HASH_ERROR)
                return LookupResult::ERROR;

            index %= this->capacity;

            auto *slot = this->map[index];

            entry->next = slot;
            entry->prev = this->map + index;

            if (slot != nullptr)
                slot->prev = &entry->next;

            this->map[index] = entry;
            this->length += 1;

            this->AppendIterItem(entry);

            return LookupResult::OK;
        }

        LookupResult Lookup(K key, HEntry **entry) const {
            *entry = nullptr;

            auto index = HashFn(key);
            if (index == HASH_ERROR)
                return LookupResult::ERROR;

            index %= this->capacity;

            for (HEntry *cur = this->map[index]; cur != nullptr; cur = cur->next) {
                bool eq;

                if (!KeyEqual(key, cur->key, eq))
                    return LookupResult::ERROR;

                if (eq) {
                    *entry = cur;

                    return LookupResult::OK;
                }
            }

            return LookupResult::NOT_FOUND;
        }

        template<typename Equal, typename Hash, typename Key>
        LookupResult Lookup(Equal equal, Hash hash, Key key, MSize key_length, HEntry **entry) const {
            *entry = nullptr;

            auto index = hash(key, key_length);
            if (index == HASH_ERROR)
                return LookupResult::ERROR;

            index %= this->capacity;

            for (HEntry *cur = this->map[index]; cur != nullptr; cur = cur->next) {
                if (equal(cur->key, key, key_length)) {
                    *entry = cur;

                    return LookupResult::OK;
                }
            }

            return LookupResult::NOT_FOUND;
        }

        LookupResult Remove(K key, HEntry **entry) {
            *entry = nullptr;

            auto index = HashFn(key);
            if (index == HASH_ERROR)
                return LookupResult::ERROR;

            index %= this->capacity;

            for (HEntry *cur = this->map[index]; cur != nullptr; cur = cur->next) {
                bool eq;

                if (!KeyEqual(key, cur->key, eq))
                    return LookupResult::ERROR;

                if (eq) {
                    (*cur->prev) = cur->next;

                    if (cur->next != nullptr)
                        cur->next->prev = cur->prev;

                    this->length -= 1;

                    this->RemoveIterItem(cur);

                    *entry = cur;

                    return LookupResult::OK;
                }
            }

            return LookupResult::NOT_FOUND;
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
                        if (prev == nullptr)
                            cur->prev = new_map + i;

                        prev = cur;
                        continue;
                    }

                    auto *old_head = new_map[hash];

                    cur->next = old_head;
                    cur->prev = new_map + hash;

                    if (old_head != nullptr)
                        old_head->prev = &cur->next;

                    new_map[hash] = cur;

                    if (prev != nullptr) {
                        prev->next = next;

                        if (next != nullptr)
                            next->prev = &prev->next;
                    } else {
                        new_map[i] = next;

                        if (next != nullptr)
                            next->prev = new_map + i;
                    }
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
}

#endif // !ORBIT_ORBITER_DATATYPE_HASHMAP_H_
