/* This file is part of Project SkyFire. See LICENSE.md. */
#ifndef SKYFIRE_CHECKED_TRANSACTION_H
#define SKYFIRE_CHECKED_TRANSACTION_H

#include "QueryResult.h"
#include "Transaction.h"
class MySQLConnection;

enum class DatabaseCommitResult { Committed, Failed, Indeterminate };

// Valid only inside DatabaseWorkerPool::WithTransaction: the pool holds one
// connection for the entire callback, including SELECT ... FOR UPDATE.
class CheckedTransaction
{
public:
    explicit CheckedTransaction(MySQLConnection& connection) : _connection(connection) { }
    bool Execute(std::string const& sql);
    QueryResult Query(std::string const& sql);
    std::string Quote(std::string const& value);
    bool Execute(SQLTransaction const& statements);
private:
    MySQLConnection& _connection;
};
#endif
