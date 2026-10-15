#ifndef SKYFIRE_BATTLEPAY_TYPES_H
#define SKYFIRE_BATTLEPAY_TYPES_H
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace BattlePay
{
constexpr uint64_t Precision = 10000;
constexpr uint32_t BoostProduct = 83;
enum class PurchaseState : uint8_t { Quoted, Paid, Delivered, Cancelled, Refunded };
struct ProductItem { uint32_t Id = 0, ItemId = 0, Count = 0; };
struct Purchase
{
    uint64_t Id = 0, Price = 0;
    uint32_t Account = 0, Realm = 0, Target = 0, Product = 0;
    uint8_t Type = 0;
    PurchaseState State = PurchaseState::Quoted;
    std::string Title, Description;
    std::vector<ProductItem> Items;
};
// Strict decimal parser: no exponent, signs, whitespace, or binary floats.
inline bool ParseCredits(std::string const& text, uint64_t& units)
{
    if (text.empty()) return false;
    uint64_t whole = 0, fraction = 0;
    size_t i = 0, digits = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9')
    {
        unsigned digit = text[i++] - '0';
        if (whole > (std::numeric_limits<uint64_t>::max() / Precision - digit) / 10) return false;
        whole = whole * 10 + digit; ++digits;
    }
    if (!digits) return false;
    unsigned places = 0;
    if (i < text.size())
    {
        if (text[i++] != '.') return false;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9' && places < 4)
        { fraction = fraction * 10 + text[i++] - '0'; ++places; }
        if (!places) return false;
    }
    if (i != text.size()) return false;
    while (places++ < 4) fraction *= 10;
    if (whole > (std::numeric_limits<uint64_t>::max() - fraction) / Precision) return false;
    units = whole * Precision + fraction;
    return true;
}
inline std::string FormatCredits(uint64_t units)
{
    std::string fraction = std::to_string(units % Precision);
    return std::to_string(units / Precision) + "." + std::string(4 - fraction.size(), '0') + fraction;
}
inline uint64_t DiscountPrice(uint64_t units, uint32_t discount)
{
    // ceil(price * (100 - discount) / 100), without multiplication overflow.
    uint64_t rate = 100 - discount;
    return (units / 100) * rate + ((units % 100) * rate + 99) / 100;
}
inline bool CanSubtract(uint64_t balance, uint64_t amount) { return amount <= balance; }
inline bool CanAdd(uint64_t balance, uint64_t amount) { return amount <= std::numeric_limits<uint64_t>::max() - balance; }
}
#endif
