

#include <ripple/app/misc/ValidatorList.h>
#include <ripple/basics/base64.h>
#include <ripple/basics/Slice.h>
#include <ripple/basics/strHex.h>
#include <test/jtx.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/Sign.h>


namespace ripple {
namespace test {

class ValidatorList_test : public beast::unit_test::suite
{
private:
    struct Validator
    {
        PublicKey masterPublic;
        PublicKey signingPublic;
        std::string manifest;
    };

    static
    PublicKey
    randomNode ()
    {
        return derivePublicKey (KeyType::secp256k1, randomSecretKey());
    }

    static
    PublicKey
    randomMasterKey ()
    {
        return derivePublicKey (KeyType::ed25519, randomSecretKey());
    }

    static
    std::string
    makeManifestString (
        PublicKey const& pk,
        SecretKey const& sk,
        PublicKey const& spk,
        SecretKey const& ssk,
        int seq)
    {
        STObject st(sfGeneric);
        st[sfSequence] = seq;
        st[sfPublicKey] = pk;

        if (seq != std::numeric_limits<std::uint32_t>::max())
        {
            st[sfSigningPubKey] = spk;
            sign(st, HashPrefix::manifest, *publicKeyType(spk), ssk);
        }

        sign(st, HashPrefix::manifest, *publicKeyType(pk), sk, sfMasterSignature);

        Serializer s;
        st.add(s);

        return std::string(static_cast<char const*> (s.data()), s.size());
    }

    static
    std::string
    makeRevocationString (
        PublicKey const& pk,
        SecretKey const& sk)
    {
        STObject st(sfGeneric);
        st[sfSequence] = std::numeric_limits<std::uint32_t>::max();
        st[sfPublicKey] = pk;

        sign(st, HashPrefix::manifest, *publicKeyType(pk), sk, sfMasterSignature);

        Serializer s;
        st.add(s);

        return std::string(static_cast<char const*> (s.data()), s.size());
    }

    static
    Validator
    randomValidator ()
    {
        auto const secret = randomSecretKey();
        auto const masterPublic =
            derivePublicKey(KeyType::ed25519, secret);
        auto const signingKeys = randomKeyPair(KeyType::secp256k1);
        return { masterPublic, signingKeys.first,
                 base64_encode(makeManifestString (
            masterPublic, secret, signingKeys.first, signingKeys.second, 1)) };
    }

    std::string
    makeList (
        std::vector <Validator> const& validators,
        std::size_t sequence,
        std::size_t expiration)
    {
        std::string data =
            "{\"sequence\":" + std::to_string(sequence) +
            ",\"expiration\":" + std::to_string(expiration) +
            ",\"validators\":[";

        for (auto const& val : validators)
        {
            data += "{\"validation_public_key\":\"" + strHex(val.masterPublic) +
                "\",\"manifest\":\"" + val.manifest + "\"},";
        }

        data.pop_back();
        data += "]}";
        return base64_encode(data);
    }

    std::string
    signList (
        std::string const& blob,
        std::pair<PublicKey, SecretKey> const& keys)
    {
        auto const data = base64_decode (blob);
        return strHex(sign(
            keys.first, keys.second, makeSlice(data)));
    }

    static hash_set<NodeID>
    asNodeIDs(std::initializer_list<PublicKey> const& pks)
    {
        hash_set<NodeID> res;
        res.reserve(pks.size());
        for (auto const& pk : pks)
            res.insert(calcNodeID(pk));
        return res;
    }

    void
    testGenesisQuorum ()
    {
        testcase ("Genesis Quorum");

        ManifestCache manifests;
        jtx::Env env (*this);
        {
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), env.journal);
            BEAST_EXPECT(trustedKeys->quorum () == 1);
        }
        {
            std::size_t minQuorum = 0;
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), env.journal, minQuorum);
            BEAST_EXPECT(trustedKeys->quorum () == minQuorum);
        }
    }

    void
    testConfigLoad ()
    {
        testcase ("Config Load");

        jtx::Env env (*this);
        PublicKey emptyLocalKey;
        std::vector<std::string> const emptyCfgKeys;
        std::vector<std::string> const emptyCfgPublishers;

        auto const localSigningKeys = randomKeyPair(KeyType::secp256k1);
        auto const localSigningPublic = localSigningKeys.first;
        auto const localSigningSecret = localSigningKeys.second;
        auto const localMasterSecret = randomSecretKey();
        auto const localMasterPublic = derivePublicKey(
            KeyType::ed25519, localMasterSecret);

        std::string const cfgManifest (makeManifestString (
            localMasterPublic, localMasterSecret,
            localSigningPublic, localSigningSecret, 1));

        auto format = [](
            PublicKey const &publicKey,
            char const* comment = nullptr)
        {
            auto ret = toBase58 (TokenType::NodePublic, publicKey);

            if (comment)
                ret += comment;

            return ret;
        };

        std::vector<PublicKey> configList;
        configList.reserve(8);

        while (configList.size () != 8)
            configList.push_back (randomNode());

        std::vector<std::string> cfgKeys ({
            format (configList[0]),
            format (configList[1], " Comment"),
            format (configList[2], " Multi Word Comment"),
            format (configList[3], "    Leading Whitespace"),
            format (configList[4], " Trailing Whitespace    "),
            format (configList[5], "    Leading & Trailing Whitespace    "),
            format (configList[6], "    Leading, Trailing & Internal    Whitespace    "),
            format (configList[7], "    ")
        });

        {
            ManifestCache manifests;
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), env.journal);

            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, emptyCfgKeys, emptyCfgPublishers));

            BEAST_EXPECT(trustedKeys->load (
                localSigningPublic, emptyCfgKeys, emptyCfgPublishers));
            BEAST_EXPECT(trustedKeys->listed (localSigningPublic));

            manifests.applyManifest (*deserializeManifest(cfgManifest));
            BEAST_EXPECT(trustedKeys->load (
                localSigningPublic, emptyCfgKeys, emptyCfgPublishers));

            BEAST_EXPECT(trustedKeys->listed (localMasterPublic));
            BEAST_EXPECT(trustedKeys->listed (localSigningPublic));
        }
        {
            ManifestCache manifests;
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), env.journal);

            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, cfgKeys, emptyCfgPublishers));

            for (auto const& n : configList)
                BEAST_EXPECT(trustedKeys->listed (n));

            auto const masterNode1 = randomMasterKey ();
            auto const masterNode2 = randomMasterKey ();

            std::vector<std::string> cfgMasterKeys({
                format (masterNode1),
                format (masterNode2, " Comment")
            });
            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, cfgMasterKeys, emptyCfgPublishers));
            BEAST_EXPECT(trustedKeys->listed (masterNode1));
            BEAST_EXPECT(trustedKeys->listed (masterNode2));

            BEAST_EXPECT(!trustedKeys->load (emptyLocalKey,
                { "NotAPublicKey" }, emptyCfgPublishers));
            BEAST_EXPECT(!trustedKeys->load (emptyLocalKey,
                { format (randomNode(), "!") }, emptyCfgPublishers));

            auto const goodKey = randomNode();
            BEAST_EXPECT(!trustedKeys->load (emptyLocalKey,
                { format (randomNode(), "!"), format (goodKey) },
                emptyCfgPublishers));
            BEAST_EXPECT(!trustedKeys->listed (goodKey));
        }
        {
            ManifestCache manifests;
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), env.journal);

            auto const localSigningPublic = parseBase58<PublicKey> (
                TokenType::NodePublic, cfgKeys.front());

            BEAST_EXPECT(trustedKeys->load (
                *localSigningPublic, cfgKeys, emptyCfgPublishers));

            BEAST_EXPECT(trustedKeys->localPublicKey() == localSigningPublic);
            BEAST_EXPECT(trustedKeys->listed (*localSigningPublic));
            for (auto const& n : configList)
                BEAST_EXPECT(trustedKeys->listed (n));
        }
        {
            ManifestCache manifests;
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), env.journal);

            auto const localSigningPublic = randomNode();
            BEAST_EXPECT(trustedKeys->load (
                localSigningPublic, cfgKeys, emptyCfgPublishers));

            BEAST_EXPECT(trustedKeys->localPublicKey() == localSigningPublic);
            BEAST_EXPECT(trustedKeys->listed (localSigningPublic));
            for (auto const& n : configList)
                BEAST_EXPECT(trustedKeys->listed (n));
        }
        {
            ManifestCache manifests;
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), env.journal);

            manifests.applyManifest (*deserializeManifest(cfgManifest));

            BEAST_EXPECT(trustedKeys->load (
                localSigningPublic, cfgKeys, emptyCfgPublishers));

            BEAST_EXPECT(trustedKeys->localPublicKey() == localMasterPublic);
            BEAST_EXPECT(trustedKeys->listed (localSigningPublic));
            BEAST_EXPECT(trustedKeys->listed (localMasterPublic));
            for (auto const& n : configList)
                BEAST_EXPECT(trustedKeys->listed (n));
        }
        {
            ManifestCache manifests;
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), env.journal);

            std::vector<std::string> badPublishers(
                {"NotASigningKey"});
            BEAST_EXPECT(!trustedKeys->load (
                emptyLocalKey, emptyCfgKeys, badPublishers));

            std::vector<PublicKey> keys ({
                randomMasterKey(), randomMasterKey(), randomMasterKey()});
            badPublishers.clear();
            for (auto const& key : keys)
                badPublishers.push_back (
                    toBase58 (TokenType::NodePublic, key));

            BEAST_EXPECT(! trustedKeys->load (
                emptyLocalKey, emptyCfgKeys, badPublishers));
            for (auto const& key : keys)
                BEAST_EXPECT(!trustedKeys->trustedPublisher (key));

            std::vector<std::string> cfgPublishers;
            for (auto const& key : keys)
                cfgPublishers.push_back (strHex(key));

            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, emptyCfgKeys, cfgPublishers));
            for (auto const& key : keys)
                BEAST_EXPECT(trustedKeys->trustedPublisher (key));
        }
        {
            ManifestCache valManifests;
            ManifestCache pubManifests;
            auto trustedKeys = std::make_unique <ValidatorList> (
                valManifests, pubManifests, env.timeKeeper(), env.journal);

            auto const pubRevokedSecret = randomSecretKey();
            auto const pubRevokedPublic =
                derivePublicKey(KeyType::ed25519, pubRevokedSecret);
            auto const pubRevokedSigning = randomKeyPair(KeyType::secp256k1);
            pubManifests.applyManifest (*deserializeManifest (
                makeManifestString (
                    pubRevokedPublic,
                    pubRevokedSecret,
                    pubRevokedSigning.first,
                    pubRevokedSigning.second,
                    std::numeric_limits<std::uint32_t>::max ())));

            auto legitKey = randomMasterKey();

            std::vector<std::string> cfgPublishers = {
                strHex(pubRevokedPublic),
                strHex(legitKey) };
            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, emptyCfgKeys, cfgPublishers));

            BEAST_EXPECT(!trustedKeys->trustedPublisher (pubRevokedPublic));
            BEAST_EXPECT(trustedKeys->trustedPublisher (legitKey));
        }
    }

    void
    testApplyList ()
    {
        testcase ("Apply list");

        std::string const siteUri = "testApplyList.test";

        ManifestCache manifests;
        jtx::Env env (*this);
        auto trustedKeys = std::make_unique<ValidatorList> (
            manifests, manifests, env.app().timeKeeper(), env.journal);

        auto const publisherSecret = randomSecretKey();
        auto const publisherPublic =
            derivePublicKey(KeyType::ed25519, publisherSecret);
        auto const pubSigningKeys1 = randomKeyPair(KeyType::secp256k1);
        auto const manifest1 = base64_encode(makeManifestString (
            publisherPublic, publisherSecret,
            pubSigningKeys1.first, pubSigningKeys1.second, 1));

        std::vector<std::string> cfgKeys1({
            strHex(publisherPublic)});
        PublicKey emptyLocalKey;
        std::vector<std::string> emptyCfgKeys;

        BEAST_EXPECT(trustedKeys->load (
            emptyLocalKey, emptyCfgKeys, cfgKeys1));

        auto constexpr listSize = 20;
        std::vector<Validator> list1;
        list1.reserve (listSize);
        while (list1.size () < listSize)
            list1.push_back (randomValidator());

        std::vector<Validator> list2;
        list2.reserve (listSize);
        while (list2.size () < listSize)
            list2.push_back (randomValidator());

        auto const version = 1;
        auto const sequence = 1;
        auto const expiredblob = makeList (
            list1, sequence, env.timeKeeper().now().time_since_epoch().count());
        auto const expiredSig = signList (expiredblob, pubSigningKeys1);

        BEAST_EXPECT(ListDisposition::stale ==
            trustedKeys->applyList (
                manifest1, expiredblob, expiredSig, version, siteUri));

        using namespace std::chrono_literals;
        NetClock::time_point const expiration =
            env.timeKeeper().now() + 3600s;
        auto const blob1 = makeList (
            list1, sequence, expiration.time_since_epoch().count());
        auto const sig1 = signList (blob1, pubSigningKeys1);

        BEAST_EXPECT(ListDisposition::accepted == trustedKeys->applyList (
            manifest1, blob1, sig1, version, siteUri));

        for (auto const& val : list1)
        {
            BEAST_EXPECT(trustedKeys->listed (val.masterPublic));
            BEAST_EXPECT(trustedKeys->listed (val.signingPublic));
        }

        auto const untrustedManifest = base64_encode(
            makeManifestString (
                randomMasterKey(), publisherSecret,
                pubSigningKeys1.first, pubSigningKeys1.second, 1));

        BEAST_EXPECT(ListDisposition::untrusted == trustedKeys->applyList (
            untrustedManifest, blob1, sig1, version, siteUri));

        auto const badVersion = 666;
        BEAST_EXPECT(ListDisposition::unsupported_version ==
            trustedKeys->applyList (
                manifest1, blob1, sig1, badVersion, siteUri));

        auto const sequence2 = 2;
        auto const blob2 = makeList (
            list2, sequence2, expiration.time_since_epoch().count());
        auto const sig2 = signList (blob2, pubSigningKeys1);

        BEAST_EXPECT(ListDisposition::accepted ==
            trustedKeys->applyList (
                manifest1, blob2, sig2, version, siteUri));

        for (auto const& val : list1)
        {
            BEAST_EXPECT(! trustedKeys->listed (val.masterPublic));
            BEAST_EXPECT(! trustedKeys->listed (val.signingPublic));
        }

        for (auto const& val : list2)
        {
            BEAST_EXPECT(trustedKeys->listed (val.masterPublic));
            BEAST_EXPECT(trustedKeys->listed (val.signingPublic));
        }

        BEAST_EXPECT(ListDisposition::stale ==
            trustedKeys->applyList (
                manifest1, blob1, sig1, version, siteUri));

        BEAST_EXPECT(ListDisposition::same_sequence ==
            trustedKeys->applyList (
                manifest1, blob2, sig2, version, siteUri));

        auto const pubSigningKeys2 = randomKeyPair(KeyType::secp256k1);
        auto manifest2 = base64_encode(makeManifestString (
            publisherPublic, publisherSecret,
            pubSigningKeys2.first, pubSigningKeys2.second, 2));

        auto const sequence3 = 3;
        auto const blob3 = makeList (
            list1, sequence3, expiration.time_since_epoch().count());
        auto const sig3 = signList (blob3, pubSigningKeys2);

        BEAST_EXPECT(ListDisposition::accepted ==
            trustedKeys->applyList (
                manifest2, blob3, sig3, version, siteUri));

        auto const sequence4 = 4;
        auto const blob4 = makeList (
            list1, sequence4, expiration.time_since_epoch().count());
        auto const badSig = signList (blob4, pubSigningKeys1);
        BEAST_EXPECT(ListDisposition::invalid ==
            trustedKeys->applyList (
                manifest1, blob4, badSig, version, siteUri));

        auto const signingKeysMax = randomKeyPair(KeyType::secp256k1);
        auto maxManifest = base64_encode(makeRevocationString (
            publisherPublic, publisherSecret));

        auto const sequence5 = 5;
        auto const blob5 = makeList (
            list1, sequence5, expiration.time_since_epoch().count());
        auto const sig5 = signList (blob5, signingKeysMax);

        BEAST_EXPECT(ListDisposition::untrusted ==
            trustedKeys->applyList (
                maxManifest, blob5, sig5, version, siteUri));

        BEAST_EXPECT(! trustedKeys->trustedPublisher(publisherPublic));
        for (auto const& val : list1)
        {
            BEAST_EXPECT(! trustedKeys->listed (val.masterPublic));
            BEAST_EXPECT(! trustedKeys->listed (val.signingPublic));
        }
    }

    void
    testUpdateTrusted ()
    {
        testcase ("Update trusted");

        std::string const siteUri = "testUpdateTrusted.test";

        PublicKey emptyLocalKey;
        ManifestCache manifests;
        jtx::Env env (*this);
        auto trustedKeys = std::make_unique <ValidatorList> (
            manifests, manifests, env.timeKeeper(), env.journal);

        std::vector<std::string> cfgPublishers;
        hash_set<NodeID> activeValidators;

        std::size_t const n = 40;
        {
            std::vector<std::string> cfgKeys;
            cfgKeys.reserve(n);
            hash_set<NodeID> unseenValidators;

            while (cfgKeys.size () != n)
            {
                auto const valKey = randomNode();
                cfgKeys.push_back (toBase58(
                    TokenType::NodePublic, valKey));
                if (cfgKeys.size () <= n - 5)
                    activeValidators.emplace (calcNodeID(valKey));
                else
                    unseenValidators.emplace (calcNodeID(valKey));
            }

            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, cfgKeys, cfgPublishers));

            TrustChanges changes =
                trustedKeys->updateTrusted(activeValidators);

            for (auto const& val : unseenValidators)
                activeValidators.emplace (val);

            BEAST_EXPECT(changes.added == activeValidators);
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(trustedKeys->quorum () ==
                std::ceil(cfgKeys.size() * 0.8f));
            for (auto const& val : cfgKeys)
            {
                if (auto const valKey = parseBase58<PublicKey>(
                    TokenType::NodePublic, val))
                {
                    BEAST_EXPECT(trustedKeys->listed (*valKey));
                    BEAST_EXPECT(trustedKeys->trusted (*valKey));
                }
                else
                    fail ();
            }

            changes =
                trustedKeys->updateTrusted(activeValidators);
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(trustedKeys->quorum () ==
                std::ceil(cfgKeys.size() * 0.8f));
        }
        {
            auto const masterPrivate  = randomSecretKey();
            auto const masterPublic =
                derivePublicKey(KeyType::ed25519, masterPrivate);

            std::vector<std::string> cfgKeys ({
                toBase58 (TokenType::NodePublic, masterPublic)});

            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, cfgKeys, cfgPublishers));

            auto const signingKeys1 = randomKeyPair(KeyType::secp256k1);
            auto const signingPublic1 = signingKeys1.first;
            activeValidators.emplace (calcNodeID(masterPublic));

            TrustChanges changes =
                trustedKeys->updateTrusted(activeValidators);
            BEAST_EXPECT(changes.added == asNodeIDs({masterPublic}));
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(trustedKeys->quorum () == std::ceil((n + 1) * 0.8f));
            BEAST_EXPECT(trustedKeys->listed (masterPublic));
            BEAST_EXPECT(trustedKeys->trusted (masterPublic));
            BEAST_EXPECT(!trustedKeys->listed (signingPublic1));
            BEAST_EXPECT(!trustedKeys->trusted (signingPublic1));

            auto m1 = deserializeManifest(makeManifestString(
                masterPublic, masterPrivate,
                signingPublic1, signingKeys1.second, 1));

            BEAST_EXPECT(
                manifests.applyManifest(std::move (*m1)) ==
                    ManifestDisposition::accepted);
            BEAST_EXPECT(trustedKeys->listed (masterPublic));
            BEAST_EXPECT(trustedKeys->trusted (masterPublic));
            BEAST_EXPECT(trustedKeys->listed (signingPublic1));
            BEAST_EXPECT(trustedKeys->trusted (signingPublic1));

            auto const signingKeys2 = randomKeyPair(KeyType::secp256k1);
            auto const signingPublic2 = signingKeys2.first;
            auto m2 = deserializeManifest(makeManifestString(
                masterPublic, masterPrivate,
                signingPublic2, signingKeys2.second, 2));
            BEAST_EXPECT(
                manifests.applyManifest(std::move (*m2)) ==
                    ManifestDisposition::accepted);
            BEAST_EXPECT(trustedKeys->listed (masterPublic));
            BEAST_EXPECT(trustedKeys->trusted (masterPublic));
            BEAST_EXPECT(trustedKeys->listed (signingPublic2));
            BEAST_EXPECT(trustedKeys->trusted (signingPublic2));
            BEAST_EXPECT(!trustedKeys->listed (signingPublic1));
            BEAST_EXPECT(!trustedKeys->trusted (signingPublic1));

            auto const signingKeysMax = randomKeyPair(KeyType::secp256k1);
            auto const signingPublicMax = signingKeysMax.first;
            activeValidators.emplace (calcNodeID(signingPublicMax));
            auto mMax = deserializeManifest(makeRevocationString(
                masterPublic, masterPrivate));

            BEAST_EXPECT(mMax->revoked ());
            BEAST_EXPECT(
                manifests.applyManifest(std::move (*mMax)) ==
                    ManifestDisposition::accepted);
            BEAST_EXPECT(manifests.getSigningKey (masterPublic) == masterPublic);
            BEAST_EXPECT(manifests.revoked (masterPublic));

            BEAST_EXPECT(trustedKeys->listed (masterPublic));
            BEAST_EXPECT(trustedKeys->trusted (masterPublic));

            changes = trustedKeys->updateTrusted (activeValidators);
            BEAST_EXPECT(changes.removed == asNodeIDs({masterPublic}));
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(trustedKeys->quorum () == std::ceil(n * 0.8f));
            BEAST_EXPECT(trustedKeys->listed (masterPublic));
            BEAST_EXPECT(!trustedKeys->trusted (masterPublic));
            BEAST_EXPECT(!trustedKeys->listed (signingPublicMax));
            BEAST_EXPECT(!trustedKeys->trusted (signingPublicMax));
            BEAST_EXPECT(!trustedKeys->listed (signingPublic2));
            BEAST_EXPECT(!trustedKeys->trusted (signingPublic2));
            BEAST_EXPECT(!trustedKeys->listed (signingPublic1));
            BEAST_EXPECT(!trustedKeys->trusted (signingPublic1));
        }
        {
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), env.journal);
            auto const publisherSecret = randomSecretKey();
            auto const publisherPublic =
                derivePublicKey(KeyType::ed25519, publisherSecret);

            std::vector<std::string> cfgPublishers({
                strHex(publisherPublic)});
            std::vector<std::string> emptyCfgKeys;

            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, emptyCfgKeys, cfgPublishers));

            TrustChanges changes =
                trustedKeys->updateTrusted(activeValidators);
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(trustedKeys->quorum () ==
                std::numeric_limits<std::size_t>::max());
        }
        {
            std::size_t const minQuorum = 1;
            ManifestCache manifests;
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), env.journal, minQuorum);

            std::size_t n = 10;
            std::vector<std::string> cfgKeys;
            cfgKeys.reserve(n);
            hash_set<NodeID> expectedTrusted;
            hash_set<NodeID> activeValidators;
            NodeID toBeSeen;

            while (cfgKeys.size () < n)
            {
                auto const valKey = randomNode();
                cfgKeys.push_back (toBase58(
                    TokenType::NodePublic, valKey));
                expectedTrusted.emplace (calcNodeID(valKey));
                if (cfgKeys.size () < std::ceil(n*0.8f))
                    activeValidators.emplace (calcNodeID(valKey));
                else if (cfgKeys.size () < std::ceil(n*0.8f))
                    toBeSeen = calcNodeID(valKey);
            }

            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, cfgKeys, cfgPublishers));

            TrustChanges changes =
                trustedKeys->updateTrusted(activeValidators);
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(changes.added == expectedTrusted);
            BEAST_EXPECT(trustedKeys->quorum () == minQuorum);

            activeValidators.emplace (toBeSeen);
            changes = trustedKeys->updateTrusted(activeValidators);
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(trustedKeys->quorum () == std::ceil(n * 0.8f));
        }
        {
            auto trustedKeys = std::make_unique<ValidatorList> (
                manifests, manifests, env.app().timeKeeper(), env.journal);

            PublicKey emptyLocalKey;
            std::vector<std::string> emptyCfgKeys;
            auto const publisherKeys = randomKeyPair(KeyType::secp256k1);
            auto const pubSigningKeys = randomKeyPair(KeyType::secp256k1);
            auto const manifest = base64_encode (
                makeManifestString (
                    publisherKeys.first, publisherKeys.second,
                    pubSigningKeys.first, pubSigningKeys.second, 1));

            std::vector<std::string> cfgKeys ({
                strHex(publisherKeys.first)});

            BEAST_EXPECT(trustedKeys->load (
                emptyLocalKey, emptyCfgKeys, cfgKeys));

            std::vector<Validator> list ({randomValidator(), randomValidator()});
            hash_set<NodeID> activeValidators(
                asNodeIDs({list[0].masterPublic, list[1].masterPublic}));

            auto const version = 1;
            auto const sequence = 1;
            using namespace std::chrono_literals;
            NetClock::time_point const expiration =
                env.timeKeeper().now() + 60s;
            auto const blob = makeList (
                list, sequence, expiration.time_since_epoch().count());
            auto const sig = signList (blob, pubSigningKeys);

            BEAST_EXPECT(ListDisposition::accepted ==
                trustedKeys->applyList (
                    manifest, blob, sig, version, siteUri));

            TrustChanges changes =
                trustedKeys->updateTrusted(activeValidators);
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(changes.added == activeValidators);
            for(Validator const & val : list)
            {
                BEAST_EXPECT(trustedKeys->trusted (val.masterPublic));
                BEAST_EXPECT(trustedKeys->trusted (val.signingPublic));
            }
            BEAST_EXPECT(trustedKeys->quorum () == 2);

            env.timeKeeper().set(expiration);
            changes = trustedKeys->updateTrusted (activeValidators);
            BEAST_EXPECT(changes.removed == activeValidators);
            BEAST_EXPECT(changes.added.empty());
            BEAST_EXPECT(! trustedKeys->trusted (list[0].masterPublic));
            BEAST_EXPECT(! trustedKeys->trusted (list[1].masterPublic));
            BEAST_EXPECT(trustedKeys->quorum () ==
                std::numeric_limits<std::size_t>::max());

            std::vector<Validator> list2 ({list[0], randomValidator()});
            activeValidators.insert(calcNodeID(list2[1].masterPublic));
            auto const sequence2 = 2;
            NetClock::time_point const expiration2 =
                env.timeKeeper().now() + 60s;
            auto const blob2 = makeList (
                list2, sequence2, expiration2.time_since_epoch().count());
            auto const sig2 = signList (blob2, pubSigningKeys);

            BEAST_EXPECT(ListDisposition::accepted ==
                trustedKeys->applyList (
                    manifest, blob2, sig2, version, siteUri));

            changes = trustedKeys->updateTrusted (activeValidators);
            BEAST_EXPECT(changes.removed.empty());
            BEAST_EXPECT(
                changes.added ==
                asNodeIDs({list2[0].masterPublic, list2[1].masterPublic}));
            for(Validator const & val : list2)
            {
                BEAST_EXPECT(trustedKeys->trusted (val.masterPublic));
                BEAST_EXPECT(trustedKeys->trusted (val.signingPublic));
            }
            BEAST_EXPECT(! trustedKeys->trusted (list[1].masterPublic));
            BEAST_EXPECT(! trustedKeys->trusted (list[1].signingPublic));
            BEAST_EXPECT(trustedKeys->quorum () == 2);
        }
        {
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), env.journal);

            std::vector<std::string> cfgPublishers;
            hash_set<NodeID> activeValidators;
            hash_set<PublicKey> activeKeys;

            std::vector<std::string> cfgKeys;
            cfgKeys.reserve(9);

            while (cfgKeys.size() < cfgKeys.capacity())
            {
                auto const valKey = randomNode();
                cfgKeys.push_back (toBase58(
                    TokenType::NodePublic, valKey));
                activeValidators.emplace (calcNodeID(valKey));
                activeKeys.emplace(valKey);
                BEAST_EXPECT(trustedKeys->load (
                    emptyLocalKey, cfgKeys, cfgPublishers));
                TrustChanges changes =
                    trustedKeys->updateTrusted(activeValidators);
                BEAST_EXPECT(changes.removed.empty());
                BEAST_EXPECT(changes.added == asNodeIDs({valKey}));
                BEAST_EXPECT(trustedKeys->quorum () ==
                    std::ceil(cfgKeys.size() * 0.8f));
                for (auto const& key : activeKeys)
                    BEAST_EXPECT(trustedKeys->trusted (key));
            }
        }
        {
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), env.journal);

            auto const localKey = randomNode();
            std::vector<std::string> cfgPublishers;
            hash_set<NodeID> activeValidators;
            hash_set<PublicKey> activeKeys;
            std::vector<std::string> cfgKeys {
                toBase58(TokenType::NodePublic, localKey)};
            cfgKeys.reserve(9);

            while (cfgKeys.size() < cfgKeys.capacity())
            {
                auto const valKey = randomNode();
                cfgKeys.push_back (toBase58(
                    TokenType::NodePublic, valKey));
                activeValidators.emplace (calcNodeID(valKey));
                activeKeys.emplace(valKey);

                BEAST_EXPECT(trustedKeys->load (
                    localKey, cfgKeys, cfgPublishers));
                TrustChanges changes =
                    trustedKeys->updateTrusted(activeValidators);
                BEAST_EXPECT(changes.removed.empty());
                if (cfgKeys.size() > 2)
                    BEAST_EXPECT(changes.added == asNodeIDs({valKey}));
                else
                    BEAST_EXPECT(
                        changes.added == asNodeIDs({localKey, valKey}));

                BEAST_EXPECT(trustedKeys->quorum () ==
                    std::ceil(cfgKeys.size() * 0.8f));

                for (auto const& key : activeKeys)
                    BEAST_EXPECT(trustedKeys->trusted (key));
            }
        }
        {
            ManifestCache manifests;
            auto trustedKeys = std::make_unique <ValidatorList> (
                manifests, manifests, env.timeKeeper(), env.journal);

            hash_set<NodeID> activeValidators;
            std::vector<Validator> valKeys;
            valKeys.reserve(n);

            while (valKeys.size () != n)
            {
                valKeys.push_back (randomValidator());
                activeValidators.emplace(
                    calcNodeID(valKeys.back().masterPublic));
            }

            auto addPublishedList = [this, &env, &trustedKeys, &valKeys, &siteUri]()
            {
                auto const publisherSecret = randomSecretKey();
                auto const publisherPublic =
                    derivePublicKey(KeyType::ed25519, publisherSecret);
                auto const pubSigningKeys = randomKeyPair(KeyType::secp256k1);
                auto const manifest = base64_encode(makeManifestString (
                    publisherPublic, publisherSecret,
                    pubSigningKeys.first, pubSigningKeys.second, 1));

                std::vector<std::string> cfgPublishers({
                    strHex(publisherPublic)});
                PublicKey emptyLocalKey;
                std::vector<std::string> emptyCfgKeys;

                BEAST_EXPECT(trustedKeys->load (
                    emptyLocalKey, emptyCfgKeys, cfgPublishers));

                auto const version = 1;
                auto const sequence = 1;
                using namespace std::chrono_literals;
                NetClock::time_point const expiration =
                    env.timeKeeper().now() + 3600s;
                auto const blob = makeList (
                    valKeys, sequence, expiration.time_since_epoch().count());
                auto const sig = signList (blob, pubSigningKeys);

                BEAST_EXPECT(ListDisposition::accepted == trustedKeys->applyList (
                    manifest, blob, sig, version, siteUri));
            };

            for (auto i = 0; i < 3; ++i)
                addPublishedList();

            TrustChanges changes =
                trustedKeys->updateTrusted(activeValidators);

            BEAST_EXPECT(trustedKeys->quorum () ==
                std::ceil(valKeys.size() * 0.8f));

            hash_set<NodeID> added;
            for (auto const& val : valKeys)
            {
                BEAST_EXPECT(trustedKeys->trusted (val.masterPublic));
                added.insert(calcNodeID(val.masterPublic));
            }
            BEAST_EXPECT(changes.added == added);
            BEAST_EXPECT(changes.removed.empty());
        }
    }

    void
    testExpires()
    {
        testcase("Expires");

        std::string const siteUri = "testExpires.test";

        jtx::Env env(*this);

        auto toStr = [](PublicKey const& publicKey) {
            return toBase58(TokenType::NodePublic, publicKey);
        };

        {
            ManifestCache manifests;
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifests, manifests, env.timeKeeper(), env.journal);

            BEAST_EXPECT(trustedKeys->expires() == boost::none);

            PublicKey emptyLocalKey;
            PublicKey localCfgListed = randomNode();
            trustedKeys->load(emptyLocalKey, {toStr(localCfgListed)}, {});
            BEAST_EXPECT(
                trustedKeys->expires() &&
                trustedKeys->expires().get() == NetClock::time_point::max());
            BEAST_EXPECT(trustedKeys->listed(localCfgListed));
        }

        {
            ManifestCache manifests;
            auto trustedKeys = std::make_unique<ValidatorList>(
                manifests, manifests, env.app().timeKeeper(), env.journal);

            std::vector<Validator> validators = {randomValidator()};
            hash_set<NodeID> activeValidators;
            for(Validator const & val : validators)
                activeValidators.insert(calcNodeID(val.masterPublic));
            struct PreparedList
            {
                std::string manifest;
                std::string blob;
                std::string sig;
                int version;
                NetClock::time_point expiration;
            };

            using namespace std::chrono_literals;
            auto addPublishedList = [this, &env, &trustedKeys, &validators]()
            {
                auto const publisherSecret = randomSecretKey();
                auto const publisherPublic =
                    derivePublicKey(KeyType::ed25519, publisherSecret);
                auto const pubSigningKeys = randomKeyPair(KeyType::secp256k1);
                auto const manifest = base64_encode(makeManifestString (
                    publisherPublic, publisherSecret,
                    pubSigningKeys.first, pubSigningKeys.second, 1));

                std::vector<std::string> cfgPublishers({
                    strHex(publisherPublic)});
                PublicKey emptyLocalKey;
                std::vector<std::string> emptyCfgKeys;

                BEAST_EXPECT(trustedKeys->load (
                    emptyLocalKey, emptyCfgKeys, cfgPublishers));

                auto const version = 1;
                auto const sequence = 1;
                NetClock::time_point const expiration =
                    env.timeKeeper().now() + 3600s;
                auto const blob = makeList(
                    validators,
                    sequence,
                    expiration.time_since_epoch().count());
                auto const sig = signList (blob, pubSigningKeys);

                return PreparedList{manifest, blob, sig, version, expiration};
            };


            PreparedList prep1 = addPublishedList();
            env.timeKeeper().set(env.timeKeeper().now() + 200s);
            PreparedList prep2 = addPublishedList();

            BEAST_EXPECT(trustedKeys->expires() == boost::none);

            BEAST_EXPECT(
                ListDisposition::accepted == trustedKeys->applyList(
                    prep1.manifest, prep1.blob, prep1.sig, prep1.version, siteUri));

            BEAST_EXPECT(trustedKeys->expires() == boost::none);

            BEAST_EXPECT(
                ListDisposition::accepted == trustedKeys->applyList(
                    prep2.manifest, prep2.blob, prep2.sig, prep2.version, siteUri));

            BEAST_EXPECT(
                trustedKeys->expires() &&
                trustedKeys->expires().get() == prep1.expiration);

            env.timeKeeper().set(prep1.expiration + 1s);
            trustedKeys->updateTrusted(activeValidators);
            BEAST_EXPECT(
                trustedKeys->expires() &&
                trustedKeys->expires().get() == prep1.expiration);
        }
}
public:
    void
    run() override
    {
        testGenesisQuorum ();
        testConfigLoad ();
        testApplyList ();
        testUpdateTrusted ();
        testExpires ();
    }
};

BEAST_DEFINE_TESTSUITE(ValidatorList, app, ripple);

} 
} 
























