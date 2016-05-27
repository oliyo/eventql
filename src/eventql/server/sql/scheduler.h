/**
 * Copyright (c) 2016 zScale Technology GmbH <legal@zscale.io>
 * Authors:
 *   - Paul Asmuth <paul@zscale.io>
 *   - Laura Schlimmer <laura@zscale.io>
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
#pragma once
#include <eventql/util/stdtypes.h>
#include <eventql/util/autoref.h>
#include <eventql/sql/result_cursor.h>
#include <eventql/sql/scheduler.h>
#include <eventql/sql/query_plan.h>
#include <eventql/db/partition_map.h>
#include <eventql/auth/internal_auth.h>

namespace eventql {

class Scheduler : public csql::DefaultScheduler {
public:

  const size_t kMaxConcurrency = 24;

  Scheduler(
      PartitionMap* pmap,
      InternalAuth* auth,
      ReplicationScheme* repl_scheme);

protected:

  ScopedPtr<csql::TableExpression> buildTableExpression(
      csql::Transaction* ctx,
      csql::ExecutionContext* execution_context,
      RefPtr<csql::TableExpressionNode> node) override;

  ScopedPtr<csql::TableExpression> buildGroupByExpression(
      csql::Transaction* txn,
      csql::ExecutionContext* execution_context,
      RefPtr<csql::GroupByNode> node) override;

  ScopedPtr<csql::TableExpression> buildPartialGroupByExpression(
      csql::Transaction* txn,
      csql::ExecutionContext* execution_context,
      RefPtr<csql::GroupByNode> node);

  ScopedPtr<csql::TableExpression> buildPipelineGroupByExpression(
      csql::Transaction* txn,
      csql::ExecutionContext* execution_context,
      RefPtr<csql::GroupByNode> node);

  struct PipelinedQueryTree {
    bool is_local;
    RefPtr<csql::QueryTreeNode> qtree;
    Vector<ReplicaRef> hosts;
  };

  bool isPipelineable(const csql::QueryTreeNode& qtree);

  Vector<PipelinedQueryTree> pipelineExpression(
      csql::Transaction* txn,
      RefPtr<csql::QueryTreeNode> qtree);

  // rewrite tbl.lastXXX to tbl WHERE time > x and time < x
  void rewriteTableTimeSuffix(
      RefPtr<csql::QueryTreeNode> node);

  PartitionMap* pmap_;
  InternalAuth* auth_;
  ReplicationScheme* repl_scheme_;
  size_t running_cnt_;
};

} // namespace eventql

