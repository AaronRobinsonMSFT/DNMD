#ifndef _SRC_INTERFACES_HCORENUM_HPP_
#define _SRC_INTERFACES_HCORENUM_HPP_

#include <internal/dnmd_platform.hpp>

enum class HCORENUMType : uint32_t
{
    Table = 1, Dynamic
};

// Represents a singly linked list or dynamic uint32_t array enumerator
class HCORENUMImpl final
{
    HCORENUMType _type;
    uint32_t _entrySpan; // The number of entries equal to a single unit.

    struct EnumData final
    {
        struct TableData final
        {
            dnmd::any_cursor Current;
            dnmd::any_cursor Start;

            explicit TableData(mdhandle_t handle) noexcept
                : Current{handle, 0}, Start{handle, 0}
            {}
        };

        union
        {
            // Enumerate for tables
            TableData Table;

            // Enumerate for dynamic uint32_t array
            struct
            {
                uint32_t Page[16];
            } Dynamic;
        };

        uint32_t ReadIn;
        uint32_t Total;
        EnumData* Next;

        explicit EnumData(mdhandle_t handle) noexcept
            : Table{handle}, ReadIn{0}, Total{0}, Next{nullptr}
        {}

        EnumData() noexcept
            : Dynamic{}, ReadIn{0}, Total{0}, Next{nullptr}
        {}

        ~EnumData() noexcept {}
    };

    EnumData _data;
    EnumData* _curr;
    EnumData* _last;

    explicit HCORENUMImpl(mdhandle_t handle) noexcept
        : _type{HCORENUMType::Table}, _entrySpan{1}, _data{handle},
          _curr{&_data}, _last{&_data}
    {}

    explicit HCORENUMImpl(uint32_t entrySpan) noexcept
        : _type{HCORENUMType::Dynamic}, _entrySpan{entrySpan}, _data{},
          _curr{&_data}, _last{&_data}
    {}

public: // static
    // Lifetime operations
    static HRESULT CreateTableEnum(_In_ mdhandle_t handle, _In_ uint32_t count, _Out_ HCORENUMImpl** impl) noexcept;
    static void InitTableEnum(_Inout_ HCORENUMImpl& impl, _In_ uint32_t index, _In_ dnmd::any_cursor cursor, _In_ uint32_t rows) noexcept;

    // If multiple values represent a single entry, the "entrySpan" argument
    // can be used to indicate the count for a single entry.
    static HRESULT CreateDynamicEnum(_Out_ HCORENUMImpl** impl, _In_ uint32_t entrySpan = 1) noexcept;
    static HRESULT AddToDynamicEnum(_Inout_ HCORENUMImpl& impl, uint32_t value) noexcept;

    static void Destroy(_In_ HCORENUMImpl* impl) noexcept;

public: // instance
    // Get the total items for this enumeration
    uint32_t Count() const noexcept;

    // Read in the tokens for this enumeration
    HRESULT ReadTokens(
        mdToken rTokens[],
        ULONG cMax,
        ULONG* pcTokens) noexcept;

    HRESULT ReadTokenPairs(
        mdToken rTokens1[],
        mdToken rTokens2[],
        ULONG cMax,
        ULONG* pcTokens) noexcept;

    // Reset the enumeration to a specific position
    HRESULT Reset(_In_ ULONG position) noexcept;

private:
    HRESULT ReadOneToken(mdToken& rToken, uint32_t& count) noexcept;
    HRESULT ReadTableTokens(
        mdToken rTokens[],
        uint32_t cMax,
        uint32_t& tokenCount) noexcept;
    HRESULT ReadDynamicTokens(
        mdToken rTokens[],
        uint32_t cMax,
        uint32_t& tokenCount) noexcept;

    HRESULT ResetTableEnum(_In_ uint32_t position) noexcept;
    HRESULT ResetDynamicEnum(_In_ uint32_t position) noexcept;
};

struct HCORENUMImplDeleter final
{
    using pointer = HCORENUMImpl*;
    void operator()(HCORENUMImpl* mem)
    {
        HCORENUMImpl::Destroy(mem);
    }
};

// C++ lifetime wrapper for HCORENUMImpl memory
using HCORENUMImpl_ptr = std::unique_ptr<HCORENUMImpl, HCORENUMImplDeleter>;

#endif // _SRC_INTERFACES_HCORENUM_HPP_