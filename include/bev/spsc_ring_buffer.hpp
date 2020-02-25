#pragma once

#include <atomic>
#include <cassert>
#include <utility>

//#include <folly/lang/Align.h>

//#include "dynamic_array.hpp"

namespace q::util {

template <typename T, typename allocator>
using dynamic_array = std::vector<T, allocator>;

// Implements a fixed length single producer, single consumer ring buffer.
template <typename allocator = std::allocator<char>>
class spsc_ring_buffer
{
public:
    explicit spsc_ring_buffer(
        const size_t size, const allocator& alloc = allocator{})
        noexcept(noexcept(decltype(this->m_buf){size, alloc}))
        : m_buf{size, alloc}
        , m_reader{m_buf.data(), size}
        , m_writer{m_buf.data(), size}
        , m_shared{m_buf.data(), size} {}

    [[nodiscard]] const char* data() const noexcept { return m_buf.data(); }

    [[nodiscard]] size_t size() const noexcept { return m_buf.size(); }

    // Try claiming a part of the buffer for writing.
    // size must be greater than 0 and less than or equal to the size of the buffer.
    // A successful call to this function must be followed by a call to commit().
    [[nodiscard]] char* try_claim(const size_t size) noexcept
    {
        assert(size != 0);
        assert(size <= this->size());
        assert(m_writer.write == m_writer.claimed);

        const auto read = m_shared.read.load(std::memory_order_acquire);
        if (m_writer.write >= read)
        {
            if (m_buf.size() - size >= static_cast<size_t>(m_writer.write - m_buf.data()))
            {
                // Enough space left at the back.
                m_writer.claimed = m_writer.write + size;
                return m_writer.write;
            }
            else if (static_cast<size_t>(read - m_buf.data()) > size)
            {
                // Can wrap around because there is enough space at the front.
                m_writer.claimed = m_buf.data() + size;
                return m_buf.data();
            }
            else
                return nullptr;
        }
        else if (static_cast<size_t>(read - m_writer.write) > size)
        {
            // Write must have already wrapped around. And there is enough space until last
            // read position.
            m_writer.claimed = m_writer.write + size;
            return m_writer.write;
        }
        else
            return nullptr;
    }

    void commit() noexcept
    {
        if (const auto claim_len = m_writer.claimed - m_writer.write; claim_len < 0)
        {
            // Wrapped around.
            m_shared.watermark.store(m_writer.write, std::memory_order_relaxed);
        }
        else if (claim_len == 0)
            return;

        m_writer.write = m_writer.claimed;
        m_shared.write.store(m_writer.write, std::memory_order_release);
    }

    [[nodiscard]] std::pair<const char*, const char*> try_read() noexcept
    {
        m_reader.prev_write = m_shared.write.load(std::memory_order_acquire);

        if (m_reader.prev_write >= m_reader.read)
            return {m_reader.read, m_reader.prev_write};

        m_reader.prev_watermark = m_shared.watermark.load(std::memory_order_relaxed);
        if (m_reader.read == m_reader.prev_watermark)
        {
            m_reader.read = m_buf.data();
            return {m_reader.read, m_reader.prev_write};
        }
        else
            return {m_reader.read, m_reader.prev_watermark};
    }

    [[nodiscard]] bool consume(const size_t size) noexcept
    {
        if (const auto readable = m_reader.prev_write - m_reader.read; readable >= 0)
        {
            if (size > static_cast<size_t>(readable))
                return false;

            m_reader.read += size;
        }
        else
        {
            const size_t remaining = m_reader.prev_watermark - m_reader.read;
            if (size > remaining)
                return false;

            if (size == remaining)
                m_reader.read = m_buf.data();
            else
                m_reader.read += size;
        }
        m_shared.read.store(m_reader.read, std::memory_order_release);
        return true;
    }

private:
    struct read_state
    {
        char* read;
        char* prev_write;
        char* prev_watermark;

        read_state(char* const begin, const size_t size) noexcept
            : read{begin}
            , prev_write{begin}
            , prev_watermark{begin + size} {}
    };

    struct write_state
    {
        char* write;
        char* claimed;
        char* watermark;

        write_state(char* const begin, const size_t size) noexcept
            : write{begin}
            , claimed{begin}
            , watermark{begin + size} {}
    };

    struct shared_state
    {
        alignas(128) std::atomic<char*> read;
        alignas(128) std::atomic<char*> write;
        alignas(128) std::atomic<char*> watermark;

        shared_state(char* const data, const size_t size) noexcept
            : read{data}
            , write{data}
            , watermark{data + size} {}
    };

    dynamic_array<char, allocator> m_buf;
    read_state m_reader;
    write_state m_writer;
    shared_state m_shared;
};

spsc_ring_buffer(size_t) -> spsc_ring_buffer<>;

} // namespace q::util
