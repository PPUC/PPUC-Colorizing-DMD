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
// Reading never allocates or copies. Any non-const access (operator[], data(), begin(), a
// resize...) first gives the array a buffer of its own if it shares one, as a vector would have
// it all along; so the first write to a uniform block materialises it. A materialised block
// that is all one value again (e.g. after an undo) stays materialised until compact().
//
// Threading, as for std::vector: const access from several threads is safe; non-const access
// must not overlap any other access to the same array object. A copy may be read on another
// thread while the array it was copied from is written: the write goes to a buffer of its own.
// Pointers and references obtained through non-const access stay valid until the array is
// copied, assigned or resized; pointers obtained through const access stay valid until the
// array is next accessed non-const (which may move it to a buffer of its own).
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
        , m_data(other.m_data)
        , m_size(other.m_size)
    {
        other.m_exclusive.store(false, std::memory_order_relaxed);
    }

    CompactArray(CompactArray&& other) noexcept
        : m_block(std::move(other.m_block))
        , m_data(other.m_data)
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
            m_data = other.m_data;
            m_size = other.m_size;
            m_exclusive.store(false, std::memory_order_relaxed);
        }
        return *this;
    }

    CompactArray& operator=(CompactArray&& other) noexcept
    {
        if (this != &other) {
            m_block = std::move(other.m_block);
            m_data = other.m_data;
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
    [[nodiscard]] const T* data() const { return m_data; }
    [[nodiscard]] const T* begin() const { return m_data; }
    [[nodiscard]] const T* end() const { return m_data + m_size; }
    [[nodiscard]] const T* cbegin() const { return m_data; }
    [[nodiscard]] const T* cend() const { return m_data + m_size; }
    [[nodiscard]] const T& operator[](size_type index) const { return m_data[index]; }
    [[nodiscard]] const T& front() const { return m_data[0]; }
    [[nodiscard]] const T& back() const { return m_data[m_size - 1]; }

    [[nodiscard]] const T& at(size_type index) const
    {
        if (index >= m_size) {
            throw std::out_of_range("CompactArray index out of range");
        }
        return m_data[index];
    }

    [[nodiscard]] std::vector<T> toVector() const { return std::vector<T>(begin(), end()); }
    [[nodiscard]] explicit operator std::vector<T>() const { return toVector(); }

    // Whether the array is one of the process-wide uniform buffers.
    [[nodiscard]] bool isSharedUniform() const { return m_block && m_block->uniform; }
    // Whether the array shares its storage with another array (a copy, or a uniform buffer).
    [[nodiscard]] bool sharesStorage() const { return m_block && m_block.use_count() > 1; }
    // The bytes of the buffer this array holds alone; 0 while it shares it (a uniform block or
    // a copy-on-write copy). An estimate for memory accounting, not exact.
    [[nodiscard]] std::size_t ownedBytes() const
    {
        return m_block && !m_block->uniform && m_block.use_count() == 1
                   ? m_block->values.capacity() * sizeof(T)
                   : 0;
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
        const T first = m_data[0];
        if (!std::all_of(m_data + 1, m_data + m_size,
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
        values.resize(count, value);
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
               (lhs.m_data == rhs.m_data || std::equal(lhs.begin(), lhs.end(), rhs.begin()));
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
        std::vector<T> values;
        // One of the process-wide uniform buffers: never written, whoever holds it.
        bool uniform = false;
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
        block->values.assign(count, value);
        block->uniform = true;
        blocks.emplace(key, block);
        bytes += count * sizeof(T);
        return block;
    }

    void setBlock(std::shared_ptr<Block> block, bool exclusive)
    {
        m_block = std::move(block);
        m_data = m_block ? m_block->values.data() : nullptr;
        m_size = m_block ? m_block->values.size() : 0;
        m_exclusive.store(exclusive, std::memory_order_relaxed);
    }

    void adoptDense(std::vector<T> values)
    {
        if (values.empty()) {
            resetToEmpty();
            return;
        }
        auto block = std::make_shared<Block>();
        block->values = std::move(values);
        setBlock(std::move(block), true);
    }

    void resetToEmpty()
    {
        m_block.reset();
        m_data = nullptr;
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
            m_exclusive.store(true, std::memory_order_relaxed);
            return;
        }
        auto block = std::make_shared<Block>();
        block->values.assign(m_data, m_data + m_size);
        setBlock(std::move(block), true);
    }

    T* writableData()
    {
        makeExclusive();
        // The buffer is ours and was created non-const.
        return const_cast<T*>(m_data);
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
        m_data = m_block->values.data();
        m_size = m_block->values.size();
    }

    std::shared_ptr<Block> m_block;
    const T* m_data = nullptr;
    size_type m_size = 0;
    // Known to be the only holder of m_block, so writes need no check of the use count.
    mutable std::atomic<bool> m_exclusive{false};
};

}  // namespace colorizing::document
