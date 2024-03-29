/** Copyright 2016 Contributors (see the AUTHORS file)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License,
 * v. 2.0. If a copy of the MPL was not distributed with this file, you can
 * obtain one at https://mozilla.org/MPL/2.0/ */

#include <cassert>
#include <queue>
#include <glog/logging.h>
#include <oio/kinetic/rpc/Delete.h>
#include "Listing.h"
#include "Removal.h"

using oio::kinetic::client::Sync;
using oio::kinetic::client::ClientInterface;
using oio::kinetic::client::ClientFactory;
using oio::kinetic::blob::RemovalBuilder;
using oio::kinetic::blob::ListingBuilder;

struct PendingDelete {
    std::string k;
    oio::kinetic::rpc::Delete op;
    std::shared_ptr<oio::kinetic::client::ClientInterface> client;
    std::shared_ptr<Sync> sync;

    void Start() {
        assert(sync.get() == nullptr);
        sync = client->Start(&op);
        DLOG(INFO) << "Started rem("<< client->Id() << "," << k << ")";
    }
};

class Removal : public oio::blob::Removal {
    friend class RemovalBuilder;
  public:
    Removal(std::shared_ptr<ClientFactory> f,
            std::vector<std::string> tv) noexcept
            : parallelism_factor{8}, chunkid(), targets(), factory(f), ops() {
        targets.swap(tv);
    }

    virtual ~Removal() noexcept { }

    virtual oio::blob::Removal::Status Prepare() noexcept;

    virtual bool Commit() noexcept;

    virtual bool Abort() noexcept;

    virtual bool Ok() noexcept;

  private:
    unsigned int parallelism_factor;
    std::string chunkid;
    std::vector<std::string> targets;
    std::shared_ptr<ClientFactory> factory;

    std::vector<PendingDelete> ops;
};

oio::blob::Removal::Status Removal::Prepare() noexcept {
    ListingBuilder builder(factory);
    builder.Name(chunkid);
    for (const auto &to: targets)
        builder.Target(to);
    auto listing = builder.Build();

    auto rc = listing->Prepare();
    switch (rc) {
        case oio::blob::Listing::Status::OK:
            break;
        case oio::blob::Listing::Status::NotFound:
            return oio::blob::Removal::Status::NotFound;
        case oio::blob::Listing::Status::NetworkError:
            return oio::blob::Removal::Status::NetworkError;
        case oio::blob::Listing::Status::ProtocolError:
            return oio::blob::Removal::Status::ProtocolError;
    }

    std::string id, key;
    while (listing->Next(id, key)) {
        PendingDelete del;
        del.k.assign(key);
        del.op.Key(key);
        del.client = factory->Get(id);
        DLOG(INFO) << "rem("<< id << ","<< key <<")";
        ops.push_back(del);
    }

    return oio::blob::Removal::Status::OK;
}

bool Removal::Commit() noexcept {
    DLOG(INFO) << __FUNCTION__ << " of " << ops.size() << " ops";
    // Pre-start as many parallel operations as the configured parallelism
    for (unsigned int i = 0; i < parallelism_factor && i < ops.size(); ++i)
        ops[i].Start();

    for (unsigned int i = 0; i < ops.size(); ++i) {
        ops[i].sync->Wait();
        // an operation finished, pre-start another one
        if (i + parallelism_factor + 1 < ops.size())
            ops[i + parallelism_factor + 1].Start();
    }

    return true;
}

bool Removal::Abort() noexcept {
    return false;
}

bool Removal::Ok() noexcept {
    return true;
}

RemovalBuilder::RemovalBuilder(std::shared_ptr<ClientFactory> f) noexcept
        : factory(f), targets(), name() {
}

void RemovalBuilder::Name(const std::string &n) noexcept {
    name.assign(n);
}

void RemovalBuilder::Name(const char *n) noexcept {
    assert(n != nullptr);
    return Name(std::string(n));
}

void RemovalBuilder::Target(const std::string &to) noexcept {
    targets.insert(to);
}

void RemovalBuilder::Target(const char *to) noexcept {
    assert(to != nullptr);
    return Target(std::string(to));
}

std::unique_ptr<oio::blob::Removal> RemovalBuilder::Build() noexcept {
    assert(!targets.empty());
    assert(!name.empty());

    std::vector<std::string> tv;
    for (const auto &t : targets)
        tv.push_back(t);
    auto rem = new Removal(factory, std::move(tv));
    rem->chunkid.assign(name);
    return std::unique_ptr<Removal>(rem);
}