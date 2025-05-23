//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#define MAGIC_ENUM_NO_CHECK_REFLECTED_ENUM

#include <ripple/app/main/Application.h>
#include <ripple/app/misc/AmendmentTable.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/reporting/P2pProxy.h>
#include <ripple/json/json_value.h>
#include <ripple/json/json_writer.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/LedgerFormats.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/impl/TransactionSign.h>
#include <boost/algorithm/string.hpp>
#include <magic/magic_enum.h>
#include <sstream>

#define MAGIC_ENUM(x, _min, _max)               \
    template <>                                 \
    struct magic_enum::customize::enum_range<x> \
    {                                           \
        static constexpr int min = _min;        \
        static constexpr int max = _max;        \
    };

#define MAGIC_ENUM_16(x)                        \
    template <>                                 \
    struct magic_enum::customize::enum_range<x> \
    {                                           \
        static constexpr int min = -128;        \
        static constexpr int max = 127;         \
    };

#define MAGIC_ENUM_FLAG(x)                      \
    template <>                                 \
    struct magic_enum::customize::enum_range<x> \
    {                                           \
        static constexpr bool is_flags = true;  \
    };

MAGIC_ENUM(ripple::SerializedTypeID, -2, 10004);
MAGIC_ENUM(ripple::LedgerEntryType, 0, 255);
MAGIC_ENUM(ripple::TELcodes, -399, 300);
MAGIC_ENUM(ripple::TEMcodes, -299, -200);
MAGIC_ENUM(ripple::TEFcodes, -199, -100);
MAGIC_ENUM(ripple::TERcodes, -99, -1);
MAGIC_ENUM(ripple::TEScodes, 0, 1);
MAGIC_ENUM(ripple::TECcodes, 100, 255);
MAGIC_ENUM_16(ripple::TxType);
MAGIC_ENUM_FLAG(ripple::UniversalFlags);
MAGIC_ENUM_FLAG(ripple::AccountSetFlags);
MAGIC_ENUM_FLAG(ripple::OfferCreateFlags);
MAGIC_ENUM_FLAG(ripple::PaymentFlags);
MAGIC_ENUM_FLAG(ripple::TrustSetFlags);
MAGIC_ENUM_FLAG(ripple::EnableAmendmentFlags);
MAGIC_ENUM_FLAG(ripple::PaymentChannelClaimFlags);
MAGIC_ENUM_FLAG(ripple::NFTokenMintFlags);
MAGIC_ENUM_FLAG(ripple::NFTokenCreateOfferFlags);
MAGIC_ENUM_FLAG(ripple::ClaimRewardFlags);
MAGIC_ENUM_16(ripple::AccountFlags);

namespace ripple {

class Definitions
{
private:
#define STR(x)                      \
    ([&] {                          \
        std::ostringstream ss;      \
        return ss << (x), ss.str(); \
    }())

    template <typename EnumType>
    void
    addFlagsToJson(Json::Value& json, std::string const& key)
    {
        for (auto const& entry : magic_enum::enum_entries<EnumType>())
        {
            const auto name = entry.second;
            json[jss::TRANSACTION_FLAGS][key][STR(name)] =
                static_cast<uint32_t>(entry.first);
        }
    }

    Json::Value
    generate()
    {
        Json::Value ret{Json::objectValue};
        ret[jss::TYPES] = Json::objectValue;

        auto const translate = [](std::string inp) -> std::string {
            auto replace = [&](const char* f, const char* r) -> std::string {
                std::string out = inp;
                boost::replace_all(out, f, r);
                return out;
            };

            auto find = [&](const char* s) -> bool {
                return inp.find(s) != std::string::npos;
            };

            if (find("UINT"))
            {
                if (find("256") || find("160") || find("128"))
                    return replace("UINT", "Hash");
                else
                    return replace("UINT", "UInt");
            }

            if (inp == "OBJECT")
                return "STObject";
            if (inp == "ARRAY")
                return "STArray";
            if (inp == "AMM")
                return "AMM";
            if (inp == "ACCOUNT")
                return "AccountID";
            if (inp == "LEDGERENTRY")
                return "LedgerEntry";
            if (inp == "NOTPRESENT")
                return "NotPresent";
            if (inp == "PATHSET")
                return "PathSet";
            if (inp == "VL")
                return "Blob";
            if (inp == "DIR_NODE")
                return "DirectoryNode";
            if (inp == "PAYCHAN")
                return "PayChannel";
            if (inp == "IMPORT_VLSEQ")
                return "ImportVLSequence";

            static const std::map<std::string, std::string>
                capitalization_exceptions = {
                    {"NFTOKEN", "NFToken"},
                    {"UNL", "UNL"},
                    {"XCHAIN", "XChain"},
                    {"ID", "ID"},
                    {"AMM", "AMM"},
                    {"URITOKEN", "URIToken"},
                    {"URI", "URI"}};

            std::string out;
            size_t pos = 0;
            for (;;)
            {
                pos = inp.find("_");
                if (pos == std::string::npos)
                    pos = inp.size();
                std::string token = inp.substr(0, pos);
                if (auto const e = capitalization_exceptions.find(token);
                    e != capitalization_exceptions.end())
                    out += e->second;
                else if (token.size() > 1)
                {
                    boost::algorithm::to_lower(token);
                    token.data()[0] -= ('a' - 'A');
                    out += token;
                }
                else
                    out += token;
                if (pos == inp.size())
                    break;
                inp = inp.substr(pos + 1);
            }
            return out;
        };

        ret[jss::TYPES]["Done"] = -1;
        std::map<int32_t, std::string> type_map{{-1, "Done"}};
        for (auto const& entry : magic_enum::enum_entries<SerializedTypeID>())
        {
            const auto name = entry.second;
            std::string type_name =
                translate(name.data() + 4 /* remove STI_ */);
            int32_t type_value = static_cast<int32_t>(entry.first);
            ret[jss::TYPES][type_name] = type_value;
            type_map[type_value] = type_name;
        }

        ret[jss::LEDGER_ENTRY_TYPES] = Json::objectValue;
        ret[jss::LEDGER_ENTRY_TYPES][jss::Invalid] = -1;
        for (auto const& entry : magic_enum::enum_entries<LedgerEntryType>())
        {
            const auto name = entry.second;
            std::string type_name = translate(name.data() + 2 /* remove lt_ */);
            int32_t type_value = static_cast<int32_t>(entry.first);
            ret[jss::LEDGER_ENTRY_TYPES][type_name] = type_value;
        }

        ret[jss::FIELDS] = Json::arrayValue;

        uint32_t i = 0;
        {
            Json::Value a = Json::arrayValue;
            a[0U] = "Generic";
            Json::Value v = Json::objectValue;
            v[jss::nth] = 0;
            v[jss::isVLEncoded] = false;
            v[jss::isSerialized] = false;
            v[jss::isSigningField] = false;
            v[jss::type] = "Unknown";
            a[1U] = v;
            ret[jss::FIELDS][i++] = a;
        }

        {
            Json::Value a = Json::arrayValue;
            a[0U] = "Invalid";
            Json::Value v = Json::objectValue;
            v[jss::nth] = -1;
            v[jss::isVLEncoded] = false;
            v[jss::isSerialized] = false;
            v[jss::isSigningField] = false;
            v[jss::type] = "Unknown";
            a[1U] = v;
            ret[jss::FIELDS][i++] = a;
        }

        {
            Json::Value a = Json::arrayValue;
            a[0U] = "ObjectEndMarker";
            Json::Value v = Json::objectValue;
            v[jss::nth] = 1;
            v[jss::isVLEncoded] = false;
            v[jss::isSerialized] = false;
            v[jss::isSigningField] = true;
            v[jss::type] = "STObject";
            a[1U] = v;
            ret[jss::FIELDS][i++] = a;
        }

        {
            Json::Value a = Json::arrayValue;
            a[0U] = "ArrayEndMarker";
            Json::Value v = Json::objectValue;
            v[jss::nth] = 1;
            v[jss::isVLEncoded] = false;
            v[jss::isSerialized] = false;
            v[jss::isSigningField] = true;
            v[jss::type] = "STArray";
            a[1U] = v;
            ret[jss::FIELDS][i++] = a;
        }

        {
            Json::Value a = Json::arrayValue;
            a[0U] = "hash";
            Json::Value v = Json::objectValue;
            v[jss::nth] = 257;
            v[jss::isVLEncoded] = false;
            v[jss::isSerialized] = false;
            v[jss::isSigningField] = false;
            v[jss::type] = "Hash256";
            a[1U] = v;
            ret[jss::FIELDS][i++] = a;
        }

        {
            Json::Value a = Json::arrayValue;
            a[0U] = "index";
            Json::Value v = Json::objectValue;
            v[jss::nth] = 258;
            v[jss::isVLEncoded] = false;
            v[jss::isSerialized] = false;
            v[jss::isSigningField] = false;
            v[jss::type] = "Hash256";
            a[1U] = v;
            ret[jss::FIELDS][i++] = a;
        }

        {
            Json::Value a = Json::arrayValue;
            a[0U] = "taker_gets_funded";
            Json::Value v = Json::objectValue;
            v[jss::nth] = 258;
            v[jss::isVLEncoded] = false;
            v[jss::isSerialized] = false;
            v[jss::isSigningField] = false;
            v[jss::type] = "Amount";
            a[1U] = v;
            ret[jss::FIELDS][i++] = a;
        }

        {
            Json::Value a = Json::arrayValue;
            a[0U] = "taker_pays_funded";
            Json::Value v = Json::objectValue;
            v[jss::nth] = 259;
            v[jss::isVLEncoded] = false;
            v[jss::isSerialized] = false;
            v[jss::isSigningField] = false;
            v[jss::type] = "Amount";
            a[1U] = v;
            ret[jss::FIELDS][i++] = a;
        }

        for (auto const& [code, f] : ripple::SField::knownCodeToField)
        {
            if (f->fieldName == "")
                continue;

            Json::Value innerObj = Json::objectValue;

            uint32_t fc = code & 0xFFU;
            uint32_t tc = code >> 16U;

            innerObj[jss::nth] = fc;

            innerObj[jss::isVLEncoded] =
                (tc == 7U /* Blob       */ || tc == 8U /* AccountID  */ ||
                 tc == 19U /* Vector256  */);

            innerObj[jss::isSerialized] =
                (tc <
                 10000); /* TRANSACTION, LEDGER_ENTRY, VALIDATION, METADATA */

            innerObj[jss::isSigningField] = f->shouldInclude(false);

            innerObj[jss::type] = type_map[tc];

            Json::Value innerArray = Json::arrayValue;
            innerArray[0U] = f->fieldName;
            innerArray[1U] = innerObj;

            ret[jss::FIELDS][i++] = innerArray;
        }

        ret[jss::TRANSACTION_RESULTS] = Json::objectValue;
        for (auto const& entry : magic_enum::enum_entries<TELcodes>())
        {
            const auto name = entry.second;
            ret[jss::TRANSACTION_RESULTS][STR(name)] =
                static_cast<int32_t>(entry.first);
        }
        for (auto const& entry : magic_enum::enum_entries<TEMcodes>())
        {
            const auto name = entry.second;
            ret[jss::TRANSACTION_RESULTS][STR(name)] =
                static_cast<int32_t>(entry.first);
        }
        for (auto const& entry : magic_enum::enum_entries<TEFcodes>())
        {
            const auto name = entry.second;
            ret[jss::TRANSACTION_RESULTS][STR(name)] =
                static_cast<int32_t>(entry.first);
        }
        for (auto const& entry : magic_enum::enum_entries<TERcodes>())
        {
            const auto name = entry.second;
            ret[jss::TRANSACTION_RESULTS][STR(name)] =
                static_cast<int32_t>(entry.first);
        }
        for (auto const& entry : magic_enum::enum_entries<TEScodes>())
        {
            const auto name = entry.second;
            ret[jss::TRANSACTION_RESULTS][STR(name)] =
                static_cast<int32_t>(entry.first);
        }
        for (auto const& entry : magic_enum::enum_entries<TECcodes>())
        {
            const auto name = entry.second;
            ret[jss::TRANSACTION_RESULTS][STR(name)] =
                static_cast<int32_t>(entry.first);
        }

        auto const translate_tt = [](std::string inp) -> std::string {
            if (inp == "PaychanClaim")
                return "PaymentChannelClaim";
            if (inp == "PaychanCreate")
                return "PaymentChannelCreate";
            if (inp == "PaychanFund")
                return "PaymentChannelFund";
            if (inp == "RegularKeySet")
                return "SetRegularKey";
            if (inp == "HookSet")
                return "SetHook";
            if (inp == "RemarksSet")
                return "SetRemarks";
            return inp;
        };

        ret[jss::TRANSACTION_TYPES] = Json::objectValue;
        ret[jss::TRANSACTION_TYPES][jss::Invalid] = -1;
        for (auto const& entry : magic_enum::enum_entries<TxType>())
        {
            const auto name = entry.second;
            std::string type_name = translate_tt(translate(name.data() + 2));
            int32_t type_value = static_cast<int32_t>(entry.first);
            ret[jss::TRANSACTION_TYPES][type_name] = type_value;
        }

        // Transaction Flags:
        ret[jss::TRANSACTION_FLAGS] = Json::objectValue;
        addFlagsToJson<UniversalFlags>(ret, "Universal");
        addFlagsToJson<AccountSetFlags>(ret, "AccountSet");
        addFlagsToJson<OfferCreateFlags>(ret, "OfferCreate");
        addFlagsToJson<PaymentFlags>(ret, "Payment");
        addFlagsToJson<TrustSetFlags>(ret, "TrustSet");
        addFlagsToJson<EnableAmendmentFlags>(ret, "EnableAmendment");
        addFlagsToJson<PaymentChannelClaimFlags>(ret, "PaymentChannelClaim");
        addFlagsToJson<NFTokenMintFlags>(ret, "NFTokenMint");
        addFlagsToJson<NFTokenCreateOfferFlags>(ret, "NFTokenCreateOffer");
        addFlagsToJson<ClaimRewardFlags>(ret, "ClaimReward");
        struct FlagData
        {
            std::string name;
            std::uint32_t value;
        };
        std::array<FlagData, 1> uriTokenMintFlags{{{"tfBurnable", tfBurnable}}};
        for (auto const& entry : uriTokenMintFlags)
        {
            ret[jss::TRANSACTION_FLAGS]["URITokenMint"][entry.name] =
                static_cast<uint32_t>(entry.value);
        }

        // Transaction Indicies Flags:
        ret[jss::TRANSACTION_FLAGS_INDICES] = Json::objectValue;
        for (auto const& entry : magic_enum::enum_entries<AccountFlags>())
        {
            const auto name = entry.second;
            ret[jss::TRANSACTION_FLAGS_INDICES]["AccountSet"][STR(name)] =
                static_cast<uint32_t>(entry.first);
        }

        ret[jss::native_currency_code] = systemCurrencyCode();

        // generate hash
        {
            const std::string out = Json::FastWriter().write(ret);
            defsHash =
                ripple::sha512Half(ripple::Slice{out.data(), out.size()});
        }
        return ret;
    }

    std::optional<uint256> defsHash;
    Json::Value defs;

public:
    Definitions() : defs(generate()){};

    uint256 const&
    getHash() const
    {
        if (!defsHash)
        {
            static const uint256 fallbackHash(
                "DF4220E93ADC6F5569063A01B4DC79F8DB9553B6A3222ADE23DEA0");
            return fallbackHash;
        }
        return *defsHash;
    }

    Json::Value const&
    operator()(void) const
    {
        return defs;
    }
};

Json::Value
doServerDefinitions(RPC::JsonContext& context)
{
    auto& params = context.params;

    uint256 reqHash;
    if (params.isMember(jss::hash))
    {
        if (!params[jss::hash].isString() ||
            !reqHash.parseHex(params[jss::hash].asString()))
            return RPC::invalid_field_error(jss::hash);
    }

    uint32_t curLgrSeq = context.ledgerMaster.getValidatedLedger()->info().seq;

    // static values used for cache
    static thread_local uint32_t lastGenerated =
        0;  // last ledger seq it was generated
    static thread_local Json::Value lastFeatures{
        Json::objectValue};  // the actual features JSON last generated
    static thread_local uint256
        lastFeatureHash;  // the hash of the features JSON last time
                          // it was generated

    // if a flag ledger has passed since it was last generated, regenerate it,
    // update the cache above
    if (curLgrSeq > ((lastGenerated >> 8) + 1) << 8 || lastGenerated == 0)
    {
        majorityAmendments_t majorities;
        if (auto const valLedger = context.ledgerMaster.getValidatedLedger())
            majorities = getMajorityAmendments(*valLedger);
        auto& table = context.app.getAmendmentTable();
        auto features = table.getJson();
        for (auto const& [h, t] : majorities)
            features[to_string(h)][jss::majority] =
                t.time_since_epoch().count();

        lastFeatures = features;
        {
            const std::string out = Json::FastWriter().write(features);
            lastFeatureHash =
                ripple::sha512Half(ripple::Slice{out.data(), out.size()});
        }
    }

    static const Definitions defs{};

    // the hash is the xor of the two parts
    uint256 retHash = lastFeatureHash ^ defs.getHash();

    if (reqHash == retHash)
    {
        Json::Value jv = Json::objectValue;
        jv[jss::hash] = to_string(retHash);
        return jv;
    }

    // definitions
    Json::Value ret = defs();
    ret[jss::hash] = to_string(retHash);
    ret[jss::features] = lastFeatures;

    return ret;
}

}  // namespace ripple
