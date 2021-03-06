

#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/SystemParameters.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/beast/utility/Zero.h>

namespace ripple {

std::string to_string(Currency const& currency)
{
    static std::string const allowed_characters =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789"
        "<>(){}[]|?!@#$%^&*";

    if (currency == beast::zero)
        return systemCurrencyCode();

    if (currency == noCurrency())
        return "1";

    static Currency const sIsoBits (
        from_hex_text<Currency>("FFFFFFFFFFFFFFFFFFFFFFFF000000FFFFFFFFFF"));

    if ((currency & sIsoBits).isZero ())
    {
        int const isoOffset = 12;

        std::string const iso(
            currency.data () + isoOffset,
            currency.data () + isoOffset + 3);

        if ((iso != systemCurrencyCode()) &&
            (iso.find_first_not_of (allowed_characters) == std::string::npos))
        {
            return iso;
        }
    }

    return strHex (currency);
}

bool to_currency(Currency& currency, std::string const& code)
{
    if (code.empty () || !code.compare (systemCurrencyCode()))
    {
        currency = beast::zero;
        return true;
    }

    static const int CURRENCY_CODE_LENGTH = 3;
    if (code.size () == CURRENCY_CODE_LENGTH)
    {
        Blob codeBlob (CURRENCY_CODE_LENGTH);

        std::transform (code.begin (), code.end (), codeBlob.begin (),
                        [](auto c)
                        {
                            return ::toupper(static_cast<unsigned char>(c));
                        });

        Serializer  s;

        s.addZeros (96 / 8);
        s.addRaw (codeBlob);
        s.addZeros (16 / 8);
        s.addZeros (24 / 8);

        s.get160 (currency, 0);
        return true;
    }

    if (40 == code.size ())
        return currency.SetHex (code);

    return false;
}

Currency to_currency(std::string const& code)
{
    Currency currency;
    if (!to_currency(currency, code))
        currency = noCurrency();
    return currency;
}

Currency const& xrpCurrency()
{
    static Currency const currency(beast::zero);
    return currency;
}

Currency const& noCurrency()
{
    static Currency const currency(1);
    return currency;
}

Currency const& badCurrency()
{
    static Currency const currency(0x5852500000000000);
    return currency;
}

} 
























