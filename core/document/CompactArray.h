#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace colorizing::document {

namespace compact_storage {
// LZ4 in its fast mode, as libserum packs its sparse vectors for playback.
[[nodiscard]] std::vector<char> pack(const void* bytes, std::size_t size);
// Fills exactly size bytes; false for damaged data.
[[nodiscard]] bool unpack(const std::vector<char>& packed, void* bytes, std::size_t size);
}  // namespace compact_storage

// The array type of the document's blocks (pixel planes, masks, color tables, rotation tables):
// the interface of a std::vector, with storage that is shared between copies and copied only
// when one of them is written (copy on write).
//
// An array that holds one value throughout ("uniform", e.g. a dynamic mask of 255, a background
// mask of 1 or of 0, a rotation table of 0) shares one buffer with every other uniform array of
// the same size and value in the process. Such blocks are the rule in real projects (most
// frames have no dynamic zone, no background, no rotation), so they cost no memory per frame.
// "Default" is never assumed: the array holds exactly the value it was given, so a block a
// file has as all 0 and one it has as all 1 are both kept and written back as they were.
//
// Any non-const access (operator[], data(), begin(), a resize...) first gives the array a buffer
// of its own if it shares one, as a vector would have it all along; so the first write to a
// uniform block materialises it. A materialised block that is all one value again (e.g. after
// an undo) stays materialised until compact().
//
// Arrays with the same values can share one buffer too (shareStorage(); the document does that
// for identical blocks), and a buffer can be packed (packStorage(): LZ4): its values are let
// go, and the first read unpacks them again and lets go of the packed bytes. So reading may
// allocate: the first read of a packed buffer.
//
// Threading, as for std::vector: const access from several threads is safe (unpacking
// included); non-const access must not overlap any other access to the same array object. A
// copy may be read on another thread while the array it was copied from is written: the write
// goes to a buffer of its own. Pointers and references obtained through non-const access stay
// valid until the array is copied, assigned, resized or packed; pointers obtained through
// const access stay valid until the array, or an array sharing its buffer, is next accessed
// non-const (which may move it to a buffer of its own) or packed. packStorage() must therefore
// only be called while nothing else reads the buffer: see ProjectDocument::packBlocks().
template <typename T>
class CompactArray {
    static_assert(std::is_arithmetic_v<T>, "CompactArray holds plain values only");

public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = T*;
    using const_iterator = const T*;

    CompactArray() = default;

    CompactArray(size_type count, const T& value)
    {
        assign(count, value);
    }

    explicit CompactArray(size_type count)
    {
        assign(count, T{});
    }

    CompactArray(std::initializer_list<T> values)
    {
        adopt(std::vector<T>(values));
    }

    // Implicit, as the record types are aggregates that tests and importers fill with vectors.
    CompactArray(std::vector<T> values)
    {
        adopt(std::move(values));
    }

    template <typename InputIt,
              typename = std::enable_if_t<!std::is_integral_v<InputIt>>>
    CompactArray(InputIt first, InputIt last)
    {
        adopt(std::vector<T>(first, last));
    }

    CompactArray(const CompactArray& other)
        : m_block(other.m_block)
        , m_size(other.m_size)
    {
        other.m_exclusive.store(false, std::memory_order_relaxed);
    }

    CompactArray(CompactArray&& other) noexcept
        : m_block(std::move(other.m_block))
        , m_size(other.m_size)
    {
        m_exclusive.store(other.m_exclusive.load(std::memory_order_relaxed),
                          std::memory_order_relaxed);
        other.resetToEmpty();
    }

    CompactArray& operator=(const CompactArray& other)
    {
        if (this != &other) {
            other.m_exclusive.store(false, std::memory_order_relaxed);
            m_block = other.m_block;
            m_size = other.m_size;
            m_exclusive.store(false, std::memory_order_relaxed);
        }
        return *this;
    }

    CompactArray& operator=(CompactArray&& other) noexcept
    {
        if (this != &other) {
            m_block = std::move(other.m_block);
            m_size = other.m_size;
            m_exclusive.store(other.m_exclusive.load(std::memory_order_relaxed),
                              std::memory_order_relaxed);
            other.resetToEmpty();
        }
        return *this;
    }

    CompactArray& operator=(std::vector<T> values)
    {
        adopt(std::move(values));
        return *this;
    }

    CompactArray& operator=(std::initializer_list<T> values)
    {
        adopt(std::vector<T>(values));
        return *this;
    }

    ~CompactArray() = default;

    // ---- reading

    [[nodiscard]] size_type size() const { return m_size; }
    [[nodiscard]] bool empty() const { return m_size == 0; }
    [[nodiscard]] const T* data() const { return m_block ? m_block->read() : nullptr; }
    [[nodiscard]] const T* begin() const { return data(); }
    [[nodiscard]] const T* end() const { return data() + m_size; }
    [[nodiscard]] const T* cbegin() const { return data(); }
    [[nodiscard]] const T* cend() const { return data() + m_size; }
    [[nodiscard]] const T& operator[](size_type index) const { return data()[index]; }
    [[nodiscard]] const T& front() const { return data()[0]; }
    [[nodiscard]] const T& back() const { return data()[m_size - 1]; }

    [[nodiscard]] const T& at(size_type index) const
    {
        if (index >= m_size) {
            throw std::out_of_range("CompactArray index out of range");
        }
        return data()[index];
    }

    [[nodiscard]] std::vector<T> toVector() const { return std::vector<T>(begin(), end()); }
    [[nodiscard]] explicit operator std::vector<T>() const { return toVector(); }

    // Whether the array is one of the process-wide uniform buffers.
    [[nodiscard]] bool isSharedUniform() const { return m_block && m_block->uniform; }
    // Whether the array shares its storage with another array (a copy, or a uniform buffer).
    [[nodiscard]] bool sharesStorage() const { return m_block && m_block.use_count() > 1; }
    // The bytes of the buffer this array holds alone (unpacked values and packed bytes); 0
    // while it shares it (a uniform block, a copy-on-write copy or an identical block). An
    // estimate for memory accounting, not exact.
    [[nodiscard]] std::size_t ownedBytes() const
    {
        return m_block && !m_block->uniform && m_block.use_count() == 1 ? m_block->heldBytes()
                                                                         : 0;
    }
    // The buffer, to tell arrays that share one; nullptr for an empty array.
    [[nodiscard]] const void* storage() const { return m_block.get(); }
    // How many arrays share the buffer.
    [[nodiscard]] long storageUseCount() const { return m_block ? m_block.use_count() : 0; }
    // The bytes of the buffer, unpacked values and packed bytes; for memory accounting.
    [[nodiscard]] std::size_t storageBytes() const
    {
        return m_block && !m_block->uniform ? m_block->heldBytes() : 0;
    }
    [[nodiscard]] bool isPacked() const { return m_block && m_block->isPacked(); }

    // Shares the buffer of an array with the same values (as a copy does).
    void shareStorage(const CompactArray& other)
    {
        if (other == *this) {
            *this = other;
        }
    }

    // Packs the buffer and lets go of its values, for every array that shares it; returns the
    // bytes let go. Nothing for a uniform buffer, a packed one, or values that do not pack to
    // less than 90 %. Only while nothing else reads the buffer, see the class comment.
    std::size_t packStorage()
    {
        m_exclusive.store(false, std::memory_order_relaxed);
        return m_block && !m_block->uniform ? m_block->pack() : 0;
    }

    // ---- writing (each first gives the array a buffer of its own)

    [[nodiscard]] T* data() { return writableData(); }
    [[nodiscard]] T* begin() { return writableData(); }
    [[nodiscard]] T* end() { return writableData() + m_size; }
    [[nodiscard]] T& operator[](size_type index) { return writableData()[index]; }
    [[nodiscard]] T& front() { return writableData()[0]; }
    [[nodiscard]] T& back() { return writableData()[m_size - 1]; }

    [[nodiscard]] T& at(size_type index)
    {
        if (index >= m_size) {
            throw std::out_of_range("CompactArray index out of range");
        }
        return writableData()[index];
    }

    void assign(size_type count, const T& value)
    {
        if (std::shared_ptr<Block> shared = uniformBlock(count, value)) {
            setBlock(std::move(shared), false);
            return;
        }
        adoptDense(std::vector<T>(count, value));
    }

    template <typename InputIt,
              typename = std::enable_if_t<!std::is_integral_v<InputIt>>>
    void assign(InputIt first, InputIt last)
    {
        adopt(std::vector<T>(first, last));
    }

    void assign(std::initializer_list<T> values) { adopt(std::vector<T>(values)); }

    // Takes the values over and stores them as a uniform buffer when they are all one value.
    void adopt(std::vector<T> values)
    {
        if (!values.empty() &&
            std::all_of(values.begin() + 1, values.end(),
                        [&values](const T& value) { return value == values.front(); })) {
            if (std::shared_ptr<Block> shared = uniformBlock(values.size(), values.front())) {
                setBlock(std::move(shared), false);
                return;
            }
        }
        adoptDense(std::move(values));
    }

    // The same from a buffer, without an intermediate vector for uniform values.
    void assignValues(const T* values, size_type count)
    {
        if (count > 0 && std::all_of(values + 1, values + count,
                                     [values](const T& value) { return value == values[0]; })) {
            if (std::shared_ptr<Block> shared = uniformBlock(count, values[0])) {
                setBlock(std::move(shared), false);
                return;
            }
        }
        adoptDense(std::vector<T>(values, values + count));
    }

    // Moves a materialised array that holds one value throughout back to the shared buffer.
    void compact()
    {
        if (!m_block || m_block->uniform || m_size == 0) {
            return;
        }
        const T* values = m_block->read();
        const T first = values[0];
        if (!std::all_of(values + 1, values + m_size,
                         [first](const T& value) { return value == first; })) {
            return;
        }
        if (std::shared_ptr<Block> shared = uniformBlock(m_size, first)) {
            setBlock(std::move(shared), false);
        }
    }

    void clear() { resetToEmpty(); }

    void resize(size_type count) { resize(count, T{}); }

    void resize(size_type count, const T& value)
    {
        if (count == m_size) {
            return;
        }
        if (m_size == 0) {
            assign(count, value);
            return;
        }
        std::vector<T>& values = writableVector();
        if (count < values.size()) {
            values.erase(values.begin() + static_cast<std::ptrdiff_t>(count), values.end());
        } else {
            // Grown into a new buffer rather than with vector::resize, which GCC 16 at -O3
            // reports as an overflow it is not.
            std::vector<T> grown(count, value);
            std::copy(values.begin(), values.end(), grown.begin());
            values.swap(grown);
        }
        refreshAfterResize();
    }

    void reserve(size_type count)
    {
        if (count <= m_size) {
            return;
        }
        writableVector().reserve(count);
        refreshAfterResize();
    }

    void push_back(const T& value)
    {
        writableVector().push_back(value);
        refreshAfterResize();
    }

    void pop_back()
    {
        writableVector().pop_back();
        refreshAfterResize();
    }

    void swap(CompactArray& other) noexcept
    {
        CompactArray temporary(std::move(other));
        other = std::move(*this);
        *this = std::move(temporary);
    }

    friend bool operator==(const CompactArray& lhs, const CompactArray& rhs)
    {
        return lhs.m_size == rhs.m_size &&
               (lhs.m_block == rhs.m_block || std::equal(lhs.begin(), lhs.end(), rhs.begin()));
    }
    friend bool operator!=(const CompactArray& lhs, const CompactArray& rhs)
    {
        return !(lhs == rhs);
    }
    friend bool operator==(const CompactArray& lhs, const std::vector<T>& rhs)
    {
        return lhs.m_size == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin());
    }
    friend bool operator!=(const CompactArray& lhs, const std::vector<T>& rhs)
    {
        return !(lhs == rhs);
    }
    friend bool operator==(const std::vector<T>& lhs, const CompactArray& rhs)
    {
        return rhs == lhs;
    }
    friend bool operator!=(const std::vector<T>& lhs, const CompactArray& rhs)
    {
        return !(rhs == lhs);
    }

private:
    struct Block {
        // The values while unpacked, or empty.
        std::vector<T> values;
        // The values packed, while packed; let go when they are unpacked, so that a block is
        // never held twice.
        std::vector<char> packed;
        // values.data() while unpacked, nullptr while packed.
        std::atomic<const T*> unpacked{nullptr};
        std::mutex mutex;
        size_type size = 0;
        // One of the process-wide uniform buffers: never written or packed, whoever holds it.
        bool uniform = false;
        // Values that did not pack well; not tried again until written.
        bool incompressible = false;

        void setValues(std::vector<T> newValues)
        {
            values = std::move(newValues);
            size = values.size();
            packed.clear();
            incompressible = false;
            unpacked.store(values.data(), std::memory_order_release);
        }

        const T* read()
        {
            if (const T* data = unpacked.load(std::memory_order_acquire)) {
                return data;
            }
            const std::lock_guard<std::mutex> lock(mutex);
            if (const T* data = unpacked.load(std::memory_order_acquire)) {
                return data;
            }
            std::vector<T> restored(size);
            if (!compact_storage::unpack(packed, restored.data(), size * sizeof(T))) {
                throw std::runtime_error("a packed block of the project is damaged");
            }
            values = std::move(restored);
            std::vector<char>().swap(packed);
            unpacked.store(values.data(), std::memory_order_release);
            return values.data();
        }

        // For writing: unpacked, and the packed bytes no longer kept.
        const T* readForWriting()
        {
            const T* data = read();
            const std::lock_guard<std::mutex> lock(mutex);
            std::vector<char>().swap(packed);
            incompressible = false;
            return data;
        }

        std::size_t pack()
        {
            const std::lock_guard<std::mutex> lock(mutex);
            const T* data = unpacked.load(std::memory_order_relaxed);
            if (!data || size == 0 || incompressible) {
                return 0;
            }
            std::vector<char> result = compact_storage::pack(data, size * sizeof(T));
            if (result.empty() || result.size() * 10 > size * sizeof(T) * 9) {
                incompressible = true;
                return 0;
            }
            packed = std::move(result);
            const std::size_t freed = values.capacity() * sizeof(T);
            unpacked.store(nullptr, std::memory_order_relaxed);
            std::vector<T>().swap(values);
            return freed;
        }

        [[nodiscard]] bool isPacked() const
        {
            return unpacked.load(std::memory_order_relaxed) == nullptr && size > 0;
        }

        [[nodiscard]] std::size_t heldBytes() const
        {
            return values.capacity() * sizeof(T) + packed.capacity();
        }
    };

    // Uniform buffers are kept for the life of the process, one per size and value. The sizes
    // are those of the record blocks; the number of distinct values is small in practice (0,
    // 1, 255, 0xffff, a few colors), and their memory is capped, beyond which uniform arrays
    // are stored like any other.
    static constexpr size_type kMaxUniformSize = size_type{1} << 16;
    static constexpr std::size_t kMaxUniformBytes = std::size_t{32} << 20;

    static std::shared_ptr<Block> uniformBlock(size_type count, const T& value)
    {
        if (count == 0 || count > kMaxUniformSize) {
            return {};
        }
        static std::mutex mutex;
        static std::map<std::pair<size_type, T>, std::shared_ptr<Block>> blocks;
        static std::size_t bytes = 0;
        const std::lock_guard<std::mutex> lock(mutex);
        const auto key = std::make_pair(count, value);
        const auto found = blocks.find(key);
        if (found != blocks.end()) {
            return found->second;
        }
        if (bytes + count * sizeof(T) > kMaxUniformBytes) {
            return {};
        }
        auto block = std::make_shared<Block>();
        block->setValues(std::vector<T>(count, value));
        block->uniform = true;
        blocks.emplace(key, block);
        bytes += count * sizeof(T);
        return block;
    }

    void setBlock(std::shared_ptr<Block> block, bool exclusive)
    {
        m_block = std::move(block);
        m_size = m_block ? m_block->size : 0;
        m_exclusive.store(exclusive, std::memory_order_relaxed);
    }

    void adoptDense(std::vector<T> values)
    {
        if (values.empty()) {
            resetToEmpty();
            return;
        }
        auto block = std::make_shared<Block>();
        block->setValues(std::move(values));
        setBlock(std::move(block), true);
    }

    void resetToEmpty()
    {
        m_block.reset();
        m_size = 0;
        m_exclusive.store(false, std::memory_order_relaxed);
    }

    // Makes sure this array is the only holder of a buffer it may write.
    void makeExclusive()
    {
        if (m_exclusive.load(std::memory_order_relaxed)) {
            return;
        }
        if (m_block && !m_block->uniform && m_block.use_count() == 1) {
            // The last other holder has let go; its reads happen before our writes.
            std::atomic_thread_fence(std::memory_order_acquire);
            m_block->readForWriting();
            m_exclusive.store(true, std::memory_order_relaxed);
            return;
        }
        const T* values = m_block ? m_block->read() : nullptr;
        auto block = std::make_shared<Block>();
        block->setValues(std::vector<T>(values, values + m_size));
        setBlock(std::move(block), true);
    }

    T* writableData()
    {
        makeExclusive();
        // The buffer is ours, unpacked, and was created non-const.
        return const_cast<T*>(m_block->unpacked.load(std::memory_order_relaxed));
    }

    std::vector<T>& writableVector()
    {
        if (!m_block) {
            setBlock(std::make_shared<Block>(), true);
        }
        makeExclusive();
        return m_block->values;
    }

    void refreshAfterResize()
    {
        m_block->size = m_block->values.size();
        m_block->unpacked.store(m_block->values.data(), std::memory_order_release);
        m_size = m_block->size;
    }

    std::shared_ptr<Block> m_block;
    size_type m_size = 0;
    // Known to be the only holder of m_block, so writes need no check of the use count.
    mutable std::atomic<bool> m_exclusive{false};
};

}  // namespace colorizing::document
