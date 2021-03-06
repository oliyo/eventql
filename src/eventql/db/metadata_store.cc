/**
 * Copyright (c) 2016 DeepCortex GmbH <legal@eventql.io>
 * Authors:
 *   - Paul Asmuth <paul@eventql.io>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License ("the license") as
 * published by the Free Software Foundation, either version 3 of the License,
 * or any later version.
 *
 * In accordance with Section 7(e) of the license, the licensing of the Program
 * under the license does not imply a trademark license. Therefore any rights,
 * title and interest in our trademarks remain entirely with us.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the license for more details.
 *
 * You can be released from the requirements of the license by purchasing a
 * commercial license. Buying such a license is mandatory as soon as you develop
 * commercial activities involving this program without disclosing the source
 * code of your own applications
 */
#include "eventql/db/metadata_store.h"
#include "eventql/util/io/fileutil.h"
#include "eventql/util/random.h"
#include "eventql/util/logging.h"

namespace eventql {

MetadataStore::MetadataStore(
    const String& path_prefix,
    size_t cache_maxbytes /* = kDefaultMaxBytes */,
    size_t cache_maxentries /* = kDefaultMaxEntries */) :
    path_prefix_(path_prefix),
    cache_maxbytes_(cache_maxbytes),
    cache_maxentries_(cache_maxentries),
    cache_head_(nullptr),
    cache_tail_(nullptr),
    cache_size_bytes_(0),
    cache_numentries_(0) {}

Status MetadataStore::getMetadataFile(
    const String& ns,
    const String& table_name,
    const SHA1Hash& txid,
    RefPtr<MetadataFile>* file) const {
  auto cache_key = StringUtil::format("$0~$1~$2", ns, table_name, txid);
  {
    std::unique_lock<std::mutex> cache_lk(cache_mutex_);
    auto cache_iter = cache_idx_.find(cache_key);
    if (cache_iter != cache_idx_.end()) {
      // found in cache
      auto cache_entry = cache_iter->second.get();

      // move cache entry to head of lru list
      if (cache_entry != cache_head_) {
        // remove from current position
        if (cache_entry->prev) {
          cache_entry->prev->next = cache_entry->next;
        } else {
          cache_head_ = cache_entry->next;
        }

        if (cache_entry->next) {
          cache_entry->next->prev = cache_entry->prev;
        } else {
          cache_tail_ = cache_entry->prev;
        }

        // relink lru head
        cache_entry->prev = nullptr;
        cache_entry->next = cache_head_;
        if (cache_head_) {
          cache_head_->prev = cache_entry;
        } else {
          cache_tail_ = cache_entry;
        }

        cache_head_ = cache_entry;
      }

      *file = cache_entry->file;
      return Status::success();
    }
  }

  auto file_path = getPath(ns, table_name, txid);
  if (!FileUtil::exists(file_path)) {
    return Status(eIOError, "metadata file does not exist");
  }

  auto file_size = FileUtil::size(file_path);
  auto is = FileInputStream::openFile(file_path);
  file->reset(new MetadataFile());

  auto rc = file->get()->decode(is.get());
  if (!rc.isSuccess()) {
    return rc;
  }

  // store file in cache
  if (file_size < cache_maxbytes_) {
    std::unique_lock<std::mutex> cache_lk(cache_mutex_);
    // make space
    while (
        cache_numentries_ > 0 && (
        cache_numentries_ >= cache_maxentries_ ||
        cache_size_bytes_ + file_size >= cache_maxbytes_)) {
      assert(cache_tail_ != nullptr);
      auto removed_entry = cache_tail_;
      String removed_key = removed_entry->key;
      if (removed_entry->prev) {
        removed_entry->prev->next = nullptr;
      } else {
        cache_head_ = nullptr;
      }
      cache_tail_ = removed_entry->prev;
      --cache_numentries_;
      cache_size_bytes_ -= removed_entry->size;
      cache_idx_.erase(removed_key);
    }

    // store new entry
    if (cache_idx_.find(cache_key) == cache_idx_.end()) {
      auto cache_entry = new CacheEntry();
      cache_entry->key = cache_key;
      cache_entry->size = file_size;
      cache_entry->file = file->get();
      cache_entry->prev = nullptr;
      cache_entry->next = cache_head_;

      if (cache_head_) {
        cache_head_->prev = cache_entry;
      } else {
        cache_tail_ = cache_entry;
      }

      cache_head_ = cache_entry;

      cache_idx_.emplace(cache_key, mkScoped(cache_entry));
      ++cache_numentries_;
      cache_size_bytes_ += file_size;
    }
  }

  return Status::success();
}

bool MetadataStore::hasMetadataFile(
    const String& ns,
    const String& table_name,
    const SHA1Hash& txid) {
  auto file_path = getPath(ns, table_name, txid);
  return FileUtil::exists(file_path);
}

Status MetadataStore::storeMetadataFile(
    const String& ns,
    const String& table_name,
    const MetadataFile& file) {
  auto txid = file.getTransactionID();

  logDebug(
      "evqld",
      "Storing metadata file: $0/$1/$2",
      ns,
      table_name,
      txid.toString());

  auto file_path = getPath(ns, table_name, txid);
  auto file_path_tmp = file_path + "~" + Random::singleton()->hex64();

  FileUtil::mkdir_p(getBasePath(ns, table_name));
  auto os = FileOutputStream::openFile(file_path_tmp);

  auto rc = file.encode(os.get());
  if (!rc.isSuccess()) {
    return rc;
  }

  std::unique_lock<std::mutex> lk(commit_mutex_);
  if (FileUtil::exists(file_path)) {
    FileUtil::rm(file_path_tmp);
    return Status(eIOError, "metadata file already exists");
  } else {
    FileUtil::mv(file_path_tmp, file_path);
    return Status::success();
  }
}

String MetadataStore::getBasePath(
    const String& ns,
    const String& table_name) const {
  return FileUtil::joinPaths(
      path_prefix_,
      StringUtil::format("$0/$1", ns, table_name));
}

String MetadataStore::getPath(
    const String& ns,
    const String& table_name,
    const SHA1Hash& txid) const {
  return FileUtil::joinPaths(getBasePath(ns, table_name), txid.toString());
}

size_t MetadataStore::getCacheSize() const {
  std::unique_lock<std::mutex> cache_lk(cache_mutex_);
  return cache_size_bytes_;
}

} // namespace eventql

