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

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/nodestore/impl/DatabaseRotatingImp.h>
#include <ripple/protocol/HashPrefix.h>

namespace ripple {
namespace NodeStore {

DatabaseRotatingImp::DatabaseRotatingImp(
    Application& app,
    Scheduler& scheduler,
    int readThreads,
    std::shared_ptr<Backend> writableBackend,
    std::shared_ptr<Backend> archiveBackend,
    Section const& config,
    beast::Journal j)
    : DatabaseRotating(scheduler, readThreads, config, j)
    , app_(app)
    , writableBackend_(std::move(writableBackend))
    , archiveBackend_(std::move(archiveBackend))
{
    if (writableBackend_)
        fdRequired_ += writableBackend_->fdRequired();
    if (archiveBackend_)
        fdRequired_ += archiveBackend_->fdRequired();
}

void
DatabaseRotatingImp::rotateWithLock(
    std::function<std::unique_ptr<NodeStore::Backend>(
        std::string const& writableBackendName)> const& f)
{
    std::lock_guard lock(mutex_);

    // Create the new backend
    auto newBackend = f(writableBackend_->getName());

    // Before rotating, ensure all pinned ledgers are in the writable backend
    JLOG(j_.info())
        << "Ensuring pinned ledgers are preserved before backend rotation";

    // Use a lambda to handle the preservation of pinned ledgers
    auto ensurePinnedLedgersInWritable = [this]() {
        // Get list of pinned ledgers
        auto pinnedLedgers = app_.getLedgerMaster().getPinnedLedgersRangeSet();

        for (auto const& range : pinnedLedgers)
        {
            for (auto seq = range.lower(); seq <= range.upper(); ++seq)
            {
                uint256 hash = app_.getLedgerMaster().getHashBySeq(seq);
                if (hash.isZero())
                    continue;

                // Try to load the ledger
                auto ledger = app_.getLedgerMaster().getLedgerByHash(hash);
                if (ledger && ledger->isImmutable())
                {
                    // If we have the ledger, store it in the writable backend
                    JLOG(j_.debug()) << "Ensuring pinned ledger " << seq
                                     << " is in writable backend";
                    Database::storeLedger(*ledger, writableBackend_);
                }
                else
                {
                    // If we don't have the ledger in memory, try to fetch its
                    // objects directly
                    JLOG(j_.debug()) << "Attempting to copy pinned ledger "
                                     << seq << " header to writable backend";
                    std::shared_ptr<NodeObject> headerObj;
                    Status status =
                        archiveBackend_->fetch(hash.data(), &headerObj);
                    if (status == ok && headerObj)
                        writableBackend_->store(headerObj);
                }
            }
        }
    };

    // Execute the lambda
    ensurePinnedLedgersInWritable();

    // Now it's safe to mark the archive backend for deletion
    archiveBackend_->setDeletePath();

    // Complete the rotation
    archiveBackend_ = std::move(writableBackend_);
    writableBackend_ = std::move(newBackend);
}

std::string
DatabaseRotatingImp::getName() const
{
    std::lock_guard lock(mutex_);
    return writableBackend_->getName();
}

std::int32_t
DatabaseRotatingImp::getWriteLoad() const
{
    std::lock_guard lock(mutex_);
    return writableBackend_->getWriteLoad();
}

void
DatabaseRotatingImp::importDatabase(Database& source)
{
    auto const backend = [&] {
        std::lock_guard lock(mutex_);
        return writableBackend_;
    }();

    importInternal(*backend, source);
}

bool
DatabaseRotatingImp::storeLedger(std::shared_ptr<Ledger const> const& srcLedger)
{
    auto const backend = [&] {
        std::lock_guard lock(mutex_);
        return writableBackend_;
    }();

    return Database::storeLedger(*srcLedger, backend);
}

void
DatabaseRotatingImp::sync()
{
    std::lock_guard lock(mutex_);
    writableBackend_->sync();
}

void
DatabaseRotatingImp::store(
    NodeObjectType type,
    Blob&& data,
    uint256 const& hash,
    std::uint32_t)
{
    auto nObj = NodeObject::createObject(type, std::move(data), hash);

    auto const backend = [&] {
        std::lock_guard lock(mutex_);
        return writableBackend_;
    }();

    backend->store(nObj);
    storeStats(1, nObj->getData().size());
}

void
DatabaseRotatingImp::sweep()
{
    // nothing to do
}

std::shared_ptr<NodeObject>
DatabaseRotatingImp::fetchNodeObject(
    uint256 const& hash,
    std::uint32_t,
    FetchReport& fetchReport,
    bool duplicate)
{
    auto fetch = [&](std::shared_ptr<Backend> const& backend) {
        Status status;
        std::shared_ptr<NodeObject> nodeObject;
        try
        {
            status = backend->fetch(hash.data(), &nodeObject);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.fatal()) << "Exception, " << e.what();
            Rethrow();
        }

        switch (status)
        {
            case ok:
            case notFound:
                break;
            case dataCorrupt:
                JLOG(j_.fatal()) << "Corrupt NodeObject #" << hash;
                break;
            default:
                JLOG(j_.warn()) << "Unknown status=" << status;
                break;
        }

        return nodeObject;
    };

    // See if the node object exists in the cache
    std::shared_ptr<NodeObject> nodeObject;

    auto [writable, archive] = [&] {
        std::lock_guard lock(mutex_);
        return std::make_pair(writableBackend_, archiveBackend_);
    }();

    // Try to fetch from the writable backend
    nodeObject = fetch(writable);
    if (!nodeObject)
    {
        // Otherwise try to fetch from the archive backend
        nodeObject = fetch(archive);
        if (nodeObject)
        {
            {
                // Refresh the writable backend pointer
                std::lock_guard lock(mutex_);
                writable = writableBackend_;
            }

            // Update writable backend with data from the archive backend
            // if (duplicate)
            writable->store(nodeObject);
        }
    }

    if (nodeObject)
        fetchReport.wasFound = true;

    return nodeObject;
}

void
DatabaseRotatingImp::for_each(
    std::function<void(std::shared_ptr<NodeObject>)> f)
{
    auto [writable, archive] = [&] {
        std::lock_guard lock(mutex_);
        return std::make_pair(writableBackend_, archiveBackend_);
    }();

    // Iterate the writable backend
    writable->for_each(f);

    // Iterate the archive backend
    archive->for_each(f);
}

}  // namespace NodeStore
}  // namespace ripple
