/** Copyright 2016 Contributors (see the AUTHORS file)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License,
 * v. 2.0. If a copy of the MPL was not distributed with this file, you can
 * obtain one at https://mozilla.org/MPL/2.0/ */

#include <array>
#include <glog/logging.h>
#include <utils/utils.h>
#include "oio/kinetic/client/ClientInterface.h"
#include "oio/kinetic/client/CoroutineClientFactory.h"
#include "oio/api/Upload.h"
#include "oio/api/Download.h"
#include "oio/api/Listing.h"
#include "oio/api/Removal.h"
#include "oio/kinetic/blob/Upload.h"
#include "oio/kinetic/blob/Download.h"
#include "oio/kinetic/blob/Removal.h"
#include "oio/kinetic/blob/Listing.h"

using oio::kinetic::client::ClientFactory;
using oio::kinetic::client::CoroutineClientFactory;
using oio::kinetic::blob::UploadBuilder;
using oio::kinetic::blob::ListingBuilder;
using oio::kinetic::blob::DownloadBuilder;

const char * chunkid = "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF";
const char * envkey_URL = "OIO_KINETIC_URL";
const char * target = ::getenv(envkey_URL);

static void test_upload_empty (std::shared_ptr<ClientFactory> factory) {
    DLOG(INFO) << __FUNCTION__;
    std::array<uint8_t,8> buf;
    buf.fill('0');

    auto builder = UploadBuilder(factory);
    builder.BlockSize(1024*1024);
    builder.Name(chunkid);
    for (int i=0; i<5 ;++i)
        builder.Target(target);

    auto up = builder.Build();
    up->Commit();
}

static void test_upload_2blocks (std::shared_ptr<ClientFactory> factory) {
    DLOG(INFO) << __FUNCTION__;
    std::array<uint8_t,8192> buf;
    buf.fill('0');

    auto builder = UploadBuilder(factory);
    builder.BlockSize(1024*1024);
    builder.Name(chunkid);
    for (int i=0; i<5 ;++i)
        builder.Target(target);

    auto up = builder.Build();
    up->Write(buf.data(), buf.size());
    up->Write(buf.data(), buf.size());
    up->Commit();
}

static void test_listing (std::shared_ptr<ClientFactory> factory) {
    DLOG(INFO) << __FUNCTION__;
    auto builder = ListingBuilder(factory);
    builder.Name(chunkid);
    builder.Target(target);
    builder.Target(target);
    builder.Target(target);
    auto list = builder.Build();
    auto rc = list->Prepare();
    assert(rc == oio::blob::Listing::Status::OK);
    std::string id, key;
    while (list->Next(id, key)) {
        DLOG(INFO) << "id:" << id << " key:" << key;
    }
}

static void test_download (std::shared_ptr<ClientFactory> factory) {
    DLOG(INFO) << __FUNCTION__;
    auto builder = DownloadBuilder(factory);
    builder.Target(target);
    builder.Name(chunkid);
    auto dl = builder.Build();
    auto rc = dl->Prepare();
    assert(rc == oio::blob::Download::Status::OK);
    DLOG(INFO) << "DL ready, chunk found, eof " << dl->IsEof();

    while (!dl->IsEof()) {
        std::vector<uint8_t> buf;
        dl->Read(buf);
    }
}

int main (int argc UNUSED, char **argv) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;

    if (target == nullptr) {
        LOG(ERROR) << "Missing " << envkey_URL << " variable in environment";
        return -1;
    }

    std::shared_ptr<ClientFactory> factory(new CoroutineClientFactory);
    test_upload_empty(factory);
    test_upload_2blocks(factory);
    test_listing(factory);
    test_download(factory);
    return 0;
}