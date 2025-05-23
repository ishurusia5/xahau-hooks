//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#include <ripple/app/hook/Enum.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/json_value.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

#include <boost/utility/string_ref.hpp>

#include <algorithm>

namespace ripple {
namespace test {

static char const* bobs_account_objects[] = {
    R"json({
  "Account" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
  "BookDirectory" : "B025997A323F5C3E03DDF1334471F5984ABDE31C59D463525D038D7EA4C68000",
  "BookNode" : "0",
  "Flags" : 65536,
  "LedgerEntryType" : "Offer",
  "OwnerNode" : "0",
  "Sequence" : 4,
  "TakerGets" : {
    "currency" : "USD",
    "issuer" : "r32rQHyesiTtdWFU7UJVtff4nCR5SHCbJW",
    "value" : "1"
  },
  "TakerPays" : "100000000",
  "index" : "A984D036A0E562433A8377CA57D1A1E056E58C0D04818F8DFD3A1AA3F217DD82"
})json",
    R"json({
    "Balance" : {
        "currency" : "USD",
        "issuer" : "rrrrrrrrrrrrrrrrrrrrBZbvji",
        "value" : "-1000"
    },
    "Flags" : 131072,
    "HighLimit" : {
        "currency" : "USD",
        "issuer" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
        "value" : "1000"
    },
    "HighNode" : "0",
    "LedgerEntryType" : "RippleState",
    "LowLimit" : {
        "currency" : "USD",
        "issuer" : "r9cZvwKU3zzuZK9JFovGg1JC5n7QiqNL8L",
        "value" : "0"
    },
    "LowNode" : "0",
    "index" : "D13183BCFFC9AAC9F96AEBB5F66E4A652AD1F5D10273AEB615478302BEBFD4A4"
})json",
    R"json({
    "Balance" : {
        "currency" : "USD",
        "issuer" : "rrrrrrrrrrrrrrrrrrrrBZbvji",
        "value" : "-1000"
    },
    "Flags" : 131072,
    "HighLimit" : {
        "currency" : "USD",
        "issuer" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
        "value" : "1000"
    },
    "HighNode" : "0",
    "LedgerEntryType" : "RippleState",
    "LowLimit" : {
        "currency" : "USD",
        "issuer" : "r32rQHyesiTtdWFU7UJVtff4nCR5SHCbJW",
        "value" : "0"
    },
    "LowNode" : "0",
    "index" : "D89BC239086183EB9458C396E643795C1134963E6550E682A190A5F021766D43"
})json",
    R"json({
    "Account" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
    "BookDirectory" : "50AD0A9E54D2B381288D535EB724E4275FFBF41580D28A925D038D7EA4C68000",
    "BookNode" : "0",
    "Flags" : 65536,
    "LedgerEntryType" : "Offer",
    "OwnerNode" : "0",
    "Sequence" : 3,
    "TakerGets" : {
        "currency" : "USD",
        "issuer" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
        "value" : "1"
    },
    "TakerPays" : "100000000",
    "index" : "E11029302EE744401427793A4F37BCB18F698D55C96851BEC5ABBD6242CF03D7"
})json"};

class AccountObjects_test : public beast::unit_test::suite
{
public:
#define HSFEE fee(100'000'000)
    void
    testErrors(FeatureBitset features)
    {
        testcase("error cases");

        using namespace jtx;
        Env env(*this, features);

        // test error on no account
        {
            auto resp = env.rpc("json", "account_objects");
            BEAST_EXPECT(resp[jss::error_message] == "Syntax error.");
        }
        // test error on  malformed account string.
        {
            Json::Value params;
            params[jss::account] =
                "n94JNrQYkDrpt62bbSR7nVEhdyAvcJXRAsjEkFYyqRkh9SUTYEqV";
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] == "Account malformed.");
        }
        // test error on account that's not in the ledger.
        {
            Json::Value params;
            params[jss::account] = Account{"bogie"}.human();
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] == "Account not found.");
        }
        Account const bob{"bob"};
        // test error on large ledger_index.
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::ledger_index] = 10;
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] == "ledgerNotFound");
        }

        env.fund(XRP(1000), bob);
        // test error on type param not a string
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::type] = 10;
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'type', not string.");
        }
        // test error on type param not a valid type
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::type] = "expedited";
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'type'.");
        }
        // test error on limit -ve
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::limit] = -1;
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'limit', not unsigned integer.");
        }
        // test errors on marker
        {
            Account const gw{"G"};
            env.fund(XRP(1000), gw);
            auto const USD = gw["USD"];
            env.trust(USD(1000), bob);
            env(pay(gw, bob, XRP(1)));
            env(offer(bob, XRP(100), bob["USD"](1)), txflags(tfPassive));

            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::limit] = 1;
            auto resp = env.rpc("json", "account_objects", to_string(params));

            auto resume_marker = resp[jss::result][jss::marker];
            std::string mark = to_string(resume_marker);
            params[jss::marker] = 10;
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'marker', not string.");

            params[jss::marker] = "This is a string with no comma";
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'marker'.");

            params[jss::marker] = "This string has a comma, but is not hex";
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'marker'.");

            params[jss::marker] = std::string(&mark[1U], 64);
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'marker'.");

            params[jss::marker] = std::string(&mark[1U], 65);
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'marker'.");

            params[jss::marker] = std::string(&mark[1U], 65) + "not hex";
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'marker'.");

            // Should this be an error?
            // A hex digit is absent from the end of marker.
            // No account objects returned.
            params[jss::marker] = std::string(&mark[1U], 128);
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(resp[jss::result][jss::account_objects].size() == 0);
        }
    }

    void
    testUnsteppedThenStepped(FeatureBitset features)
    {
        testcase("unsteppedThenStepped");

        using namespace jtx;
        Env env(*this, features);

        Account const gw1{"G1"};
        Account const gw2{"G2"};
        Account const bob{"bob"};

        auto const USD1 = gw1["USD"];
        auto const USD2 = gw2["USD"];

        env.fund(XRP(1000), gw1, gw2, bob);
        env.trust(USD1(1000), bob);
        env.trust(USD2(1000), bob);

        env(pay(gw1, bob, USD1(1000)));
        env(pay(gw2, bob, USD2(1000)));

        env(offer(bob, XRP(100), bob["USD"](1)), txflags(tfPassive));
        env(offer(bob, XRP(100), USD1(1)), txflags(tfPassive));

        Json::Value bobj[4];
        for (int i = 0; i < 4; ++i)
            Json::Reader{}.parse(bobs_account_objects[i], bobj[i]);

        // test 'unstepped'
        // i.e. request account objects without explicit limit/marker paging
        {
            Json::Value params;
            params[jss::account] = bob.human();
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(!resp.isMember(jss::marker));

            BEAST_EXPECT(resp[jss::result][jss::account_objects].size() == 4);
            for (int i = 0; i < 4; ++i)
            {
                auto& aobj = resp[jss::result][jss::account_objects][i];
                aobj.removeMember("PreviousTxnID");
                aobj.removeMember("PreviousTxnLgrSeq");
                BEAST_EXPECT(aobj == bobj[i]);
            }
        }
        // test request with type parameter as filter, unstepped
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::type] = jss::state;
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(!resp.isMember(jss::marker));

            BEAST_EXPECT(resp[jss::result][jss::account_objects].size() == 2);
            for (int i = 0; i < 2; ++i)
            {
                auto& aobj = resp[jss::result][jss::account_objects][i];
                aobj.removeMember("PreviousTxnID");
                aobj.removeMember("PreviousTxnLgrSeq");
                BEAST_EXPECT(aobj == bobj[i + 1]);
            }
        }
        // test stepped one-at-a-time with limit=1, resume from prev marker
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::limit] = 1;
            for (int i = 0; i < 4; ++i)
            {
                auto resp =
                    env.rpc("json", "account_objects", to_string(params));
                auto& aobjs = resp[jss::result][jss::account_objects];
                BEAST_EXPECT(aobjs.size() == 1);
                auto& aobj = aobjs[0U];
                if (i < 3)
                    BEAST_EXPECT(resp[jss::result][jss::limit] == 1);
                else
                    BEAST_EXPECT(!resp[jss::result].isMember(jss::limit));

                aobj.removeMember("PreviousTxnID");
                aobj.removeMember("PreviousTxnLgrSeq");
                BEAST_EXPECT(aobj == bobj[i]);
                params[jss::marker] = resp[jss::result][jss::marker];
            }
        }
    }

    void
    testUnsteppedThenSteppedWithNFTs(FeatureBitset features)
    {
        // The preceding test case, unsteppedThenStepped(), found a bug in the
        // support for NFToken Pages.  So we're leaving that test alone when
        // adding tests to exercise NFTokenPages.
        testcase("unsteppedThenSteppedWithNFTs");

        using namespace jtx;
        Env env(*this, features);

        Account const gw1{"G1"};
        Account const gw2{"G2"};
        Account const bob{"bob"};

        auto const USD1 = gw1["USD"];
        auto const USD2 = gw2["USD"];

        env.fund(XRP(1000), gw1, gw2, bob);
        env.close();

        // Check behavior if there are no account objects.
        {
            // Unpaged
            Json::Value params;
            params[jss::account] = bob.human();
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(!resp.isMember(jss::marker));
            BEAST_EXPECT(resp[jss::result][jss::account_objects].size() == 0);

            // Limit == 1
            params[jss::limit] = 1;
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(!resp.isMember(jss::marker));
            BEAST_EXPECT(resp[jss::result][jss::account_objects].size() == 0);
        }

        // Check behavior if there are only NFTokens.
        env(token::mint(bob, 0u), txflags(tfTransferable));
        env.close();

        // test 'unstepped'
        // i.e. request account objects without explicit limit/marker paging
        Json::Value unpaged;
        {
            Json::Value params;
            params[jss::account] = bob.human();
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(!resp.isMember(jss::marker));

            unpaged = resp[jss::result][jss::account_objects];
            BEAST_EXPECT(unpaged.size() == 1);
        }
        // test request with type parameter as filter, unstepped
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::type] = jss::nft_page;
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(!resp.isMember(jss::marker));
            Json::Value& aobjs = resp[jss::result][jss::account_objects];
            BEAST_EXPECT(aobjs.size() == 1);
            BEAST_EXPECT(
                aobjs[0u][sfLedgerEntryType.jsonName] == jss::NFTokenPage);
            BEAST_EXPECT(aobjs[0u][sfNFTokens.jsonName].size() == 1);
        }
        // test stepped one-at-a-time with limit=1, resume from prev marker
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::limit] = 1;

            Json::Value resp =
                env.rpc("json", "account_objects", to_string(params));
            Json::Value& aobjs = resp[jss::result][jss::account_objects];
            BEAST_EXPECT(aobjs.size() == 1);
            auto& aobj = aobjs[0U];
            BEAST_EXPECT(!resp[jss::result].isMember(jss::limit));
            BEAST_EXPECT(!resp[jss::result].isMember(jss::marker));

            BEAST_EXPECT(aobj == unpaged[0u]);
        }

        // Add more objects in addition to the NFToken Page.
        env.trust(USD1(1000), bob);
        env.trust(USD2(1000), bob);

        env(pay(gw1, bob, USD1(1000)));
        env(pay(gw2, bob, USD2(1000)));

        env(offer(bob, XRP(100), bob["USD"](1)), txflags(tfPassive));
        env(offer(bob, XRP(100), USD1(1)), txflags(tfPassive));
        env.close();

        // test 'unstepped'
        {
            Json::Value params;
            params[jss::account] = bob.human();
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(!resp.isMember(jss::marker));

            unpaged = resp[jss::result][jss::account_objects];
            BEAST_EXPECT(unpaged.size() == 5);
        }
        // test request with type parameter as filter, unstepped
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::type] = jss::nft_page;
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(!resp.isMember(jss::marker));
            Json::Value& aobjs = resp[jss::result][jss::account_objects];
            BEAST_EXPECT(aobjs.size() == 1);
            BEAST_EXPECT(
                aobjs[0u][sfLedgerEntryType.jsonName] == jss::NFTokenPage);
            BEAST_EXPECT(aobjs[0u][sfNFTokens.jsonName].size() == 1);
        }
        // test stepped one-at-a-time with limit=1, resume from prev marker
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::limit] = 1;
            for (int i = 0; i < 5; ++i)
            {
                Json::Value resp =
                    env.rpc("json", "account_objects", to_string(params));
                Json::Value& aobjs = resp[jss::result][jss::account_objects];
                BEAST_EXPECT(aobjs.size() == 1);
                auto& aobj = aobjs[0U];
                if (i < 4)
                {
                    BEAST_EXPECT(resp[jss::result][jss::limit] == 1);
                    BEAST_EXPECT(resp[jss::result].isMember(jss::marker));
                }
                else
                {
                    BEAST_EXPECT(!resp[jss::result].isMember(jss::limit));
                    BEAST_EXPECT(!resp[jss::result].isMember(jss::marker));
                }

                BEAST_EXPECT(aobj == unpaged[i]);

                params[jss::marker] = resp[jss::result][jss::marker];
            }
        }

        // Make sure things still work if there is more than 1 NFT Page.
        for (int i = 0; i < 32; ++i)
        {
            env(token::mint(bob, 0u), txflags(tfTransferable));
            env.close();
        }
        // test 'unstepped'
        {
            Json::Value params;
            params[jss::account] = bob.human();
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(!resp.isMember(jss::marker));

            unpaged = resp[jss::result][jss::account_objects];
            BEAST_EXPECT(unpaged.size() == 6);
        }
        // test request with type parameter as filter, unstepped
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::type] = jss::nft_page;
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(!resp.isMember(jss::marker));
            Json::Value& aobjs = resp[jss::result][jss::account_objects];
            BEAST_EXPECT(aobjs.size() == 2);
        }
        // test stepped one-at-a-time with limit=1, resume from prev marker
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::limit] = 1;
            for (int i = 0; i < 6; ++i)
            {
                Json::Value resp =
                    env.rpc("json", "account_objects", to_string(params));
                Json::Value& aobjs = resp[jss::result][jss::account_objects];
                BEAST_EXPECT(aobjs.size() == 1);
                auto& aobj = aobjs[0U];
                if (i < 5)
                {
                    BEAST_EXPECT(resp[jss::result][jss::limit] == 1);
                    BEAST_EXPECT(resp[jss::result].isMember(jss::marker));
                }
                else
                {
                    BEAST_EXPECT(!resp[jss::result].isMember(jss::limit));
                    BEAST_EXPECT(!resp[jss::result].isMember(jss::marker));
                }

                BEAST_EXPECT(aobj == unpaged[i]);

                params[jss::marker] = resp[jss::result][jss::marker];
            }
        }
    }

    void
    testObjectTypes(FeatureBitset features)
    {
        testcase("object types");

        // Give gw a bunch of ledger objects and make sure we can retrieve
        // them by type.
        using namespace jtx;

        Account const alice{"alice"};
        Account const gw{"gateway"};
        auto const USD = gw["USD"];

        Env env(*this, features);

        // Make a lambda we can use to get "account_objects" easily.
        auto acct_objs = [&env](Account const& acct, char const* type) {
            Json::Value params;
            params[jss::account] = acct.human();
            params[jss::type] = type;
            params[jss::ledger_index] = "validated";
            return env.rpc("json", "account_objects", to_string(params));
        };

        // Make a lambda that easily identifies the size of account objects.
        auto acct_objs_is_size = [](Json::Value const& resp, unsigned size) {
            return resp[jss::result][jss::account_objects].isArray() &&
                (resp[jss::result][jss::account_objects].size() == size);
        };

        env.fund(XRP(10000), gw, alice);
        env.close();

        // Since the account is empty now, all account objects should come
        // back empty.
        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::account), 0));
        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::amendments), 0));
        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::check), 0));
        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::deposit_preauth), 0));
        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::directory), 0));
        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::escrow), 0));
        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::fee), 0));
        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::hashes), 0));
        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::nft_page), 0));
        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::offer), 0));
        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::payment_channel), 0));
        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::signer_list), 0));
        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::state), 0));
        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::ticket), 0));
        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::uri_token), 0));
        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::hook), 0));

        // gw mints an NFT so we can find it.
        uint256 const nftID{token::getNextID(env, gw, 0u, tfTransferable)};
        env(token::mint(gw, 0u), txflags(tfTransferable));
        env.close();
        {
            // Find the NFToken page and make sure it's the right one.
            Json::Value const resp = acct_objs(gw, jss::nft_page);
            BEAST_EXPECT(acct_objs_is_size(resp, 1));

            auto const& nftPage = resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(nftPage[sfNFTokens.jsonName].size() == 1);
            BEAST_EXPECT(
                nftPage[sfNFTokens.jsonName][0u][sfNFToken.jsonName]
                       [sfNFTokenID.jsonName] == to_string(nftID));
        }

        // Set up a trust line so we can find it.
        env.trust(USD(1000), alice);
        env.close();
        env(pay(gw, alice, USD(5)));
        env.close();
        {
            // Find the trustline and make sure it's the right one.
            Json::Value const resp = acct_objs(gw, jss::state);
            BEAST_EXPECT(acct_objs_is_size(resp, 1));

            auto const& state = resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(state[sfBalance.jsonName][jss::value].asInt() == -5);
            BEAST_EXPECT(
                state[sfHighLimit.jsonName][jss::value].asUInt() == 1000);
        }
        // gw writes a check for USD(10) to alice.
        env(check::create(gw, alice, USD(10)));
        env.close();
        {
            // Find the check.
            Json::Value const resp = acct_objs(gw, jss::check);
            BEAST_EXPECT(acct_objs_is_size(resp, 1));

            auto const& check = resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(check[sfAccount.jsonName] == gw.human());
            BEAST_EXPECT(check[sfDestination.jsonName] == alice.human());
            BEAST_EXPECT(check[sfSendMax.jsonName][jss::value].asUInt() == 10);
        }
        // gw preauthorizes payments from alice.
        env(deposit::auth(gw, alice));
        env.close();
        {
            // Find the preauthorization.
            Json::Value const resp = acct_objs(gw, jss::deposit_preauth);
            BEAST_EXPECT(acct_objs_is_size(resp, 1));

            auto const& preauth = resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(preauth[sfAccount.jsonName] == gw.human());
            BEAST_EXPECT(preauth[sfAuthorize.jsonName] == alice.human());
        }
        {
            // gw creates an escrow that we can look for in the ledger.
            Json::Value jvEscrow;
            jvEscrow[jss::TransactionType] = jss::EscrowCreate;
            jvEscrow[jss::Flags] = tfUniversal;
            jvEscrow[jss::Account] = gw.human();
            jvEscrow[jss::Destination] = gw.human();
            jvEscrow[jss::Amount] = XRP(100).value().getJson(JsonOptions::none);
            jvEscrow[sfFinishAfter.jsonName] =
                env.now().time_since_epoch().count() + 1;
            env(jvEscrow);
            env.close();
        }
        {
            // Find the escrow.
            Json::Value const resp = acct_objs(gw, jss::escrow);
            BEAST_EXPECT(acct_objs_is_size(resp, 1));

            auto const& escrow = resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(escrow[sfAccount.jsonName] == gw.human());
            BEAST_EXPECT(escrow[sfDestination.jsonName] == gw.human());
            BEAST_EXPECT(escrow[sfAmount.jsonName].asUInt() == 100'000'000);
        }
        // gw creates an offer that we can look for in the ledger.
        env(offer(gw, USD(7), XRP(14)));
        env.close();
        {
            // Find the offer.
            Json::Value const resp = acct_objs(gw, jss::offer);
            BEAST_EXPECT(acct_objs_is_size(resp, 1));

            auto const& offer = resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(offer[sfAccount.jsonName] == gw.human());
            BEAST_EXPECT(offer[sfTakerGets.jsonName].asUInt() == 14'000'000);
            BEAST_EXPECT(offer[sfTakerPays.jsonName][jss::value].asUInt() == 7);
        }
        {
            // Create a payment channel from qw to alice that we can look for.
            Json::Value jvPayChan;
            jvPayChan[jss::TransactionType] = jss::PaymentChannelCreate;
            jvPayChan[jss::Flags] = tfUniversal;
            jvPayChan[jss::Account] = gw.human();
            jvPayChan[jss::Destination] = alice.human();
            jvPayChan[jss::Amount] =
                XRP(300).value().getJson(JsonOptions::none);
            jvPayChan[sfSettleDelay.jsonName] = 24 * 60 * 60;
            jvPayChan[sfPublicKey.jsonName] = strHex(gw.pk().slice());
            env(jvPayChan);
            env.close();
        }
        {
            // Find the payment channel.
            Json::Value const resp = acct_objs(gw, jss::payment_channel);
            BEAST_EXPECT(acct_objs_is_size(resp, 1));

            auto const& payChan = resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(payChan[sfAccount.jsonName] == gw.human());
            BEAST_EXPECT(payChan[sfAmount.jsonName].asUInt() == 300'000'000);
            BEAST_EXPECT(
                payChan[sfSettleDelay.jsonName].asUInt() == 24 * 60 * 60);
        }
        // Make gw multisigning by adding a signerList.
        env(signers(gw, 6, {{alice, 7}}));
        env.close();
        {
            // Find the signer list.
            Json::Value const resp = acct_objs(gw, jss::signer_list);
            BEAST_EXPECT(acct_objs_is_size(resp, 1));

            auto const& signerList =
                resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(signerList[sfSignerQuorum.jsonName] == 6);
            auto const& entry = signerList[sfSignerEntries.jsonName][0u]
                                          [sfSignerEntry.jsonName];
            BEAST_EXPECT(entry[sfAccount.jsonName] == alice.human());
            BEAST_EXPECT(entry[sfSignerWeight.jsonName].asUInt() == 7);
        }
        // Create a Ticket for gw.
        env(ticket::create(gw, 1));
        env.close();
        {
            // Find the ticket.
            Json::Value const resp = acct_objs(gw, jss::ticket);
            BEAST_EXPECT(acct_objs_is_size(resp, 1));

            auto const& ticket = resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(ticket[sfAccount.jsonName] == gw.human());
            BEAST_EXPECT(ticket[sfLedgerEntryType.jsonName] == jss::Ticket);
            BEAST_EXPECT(ticket[sfTicketSequence.jsonName].asUInt() == 10);
        }
        {
            // Create a uri token.
            std::string const uri(maxTokenURILength, '?');
            Json::Value jfURIToken;
            jfURIToken[jss::TransactionType] = jss::URITokenMint;
            jfURIToken[jss::Flags] = tfBurnable;
            jfURIToken[jss::Account] = gw.human();
            jfURIToken[sfURI.jsonName] = strHex(uri);
            env(jfURIToken);
            env.close();
        }
        {
            // Find the uri token.
            std::string const uri(maxTokenURILength, '?');
            Json::Value const resp = acct_objs(gw, jss::uri_token);
            BEAST_EXPECT(acct_objs_is_size(resp, 1));

            auto const& uritoken = resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(uritoken[sfOwner.jsonName] == gw.human());
            BEAST_EXPECT(uritoken[sfIssuer.jsonName] == gw.human());
            BEAST_EXPECT(uritoken[sfURI.jsonName] == strHex(uri));
        }
        {
            // Create hook
            auto setHook = [](test::jtx::Account const& account) {
                std::string const createCodeHex =
                    "0061736D0100000001130360037F7F7E017E60027F7F017F60017F017E"
                    "02170203656E7606616363657074000003656E76025F67000103020102"
                    "0503010002062B077F01418088040B7F004180080B7F004180080B7F00"
                    "4180080B7F00418088040B7F0041000B7F0041010B07080104686F6F6B"
                    "00020AB5800001B1800001017F230041106B220124002001200036020C"
                    "410022002000420010001A41012200200010011A200141106A24004200"
                    "0B";
                Json::Value jv =
                    ripple::test::jtx::hook(account, {{hso(createCodeHex)}}, 0);
                return jv;
            };
            env(setHook(gw), HSFEE);
            env.close();
        }
        {
            // Find the hook.
            Json::Value const resp = acct_objs(gw, jss::hook);
            BEAST_EXPECT(acct_objs_is_size(resp, 1));
            auto const& hook = resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(hook[sfAccount.jsonName] == gw.human());
        }
        {
            // See how "deletion_blockers_only" handles gw's directory.
            Json::Value params;
            params[jss::account] = gw.human();
            params[jss::deletion_blockers_only] = true;
            auto resp = env.rpc("json", "account_objects", to_string(params));

            std::vector<std::string> const expectedLedgerTypes = [] {
                std::vector<std::string> v{
                    jss::Escrow.c_str(),
                    jss::Hook.c_str(),
                    jss::Check.c_str(),
                    jss::NFTokenPage.c_str(),
                    jss::RippleState.c_str(),
                    jss::PayChannel.c_str(),
                    jss::URIToken.c_str()};
                std::sort(v.begin(), v.end());
                return v;
            }();

            std::uint32_t const expectedAccountObjects{
                static_cast<std::uint32_t>(std::size(expectedLedgerTypes))};

            if (BEAST_EXPECT(acct_objs_is_size(resp, expectedAccountObjects)))
            {
                auto const& aobjs = resp[jss::result][jss::account_objects];
                std::vector<std::string> gotLedgerTypes;
                gotLedgerTypes.reserve(expectedAccountObjects);
                for (std::uint32_t i = 0; i < expectedAccountObjects; ++i)
                {
                    gotLedgerTypes.push_back(
                        aobjs[i]["LedgerEntryType"].asString());
                }
                std::sort(gotLedgerTypes.begin(), gotLedgerTypes.end());
                BEAST_EXPECT(gotLedgerTypes == expectedLedgerTypes);
            }
        }
        {
            // See how "deletion_blockers_only" with `type` handles gw's
            // directory.
            Json::Value params;
            params[jss::account] = gw.human();
            params[jss::deletion_blockers_only] = true;
            params[jss::type] = jss::escrow;
            auto resp = env.rpc("json", "account_objects", to_string(params));

            if (BEAST_EXPECT(acct_objs_is_size(resp, 1u)))
            {
                auto const& aobjs = resp[jss::result][jss::account_objects];
                BEAST_EXPECT(aobjs[0u]["LedgerEntryType"] == jss::Escrow);
            }
        }

        // Run up the number of directory entries so gw has two
        // directory nodes.
        for (int d = 1'000'032; d >= 1'000'000; --d)
        {
            env(offer(gw, USD(1), drops(d)), HSFEE);
            env.close();
        }

        // Verify that the non-returning types still don't return anything.
        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::account), 0));
        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::amendments), 0));
        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::directory), 0));
        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::fee), 0));
        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::hashes), 0));
    }

    void
    run() override
    {
        using namespace jtx;
        FeatureBitset const all{supported_amendments()};
        testErrors(all);
        testUnsteppedThenStepped(all);
        testUnsteppedThenSteppedWithNFTs(all);
        testObjectTypes(all);
    }
};

BEAST_DEFINE_TESTSUITE(AccountObjects, app, ripple);

}  // namespace test
}  // namespace ripple
