//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/app/main/Application.h>
#include <ripple/basics/ByteUtilities.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/base64.h>
#include <ripple/basics/contract.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/core/Config.h>
#include <ripple/json/Object.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/to_string.h>
#include <ripple/net/HTTPClient.h>
#include <ripple/net/RPCCall.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/SystemParameters.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/ServerHandler.h>
#include <ripple/rpc/impl/RPCHelpers.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/beast/core/string.hpp>
#include <boost/regex.hpp>

#include <array>
#include <iostream>
#include <type_traits>
#include <unordered_map>

namespace ripple {

class RPCParser;

//
// HTTP protocol
//
// This ain't Apache.  We're just using HTTP header for the length field
// and to be compatible with other JSON-RPC implementations.
//

std::string
createHTTPPost(
    std::string const& strHost,
    std::string const& strPath,
    std::string const& strMsg,
    std::unordered_map<std::string, std::string> const& mapRequestHeaders)
{
    std::ostringstream s;

    // CHECKME this uses a different version than the replies below use. Is
    //         this by design or an accident or should it be using
    //         BuildInfo::getFullVersionString () as well?

    s << "POST " << (strPath.empty() ? "/" : strPath) << " HTTP/1.0\r\n"
      << "User-Agent: " << systemName() << "-json-rpc/v1\r\n"
      << "Host: " << strHost << "\r\n"
      << "Content-Type: application/json\r\n"
      << "Content-Length: " << strMsg.size() << "\r\n"
      << "Accept: application/json\r\n";

    for (auto const& [k, v] : mapRequestHeaders)
        s << k << ": " << v << "\r\n";

    s << "\r\n" << strMsg;

    return s.str();
}

class RPCParser
{
private:
    beast::Journal const j_;

    // TODO New routine for parsing ledger parameters, other routines should
    // standardize on this.
    static bool
    jvParseLedger(Json::Value& jvRequest, std::string const& strLedger)
    {
        if (strLedger == "current" || strLedger == "closed" ||
            strLedger == "validated")
        {
            jvRequest[jss::ledger_index] = strLedger;
        }
        else if (strLedger.length() == 64)
        {
            // YYY Could confirm this is a uint256.
            jvRequest[jss::ledger_hash] = strLedger;
        }
        else
        {
            jvRequest[jss::ledger_index] =
                beast::lexicalCast<std::uint32_t>(strLedger);
        }

        return true;
    }

    // Build a object { "currency" : "XYZ", "issuer" : "rXYX" }
    static Json::Value
    jvParseCurrencyIssuer(std::string const& strCurrencyIssuer)
    {
        static boost::regex reCurIss("\\`([[:alpha:]]{3})(?:/(.+))?\\'");

        boost::smatch smMatch;

        if (boost::regex_match(strCurrencyIssuer, smMatch, reCurIss))
        {
            Json::Value jvResult(Json::objectValue);
            std::string strCurrency = smMatch[1];
            std::string strIssuer = smMatch[2];

            jvResult[jss::currency] = strCurrency;

            if (strIssuer.length())
            {
                // Could confirm issuer is a valid Ripple address.
                jvResult[jss::issuer] = strIssuer;
            }

            return jvResult;
        }
        else
        {
            return RPC::make_param_error(
                std::string("Invalid currency/issuer '") + strCurrencyIssuer +
                "'");
        }
    }

    // Build an object
    // { "currency" : "XYZ", "issuer" : "rXYX", "value": 1000 }
    static Json::Value
    jvParseSTAmount(std::string const& strIC)
    {
        static boost::regex reCurIss(
            "\\`(0|[1-9][0-9]*)(?:/([[:alpha:]]{3}))(?:/(.+))?\\'");

        boost::smatch icMatch;

        Json::Value jvResult(Json::objectValue);
        if (boost::regex_match(strIC, icMatch, reCurIss))
        {
            std::string strAmount = icMatch[1];
            std::string strCurrency = icMatch[2];
            std::string strIssuer = icMatch[3];

            jvResult[jss::currency] = strCurrency;
            jvResult[jss::value] = strAmount;

            if (strIssuer.length())
            {
                // Could confirm issuer is a valid Ripple address.
                jvResult[jss::issuer] = strIssuer;
            }
        }
        return jvResult;
    }

    static bool
    validPublicKey(
        std::string const& strPk,
        TokenType type = TokenType::AccountPublic)
    {
        if (parseBase58<PublicKey>(type, strPk))
            return true;

        auto pkHex = strUnHex(strPk);
        if (!pkHex)
            return false;

        if (!publicKeyType(makeSlice(*pkHex)))
            return false;

        return true;
    }

private:
    using parseFuncPtr =
        Json::Value (RPCParser::*)(Json::Value const& jvParams);

    Json::Value
    parseAsIs(Json::Value const& jvParams)
    {
        Json::Value v(Json::objectValue);

        if (jvParams.isArray() && (jvParams.size() > 0))
            v[jss::params] = jvParams;

        return v;
    }

    Json::Value
    parseDownloadShard(Json::Value const& jvParams)
    {
        Json::Value jvResult(Json::objectValue);
        unsigned int sz{jvParams.size()};
        unsigned int i{0};

        // If odd number of params then 'novalidate' may have been specified
        if (sz & 1)
        {
            if (boost::iequals(jvParams[0u].asString(), "novalidate"))
                ++i;
            else if (!boost::iequals(jvParams[--sz].asString(), "novalidate"))
                return rpcError(rpcINVALID_PARAMS);
        }

        // Create the 'shards' array
        Json::Value shards(Json::arrayValue);
        for (; i < sz; i += 2)
        {
            Json::Value shard(Json::objectValue);
            shard[jss::index] = jvParams[i].asUInt();
            shard[jss::url] = jvParams[i + 1].asString();
            shards.append(std::move(shard));
        }
        jvResult[jss::shards] = std::move(shards);

        return jvResult;
    }

    Json::Value
    parseInternal(Json::Value const& jvParams)
    {
        Json::Value v(Json::objectValue);
        v[jss::internal_command] = jvParams[0u];

        Json::Value params(Json::arrayValue);

        for (unsigned i = 1; i < jvParams.size(); ++i)
            params.append(jvParams[i]);

        v[jss::params] = params;

        return v;
    }

    Json::Value
    parseManifest(Json::Value const& jvParams)
    {
        if (jvParams.size() == 1)
        {
            Json::Value jvRequest(Json::objectValue);

            std::string const strPk = jvParams[0u].asString();
            if (!validPublicKey(strPk, TokenType::NodePublic))
                return rpcError(rpcPUBLIC_MALFORMED);

            jvRequest[jss::public_key] = strPk;

            return jvRequest;
        }

        return rpcError(rpcINVALID_PARAMS);
    }

    // fetch_info [clear]
    Json::Value
    parseFetchInfo(Json::Value const& jvParams)
    {
        Json::Value jvRequest(Json::objectValue);
        unsigned int iParams = jvParams.size();

        if (iParams != 0)
            jvRequest[jvParams[0u].asString()] = true;

        return jvRequest;
    }

    // account_tx accountID [ledger_min [ledger_max [limit [offset]]]] [binary]
    // [count] [descending]
    Json::Value
    parseAccountTransactions(Json::Value const& jvParams)
    {
        Json::Value jvRequest(Json::objectValue);
        unsigned int iParams = jvParams.size();

        auto const account = parseBase58<AccountID>(jvParams[0u].asString());
        if (!account)
            return rpcError(rpcACT_MALFORMED);

        jvRequest[jss::account] = toBase58(*account);

        bool bDone = false;

        while (!bDone && iParams >= 2)
        {
            // VFALCO Why is Json::StaticString appearing on the right side?
            if (jvParams[iParams - 1].asString() == jss::binary)
            {
                jvRequest[jss::binary] = true;
                --iParams;
            }
            else if (jvParams[iParams - 1].asString() == jss::count)
            {
                jvRequest[jss::count] = true;
                --iParams;
            }
            else if (jvParams[iParams - 1].asString() == jss::descending)
            {
                jvRequest[jss::descending] = true;
                --iParams;
            }
            else
            {
                bDone = true;
            }
        }

        if (1 == iParams)
        {
        }
        else if (2 == iParams)
        {
            if (!jvParseLedger(jvRequest, jvParams[1u].asString()))
                return jvRequest;
        }
        else
        {
            std::int64_t uLedgerMin = jvParams[1u].asInt();
            std::int64_t uLedgerMax = jvParams[2u].asInt();

            if (uLedgerMax != -1 && uLedgerMax < uLedgerMin)
            {
                // The command line always follows apiMaximumSupportedVersion
                if (RPC::apiMaximumSupportedVersion == 1)
                    return rpcError(rpcLGR_IDXS_INVALID);
                return rpcError(rpcNOT_SYNCED);
            }

            jvRequest[jss::ledger_index_min] = jvParams[1u].asInt();
            jvRequest[jss::ledger_index_max] = jvParams[2u].asInt();

            if (iParams >= 4)
                jvRequest[jss::limit] = jvParams[3u].asInt();

            if (iParams >= 5)
                jvRequest[jss::offset] = jvParams[4u].asInt();
        }

        return jvRequest;
    }

    // tx_account accountID [ledger_min [ledger_max [limit]]]] [binary] [count]
    // [forward]
    Json::Value
    parseTxAccount(Json::Value const& jvParams)
    {
        Json::Value jvRequest(Json::objectValue);
        unsigned int iParams = jvParams.size();

        auto const account = parseBase58<AccountID>(jvParams[0u].asString());
        if (!account)
            return rpcError(rpcACT_MALFORMED);

        jvRequest[jss::account] = toBase58(*account);

        bool bDone = false;

        while (!bDone && iParams >= 2)
        {
            if (jvParams[iParams - 1].asString() == jss::binary)
            {
                jvRequest[jss::binary] = true;
                --iParams;
            }
            else if (jvParams[iParams - 1].asString() == jss::count)
            {
                jvRequest[jss::count] = true;
                --iParams;
            }
            else if (jvParams[iParams - 1].asString() == jss::forward)
            {
                jvRequest[jss::forward] = true;
                --iParams;
            }
            else
            {
                bDone = true;
            }
        }

        if (1 == iParams)
        {
        }
        else if (2 == iParams)
        {
            if (!jvParseLedger(jvRequest, jvParams[1u].asString()))
                return jvRequest;
        }
        else
        {
            std::int64_t uLedgerMin = jvParams[1u].asInt();
            std::int64_t uLedgerMax = jvParams[2u].asInt();

            if (uLedgerMax != -1 && uLedgerMax < uLedgerMin)
            {
                // The command line always follows apiMaximumSupportedVersion
                if (RPC::apiMaximumSupportedVersion == 1)
                    return rpcError(rpcLGR_IDXS_INVALID);
                return rpcError(rpcNOT_SYNCED);
            }

            jvRequest[jss::ledger_index_min] = jvParams[1u].asInt();
            jvRequest[jss::ledger_index_max] = jvParams[2u].asInt();

            if (iParams >= 4)
                jvRequest[jss::limit] = jvParams[3u].asInt();
        }

        return jvRequest;
    }

    // book_offers <taker_pays> <taker_gets> [<taker> [<ledger> [<limit>
    // [<proof> [<marker>]]]]] limit: 0 = no limit proof: 0 or 1
    //
    // Mnemonic: taker pays --> offer --> taker gets
    Json::Value
    parseBookOffers(Json::Value const& jvParams)
    {
        Json::Value jvRequest(Json::objectValue);

        Json::Value jvTakerPays =
            jvParseCurrencyIssuer(jvParams[0u].asString());
        Json::Value jvTakerGets =
            jvParseCurrencyIssuer(jvParams[1u].asString());

        if (isRpcError(jvTakerPays))
        {
            return jvTakerPays;
        }
        else
        {
            jvRequest[jss::taker_pays] = jvTakerPays;
        }

        if (isRpcError(jvTakerGets))
        {
            return jvTakerGets;
        }
        else
        {
            jvRequest[jss::taker_gets] = jvTakerGets;
        }

        if (jvParams.size() >= 3)
        {
            jvRequest[jss::issuer] = jvParams[2u].asString();
        }

        if (jvParams.size() >= 4 &&
            !jvParseLedger(jvRequest, jvParams[3u].asString()))
            return jvRequest;

        if (jvParams.size() >= 5)
        {
            int iLimit = jvParams[5u].asInt();

            if (iLimit > 0)
                jvRequest[jss::limit] = iLimit;
        }

        if (jvParams.size() >= 6 && jvParams[5u].asInt())
        {
            jvRequest[jss::proof] = true;
        }

        if (jvParams.size() == 7)
            jvRequest[jss::marker] = jvParams[6u];

        return jvRequest;
    }

    // can_delete [<ledgerid>|<ledgerhash>|now|always|never]
    Json::Value
    parseCanDelete(Json::Value const& jvParams)
    {
        Json::Value jvRequest(Json::objectValue);

        if (!jvParams.size())
            return jvRequest;

        std::string input = jvParams[0u].asString();
        if (input.find_first_not_of("0123456789") == std::string::npos)
            jvRequest["can_delete"] = jvParams[0u].asUInt();
        else
            jvRequest["can_delete"] = input;

        return jvRequest;
    }

    // connect <ip[:port]> [port]
    Json::Value
    parseConnect(Json::Value const& jvParams)
    {
        Json::Value jvRequest(Json::objectValue);
        std::string ip = jvParams[0u].asString();
        if (jvParams.size() == 2)
        {
            jvRequest[jss::ip] = ip;
            jvRequest[jss::port] = jvParams[1u].asUInt();
            return jvRequest;
        }

        // handle case where there is one argument of the form ip:port
        if (std::count(ip.begin(), ip.end(), ':') == 1)
        {
            std::size_t colon = ip.find_last_of(":");
            jvRequest[jss::ip] = std::string{ip, 0, colon};
            jvRequest[jss::port] =
                Json::Value{std::string{ip, colon + 1}}.asUInt();
            return jvRequest;
        }

        // default case, no port
        jvRequest[jss::ip] = ip;
        return jvRequest;
    }

    // deposit_authorized <source_account> <destination_account> [<ledger>]
    Json::Value
    parseDepositAuthorized(Json::Value const& jvParams)
    {
        Json::Value jvRequest(Json::objectValue);
        jvRequest[jss::source_account] = jvParams[0u].asString();
        jvRequest[jss::destination_account] = jvParams[1u].asString();

        if (jvParams.size() == 3)
            jvParseLedger(jvRequest, jvParams[2u].asString());

        return jvRequest;
    }

    // Return an error for attemping to subscribe/unsubscribe via RPC.
    Json::Value
    parseEvented(Json::Value const& jvParams)
    {
        return rpcError(rpcNO_EVENTS);
    }

    // feature [<feature>] [accept|reject]
    Json::Value
    parseFeature(Json::Value const& jvParams)
    {
        Json::Value jvRequest(Json::objectValue);

        if (jvParams.size() > 0)
            jvRequest[jss::feature] = jvParams[0u].asString();

        if (jvParams.size() > 1)
        {
            auto const action = jvParams[1u].asString();

            // This may look reversed, but it's intentional: jss::vetoed
            // determines whether an amendment is vetoed - so "reject" means
            // that jss::vetoed is true.
            if (boost::iequals(action, "reject"))
                jvRequest[jss::vetoed] = Json::Value(true);
            else if (boost::iequals(action, "accept"))
                jvRequest[jss::vetoed] = Json::Value(false);
            else
                return rpcError(rpcINVALID_PARAMS);
        }

        return jvRequest;
    }

    // fee [<txblob>]
    Json::Value
    parseFee(Json::Value const& jvParams)
    {
        Json::Value jvRequest(Json::objectValue);

        if (jvParams.size() == 0)
        {
            return jvRequest;
        }
        else if (jvParams.size() > 1)
        {
            return rpcError(rpcINVALID_PARAMS);
        }

        jvRequest[jss::tx_blob] = jvParams[0u].asString();
        return jvRequest;
    }

    // get_counts [<min_count>]
    Json::Value
    parseGetCounts(Json::Value const& jvParams)
    {
        Json::Value jvRequest(Json::objectValue);

        if (jvParams.size())
            jvRequest[jss::min_count] = jvParams[0u].asUInt();

        return jvRequest;
    }

    // sign_for <account> <secret> <json> offline
    // sign_for <account> <secret> <json>
    Json::Value
    parseSignFor(Json::Value const& jvParams)
    {
        bool const bOffline =
            4 == jvParams.size() && jvParams[3u].asString() == "offline";

        if (3 == jvParams.size() || bOffline)
        {
            Json::Value txJSON;
            Json::Reader reader;
            if (reader.parse(jvParams[2u].asString(), txJSON))
            {
                // sign_for txJSON.
                Json::Value jvRequest{Json::objectValue};

                jvRequest[jss::account] = jvParams[0u].asString();
                jvRequest[jss::secret] = jvParams[1u].asString();
                jvRequest[jss::tx_json] = txJSON;

                if (bOffline)
                    jvRequest[jss::offline] = true;

                return jvRequest;
            }
        }
        return rpcError(rpcINVALID_PARAMS);
    }

    // json <command> <json>
    Json::Value
    parseJson(Json::Value const& jvParams)
    {
        Json::Reader reader;
        Json::Value jvRequest;

        JLOG(j_.trace()) << "RPC method: " << jvParams[0u];
        JLOG(j_.trace()) << "RPC json: " << jvParams[1u];

        if (reader.parse(jvParams[1u].asString(), jvRequest))
        {
            if (!jvRequest.isObjectOrNull())
                return rpcError(rpcINVALID_PARAMS);

            jvRequest[jss::method] = jvParams[0u];

            return jvRequest;
        }

        return rpcError(rpcINVALID_PARAMS);
    }

    bool
    isValidJson2(Json::Value const& jv)
    {
        if (jv.isArray())
        {
            if (jv.size() == 0)
                return false;
            for (auto const& j : jv)
            {
                if (!isValidJson2(j))
                    return false;
            }
            return true;
        }
        if (jv.isObject())
        {
            if (jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0" &&
                jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0" &&
                jv.isMember(jss::id) && jv.isMember(jss::method))
            {
                if (jv.isMember(jss::params) &&
                    !(jv[jss::params].isNull() || jv[jss::params].isArray() ||
                      jv[jss::params].isObject()))
                    return false;
                return true;
            }
        }
        return false;
    }

    Json::Value
    parseJson2(Json::Value const& jvParams)
    {
        Json::Reader reader;
        Json::Value jv;
        bool valid_parse = reader.parse(jvParams[0u].asString(), jv);
        if (valid_parse && isValidJson2(jv))
        {
            if (jv.isObject())
            {
                Json::Value jv1{Json::objectValue};
                if (jv.isMember(jss::params))
                {
                    auto const& params = jv[jss::params];
                    for (auto i = params.begin(); i != params.end(); ++i)
                        jv1[i.key().asString()] = *i;
                }
                jv1[jss::jsonrpc] = jv[jss::jsonrpc];
                jv1[jss::ripplerpc] = jv[jss::ripplerpc];
                jv1[jss::id] = jv[jss::id];
                jv1[jss::method] = jv[jss::method];
                return jv1;
            }
            // else jv.isArray()
            Json::Value jv1{Json::arrayValue};
            for (Json::UInt j = 0; j < jv.size(); ++j)
            {
                if (jv[j].isMember(jss::params))
                {
                    auto const& params = jv[j][jss::params];
                    for (auto i = params.begin(); i != params.end(); ++i)
                        jv1[j][i.key().asString()] = *i;
                }
                jv1[j][jss::jsonrpc] = jv[j][jss::jsonrpc];
                jv1[j][jss::ripplerpc] = jv[j][jss::ripplerpc];
                jv1[j][jss::id] = jv[j][jss::id];
                jv1[j][jss::method] = jv[j][jss::method];
            }
            return jv1;
        }
        auto jv_error = rpcError(rpcINVALID_PARAMS);
        if (jv.isMember(jss::jsonrpc))
            jv_error[jss::jsonrpc] = jv[jss::jsonrpc];
        if (jv.isMember(jss::ripplerpc))
            jv_error[jss::ripplerpc] = jv[jss::ripplerpc];
        if (jv.isMember(jss::id))
            jv_error[jss::id] = jv[jss::id];
        return jv_error;
    }

    // ledger [id|index|current|closed|validated] [full|tx]
    Json::Value
    parseLedger(Json::Value const& jvParams)
    {
        Json::Value jvRequest(Json::objectValue);

        if (!jvParams.size())
        {
            return jvRequest;
        }

        jvParseLedger(jvRequest, jvParams[0u].asString());

        if (2 == jvParams.size())
        {
            if (jvParams[1u].asString() == "full")
            {
                jvRequest[jss::full] = true;
            }
            else if (jvParams[1u].asString() == "tx")
            {
                jvRequest[jss::transactions] = true;
                jvRequest[jss::expand] = true;
            }
        }

        return jvRequest;
    }

    // ledger_header <id>|<index>
    Json::Value
    parseLedgerId(Json::Value const& jvParams)
    {
        Json::Value jvRequest(Json::objectValue);

        std::string strLedger = jvParams[0u].asString();

        if (strLedger.length() == 64)
        {
            jvRequest[jss::ledger_hash] = strLedger;
        }
        else
        {
            jvRequest[jss::ledger_index] =
                beast::lexicalCast<std::uint32_t>(strLedger);
        }

        return jvRequest;
    }

    // log_level:                           Get log levels
    // log_level <severity>:                Set master log level to the
    // specified severity log_level <partition> <severity>:    Set specified
    // partition to specified severity
    Json::Value
    parseLogLevel(Json::Value const& jvParams)
    {
        Json::Value jvRequest(Json::objectValue);

        if (jvParams.size() == 1)
        {
            jvRequest[jss::severity] = jvParams[0u].asString();
        }
        else if (jvParams.size() == 2)
        {
            jvRequest[jss::partition] = jvParams[0u].asString();
            jvRequest[jss::severity] = jvParams[1u].asString();
        }

        return jvRequest;
    }

    // owner_info <account>
    // account_info <account> [<ledger>]
    // account_offers <account> [<ledger>]
    Json::Value
    parseAccountItems(Json::Value const& jvParams)
    {
        return parseAccountRaw1(jvParams);
    }

    Json::Value
    parseAccountCurrencies(Json::Value const& jvParams)
    {
        return parseAccountRaw1(jvParams);
    }

    // account_lines <account> <account>|"" [<ledger>]
    Json::Value
    parseAccountLines(Json::Value const& jvParams)
    {
        return parseAccountRaw2(jvParams, jss::peer);
    }

    // account_namespace <account> <namespace hex> [<ledger>]
    Json::Value
    parseAccountNamespace(Json::Value const& jvParams)
    {
        return parseAccountNamespaceRaw(jvParams);
    }

    // account_channels <account> <account>|"" [<ledger>]
    Json::Value
    parseAccountChannels(Json::Value const& jvParams)
    {
        return parseAccountRaw2(jvParams, jss::destination_account);
    }

    // catalogue_create <min_ledger> <max_ledger> <output_file>
    // [compression_level]
    Json::Value
    parseCatalogueCreate(Json::Value const& jvParams)
    {
        Json::Value jvRequest(Json::objectValue);

        if (jvParams.size() >= 3)
        {
            jvRequest[jss::min_ledger] = jvParams[0u].asUInt();
            jvRequest[jss::max_ledger] = jvParams[1u].asUInt();
            jvRequest[jss::output_file] = jvParams[2u].asString();

            if (jvParams.size() >= 4)
            {
                // Handle compression level parameter
                if (jvParams[3u].isString())
                {
                    // If string parameter, convert to integer
                    jvRequest[jss::compression_level] =
                        beast::lexicalCast<std::uint32_t>(
                            jvParams[3u].asString());
                }
                else
                {
                    jvRequest[jss::compression_level] = jvParams[3u].asUInt();
                }
            }
        }

        return jvRequest;
    }

    // catalogue_load <input_file> [ignore_hash]
    Json::Value
    parseCatalogueLoad(Json::Value const& jvParams)
    {
        Json::Value jvRequest(Json::objectValue);

        if (jvParams.size() >= 1)
        {
            jvRequest[jss::input_file] = jvParams[0u].asString();

            if (jvParams.size() >= 2 &&
                boost::iequals(jvParams[1u].asString(), "ignore_hash"))
            {
                jvRequest[jss::ignore_hash] = true;
            }
        }

        return jvRequest;
    }

    // catalogue_status - no parameters required
    Json::Value
    parseCatalogueStatus(Json::Value const& jvParams)
    {
        // No special parameters needed - just return an empty object
        return {Json::objectValue};
    }

    // channel_authorize: <private_key> [<key_type>] <channel_id> <drops |
    // amount>
    Json::Value
    parseChannelAuthorize(Json::Value const& jvParams)
    {
        Json::Value jvRequest(Json::objectValue);

        unsigned int index = 0;

        if (jvParams.size() == 4)
        {
            jvRequest[jss::passphrase] = jvParams[index];
            index++;

            if (!keyTypeFromString(jvParams[index].asString()))
                return rpcError(rpcBAD_KEY_TYPE);
            jvRequest[jss::key_type] = jvParams[index];
            index++;
        }
        else
        {
            jvRequest[jss::secret] = jvParams[index];
            index++;
        }

        {
            // verify the channel id is a valid 256 bit number
            uint256 channelId;
            if (!channelId.parseHex(jvParams[index].asString()))
                return rpcError(rpcCHANNEL_MALFORMED);
            jvRequest[jss::channel_id] = to_string(channelId);
            index++;
        }

        {
            // validate amount string | json
            if (!jvParams[index].isString())
                return rpcError(rpcCHANNEL_AMT_MALFORMED);

            // parse string
            Json::Value amountJson =
                jvParseSTAmount(jvParams[index].asString());
            if (!amountJson)
            {
                // amount is string
                if (!to_uint64(jvParams[index].asString()))
                    return rpcError(rpcCHANNEL_AMT_MALFORMED);

                jvRequest[jss::amount] = jvParams[index].asString();
            }
            else
            {
                // amount is json
                STAmount amount;
                bool isAmount = amountFromJsonNoThrow(amount, amountJson);
                if (!isAmount)
                    return rpcError(rpcCHANNEL_AMT_MALFORMED);

                jvRequest[jss::amount] = amountJson;
            }
        }

        // If additional parameters are appended, be sure to increment index
        // here
        return jvRequest;
    }

    // channel_verify <public_key> <channel_id> <drops> <signature>
    Json::Value
    parseChannelVerify(Json::Value const& jvParams)
    {
        std::string const strPk = jvParams[0u].asString();

        if (!validPublicKey(strPk))
            return rpcError(rpcPUBLIC_MALFORMED);

        Json::Value jvRequest(Json::objectValue);

        jvRequest[jss::public_key] = strPk;
        {
            // verify the channel id is a valid 256 bit number
            uint256 channelId;
            if (!channelId.parseHex(jvParams[1u].asString()))
                return rpcError(rpcCHANNEL_MALFORMED);
        }
        jvRequest[jss::channel_id] = jvParams[1u].asString();

        {
            // validate amount string | json
            if (!jvParams[2u].isString())
                return rpcError(rpcCHANNEL_AMT_MALFORMED);
            // parse string
            Json::Value amountJson = jvParseSTAmount(jvParams[2u].asString());
            if (!amountJson)
            {
                // amount is string
                if (!to_uint64(jvParams[2u].asString()))
                    return rpcError(rpcCHANNEL_AMT_MALFORMED);

                jvRequest[jss::amount] = jvParams[2u].asString();
            }
            else
            {
                // amount is json
                STAmount amount;
                bool isAmount = amountFromJsonNoThrow(amount, amountJson);
                if (!isAmount)
                    return rpcError(rpcCHANNEL_AMT_MALFORMED);

                jvRequest[jss::amount] = amountJson;
            }
        }

        jvRequest[jss::signature] = jvParams[3u].asString();
        return jvRequest;
    }

    Json::Value
    parseAccountNamespaceRaw(Json::Value const& jvParams)
    {
        auto const nParams = jvParams.size();
        Json::Value jvRequest(Json::objectValue);

        for (auto i = 0; i < nParams; ++i)
        {
            std::string strParam = jvParams[i].asString();

            if (i == 0)
            {
                // account
                if (parseBase58<PublicKey>(
                        TokenType::AccountPublic, strParam) ||
                    parseBase58<AccountID>(strParam) ||
                    parseGenericSeed(strParam))
                {
                    jvRequest[jss::account] = std::move(strParam);
                }
                else
                {
                    return rpcError(rpcACT_MALFORMED);
                }
                continue;
            }

            if (i == 1)
            {
                // namespace hex
                uint256 namespaceId;
                if (!namespaceId.parseHex(strParam))
                    return rpcError(rpcNAMESPACE_MALFORMED);
                jvRequest[jss::namespace_id] = to_string(namespaceId);
                continue;
            }

            if (i == 2)
            {
                // ledger index (optional)
                if (strParam.empty())
                    break;

                if (jvParseLedger(jvRequest, strParam))
                    break;
                else
                    return rpcError(rpcLGR_IDX_MALFORMED);

                continue;
            }
        }

        jvRequest[jss::signature] = jvParams[3u].asString();
        return jvRequest;
    }

    Json::Value
    parseAccountRaw2(Json::Value const& jvParams, char const* const acc2Field)
    {
        std::array<char const* const, 2> accFields{{jss::account, acc2Field}};
        auto const nParams = jvParams.size();
        Json::Value jvRequest(Json::objectValue);
        for (auto i = 0; i < nParams; ++i)
        {
            std::string strParam = jvParams[i].asString();

            if (i == 1 && strParam.empty())
                continue;

            // Parameters 0 and 1 are accounts
            if (i < 2)
            {
                if (parseBase58<AccountID>(strParam))
                {
                    jvRequest[accFields[i]] = std::move(strParam);
                }
                else
                {
                    return rpcError(rpcACT_MALFORMED);
                }
            }
            else
            {
                if (jvParseLedger(jvRequest, strParam))
                    return jvRequest;
                return rpcError(rpcLGR_IDX_MALFORMED);
            }
        }

        return jvRequest;
    }

    // TODO: Get index from an alternate syntax: rXYZ:<index>
    Json::Value
    parseAccountRaw1(Json::Value const& jvParams)
    {
        std::string strIdent = jvParams[0u].asString();
        unsigned int iCursor = jvParams.size();

        if (!parseBase58<AccountID>(strIdent))
            return rpcError(rpcACT_MALFORMED);

        // Get info on account.
        Json::Value jvRequest(Json::objectValue);

        jvRequest[jss::account] = strIdent;

        if (iCursor == 2 && !jvParseLedger(jvRequest, jvParams[1u].asString()))
            return rpcError(rpcLGR_IDX_MALFORMED);

        return jvRequest;
    }

    Json::Value
    parseNodeToShard(Json::Value const& jvParams)
    {
        Json::Value jvRequest;
        jvRequest[jss::action] = jvParams[0u].asString();

        return jvRequest;
    }

    // peer_reservations_add <public_key> [<name>]
    Json::Value
    parsePeerReservationsAdd(Json::Value const& jvParams)
    {
        Json::Value jvRequest;
        jvRequest[jss::public_key] = jvParams[0u].asString();
        if (jvParams.size() > 1)
        {
            jvRequest[jss::description] = jvParams[1u].asString();
        }
        return jvRequest;
    }

    // peer_reservations_del <public_key>
    Json::Value
    parsePeerReservationsDel(Json::Value const& jvParams)
    {
        Json::Value jvRequest;
        jvRequest[jss::public_key] = jvParams[0u].asString();
        return jvRequest;
    }

    // ripple_path_find <json> [<ledger>]
    Json::Value
    parseRipplePathFind(Json::Value const& jvParams)
    {
        Json::Reader reader;
        Json::Value jvRequest{Json::objectValue};
        bool bLedger = 2 == jvParams.size();

        JLOG(j_.trace()) << "RPC json: " << jvParams[0u];

        if (reader.parse(jvParams[0u].asString(), jvRequest))
        {
            if (bLedger)
            {
                jvParseLedger(jvRequest, jvParams[1u].asString());
            }

            return jvRequest;
        }

        return rpcError(rpcINVALID_PARAMS);
    }

    // sign/submit any transaction to the network
    //
    // sign <private_key> <json> offline
    // submit <private_key> <json>
    // submit <tx_blob>
    Json::Value
    parseSignSubmit(Json::Value const& jvParams)
    {
        Json::Value txJSON;
        Json::Reader reader;
        bool const bOffline =
            3 == jvParams.size() && jvParams[2u].asString() == "offline";

        if (1 == jvParams.size())
        {
            // Submitting tx_blob

            Json::Value jvRequest{Json::objectValue};

            jvRequest[jss::tx_blob] = jvParams[0u].asString();

            return jvRequest;
        }
        else if (
            (2 == jvParams.size() || bOffline) &&
            reader.parse(jvParams[1u].asString(), txJSON))
        {
            // Signing or submitting tx_json.
            Json::Value jvRequest{Json::objectValue};

            jvRequest[jss::secret] = jvParams[0u].asString();
            jvRequest[jss::tx_json] = txJSON;

            if (bOffline)
                jvRequest[jss::offline] = true;

            return jvRequest;
        }

        return rpcError(rpcINVALID_PARAMS);
    }

    // submit any multisigned transaction to the network
    //
    // submit_multisigned <json>
    Json::Value
    parseSubmitMultiSigned(Json::Value const& jvParams)
    {
        if (1 == jvParams.size())
        {
            Json::Value txJSON;
            Json::Reader reader;
            if (reader.parse(jvParams[0u].asString(), txJSON))
            {
                Json::Value jvRequest{Json::objectValue};
                jvRequest[jss::tx_json] = txJSON;
                return jvRequest;
            }
        }

        return rpcError(rpcINVALID_PARAMS);
    }

    // transaction_entry <tx_hash> <ledger_hash/ledger_index>
    Json::Value
    parseTransactionEntry(Json::Value const& jvParams)
    {
        // Parameter count should have already been verified.
        assert(jvParams.size() == 2);

        std::string const txHash = jvParams[0u].asString();
        if (txHash.length() != 64)
            return rpcError(rpcINVALID_PARAMS);

        Json::Value jvRequest{Json::objectValue};
        jvRequest[jss::tx_hash] = txHash;

        jvParseLedger(jvRequest, jvParams[1u].asString());

        // jvParseLedger inserts a "ledger_index" of 0 if it doesn't
        // find a match.
        if (jvRequest.isMember(jss::ledger_index) &&
            jvRequest[jss::ledger_index] == 0)
            return rpcError(rpcINVALID_PARAMS);

        return jvRequest;
    }

    // tx <transaction_id>
    Json::Value
    parseTx(Json::Value const& jvParams)
    {
        Json::Value jvRequest{Json::objectValue};

        if (jvParams.size() == 2 || jvParams.size() == 4)
        {
            if (jvParams[1u].asString() == jss::binary)
                jvRequest[jss::binary] = true;
        }

        if (jvParams.size() >= 3)
        {
            const auto offset = jvParams.size() == 3 ? 0 : 1;

            jvRequest[jss::min_ledger] = jvParams[1u + offset].asString();
            jvRequest[jss::max_ledger] = jvParams[2u + offset].asString();
        }

        if (jvParams[0u].asString().length() == 16)
            jvRequest[jss::ctid] = jvParams[0u].asString();
        else
            jvRequest[jss::transaction] = jvParams[0u].asString();

        return jvRequest;
    }

    // tx_history <index>
    Json::Value
    parseTxHistory(Json::Value const& jvParams)
    {
        Json::Value jvRequest{Json::objectValue};

        jvRequest[jss::start] = jvParams[0u].asUInt();

        return jvRequest;
    }

    // validation_create [<pass_phrase>|<seed>|<seed_key>]
    //
    // NOTE: It is poor security to specify secret information on the command
    // line.  This information might be saved in the command shell history file
    // (e.g. .bash_history) and it may be leaked via the process status command
    // (i.e. ps).
    Json::Value
    parseValidationCreate(Json::Value const& jvParams)
    {
        Json::Value jvRequest{Json::objectValue};

        if (jvParams.size())
            jvRequest[jss::secret] = jvParams[0u].asString();

        return jvRequest;
    }

    // wallet_propose [<passphrase>]
    // <passphrase> is only for testing. Master seeds should only be generated
    // randomly.
    Json::Value
    parseWalletPropose(Json::Value const& jvParams)
    {
        Json::Value jvRequest{Json::objectValue};

        if (jvParams.size())
            jvRequest[jss::passphrase] = jvParams[0u].asString();

        return jvRequest;
    }

    // parse gateway balances
    // gateway_balances [<ledger>] <issuer_account> [ <hotwallet> [ <hotwallet>
    // ]]

    Json::Value
    parseGatewayBalances(Json::Value const& jvParams)
    {
        unsigned int index = 0;
        const unsigned int size = jvParams.size();

        Json::Value jvRequest{Json::objectValue};

        std::string param = jvParams[index++].asString();
        if (param.empty())
            return RPC::make_param_error("Invalid first parameter");

        if (param[0] != 'r')
        {
            if (param.size() == 64)
                jvRequest[jss::ledger_hash] = param;
            else
                jvRequest[jss::ledger_index] = param;

            if (size <= index)
                return RPC::make_param_error("Invalid hotwallet");

            param = jvParams[index++].asString();
        }

        jvRequest[jss::account] = param;

        if (index < size)
        {
            Json::Value& hotWallets =
                (jvRequest["hotwallet"] = Json::arrayValue);
            while (index < size)
                hotWallets.append(jvParams[index++].asString());
        }

        return jvRequest;
    }

    // server_info [counters]
    Json::Value
    parseServerInfo(Json::Value const& jvParams)
    {
        Json::Value jvRequest(Json::objectValue);
        if (jvParams.size() == 1 && jvParams[0u].asString() == "counters")
            jvRequest[jss::counters] = true;
        return jvRequest;
    }

public:
    //--------------------------------------------------------------------------

    explicit RPCParser(beast::Journal j) : j_(j)
    {
    }

    //--------------------------------------------------------------------------

    // Convert a rpc method and params to a request.
    // <-- { method: xyz, params: [... ] } or { error: ..., ... }
    Json::Value
    parseCommand(
        std::string strMethod,
        Json::Value jvParams,
        bool allowAnyCommand)
    {
        if (auto stream = j_.trace())
        {
            stream << "Method: '" << strMethod << "'";
            stream << "Params: " << jvParams;
        }

        struct Command
        {
            const char* name;
            parseFuncPtr parse;
            int minParams;
            int maxParams;
        };

        static constexpr Command commands[] = {
            // Request-response methods
            // - Returns an error, or the request.
            // - To modify the method, provide a new method in the request.
            {"account_currencies", &RPCParser::parseAccountCurrencies, 1, 3},
            {"account_info", &RPCParser::parseAccountItems, 1, 3},
            {"account_lines", &RPCParser::parseAccountLines, 1, 5},
            {"account_namespace", &RPCParser::parseAccountNamespace, 2, 3},
            {"account_channels", &RPCParser::parseAccountChannels, 1, 3},
            {"account_nfts", &RPCParser::parseAccountItems, 1, 5},
            {"account_objects", &RPCParser::parseAccountItems, 1, 5},
            {"account_offers", &RPCParser::parseAccountItems, 1, 4},
            {"account_tx", &RPCParser::parseAccountTransactions, 1, 8},
            {"book_changes", &RPCParser::parseLedgerId, 1, 1},
            {"book_offers", &RPCParser::parseBookOffers, 2, 7},
            {"can_delete", &RPCParser::parseCanDelete, 0, 1},
            {"catalogue_create", &RPCParser::parseCatalogueCreate, 3, 4},
            {"catalogue_load", &RPCParser::parseCatalogueLoad, 1, 2},
            {"catalogue_status", &RPCParser::parseCatalogueStatus, 0, 0},
            {"channel_authorize", &RPCParser::parseChannelAuthorize, 3, 4},
            {"channel_verify", &RPCParser::parseChannelVerify, 4, 4},
            {"connect", &RPCParser::parseConnect, 1, 2},
            {"consensus_info", &RPCParser::parseAsIs, 0, 0},
            {"deposit_authorized", &RPCParser::parseDepositAuthorized, 2, 3},
            {"download_shard", &RPCParser::parseDownloadShard, 2, -1},
            {"feature", &RPCParser::parseFeature, 0, 2},
            {"fee", &RPCParser::parseFee, 0, 1},
            {"fetch_info", &RPCParser::parseFetchInfo, 0, 1},
            {"gateway_balances", &RPCParser::parseGatewayBalances, 1, -1},
            {"get_counts", &RPCParser::parseGetCounts, 0, 1},
            {"json", &RPCParser::parseJson, 2, 2},
            {"json2", &RPCParser::parseJson2, 1, 1},
            {"ledger", &RPCParser::parseLedger, 0, 2},
            {"ledger_accept", &RPCParser::parseAsIs, 0, 0},
            {"ledger_closed", &RPCParser::parseAsIs, 0, 0},
            {"ledger_current", &RPCParser::parseAsIs, 0, 0},
            //      {   "ledger_entry",         &RPCParser::parseLedgerEntry,
            //      -1, -1   },
            {"ledger_header", &RPCParser::parseLedgerId, 1, 1},
            {"ledger_request", &RPCParser::parseLedgerId, 1, 1},
            {"log_level", &RPCParser::parseLogLevel, 0, 2},
            {"logrotate", &RPCParser::parseAsIs, 0, 0},
            {"manifest", &RPCParser::parseManifest, 1, 1},
            {"node_to_shard", &RPCParser::parseNodeToShard, 1, 1},
            {"owner_info", &RPCParser::parseAccountItems, 1, 3},
            {"peers", &RPCParser::parseAsIs, 0, 0},
            {"ping", &RPCParser::parseAsIs, 0, 0},
            {"print", &RPCParser::parseAsIs, 0, 1},
            //      {   "profile",              &RPCParser::parseProfile, 1,  9
            //      },
            {"random", &RPCParser::parseAsIs, 0, 0},
            {"peer_reservations_add",
             &RPCParser::parsePeerReservationsAdd,
             1,
             2},
            {"peer_reservations_del",
             &RPCParser::parsePeerReservationsDel,
             1,
             1},
            {"peer_reservations_list", &RPCParser::parseAsIs, 0, 0},
            {"ripple_path_find", &RPCParser::parseRipplePathFind, 1, 2},
            {"sign", &RPCParser::parseSignSubmit, 2, 3},
            {"sign_for", &RPCParser::parseSignFor, 3, 4},
            {"submit", &RPCParser::parseSignSubmit, 1, 3},
            {"submit_multisigned", &RPCParser::parseSubmitMultiSigned, 1, 1},
            {"server_info", &RPCParser::parseServerInfo, 0, 1},
            {"server_state", &RPCParser::parseServerInfo, 0, 1},
            {"crawl_shards", &RPCParser::parseAsIs, 0, 2},
            {"stop", &RPCParser::parseAsIs, 0, 0},
            {"transaction_entry", &RPCParser::parseTransactionEntry, 2, 2},
            {"tx", &RPCParser::parseTx, 1, 4},
            {"tx_account", &RPCParser::parseTxAccount, 1, 7},
            {"tx_history", &RPCParser::parseTxHistory, 1, 1},
            {"unl_list", &RPCParser::parseAsIs, 0, 0},
            {"validation_create", &RPCParser::parseValidationCreate, 0, 1},
            {"validator_info", &RPCParser::parseAsIs, 0, 0},
            {"version", &RPCParser::parseAsIs, 0, 0},
            {"wallet_propose", &RPCParser::parseWalletPropose, 0, 1},
            {"internal", &RPCParser::parseInternal, 1, -1},

            // Evented methods
            {"path_find", &RPCParser::parseEvented, -1, -1},
            {"subscribe", &RPCParser::parseEvented, -1, -1},
            {"unsubscribe", &RPCParser::parseEvented, -1, -1},
        };

        auto const count = jvParams.size();

        for (auto const& command : commands)
        {
            if (strMethod == command.name)
            {
                if ((command.minParams >= 0 && count < command.minParams) ||
                    (command.maxParams >= 0 && count > command.maxParams))
                {
                    JLOG(j_.debug())
                        << "Wrong number of parameters for " << command.name
                        << " minimum=" << command.minParams
                        << " maximum=" << command.maxParams
                        << " actual=" << count;

                    return rpcError(rpcBAD_SYNTAX);
                }

                return (this->*(command.parse))(jvParams);
            }
        }

        // The command could not be found
        if (!allowAnyCommand)
            return rpcError(rpcUNKNOWN_COMMAND);

        return parseAsIs(jvParams);
    }
};

//------------------------------------------------------------------------------

//
// JSON-RPC protocol.  Bitcoin speaks version 1.0 for maximum compatibility,
// but uses JSON-RPC 1.1/2.0 standards for parts of the 1.0 standard that were
// unspecified (HTTP errors and contents of 'error').
//
// 1.0 spec: http://json-rpc.org/wiki/specification
// 1.2 spec: http://groups.google.com/group/json-rpc/web/json-rpc-over-http
//

std::string
JSONRPCRequest(
    std::string const& strMethod,
    Json::Value const& params,
    Json::Value const& id)
{
    Json::Value request;
    request[jss::method] = strMethod;
    request[jss::params] = params;
    request[jss::id] = id;
    return to_string(request) + "\n";
}

namespace {
// Special local exception type thrown when request can't be parsed.
class RequestNotParseable : public std::runtime_error
{
    using std::runtime_error::runtime_error;  // Inherit constructors
};
};  // namespace

struct RPCCallImp
{
    explicit RPCCallImp() = default;

    // VFALCO NOTE Is this a to-do comment or a doc comment?
    // Place the async result somewhere useful.
    static void
    callRPCHandler(Json::Value* jvOutput, Json::Value const& jvInput)
    {
        (*jvOutput) = jvInput;
    }

    static bool
    onResponse(
        std::function<void(Json::Value const& jvInput)> callbackFuncP,
        const boost::system::error_code& ecResult,
        int iStatus,
        std::string const& strData,
        beast::Journal j)
    {
        if (callbackFuncP)
        {
            // Only care about the result, if we care to deliver it
            // callbackFuncP.

            // Receive reply
            if (strData.empty())
                Throw<std::runtime_error>("no response from server");

            // Parse reply
            JLOG(j.debug()) << "RPC reply: " << strData << std::endl;
            if (strData.find("Unable to parse request") == 0 ||
                strData.find(jss::invalid_API_version.c_str()) == 0)
                Throw<RequestNotParseable>(strData);
            Json::Reader reader;
            Json::Value jvReply;
            if (!reader.parse(strData, jvReply))
                Throw<std::runtime_error>("couldn't parse reply from server");

            if (!jvReply)
                Throw<std::runtime_error>(
                    "expected reply to have result, error and id properties");

            Json::Value jvResult(Json::objectValue);

            jvResult["result"] = jvReply;

            (callbackFuncP)(jvResult);
        }

        return false;
    }

    // Build the request.
    static void
    onRequest(
        std::string const& strMethod,
        Json::Value const& jvParams,
        std::unordered_map<std::string, std::string> const& headers,
        std::string const& strPath,
        boost::asio::streambuf& sb,
        std::string const& strHost,
        beast::Journal j)
    {
        JLOG(j.debug()) << "requestRPC: strPath='" << strPath << "'";

        std::ostream osRequest(&sb);
        osRequest << createHTTPPost(
            strHost,
            strPath,
            JSONRPCRequest(strMethod, jvParams, Json::Value(1)),
            headers);
    }
};

//------------------------------------------------------------------------------

// Used internally by rpcClient.
static Json::Value
rpcCmdLineToJson(
    std::vector<std::string> const& args,
    Json::Value& retParams,
    beast::Journal j)
{
    Json::Value jvRequest(Json::objectValue);

    RPCParser rpParser(j);
    Json::Value jvRpcParams(Json::arrayValue);

    for (int i = 1; i != args.size(); i++)
        jvRpcParams.append(args[i]);

    retParams = Json::Value(Json::objectValue);

    retParams[jss::method] = args[0];
    retParams[jss::params] = jvRpcParams;

    jvRequest = rpParser.parseCommand(args[0], jvRpcParams, true);

    auto insert_api_version = [](Json::Value& jr) {
        if (jr.isObject() && !jr.isMember(jss::error) &&
            !jr.isMember(jss::api_version))
        {
            jr[jss::api_version] = RPC::apiMaximumSupportedVersion;
        }
    };

    if (jvRequest.isObject())
        insert_api_version(jvRequest);
    else if (jvRequest.isArray())
        std::for_each(jvRequest.begin(), jvRequest.end(), insert_api_version);

    JLOG(j.trace()) << "RPC Request: " << jvRequest << std::endl;
    return jvRequest;
}

Json::Value
cmdLineToJSONRPC(std::vector<std::string> const& args, beast::Journal j)
{
    Json::Value jv = Json::Value(Json::objectValue);
    auto const paramsObj = rpcCmdLineToJson(args, jv, j);

    // Re-use jv to return our formatted result.
    jv.clear();

    // Allow parser to rewrite method.
    jv[jss::method] = paramsObj.isMember(jss::method)
        ? paramsObj[jss::method].asString()
        : args[0];

    // If paramsObj is not empty, put it in a [params] array.
    if (paramsObj.begin() != paramsObj.end())
    {
        auto& paramsArray = Json::setArray(jv, jss::params);
        paramsArray.append(paramsObj);
    }
    if (paramsObj.isMember(jss::jsonrpc))
        jv[jss::jsonrpc] = paramsObj[jss::jsonrpc];
    if (paramsObj.isMember(jss::ripplerpc))
        jv[jss::ripplerpc] = paramsObj[jss::ripplerpc];
    if (paramsObj.isMember(jss::id))
        jv[jss::id] = paramsObj[jss::id];
    return jv;
}

//------------------------------------------------------------------------------

std::pair<int, Json::Value>
rpcClient(
    std::vector<std::string> const& args,
    Config const& config,
    Logs& logs,
    std::unordered_map<std::string, std::string> const& headers)
{
    static_assert(
        rpcBAD_SYNTAX == 1 && rpcSUCCESS == 0,
        "Expect specific rpc enum values.");
    if (args.empty())
        return {rpcBAD_SYNTAX, {}};  // rpcBAD_SYNTAX = print usage

    int nRet = rpcSUCCESS;
    Json::Value jvOutput;
    Json::Value jvRequest(Json::objectValue);

    try
    {
        Json::Value jvRpc = Json::Value(Json::objectValue);
        jvRequest = rpcCmdLineToJson(args, jvRpc, logs.journal("RPCParser"));

        if (jvRequest.isMember(jss::error))
        {
            jvOutput = jvRequest;
            jvOutput["rpc"] = jvRpc;
        }
        else
        {
            ServerHandler::Setup setup;
            try
            {
                setup = setup_ServerHandler(
                    config,
                    beast::logstream{logs.journal("HTTPClient").warn()});
            }
            catch (std::exception const&)
            {
                // ignore any exceptions, so the command
                // line client works without a config file
            }

            if (config.rpc_ip)
            {
                setup.client.ip = config.rpc_ip->address().to_string();
                setup.client.port = config.rpc_ip->port();
            }

            Json::Value jvParams(Json::arrayValue);

            if (!setup.client.admin_user.empty())
                jvRequest["admin_user"] = setup.client.admin_user;

            if (!setup.client.admin_password.empty())
                jvRequest["admin_password"] = setup.client.admin_password;

            if (jvRequest.isObject())
                jvParams.append(jvRequest);
            else if (jvRequest.isArray())
            {
                for (Json::UInt i = 0; i < jvRequest.size(); ++i)
                    jvParams.append(jvRequest[i]);
            }

            {
                boost::asio::io_service isService;
                RPCCall::fromNetwork(
                    isService,
                    setup.client.ip,
                    setup.client.port,
                    setup.client.user,
                    setup.client.password,
                    "",
                    jvRequest.isMember(
                        jss::method)  // Allow parser to rewrite method.
                        ? jvRequest[jss::method].asString()
                        : jvRequest.isArray() ? "batch" : args[0],
                    jvParams,                  // Parsed, execute.
                    setup.client.secure != 0,  // Use SSL
                    config.quiet(),
                    logs,
                    std::bind(
                        RPCCallImp::callRPCHandler,
                        &jvOutput,
                        std::placeholders::_1),
                    headers);
                isService.run();  // This blocks until there are no more
                                  // outstanding async calls.
            }
            if (jvOutput.isMember("result"))
            {
                // Had a successful JSON-RPC 2.0 call.
                jvOutput = jvOutput["result"];

                // jvOutput may report a server side error.
                // It should report "status".
            }
            else
            {
                // Transport error.
                Json::Value jvRpcError = jvOutput;

                jvOutput = rpcError(rpcJSON_RPC);
                jvOutput["result"] = jvRpcError;
            }

            // If had an error, supply invocation in result.
            if (jvOutput.isMember(jss::error))
            {
                jvOutput["rpc"] =
                    jvRpc;  // How the command was seen as method + params.
                jvOutput["request_sent"] =
                    jvRequest;  // How the command was translated.
            }
        }

        if (jvOutput.isMember(jss::error))
        {
            jvOutput[jss::status] = "error";
            if (jvOutput.isMember(jss::error_code))
                nRet = std::stoi(jvOutput[jss::error_code].asString());
            else if (jvOutput[jss::error].isMember(jss::error_code))
                nRet =
                    std::stoi(jvOutput[jss::error][jss::error_code].asString());
            else
                nRet = rpcBAD_SYNTAX;
        }

        // YYY We could have a command line flag for single line output for
        // scripts. YYY We would intercept output here and simplify it.
    }
    catch (RequestNotParseable& e)
    {
        jvOutput = rpcError(rpcINVALID_PARAMS);
        jvOutput["error_what"] = e.what();
        nRet = rpcINVALID_PARAMS;
    }
    catch (std::exception& e)
    {
        jvOutput = rpcError(rpcINTERNAL);
        jvOutput["error_what"] = e.what();
        nRet = rpcINTERNAL;
    }

    return {nRet, std::move(jvOutput)};
}

//------------------------------------------------------------------------------

namespace RPCCall {

int
fromCommandLine(
    Config const& config,
    const std::vector<std::string>& vCmd,
    Logs& logs)
{
    auto const result = rpcClient(vCmd, config, logs);

    std::cout << result.second.toStyledString();

    return result.first;
}

//------------------------------------------------------------------------------

void
fromNetwork(
    boost::asio::io_service& io_service,
    std::string const& strIp,
    const std::uint16_t iPort,
    std::string const& strUsername,
    std::string const& strPassword,
    std::string const& strPath,
    std::string const& strMethod,
    Json::Value const& jvParams,
    const bool bSSL,
    const bool quiet,
    Logs& logs,
    std::function<void(Json::Value const& jvInput)> callbackFuncP,
    std::unordered_map<std::string, std::string> headers)
{
    auto j = logs.journal("HTTPClient");

    // Connect to localhost
    if (!quiet)
    {
        JLOG(j.info()) << (bSSL ? "Securely connecting to " : "Connecting to ")
                       << strIp << ":" << iPort << std::endl;
    }

    // HTTP basic authentication
    headers["Authorization"] =
        std::string("Basic ") + base64_encode(strUsername + ":" + strPassword);

    // Send request

    // Number of bytes to try to receive if no
    // Content-Length header received
    constexpr auto RPC_REPLY_MAX_BYTES = megabytes(256);

    using namespace std::chrono_literals;
    // auto constexpr RPC_NOTIFY = 10min; // Wietse: lolwut 10 minutes for one
    // HTTP call?
    auto constexpr RPC_NOTIFY = 30s;

    HTTPClient::request(
        bSSL,
        io_service,
        strIp,
        iPort,
        std::bind(
            &RPCCallImp::onRequest,
            strMethod,
            jvParams,
            headers,
            strPath,
            std::placeholders::_1,
            std::placeholders::_2,
            j),
        RPC_REPLY_MAX_BYTES,
        RPC_NOTIFY,
        std::bind(
            &RPCCallImp::onResponse,
            callbackFuncP,
            std::placeholders::_1,
            std::placeholders::_2,
            std::placeholders::_3,
            j),
        j);
}

}  // namespace RPCCall

}  // namespace ripple
