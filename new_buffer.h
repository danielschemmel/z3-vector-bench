#ifndef NEW_BUFFER_H_
#define NEW_BUFFER_H_

#include "util/debug.h"
#include "util/memory_manager.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <iterator>
#include <utility>
#include <memory>
#include <algorithm>

namespace new_buffer_detail {
    // copy_into, move_into and destroy can reasonably be flattened to memcpy by the optimizer, we are just being paranoid here
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 8
#pragma GCC diagnostic push
    // in this context it should be legal to use `std::memcpy` if the target type is trivially move constructible and possibly
    // trivially destructible. GCC 8, however, requires the target type to be (fully) trivially copyable, or it will warn.
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
    template<typename T>
    inline typename std::enable_if<std::is_trivially_move_constructible<typename std::remove_cv<T>::type>::value>::type
    copy_into(T* destination, T const* source, std::size_t count) {
        std::memcpy(destination, source, count * sizeof(T));
    }

    template<typename T>
    inline typename std::enable_if<!std::is_trivially_move_constructible<typename std::remove_cv<T>::type>::value>::type
    copy_into(T* destination, T const* source, std::size_t count) {
        std::uninitialized_copy(source, source + count, destination);
    }

    template<typename T>
    inline typename std::enable_if<std::is_trivially_move_constructible<typename std::remove_cv<T>::type>::value>::type
    move_into(T* destination, T* source, std::size_t count) {
        std::memcpy(destination, source, count * sizeof(T));
    }

    template<typename T>
    inline typename std::enable_if<!std::is_trivially_move_constructible<typename std::remove_cv<T>::type>::value>::type
    move_into(T* destination, T* source, std::size_t count) {
        std::uninitialized_copy(std::make_move_iterator(source), std::make_move_iterator(source + count), destination);
    }

    template<typename T>
    inline typename std::enable_if<std::is_trivially_move_constructible<typename std::remove_cv<T>::type>::value>::type
    move_around(T* destination, T* source, std::size_t count) {
        std::memmove(destination, source, count * sizeof(T));
    }

    template<typename T>
    inline typename std::enable_if<!std::is_trivially_move_constructible<typename std::remove_cv<T>::type>::value>::type
    move_around(T* destination, T* source, std::size_t count) {
        if(destination < source) {
            for(std::size_t i = 0; i < count; ++i) {
                new(destination + i) T(std::move(source[i]));
                source[i].~T();
            }
        } else {
            for(std::size_t i = count; i > 0; ) {
                --i;
                new(destination + i) T(std::move(source[i]));
                source[i].~T();
            }
        }
    }

    template<typename T>
    inline typename std::enable_if<!std::is_trivially_destructible<typename std::remove_cv<T>::type>::value>::type
    destroy(T* begin, T* end)
    noexcept(std::is_nothrow_destructible<typename std::remove_cv<T>::type>::value) {
        using actual_t = typename std::remove_cv<T>::type;
        // std::destroy is C++17 only
        for(; begin < end; ++begin) {
            begin->~actual_t();
        }
    }

    template<typename T>
    inline typename std::enable_if<std::is_trivially_destructible<typename std::remove_cv<T>::type>::value>::type
    destroy(T* /*begin*/, T* /*end*/)
    noexcept {
        // nothing to do
    }
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 8
#pragma GCC diagnostic pop // -Wclass-memaccess
#endif
}

//----------------------------- vector that stores INITIAL_SIZE elements locally -----------------------------//

template<typename T, typename SZ, std::size_t INITIAL_SIZE>
class new_buffer {
    static_assert(std::numeric_limits<SZ>::is_integer, "SZ must be an unsigned integer type of reasonable size");
    static_assert(!std::numeric_limits<SZ>::is_signed, "SZ must be an unsigned integer type of reasonable size");
    static_assert(std::numeric_limits<SZ>::digits >= 8, "SZ must be an unsigned integer type of reasonable size");
    static_assert(std::numeric_limits<SZ>::digits <= std::numeric_limits<std::size_t>::digits, "SZ must be an unsigned integer type of reasonable size");
    static_assert(static_cast<SZ>(INITIAL_SIZE) == INITIAL_SIZE, "INITIAL_SIZE is too large for the chosen size_type SZ");
    static_assert(INITIAL_SIZE > 0, "In this specialization, INITIAL_SIZE must be non-zero");

public:
    using value_type = T;
    using size_type = SZ;
    using difference_type = std::ptrdiff_t;
    using reference = value_type&;
    using const_reference = value_type const&;
    using pointer = value_type*;
    using const_pointer = value_type const*;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    static constexpr const size_type initial_size = static_cast<size_type>(INITIAL_SIZE);

private:
    using initial_buffer_type = typename std::aligned_union<0, value_type[initial_size]>::type;

    pointer m_data = reinterpret_cast<pointer>(&m_initial_buffer);
    size_type m_size = 0;
    size_type m_capacity = initial_size;
    initial_buffer_type m_initial_buffer;

    inline pointer ptr() noexcept { return m_data; }
    inline const_pointer ptr() const noexcept { return m_data; }

    inline size_type next_capacity() const noexcept {
        auto const cap = capacity();
        return cap == 0 ? 2 : (3 * cap + 1) / 2;
        // it may make sense in production to use a more aggressive reallocation policy for prebuffered objects,
        // but we are more interested in a fair comparison at the moment
        // return 2 * capacity();
    }

    template<typename U = value_type>
    typename std::enable_if<std::is_trivially_move_constructible<typename std::remove_cv<U>::type>::value && std::is_trivially_destructible<typename std::remove_cv<U>::type>::value>::type
    reallocate(size_type const new_capacity) {
        SASSERT(new_capacity >= size());
        std::size_t const new_bytesize = static_cast<std::size_t>(new_capacity) * sizeof(value_type);

        if(m_data != reinterpret_cast<pointer>(&m_initial_buffer)) {
            m_data = reinterpret_cast<pointer>(memory::reallocate(m_data, new_bytesize));
        } else {
            pointer const new_buffer = reinterpret_cast<pointer>(memory::allocate(new_bytesize));
            new_buffer_detail::move_into(new_buffer, m_data, m_size);
            m_data = new_buffer;
        }
        m_capacity = new_capacity;
    }

    template<typename U = value_type>
    typename std::enable_if<!std::is_trivially_move_constructible<typename std::remove_cv<U>::type>::value || !std::is_trivially_destructible<typename std::remove_cv<U>::type>::value>::type
    reallocate(size_type const new_capacity) {
        SASSERT(new_capacity >= size());
        std::size_t const new_bytesize = static_cast<std::size_t>(new_capacity) * sizeof(value_type);
        pointer const new_buffer = reinterpret_cast<pointer>(memory::allocate(new_bytesize));

        new_buffer_detail::move_into(new_buffer, m_data, m_size);
        new_buffer_detail::destroy(m_data, m_data + m_size);
        if(m_data != reinterpret_cast<pointer>(&m_initial_buffer)) {
            memory::deallocate(m_data);
        }

        m_data = new_buffer;
        m_capacity = new_capacity;
    }

public:
    constexpr new_buffer() noexcept = default;

    new_buffer(new_buffer const& other) {
        size_type const pos = other.m_size;
        m_size = pos;
        if(pos <= initial_size) {
            m_data = reinterpret_cast<pointer>(&m_initial_buffer);
            m_capacity = initial_size;
        } else {
            m_data = reinterpret_cast<pointer>(memory::allocate(static_cast<std::size_t>(pos) * sizeof(value_type)));
            m_capacity = pos;
        }
        new_buffer_detail::copy_into(m_data, other.m_data, pos);
    }

    new_buffer& operator=(new_buffer const& other) {
        if(this != &other) {
            clear();
            reserve(other.m_size);
            m_size = other.m_size;
            new_buffer_detail::copy_into(m_data, other.m_data, m_size);
        }
        return *this;
    }

    // Depending on initial_size, move construction may still be very expensive
    new_buffer(new_buffer&& other) {
        if(other.m_data != reinterpret_cast<pointer>(&other.m_initial_buffer)) {
            m_data = other.m_data;
            m_size = other.m_size;
            m_capacity = other.m_capacity;
            other.m_data = reinterpret_cast<pointer>(&other.m_initial_buffer);
            other.m_size = 0;
            other.m_size = initial_size;
        } else {
            m_data = reinterpret_cast<pointer>(&m_initial_buffer);
            m_size = other.m_size;
            m_capacity = initial_size;
            new_buffer_detail::move_into(m_data, other.m_data, m_size);
            new_buffer_detail::destroy(other.begin(), other.end()); // would happen during destruction ´other´ anyway
            other.m_size = 0;
        }
    }

    // Depending on initial_size, move assignment may still be very expensive
    new_buffer& operator=(new_buffer&& other) {
        using std::swap;
        if(this != &other) {
            if(other.m_data != reinterpret_cast<pointer>(&other.m_initial_buffer)) {
                if(m_data != reinterpret_cast<pointer>(&m_initial_buffer)) {
                    swap(m_data, other.m_data);
                    swap(m_size, other.m_size);
                    swap(m_capacity, other.m_capacity);
                } else {
                    new_buffer_detail::destroy(begin(), end());
                    m_data = other.m_data;
                    m_size = other.m_size;
                    m_capacity = other.m_capacity;
                    other.m_data = reinterpret_cast<pointer>(&other.m_initial_buffer);
                    other.m_size = 0;
                    other.m_size = initial_size;
                }
            } else {
                new_buffer_detail::destroy(begin(), end());
                m_size = other.m_size;
                new_buffer_detail::move_into(m_data, other.m_data, m_size);
            }
        }
        return *this;
    }

    ~new_buffer() {
        new_buffer_detail::destroy(begin(), end());
        if(m_data != reinterpret_cast<pointer>(&m_initial_buffer)) {
            memory::deallocate(m_data);
        }
    }

    new_buffer(size_type count, value_type const& elem) {
        m_size = count;
        if(count <= initial_size) {
            m_data = reinterpret_cast<pointer>(&m_initial_buffer);
            m_capacity = initial_size;
        } else {
            m_data = reinterpret_cast<pointer>(memory::allocate(static_cast<std::size_t>(count) * sizeof(value_type)));
            m_capacity = count;
        }
        for(size_type i = 0; i < count; ++i) {
            ::new(m_data + i) value_type(elem);
        }
    }

    friend void swap(new_buffer& lhs, new_buffer& rhs) {
        using std::swap;
        if(lhs.m_data != reinterpret_cast<pointer>(&lhs.m_initial_buffer)) {
            if(rhs.m_data != reinterpret_cast<pointer>(&rhs.m_initial_buffer)) {
                swap(lhs.m_data, rhs.m_data);
                swap(lhs.m_size, rhs.m_size);
                swap(lhs.m_capacity, rhs.m_capacity);
            } else {
                new_buffer_detail::move_into(reinterpret_cast<pointer>(&lhs.m_initial_buffer), rhs.m_data, rhs.m_size);
                new_buffer_detail::destroy(rhs.m_data, rhs.m_data + rhs.m_size);
                rhs.m_data = lhs.m_data;
                rhs.m_capacity = rhs.m_capacity;
                lhs.m_data = reinterpret_cast<pointer>(&lhs.m_initial_buffer);
                lhs.m_capacity = initial_size;
            }
        } else {
            if(rhs.m_data != reinterpret_cast<pointer>(&rhs.m_initial_buffer)) {
                new_buffer_detail::move_into(reinterpret_cast<pointer>(&rhs.m_initial_buffer), lhs.m_data, lhs.m_size);
                new_buffer_detail::destroy(lhs.m_data, lhs.m_data + lhs.m_size);
                lhs.m_data = rhs.m_data;
                lhs.m_capacity = lhs.m_capacity;
                rhs.m_data = reinterpret_cast<pointer>(&rhs.m_initial_buffer);
                rhs.m_capacity = initial_size;
            } else {
                // since initial_buffer_type may be large, we don't want to put one on the stack
                // this way we even potentially gain some speed, as we eliminate one O(n) element-wise copy
                SASSERT(lhs.m_capacity == initial_size);
                SASSERT(rhs.m_capacity == initial_size);
                rhs.m_capacity = rhs.next_capacity();
                pointer buffer = reinterpret_cast<pointer>(memory::allocate(static_cast<std::size_t>(rhs.m_capacity) * sizeof(value_type)));
                new_buffer_detail::move_into(buffer, lhs.m_data, lhs.m_size);

                // this could potentially be optimized by considering that we can move-assign to those objects that already exist
                new_buffer_detail::destroy(lhs.m_data, lhs.m_data + lhs.m_size);
                new_buffer_detail::move_into(lhs.m_data, rhs.m_data, rhs.m_size);

                new_buffer_detail::destroy(rhs.m_data, rhs.m_data + rhs.m_size);
                rhs.m_data = buffer;

                swap(lhs.m_size, rhs.m_size);
            }
        }
    }

    // [[nodiscard]] is C++17
    bool empty() const noexcept { return size() == 0; }
    size_type size() const noexcept { return m_size; }
    size_type capacity() const noexcept { return m_capacity; }

    void clear() noexcept {
        new_buffer_detail::destroy(begin(), end());
        m_size = 0;
    }

    void resize(size_type count) {
        reserve(count);
        auto const ptr = this->ptr();
        auto const size = this->size();
        new_buffer_detail::destroy(ptr + count, ptr + size);
        for(size_type i = size; i < count; ++i) {
            ::new(ptr + i) value_type();
        }
        m_size = count;
    }

    void resize(size_type count, value_type const& value) {
        reserve(count);
        auto const ptr = this->ptr();
        auto const size = this->size();
        new_buffer_detail::destroy(ptr + count, ptr + size);
        for(size_type i = size; i < count; ++i) {
            ::new(ptr + i) value_type(value);
        }
        m_size = count;
    }

    void reserve(size_type new_capacity) {
        if(capacity() < new_capacity) {
            reallocate(new_capacity);
        }
    }

    void shrink_to_fit() {
        if(m_size <= initial_size) {
            if(m_data != reinterpret_cast<pointer>(&m_initial_buffer)) {
                new_buffer_detail::move_into(reinterpret_cast<pointer>(&m_initial_buffer), m_data, m_size);
                memory::deallocate(m_data);
                m_data = reinterpret_cast<pointer>(&m_initial_buffer);
            }
        } else {
            if(size() < capacity()) {
                reallocate(m_size);
            }
        }
    }

    reference operator[](size_type index) {
        SASSERT(index < size());
        return ptr()[index];
    }

    const_reference operator[](size_type index) const {
        SASSERT(index < size());
        return ptr()[index];
    }

          iterator           begin()       noexcept { return ptr(); }
    const_iterator           begin() const noexcept { return ptr(); }
    const_iterator          cbegin() const noexcept { return ptr(); }
          iterator           end()         noexcept { return ptr() + size(); }
    const_iterator           end()   const noexcept { return ptr() + size(); }
    const_iterator          cend()   const noexcept { return ptr() + size(); }
          reverse_iterator  rbegin()       noexcept { return static_cast<reverse_iterator>(end()); }
    const_reverse_iterator  rbegin() const noexcept { return static_cast<const_reverse_iterator>(end()); }
    const_reverse_iterator crbegin() const noexcept { return static_cast<const_reverse_iterator>(end()); }
          reverse_iterator  rend()         noexcept { return static_cast<reverse_iterator>(begin()); }
    const_reverse_iterator  rend()   const noexcept { return static_cast<const_reverse_iterator>(begin()); }
    const_reverse_iterator crend()   const noexcept { return static_cast<const_reverse_iterator>(begin()); }
    // interferes with the data member type
    // pointer data() noexcept { return ptr(); }
    // const_pointer data() const noexcept { return ptr(); }

    reference front() { 
        SASSERT(!empty()); 
        return begin()[0]; 
    }

    const_reference front() const { 
        SASSERT(!empty()); 
        return begin()[0]; 
    }

    reference back() { 
        SASSERT(!empty()); 
        return end()[-1]; 
    }

    const_reference back() const { 
        SASSERT(!empty()); 
        return end()[-1]; 
    }

    void push_back(value_type const& value) {
        auto const size = this->size();
        if(size >= capacity()) {
            reallocate(next_capacity());
        }
        ::new(ptr() + size) value_type(value);
        ++m_size;
    }

    void push_back(value_type&& value) {
        auto const size = this->size();
        if(size >= capacity()) {
            reallocate(next_capacity());
        }
        ::new(ptr() + size) value_type(std::move(value));
        ++m_size;
    }

    template<typename... Args>
    void emplace_back(Args&&... args) {
        auto const size = this->size();
        if(size >= capacity()) {
            reallocate(next_capacity());
        }
        ::new(ptr() + size) value_type(std::forward<Args>(args)...);
        ++m_size;
    }

    void pop_back() {
        SASSERT(!empty()); 
        end()->~value_type();
        --m_size;
    }

    // adaptors for the old buffer interface
    using data = value_type;
    void reset() noexcept { clear(); }
    void finalize() { clear(); shrink_to_fit(); }
    reference get(size_type index) { return (*this)[index]; }
    const_reference get(size_type index) const { return (*this)[index]; }
    void set(size_type index, value_type const& value) { (*this)[index] = value; }
    void shrink(size_type count) { resize(count); }
    // set_end // not currently implemented
    pointer c_ptr() const { return m_data; } // breaks logical const-ness, prefer data() [which is disabled due to the data type]

    void append(unsigned n, T const * elems) {
        for (unsigned i = 0; i < n; i++) {
            push_back(elems[i]);
        }
    }

    void append(const new_buffer& source) {
        append(source.size(), source.ptr());
    }
};

//----------------------------- vector that stores everything on the heap -----------------------------//

template<typename T, typename SZ>
class new_buffer<T, SZ, 0> {
    static_assert(std::numeric_limits<SZ>::is_integer, "SZ must be an unsigned integer type of reasonable size");
    static_assert(!std::numeric_limits<SZ>::is_signed, "SZ must be an unsigned integer type of reasonable size");
    static_assert(std::numeric_limits<SZ>::digits >= 8, "SZ must be an unsigned integer type of reasonable size");
    static_assert(std::numeric_limits<SZ>::digits <= std::numeric_limits<std::size_t>::digits, "SZ must be an unsigned integer type of reasonable size");

public:
    using value_type = T;
    using size_type = SZ;
    using difference_type = std::ptrdiff_t;
    using reference = value_type&;
    using const_reference = value_type const&;
    using pointer = value_type*;
    using const_pointer = value_type const*;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    static constexpr const size_type initial_size = 0;

private:
    struct alignas(alignof(value_type)) header_t {
        size_type m_size;
        size_type m_capacity;
    };

    char* m_data = nullptr;

    header_t* header() noexcept {
        SASSERT(m_data);
        return reinterpret_cast<header_t*>(m_data - sizeof(header_t));
    }

    header_t const* header() const noexcept {
        SASSERT(m_data);
        return reinterpret_cast<header_t const*>(m_data - sizeof(header_t));
    }

    // ptr should really be public and called "data"
    pointer ptr() noexcept { return reinterpret_cast<value_type*>(m_data); }
    const_pointer ptr() const noexcept { return reinterpret_cast<value_type*>(m_data); }

    inline size_type next_capacity() const noexcept {
        auto const cap = capacity();
        return cap == 0 ? 2 : (3 * cap + 1) / 2;
    }

    template<typename U = value_type>
    typename std::enable_if<std::is_trivially_move_constructible<typename std::remove_cv<U>::type>::value && std::is_trivially_destructible<typename std::remove_cv<U>::type>::value>::type
    reallocate(size_type const new_capacity) {
        SASSERT(new_capacity >= size());
        SASSERT(new_capacity > 0);
        std::size_t const new_bytesize = sizeof(header_t) + static_cast<std::size_t>(new_capacity) * sizeof(value_type);
        header_t* new_header = nullptr;
        if(m_data == nullptr) { // memory::reallocate does not support realloc(0)
            new_header = reinterpret_cast<header_t*>(memory::allocate(new_bytesize));
            new_header->m_size = 0;
            new_header->m_capacity = new_capacity;
        } else {
            auto old_header = header();
            auto size = old_header->m_size;
            new_header = reinterpret_cast<header_t*>(memory::reallocate(old_header, new_bytesize));
            new_header->m_size = size;
            new_header->m_capacity = new_capacity;
        }
        m_data = reinterpret_cast<char*>(new_header) + sizeof(header_t);
    }

    template<typename U = value_type>
    typename std::enable_if<!std::is_trivially_move_constructible<typename std::remove_cv<U>::type>::value || !std::is_trivially_destructible<typename std::remove_cv<U>::type>::value>::type
    reallocate(size_type const new_capacity) {
        SASSERT(new_capacity >= size());
        SASSERT(new_capacity > 0);
        std::size_t const new_bytesize = sizeof(header_t) + static_cast<std::size_t>(new_capacity) * sizeof(value_type);
        header_t* const new_header = reinterpret_cast<header_t*>(memory::allocate(new_bytesize));
        new_header->m_capacity = new_capacity;
        
        if(m_data == nullptr) {
            new_header->m_size = 0;
        } else {
            size_type const size = header()->m_size;
            new_header->m_size = size;
            new_buffer_detail::move_into(reinterpret_cast<pointer>(reinterpret_cast<char*>(new_header) + sizeof(header_t)), ptr(), size);
            new_buffer_detail::destroy(ptr(), ptr() + size);
            memory::deallocate(header());
        }
        m_data = reinterpret_cast<char*>(new_header) + sizeof(header_t);
    }

public:
    constexpr new_buffer() noexcept = default;

    new_buffer(new_buffer const& other) {
        auto const size = other.size();
        if(size > 0) {
            reallocate(size);
            new_buffer_detail::copy_into(ptr(), other.ptr(), size);
            header()->m_size = size;
        }
    }

    new_buffer& operator=(new_buffer const& other) {
        if(this != &other) {
            clear();
            auto const size = other.size();
            if(size > 0) {
                reallocate(size);
                new_buffer_detail::copy_into(ptr(), other.ptr(), size);
                header()->m_size = size;
            }
        }
        return *this;
    }

    new_buffer(new_buffer&& other) : m_data(other.m_data) { other.m_data = nullptr; }
    new_buffer& operator=(new_buffer&& other) {
        using std::swap;
        swap(m_data, other.m_data);
        return *this;
    }

    friend void swap(new_buffer& lhs, new_buffer& rhs) {
        using std::swap;
        swap(lhs.m_data, rhs.m_data);
    }

    ~new_buffer() {
        if(m_data) {
            new_buffer_detail::destroy(begin(), end());
            memory::deallocate(header());
            m_data = nullptr;
        }
    }

    // FIXME: src/muz/pdr/pdr_context.cpp relies on this constructor being implicit
    /* explicit */ new_buffer(size_type count) { resize(count); }
    new_buffer(size_type count, value_type const& element) { resize(count, element); }
    new_buffer(size_type count, const_pointer elements) {
        if(count > 0) {
            reallocate(count);
            new_buffer_detail::copy_into(ptr(), elements, count);
            header()->m_size = count;
        }
    }

    // [[nodiscard]] is C++17
    bool empty() const noexcept { return size() == 0; }
    size_type size() const noexcept { return m_data ? header()->m_size : 0; }
    size_type capacity() const noexcept { return m_data ? header()->m_capacity : 0; }

    void clear() noexcept {
        if(m_data != nullptr) {
            new_buffer_detail::destroy(ptr(), ptr() + size());
            header()->m_size = 0;
        }
    }

    void resize(size_type count) {
        if(count == 0) {
            clear();
        } else {
            _reserve(count);
            auto const size = this->size();
            new_buffer_detail::destroy(ptr() + count, ptr() + size);
            for(size_type i = size; i < count; ++i) {
                ::new(ptr() + i) value_type();
            }
            header()->m_size = count;
        }
    }

    void resize(size_type count, value_type const& value) {
        if(count == 0) {
            clear();
        } else {
            _reserve(count);
            auto const size = this->size();
            new_buffer_detail::destroy(ptr() + count, ptr() + size);
            for(size_type i = size; i < count; ++i) {
                ::new(ptr() + i) value_type(value);
            }
            header()->m_size = count;
        }
    }

    // vector::reserve actually does an enlarge-only resize as per the old API
private:
    void _reserve(size_type new_capacity) {
        if(new_capacity > capacity()) {
            reallocate(new_capacity);
        }
    }
public:

    void shrink_to_fit() {
        if(size() > 0) {
            if(size() < capacity()) {
                reallocate(size());
            }
        } else {
            if(m_data) {
                new_buffer_detail::destroy(begin(), end());
                memory::deallocate(header());
                m_data = nullptr;
            }
        }
    }

    reference operator[](size_type index) {
        SASSERT(index < size());
        return ptr()[index];
    }

    const_reference operator[](size_type index) const {
        SASSERT(index < size());
        return ptr()[index];
    }

          iterator           begin()       noexcept { return ptr(); }
    const_iterator           begin() const noexcept { return ptr(); }
    const_iterator          cbegin() const noexcept { return ptr(); }
          iterator           end()         noexcept { return ptr() + size(); }
    const_iterator           end()   const noexcept { return ptr() + size(); }
    const_iterator          cend()   const noexcept { return ptr() + size(); }
          reverse_iterator  rbegin()       noexcept { return static_cast<reverse_iterator>(end()); }
    const_reverse_iterator  rbegin() const noexcept { return static_cast<const_reverse_iterator>(end()); }
    const_reverse_iterator crbegin() const noexcept { return static_cast<const_reverse_iterator>(end()); }
          reverse_iterator  rend()         noexcept { return static_cast<reverse_iterator>(begin()); }
    const_reverse_iterator  rend()   const noexcept { return static_cast<const_reverse_iterator>(begin()); }
    const_reverse_iterator crend()   const noexcept { return static_cast<const_reverse_iterator>(begin()); }
    // interferes with the data member type
    // pointer data() noexcept { return ptr(); }
    // const_pointer data() const noexcept { return ptr(); }

    reference front() { 
        SASSERT(!empty()); 
        return begin()[0]; 
    }

    const_reference front() const { 
        SASSERT(!empty()); 
        return begin()[0]; 
    }

    reference back() { 
        SASSERT(!empty()); 
        return end()[-1]; 
    }

    const_reference back() const { 
        SASSERT(!empty()); 
        return end()[-1]; 
    }

    void push_back(value_type const& value) {
        auto const size = this->size();
        if(size >= capacity()) {
            reallocate(next_capacity());
        }
        ::new(ptr() + size) value_type(value);
        ++header()->m_size;
    }

    void push_back(value_type&& value) {
        auto const size = this->size();
        if(size >= capacity()) {
            reallocate(next_capacity());
        }
        ::new(ptr() + size) value_type(std::move(value));
        ++header()->m_size;
    }

    template<typename... Args>
    void emplace_back(Args&&... args) {
        auto const size = this->size();
        if(size >= capacity()) {
            reallocate(next_capacity());
        }
        ::new(ptr() + size) value_type(std::forward<Args>(args)...);
        ++header()->m_size;
    }

    void pop_back() {
        SASSERT(!empty()); 
        end()->~value_type();
        --header()->m_size;
    }

    iterator erase(const_iterator position) {
        SASSERT(position != end());
        auto const index = static_cast<size_type>(position - ptr());
        ptr()[index].~value_type();
        new_buffer_detail::move_around(ptr() + index, ptr() + index + 1, size() - index - 1);
        --header()->m_size;
        return ptr() + index;
    }

    iterator erase(value_type const& element) {
        iterator it = std::find(begin(), end(), element);
        if(it != end()) {
            return erase(it);
        }
        return end();
    }

    // adaptors for the old vector interface
    using data = value_type;
    void reset() noexcept { clear(); }
    void finalize() { clear(); shrink_to_fit(); }
    reference get(size_type index) { return (*this)[index]; }
    const_reference get(size_type index) const { return (*this)[index]; }
    const_reference get(size_type index, value_type const& otherwise) const { return index < size() ? (*this)[index] : otherwise; }
    void set(size_type index, value_type const& value) { (*this)[index] = value; }
    void setx(size_type index, value_type const& value, value_type const& default_value) {
        if(index >= size()) {
            resize(index + 1, default_value);
        }
        (*this)[index] = value;
    }
    bool contains(value_type const& element) const { return std::find(begin(), end(), element) != end(); }
    void reverse() { std::reverse(begin(), end()); }
    void insert(value_type const& element) { push_back(element); }
    void fill(T const & elem) { std::fill(begin(), end(), elem); }
    void fill(unsigned sz, T const & elem) { resize(sz); fill(sz, elem); }
    void shrink(size_type count) {
        SASSERT(count <= size());
        if(m_data != nullptr) {
            new_buffer_detail::destroy(ptr() + count, ptr() + size());
            header()->m_size = count;
        }
    }
    void set_end(iterator it) {
        if(m_data != nullptr) {
            auto const index = static_cast<size_type>(it - ptr());
            new_buffer_detail::destroy(ptr() + index, ptr() + size());
            header()->m_size = index;
        } else {
            SASSERT(it == nullptr);
        }
    }

    // emplace_resize
    template<typename... Args>
    void resize(size_type count, Args const&... values) {
        if(count == 0) {
            clear();
        } else {
            _reserve(count);
            auto const size = this->size();
            new_buffer_detail::destroy(ptr() + count, ptr() + size);
            for(size_type i = size; i < count; ++i) {
                ::new(ptr() + i) value_type(values...);
            }
            header()->m_size = count;
        }
    }

    pointer c_ptr() const noexcept { return m_data ? reinterpret_cast<value_type*>(m_data) : nullptr; } // breaks logical const-ness, prefer data() [which is disabled due to the data type]

    void swap(new_buffer& other) {
        using std::swap;
        swap(*this, other);
    }

    void append(unsigned n, T const * elems) {
        for (unsigned i = 0; i < n; i++) {
            push_back(elems[i]);
        }
    }

    void append(const new_buffer& source) {
        append(source.size(), source.ptr());
    }

    void reserve(size_type count) {
        if(count > size()) {
            resize(count);
        }
    }

    void reserve(size_type count, value_type const& default_element) {
        if(count > size()) {
            resize(count, default_element);
        }
    }
};

//----------------------------- vector that stores its size and capacity locally -----------------------------//
template<typename T, typename SZ>
class new_buffer<T, SZ, -1> {
    static_assert(std::numeric_limits<SZ>::is_integer, "SZ must be an unsigned integer type of reasonable size");
    static_assert(!std::numeric_limits<SZ>::is_signed, "SZ must be an unsigned integer type of reasonable size");
    static_assert(std::numeric_limits<SZ>::digits >= 8, "SZ must be an unsigned integer type of reasonable size");
    static_assert(std::numeric_limits<SZ>::digits <= std::numeric_limits<std::size_t>::digits, "SZ must be an unsigned integer type of reasonable size");

public:
    using value_type = T;
    using size_type = SZ;
    using difference_type = std::ptrdiff_t;
    using reference = value_type&;
    using const_reference = value_type const&;
    using pointer = value_type*;
    using const_pointer = value_type const*;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    static constexpr const size_type initial_size = 0;

private:
    pointer m_data = nullptr;
    size_type m_size = 0;
    size_type m_capacity = 0;

    // ptr should really be public and called "data"
    pointer ptr() noexcept { return m_data; }
    const_pointer ptr() const noexcept { return m_data; }

    inline size_type next_capacity() const noexcept {
        auto const cap = capacity();
        return cap == 0 ? 2 : (3 * cap + 1) / 2;
    }

    template<typename U = value_type>
    typename std::enable_if<std::is_trivially_move_constructible<typename std::remove_cv<U>::type>::value && std::is_trivially_destructible<typename std::remove_cv<U>::type>::value>::type
    reallocate(size_type const new_capacity) {
        SASSERT(new_capacity >= size());
        SASSERT(new_capacity > 0);
        std::size_t const new_bytesize = static_cast<std::size_t>(new_capacity) * sizeof(value_type);
        if(m_data == nullptr) { // memory::reallocate does not support realloc(0)
            m_data = reinterpret_cast<pointer>(memory::allocate(new_bytesize));
            m_capacity = new_capacity;
        } else {
            m_data = reinterpret_cast<pointer>(memory::reallocate(m_data, new_bytesize));
            m_capacity = new_capacity;
        }
    }

    template<typename U = value_type>
    typename std::enable_if<!std::is_trivially_move_constructible<typename std::remove_cv<U>::type>::value || !std::is_trivially_destructible<typename std::remove_cv<U>::type>::value>::type
    reallocate(size_type new_capacity) {
        SASSERT(new_capacity >= size());
        SASSERT(new_capacity > 0);
        std::size_t const new_bytesize = static_cast<std::size_t>(new_capacity) * sizeof(value_type);
        auto const new_data = reinterpret_cast<pointer>(memory::allocate(new_bytesize));
        new_buffer_detail::move_into(new_data, m_data, m_size);
        new_buffer_detail::destroy(m_data, m_data + m_size);
        if(m_data) { // TODO: check if memory::deallocate supports free(nullptr)
            memory::deallocate(m_data);
        }
        m_data = new_data;
        m_capacity = new_capacity;
    }

public:
    constexpr new_buffer() noexcept = default;

    new_buffer(new_buffer const& other) {
        auto const size = other.m_size;
        if(size > 0) {
            reallocate(size);
            new_buffer_detail::copy_into(ptr(), other.ptr(), size);
            m_size = size;
        }
    }

    new_buffer& operator=(new_buffer const& other) {
        if(this != &other) {
            clear();
            auto const size = other.m_size;
            if(size > 0) {
                reallocate(size);
                new_buffer_detail::copy_into(ptr(), other.ptr(), size);
                m_size = size;
            }
        }
        return *this;
    }

    new_buffer(new_buffer&& other) : m_data(other.m_data), m_size(other.m_size), m_capacity(other.m_capacity) {
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_capacity = 0;
    }

    new_buffer& operator=(new_buffer&& other) {
        using std::swap;
        swap(m_data, other.m_data);
        swap(m_size, other.m_size);
        swap(m_capacity, other.m_capacity);
        return *this;
    }

    friend void swap(new_buffer& lhs, new_buffer& rhs) {
        using std::swap;
        swap(lhs.m_data, rhs.m_data);
        swap(lhs.m_size, rhs.m_size);
        swap(lhs.m_capacity, rhs.m_capacity);
    }

    ~new_buffer() {
        if(m_data) { // TODO: find out if memory::deallocate supports free(NULL)
            new_buffer_detail::destroy(begin(), end());
            memory::deallocate(m_data);
            m_data = nullptr;
        }
    }

    // FIXME: src/muz/pdr/pdr_context.cpp relies on this constructor being implicit
    /* explicit */ new_buffer(size_type count) { resize(count); }
    new_buffer(size_type count, value_type const& element) { resize(count, element); }
    new_buffer(size_type count, const_pointer elements) {
        if(count > 0) {
            reallocate(count);
            new_buffer_detail::copy_into(ptr(), elements, count);
            m_size = count;
        }
    }

    // [[nodiscard]] is C++17
    bool empty() const noexcept { return size() == 0; }
    size_type size() const noexcept { return m_size; }
    size_type capacity() const noexcept { return m_capacity; }

    void clear() noexcept {
        new_buffer_detail::destroy(ptr(), ptr() + size());
        m_size = 0;
    }

    void resize(size_type count) {
        _reserve(count);
        new_buffer_detail::destroy(ptr() + count, ptr() + size());
        for(size_type i = size(); i < count; ++i) {
            ::new(ptr() + i) value_type();
        }
        m_size = count;
    }

    void resize(size_type count, value_type const& value) {
        _reserve(count);
        new_buffer_detail::destroy(ptr() + count, ptr() + size());
        for(size_type i = size(); i < count; ++i) {
            ::new(ptr() + i) value_type(value);
        }
        m_size = count;
    }

    // vector::reserve actually does an enlarge-only resize as per the old API
private:
    void _reserve(size_type new_capacity) {
        if(new_capacity > capacity()) {
            reallocate(new_capacity);
        }
    }
public:

    void shrink_to_fit() {
        if(size() > 0) {
            if(size() != capacity()) {
                reallocate(size());
            }
        } else {
            if(m_data) {
                new_buffer_detail::destroy(begin(), end());
                memory::deallocate(m_data);
                m_data = nullptr;
                m_capacity = 0;
            }
        }
    }

    reference operator[](size_type index) {
        SASSERT(index < size());
        return ptr()[index];
    }

    const_reference operator[](size_type index) const {
        SASSERT(index < size());
        return ptr()[index];
    }

          iterator           begin()       noexcept { return ptr(); }
    const_iterator           begin() const noexcept { return ptr(); }
    const_iterator          cbegin() const noexcept { return ptr(); }
          iterator           end()         noexcept { return ptr() + size(); }
    const_iterator           end()   const noexcept { return ptr() + size(); }
    const_iterator          cend()   const noexcept { return ptr() + size(); }
          reverse_iterator  rbegin()       noexcept { return static_cast<reverse_iterator>(end()); }
    const_reverse_iterator  rbegin() const noexcept { return static_cast<const_reverse_iterator>(end()); }
    const_reverse_iterator crbegin() const noexcept { return static_cast<const_reverse_iterator>(end()); }
          reverse_iterator  rend()         noexcept { return static_cast<reverse_iterator>(begin()); }
    const_reverse_iterator  rend()   const noexcept { return static_cast<const_reverse_iterator>(begin()); }
    const_reverse_iterator crend()   const noexcept { return static_cast<const_reverse_iterator>(begin()); }
    // interferes with the data member type
    // pointer data() noexcept { return ptr(); }
    // const_pointer data() const noexcept { return ptr(); }

    reference front() { 
        SASSERT(!empty()); 
        return begin()[0]; 
    }

    const_reference front() const { 
        SASSERT(!empty()); 
        return begin()[0]; 
    }

    reference back() { 
        SASSERT(!empty()); 
        return end()[-1]; 
    }

    const_reference back() const { 
        SASSERT(!empty()); 
        return end()[-1]; 
    }

    void push_back(value_type const& value) {
        if(size() >= capacity()) {
            reallocate(next_capacity());
        }
        ::new(ptr() + size()) value_type(value);
        ++m_size;
    }

    void push_back(value_type&& value) {
        if(size() >= capacity()) {
            reallocate(next_capacity());
        }
        ::new(ptr() + size()) value_type(std::move(value));
        ++m_size;
    }

    template<typename... Args>
    void emplace_back(Args&&... args) {
        if(size() >= capacity()) {
            reallocate(next_capacity());
        }
        ::new(ptr() + size()) value_type(std::forward<Args>(args)...);
        ++m_size;
    }

    void pop_back() {
        SASSERT(!empty()); 
        end()->~value_type();
        --m_size;
    }

    iterator erase(const_iterator position) {
        SASSERT(position != end());
        auto const index = static_cast<size_type>(position - ptr());
        ptr()[index].~value_type();
        new_buffer_detail::move_around(ptr() + index, ptr() + index + 1, size() - index - 1);
        --m_size;
        return ptr() + index;
    }

    iterator erase(value_type const& element) {
        iterator it = std::find(begin(), end(), element);
        if(it != end()) {
            return erase(it);
        }
        return end();
    }

    // adaptors for the old vector interface
    using data = value_type;
    void reset() noexcept { clear(); }
    void finalize() { clear(); shrink_to_fit(); }
    reference get(size_type index) { return (*this)[index]; }
    const_reference get(size_type index) const { return (*this)[index]; }
    const_reference get(size_type index, value_type const& otherwise) const { return index < size() ? (*this)[index] : otherwise; }
    void set(size_type index, value_type const& value) { (*this)[index] = value; }
    void setx(size_type index, value_type const& value, value_type const& default_value) {
        if(index >= size()) {
            resize(index + 1, default_value);
        }
        (*this)[index] = value;
    }
    bool contains(value_type const& element) const { return std::find(begin(), end(), element) != end(); }
    void reverse() { std::reverse(begin(), end()); }
    void insert(value_type const& element) { push_back(element); }
    void fill(T const & elem) { std::fill(begin(), end(), elem); }
    void fill(unsigned sz, T const & elem) { resize(sz); fill(sz, elem); }
    void shrink(size_type count) {
        SASSERT(count <= size());
		new_buffer_detail::destroy(ptr() + count, ptr() + size());
		m_size = count;
    }
    void set_end(iterator it) {
            auto const index = static_cast<size_type>(it - ptr());
		new_buffer_detail::destroy(ptr() + index, ptr() + size());
		m_size = index;
    }

    // emplace_resize
    template<typename... Args>
    void resize(size_type count, Args const&... values) {
        if(count == 0) {
            clear();
        } else {
            _reserve(count);
            auto const size = this->size();
            new_buffer_detail::destroy(ptr() + count, ptr() + size);
            for(size_type i = size; i < count; ++i) {
                ::new(ptr() + i) value_type(values...);
            }
            m_size = count;
        }
    }

    pointer c_ptr() const noexcept { return m_data; } // breaks logical const-ness, prefer data() [which is disabled due to the data type]

    void swap(new_buffer& other) {
        using std::swap;
        swap(*this, other);
    }

    void append(unsigned n, T const * elems) {
        for (unsigned i = 0; i < n; i++) {
            push_back(elems[i]);
        }
    }

    void append(const new_buffer& source) {
        append(source.size(), source.ptr());
    }

    void reserve(size_type count) {
        if(count > size()) {
            resize(count);
        }
    }

    void reserve(size_type count, value_type const& default_element) {
        if(count > size()) {
            resize(count, default_element);
        }
    }
};

//----------------------------- vector that stores its size and capacity locally (allocator aware variant) -----------------------------//
template<typename T, typename SZ>
class new_buffer<T, SZ, -2> {
    static_assert(std::numeric_limits<SZ>::is_integer, "SZ must be an unsigned integer type of reasonable size");
    static_assert(!std::numeric_limits<SZ>::is_signed, "SZ must be an unsigned integer type of reasonable size");
    static_assert(std::numeric_limits<SZ>::digits >= 8, "SZ must be an unsigned integer type of reasonable size");
    static_assert(std::numeric_limits<SZ>::digits <= std::numeric_limits<std::size_t>::digits, "SZ must be an unsigned integer type of reasonable size");

public:
    using value_type = T;
    using size_type = SZ;
    using difference_type = std::ptrdiff_t;
    using reference = value_type&;
    using const_reference = value_type const&;
    using pointer = value_type*;
    using const_pointer = value_type const*;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    static constexpr const size_type initial_size = 0;

private:
    pointer m_data = nullptr;
    size_type m_size = 0;
    size_type m_capacity = 0;

    // ptr should really be public and called "data"
    pointer ptr() noexcept { return m_data; }
    const_pointer ptr() const noexcept { return m_data; }

    inline size_type next_capacity() const noexcept {
        auto const cap = capacity();
        return cap == 0 ? 2 : (3 * cap + 1) / 2;
    }

    template<typename U = value_type>
    typename std::enable_if<std::is_trivially_move_constructible<typename std::remove_cv<U>::type>::value && std::is_trivially_destructible<typename std::remove_cv<U>::type>::value>::type
    reallocate(size_type const new_capacity) {
        SASSERT(new_capacity >= size());
        SASSERT(new_capacity > 0);
        std::size_t const new_bytesize = static_cast<std::size_t>(new_capacity) * sizeof(value_type);
        if(m_data == nullptr) { // memory::reallocate does not support realloc(0)
            std::size_t actual_size;
            m_data = reinterpret_cast<pointer>(memory::allocate(new_bytesize, actual_size));
            m_capacity = actual_size / sizeof(value_type);
        } else {
            std::size_t actual_size;
            m_data = reinterpret_cast<pointer>(memory::reallocate(m_data, new_bytesize, actual_size));
            m_capacity = actual_size / sizeof(value_type);
        }
    }

    template<typename U = value_type>
    typename std::enable_if<!std::is_trivially_move_constructible<typename std::remove_cv<U>::type>::value || !std::is_trivially_destructible<typename std::remove_cv<U>::type>::value>::type
    reallocate(size_type new_capacity) {
        SASSERT(new_capacity >= size());
        SASSERT(new_capacity > 0);
        std::size_t const new_bytesize = static_cast<std::size_t>(new_capacity) * sizeof(value_type);
        std::size_t actual_size;
        auto const new_data = reinterpret_cast<pointer>(memory::allocate(new_bytesize, actual_size));
        new_capacity = static_cast<size_type>(actual_size / sizeof(value_type));
        new_buffer_detail::move_into(new_data, m_data, m_size);
        new_buffer_detail::destroy(m_data, m_data + m_size);
        if(m_data) { // TODO: check if memory::deallocate supports free(nullptr)
            memory::deallocate(m_data, m_capacity * sizeof(value_type));
        }
        m_data = new_data;
        m_capacity = new_capacity;
    }

public:
    constexpr new_buffer() noexcept = default;

    new_buffer(new_buffer const& other) {
        auto const size = other.m_size;
        if(size > 0) {
            reallocate(size);
            new_buffer_detail::copy_into(ptr(), other.ptr(), size);
            m_size = size;
        }
    }

    new_buffer& operator=(new_buffer const& other) {
        if(this != &other) {
            clear();
            auto const size = other.m_size;
            if(size > 0) {
                reallocate(size);
                new_buffer_detail::copy_into(ptr(), other.ptr(), size);
                m_size = size;
            }
        }
        return *this;
    }

    new_buffer(new_buffer&& other) : m_data(other.m_data), m_size(other.m_size), m_capacity(other.m_capacity) {
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_capacity = 0;
    }

    new_buffer& operator=(new_buffer&& other) {
        using std::swap;
        swap(m_data, other.m_data);
        swap(m_size, other.m_size);
        swap(m_capacity, other.m_capacity);
        return *this;
    }

    friend void swap(new_buffer& lhs, new_buffer& rhs) {
        using std::swap;
        swap(lhs.m_data, rhs.m_data);
        swap(lhs.m_size, rhs.m_size);
        swap(lhs.m_capacity, rhs.m_capacity);
    }

    ~new_buffer() {
        if(m_data) { // TODO: find out if memory::deallocate supports free(NULL)
            new_buffer_detail::destroy(begin(), end());
            memory::deallocate(m_data, m_capacity * sizeof(value_type));
            m_data = nullptr;
        }
    }

    // FIXME: src/muz/pdr/pdr_context.cpp relies on this constructor being implicit
    /* explicit */ new_buffer(size_type count) { resize(count); }
    new_buffer(size_type count, value_type const& element) { resize(count, element); }
    new_buffer(size_type count, const_pointer elements) {
        if(count > 0) {
            reallocate(count);
            new_buffer_detail::copy_into(ptr(), elements, count);
            m_size = count;
        }
    }

    // [[nodiscard]] is C++17
    bool empty() const noexcept { return size() == 0; }
    size_type size() const noexcept { return m_size; }
    size_type capacity() const noexcept { return m_capacity; }

    void clear() noexcept {
        new_buffer_detail::destroy(ptr(), ptr() + size());
        m_size = 0;
    }

    void resize(size_type count) {
        _reserve(count);
        new_buffer_detail::destroy(ptr() + count, ptr() + size());
        for(size_type i = size(); i < count; ++i) {
            ::new(ptr() + i) value_type();
        }
        m_size = count;
    }

    void resize(size_type count, value_type const& value) {
        _reserve(count);
        new_buffer_detail::destroy(ptr() + count, ptr() + size());
        for(size_type i = size(); i < count; ++i) {
            ::new(ptr() + i) value_type(value);
        }
        m_size = count;
    }

    // vector::reserve actually does an enlarge-only resize as per the old API
private:
    void _reserve(size_type new_capacity) {
        if(new_capacity > capacity()) {
            reallocate(new_capacity);
        }
    }
public:

    void shrink_to_fit() {
        if(size() > 0) {
            if(size() != capacity()) {
                reallocate(size());
            }
        } else {
            if(m_data) {
                new_buffer_detail::destroy(begin(), end());
                memory::deallocate(m_data, m_capacity * sizeof(value_type));
                m_data = nullptr;
                m_capacity = 0;
            }
        }
    }

    reference operator[](size_type index) {
        SASSERT(index < size());
        return ptr()[index];
    }

    const_reference operator[](size_type index) const {
        SASSERT(index < size());
        return ptr()[index];
    }

          iterator           begin()       noexcept { return ptr(); }
    const_iterator           begin() const noexcept { return ptr(); }
    const_iterator          cbegin() const noexcept { return ptr(); }
          iterator           end()         noexcept { return ptr() + size(); }
    const_iterator           end()   const noexcept { return ptr() + size(); }
    const_iterator          cend()   const noexcept { return ptr() + size(); }
          reverse_iterator  rbegin()       noexcept { return static_cast<reverse_iterator>(end()); }
    const_reverse_iterator  rbegin() const noexcept { return static_cast<const_reverse_iterator>(end()); }
    const_reverse_iterator crbegin() const noexcept { return static_cast<const_reverse_iterator>(end()); }
          reverse_iterator  rend()         noexcept { return static_cast<reverse_iterator>(begin()); }
    const_reverse_iterator  rend()   const noexcept { return static_cast<const_reverse_iterator>(begin()); }
    const_reverse_iterator crend()   const noexcept { return static_cast<const_reverse_iterator>(begin()); }
    // interferes with the data member type
    // pointer data() noexcept { return ptr(); }
    // const_pointer data() const noexcept { return ptr(); }

    reference front() { 
        SASSERT(!empty()); 
        return begin()[0]; 
    }

    const_reference front() const { 
        SASSERT(!empty()); 
        return begin()[0]; 
    }

    reference back() { 
        SASSERT(!empty()); 
        return end()[-1]; 
    }

    const_reference back() const { 
        SASSERT(!empty()); 
        return end()[-1]; 
    }

    void push_back(value_type const& value) {
        if(size() >= capacity()) {
            reallocate(next_capacity());
        }
        ::new(ptr() + size()) value_type(value);
        ++m_size;
    }

    void push_back(value_type&& value) {
        if(size() >= capacity()) {
            reallocate(next_capacity());
        }
        ::new(ptr() + size()) value_type(std::move(value));
        ++m_size;
    }

    template<typename... Args>
    void emplace_back(Args&&... args) {
        if(size() >= capacity()) {
            reallocate(next_capacity());
        }
        ::new(ptr() + size()) value_type(std::forward<Args>(args)...);
        ++m_size;
    }

    void pop_back() {
        SASSERT(!empty()); 
        end()->~value_type();
        --m_size;
    }

    iterator erase(const_iterator position) {
        SASSERT(position != end());
        auto const index = static_cast<size_type>(position - ptr());
        ptr()[index].~value_type();
        new_buffer_detail::move_around(ptr() + index, ptr() + index + 1, size() - index - 1);
        --m_size();
        return ptr() + index;
    }

    iterator erase(value_type const& element) {
        iterator it = std::find(begin(), end(), element);
        if(it != end()) {
            return erase(it);
        }
        return end();
    }

    // adaptors for the old vector interface
    pointer c_ptr() const { return m_data; }
};



template<typename T1, typename SZ1, SZ1 INITIAL_SIZE1, typename T2, typename SZ2, SZ2 INITIAL_SIZE2>
bool operator==(new_buffer<T1, SZ1, INITIAL_SIZE1> const& lhs, new_buffer<T2, SZ2, INITIAL_SIZE2> const& rhs) {
    if(lhs.size() != rhs.size()) {
        return false;
    }
    auto li = lhs.cbegin();
    auto lend = lhs.cend();
    auto ri = rhs.cbegin();
    for( ; li != lend; ++li, ++ri) {
        if(*li != *ri) {
            return false;
        }
    }
    SASSERT(ri == rhs.cend());
    return true;
}

template<typename T1, typename SZ1, SZ1 INITIAL_SIZE1, typename T2, typename SZ2, SZ2 INITIAL_SIZE2>
bool operator!=(new_buffer<T1, SZ1, INITIAL_SIZE1> const& lhs, new_buffer<T2, SZ2, INITIAL_SIZE2> const& rhs) {
    return !(lhs == rhs);
}

template<typename T1, typename SZ1, SZ1 INITIAL_SIZE1, typename T2, typename SZ2, SZ2 INITIAL_SIZE2>
bool operator< (new_buffer<T1, SZ1, INITIAL_SIZE1> const& lhs, new_buffer<T2, SZ2, INITIAL_SIZE2> const& rhs) {
    return std::lexicographical_compare(lhs.cbegin(), lhs.cend(), rhs.cbegin(), rhs.cend(), [](T1 const& lhs, T2 const& rhs) -> bool { return lhs < rhs; });
}

template<typename T1, typename SZ1, SZ1 INITIAL_SIZE1, typename T2, typename SZ2, SZ2 INITIAL_SIZE2>
bool operator<=(new_buffer<T1, SZ1, INITIAL_SIZE1> const& lhs, new_buffer<T2, SZ2, INITIAL_SIZE2> const& rhs) {
    return std::lexicographical_compare(lhs.cbegin(), lhs.cend(), rhs.cbegin(), rhs.cend(), [](T1 const& lhs, T2 const& rhs) -> bool { return lhs <= rhs; });
}

template<typename T1, typename SZ1, SZ1 INITIAL_SIZE1, typename T2, typename SZ2, SZ2 INITIAL_SIZE2>
bool operator> (new_buffer<T1, SZ1, INITIAL_SIZE1> const& lhs, new_buffer<T2, SZ2, INITIAL_SIZE2> const& rhs) {
    return std::lexicographical_compare(lhs.cbegin(), lhs.cend(), rhs.cbegin(), rhs.cend(), [](T1 const& lhs, T2 const& rhs) -> bool { return lhs > rhs; });
}

template<typename T1, typename SZ1, SZ1 INITIAL_SIZE1, typename T2, typename SZ2, SZ2 INITIAL_SIZE2>
bool operator>=(new_buffer<T1, SZ1, INITIAL_SIZE1> const& lhs, new_buffer<T2, SZ2, INITIAL_SIZE2> const& rhs) {
    return std::lexicographical_compare(lhs.cbegin(), lhs.cend(), rhs.cbegin(), rhs.cend(), [](T1 const& lhs, T2 const& rhs) -> bool { return lhs >= rhs; });
}

namespace std {
    template<typename T, typename SZ, std::size_t INITIAL_SIZE>
    struct hash<::new_buffer<T, SZ, INITIAL_SIZE>> {
        // investigate `get_composite_hash`
        using argument_type = ::new_buffer<T, SZ, INITIAL_SIZE>;
        using result_type = ::std::size_t;
        result_type operator()(argument_type const& value) const {
            result_type result = ::std::hash<typename argument_type::size_type>()(value.size());
            ::std::hash<typename argument_type::value_type> hasher;
            for(auto const& element : value) {
                static_assert(::std::numeric_limits<result_type>::digits > 11, "Something has gone horribly wrong");
                result = (result << 11) | (result >> (::std::numeric_limits<result_type>::digits - 11));
                result ^= hasher(element);
            }
            return result;
        }
    };
}

#endif /* NEW_BUFFER_H_ */
