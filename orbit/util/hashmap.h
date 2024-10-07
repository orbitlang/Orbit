// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_UTIL_HASHMAP_H_
#define ORBIT_UTIL_HASHMAP_H_

#include <atomic>
#include <functional>

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

template
<
    typename K, typename V,
    void*(*Alloc)(size_t),
    void*(*REAlloc)(void *, size_t),
    void(*Free)(void *),
    auto EqualFn, // bool EqualFn(cons K, const K)
    auto HashFn // size_t HashFn(const K)
>
struct HashMap {
    static constexpr auto kHashMapInitialSize = 24;
    static constexpr auto kHashMapLoadFactor = 0.75f;
    static constexpr auto kHashMapMulFactor = 2;
    static constexpr auto kHashMapFreeNodeDefault = 1024;

    static constexpr size_t HASH_ERROR = std::numeric_limits<size_t>::max();

    HEntry<K, V> **map;
    HEntry<K, V> *free_node;
    HEntry<K, V> *iter_begin;
    HEntry<K, V> *iter_end;

    size_t capacity;
    size_t length;

    size_t free_count;
    size_t free_max;

    bool Initialize(size_t capacity_, size_t free_nodes) {
        this->map = (HEntry<K, V> **) Alloc(capacity_ * sizeof(void *));
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

    bool Insert(HEntry<K, V> *entry) {
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

    bool Lookup(K key, HEntry<K, V> **entry) const {
        *entry = nullptr;

        auto index = HashFn(key);
        if (index == HASH_ERROR)
            return false;

        index %= this->capacity;

        for (HEntry<K, V> *cur = this->map[index]; cur != nullptr; cur = cur->next) {
            if (EqualFn(key, cur->key)) {
                *entry = cur;

                return true;
            }
        }

        return false;
    }

    bool Remove(K key, HEntry<K, V> **entry) {
        *entry = nullptr;

        auto index = HashFn(key);
        if (index == HASH_ERROR)
            return false;

        index %= this->capacity;

        for (HEntry<K, V> *cur = this->map[index]; cur != nullptr; cur = cur->next) {
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

        auto *new_map = (HEntry<K, V> **) REAlloc(this->map, new_cap * sizeof(void *));
        if (new_map == nullptr)
            return false;

        memset(new_map + this->capacity, 0, (new_cap - this->capacity) * sizeof(void *));

        for (auto i = 0; i < this->capacity; i++) {
            for (HEntry<K, V> *prev = nullptr, *cur = new_map[i], *next; cur != nullptr; cur = next) {
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

    HEntry<K, V> *AllocHEntry() {
        HEntry<K, V> *ret;

        if (this->free_count > 0) {
            ret = this->free_node;

            this->free_node = ret->next;

            this->free_count -= 1;
        } else {
            ret = (HEntry<K, V> *) Alloc(sizeof(HEntry<K, V>));
            if (ret == nullptr)
                return nullptr;

            memset(ret, 0, sizeof(HEntry<K, V>));
        }

        ret->ref = 1;

        return ret;
    }

    void AppendIterItem(HEntry<K, V> *entry) {
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

    void Clear(std::function<void(HEntry<K, V> *)> clear_fn) {
        HEntry<K, V> *tmp;

        for (HEntry<K, V> *cur = this->iter_begin; cur != nullptr; cur = tmp) {
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

    void Finalize(std::function<void(HEntry<K, V> *)> clear_fn) {
        HEntry<K, V> *tmp;

        for (HEntry<K, V> *cur = this->iter_begin; cur != nullptr; cur = tmp) {
            tmp = cur->iter_next;

            if (clear_fn != nullptr)
                clear_fn(cur);

            Free((void *) cur);
        }

        for (HEntry<K, V> *cur = this->free_node; cur != nullptr; cur = tmp) {
            tmp = cur->next;
            Free((void *) cur);
        }

        Free(this->map);
    }

    void FreeHEntry(HEntry<K, V> *entry) {
        if (entry->ref.fetch_sub(1) != 1)
            return;

        entry->key = (K) 0;

        if (this->free_count + 1 > this->free_max) {
            Free((void *) entry);
            return;
        }

        entry->next = this->free_node;
        this->free_node = entry;
        this->free_count += 1;
    }

    void RemoveIterItem(HEntry<K, V> *entry) {
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

#endif // !ORBIT_UTIL_HASHMAP_H_
