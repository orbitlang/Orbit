// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_SUPPORT_BYTEOPS_H_
#define ORBIT_ORBITER_DATATYPE_SUPPORT_BYTEOPS_H_

#include <cstring>

#include <orbit/datatype.h>

namespace orbiter::datatype::support {

    namespace detail {
        constexpr MSize kAlphabetSize = 256;

        /// Patterns up to this length use first-byte memchr + memcmp instead of BMH,
        /// avoiding the 256-entry table setup whose cost exceeds the skip benefit.
        constexpr MSize kSearchLinearThreshold = 8;

        /**
         * @brief Fill the bad-character skip table for forward Boyer-Moore-Horspool search.
         *
         * For each byte value c:
         *   skip[c] = plen - 1 - rightmost occurrence of c in pattern[0..plen-2]
         *   skip[c] = plen     if c does not appear in pattern[0..plen-2]
         *
         * pattern[plen-1] is excluded so the minimum shift is always >= 1.
         */
        template<typename T>
        void FillForwardSkip(MSize *skip, const T *pattern, const MSize plen) {
            for (MSize i = 0; i < kAlphabetSize; i++)
                skip[i] = plen;

            for (MSize i = 0; i < plen - 1; i++)
                skip[(unsigned char) pattern[i]] = plen - 1 - i;
        }

        /**
         * @brief Fill the bad-character skip table for reverse Boyer-Moore-Horspool search.
         *
         * For each byte value c:
         *   skip[c] = leftmost i >= 1 where pattern[i] == c
         *   skip[c] = plen  if c does not appear in pattern[1..plen-1]
         *
         * pattern[0] is excluded so the minimum shift is always >= 1.
         */
        template<typename T>
        void FillReverseSkip(MSize *skip, const T *pattern, const MSize plen) {
            for (MSize i = 0; i < kAlphabetSize; i++)
                skip[i] = plen;

            for (MSize i = 1; i < plen; i++) {
                const auto c = (unsigned char) pattern[i];
                if (skip[c] == plen) // keep leftmost (smallest i >= 1)
                    skip[c] = i;
            }
        }
    } // namespace detail

    /**
     * @brief Search for the first (leftmost) occurrence of @p pattern in @p buf.
     *
     * Dispatches to the fastest strategy based on pattern length:
     *   - plen == 1               : memchr
     *   - plen <= linear threshold: first-byte memchr + memcmp (avoids BMH table setup)
     *   - plen >  linear threshold: Boyer-Moore-Horspool
     *
     * @tparam T  Byte-sized element type (sizeof(T) must be 1).
     *
     * @param buf     Buffer to search in.
     * @param blen    Length of the buffer in elements.
     * @param pattern Pattern to search for.
     * @param plen    Length of the pattern in elements.
     *
     * @return Offset of the first match, or -1 if not found.
     *         Returns 0 if @p plen is 0.
     */
    template<typename T>
    MSSize Search(const T *buf, const MSize blen, const T *pattern, const MSize plen) {
        static_assert(sizeof(T) == 1, "Search requires a byte-sized element type");

        if (plen == 0)
            return 0;

        if (plen > blen)
            return -1;

        // Fast path: single byte
        if (plen == 1) {
            const auto *found = (const T *) memchr(buf, (unsigned char) pattern[0], blen);

            return found != nullptr ? (MSSize) (found - buf) : -1;
        }

        // Fast path: short pattern — scan for first byte with memchr, verify rest with memcmp.
        // For plen <= kSearchLinearThreshold the BMH table setup (256 writes) costs more
        // than the skip benefit it provides.
        if (plen <= detail::kSearchLinearThreshold) {
            const auto first = (unsigned char) pattern[0];
            MSize pos = 0;

            while (pos <= blen - plen) {
                const auto *found = (const T *) memchr(buf + pos, first, blen - plen - pos + 1);
                if (found == nullptr)
                    return -1;

                pos = (MSize) (found - buf);

                if (memcmp(found, pattern, plen) == 0)
                    return (MSSize) pos;

                pos++;
            }

            return -1;
        }

        // General case: Boyer-Moore-Horspool
        MSize skip[detail::kAlphabetSize];
        detail::FillForwardSkip(skip, pattern, plen);

        MSize pos = 0;

        while (pos <= blen - plen) {
            MSSize i = (MSSize) plen - 1;

            while (i >= 0 && buf[pos + (MSize) i] == pattern[i])
                i--;

            if (i < 0)
                return (MSSize) pos;

            // Shift using the rightmost character of the current window (standard BMH)
            pos += skip[(unsigned char) buf[pos + plen - 1]];
        }

        return -1;
    }

    /**
     * @brief Search for the last (rightmost) occurrence of @p pattern in @p buf.
     *
     * Dispatches to the fastest strategy based on pattern length:
     *   - plen == 1               : reverse linear scan
     *   - plen <= linear threshold: right-to-left first-byte scan + memcmp
     *   - plen >  linear threshold: reverse Boyer-Moore-Horspool
     *
     * @tparam T  Byte-sized element type (sizeof(T) must be 1).
     *
     * @param buf     Buffer to search in.
     * @param blen    Length of the buffer in elements.
     * @param pattern Pattern to search for.
     * @param plen    Length of the pattern in elements.
     *
     * @return Offset of the last match, or -1 if not found.
     *         Returns @p blen if @p plen is 0.
     */
    template<typename T>
    MSSize RSearch(const T *buf, const MSize blen, const T *pattern, const MSize plen) {
        static_assert(sizeof(T) == 1, "RSearch requires a byte-sized element type");

        if (plen == 0)
            return (MSSize) blen;

        if (plen > blen)
            return -1;

        // Fast path: single byte — plain reverse scan.
        // We avoid memrchr intentionally: it is POSIX-only and not available on all platforms.
        if (plen == 1) {
            const auto target = pattern[0];
            MSSize pos = (MSSize) blen - 1;

            while (pos >= 0) {
                if (buf[pos] == target)
                    return pos;

                pos--;
            }

            return -1;
        }

        // Fast path: short pattern — scan right-to-left filtering on the first byte,
        // then confirm with memcmp. Avoids BMH table setup whose cost exceeds the benefit.
        if (plen <= detail::kSearchLinearThreshold) {
            const auto first = pattern[0];
            auto pos = (MSSize) (blen - plen);

            while (pos >= 0) {
                if (buf[pos] == first && memcmp(buf + pos, pattern, plen) == 0)
                    return pos;

                pos--;
            }

            return -1;
        }

        // General case: reverse Boyer-Moore-Horspool
        MSize skip[detail::kAlphabetSize];
        detail::FillReverseSkip(skip, pattern, plen);

        auto pos = (MSSize) (blen - plen);

        while (pos >= 0) {
            MSize i = 0;

            while (i < plen && buf[(MSize) pos + i] == pattern[i])
                i++;

            if (i == plen)
                return pos;

            // Shift using the leftmost character of the current window (reverse BMH)
            pos -= (MSSize) skip[(unsigned char) buf[(MSize) pos]];
        }

        return -1;
    }

} // namespace orbiter::datatype::support

#endif // !ORBIT_ORBITER_DATATYPE_SUPPORT_BYTEOPS_H_
