#include "CheckedTransaction.h"
#include "MySQLConnection.h"

bool CheckedTransaction::Execute(std::string const& sql) { return _connection.Execute(sql.c_str()); }
QueryResult CheckedTransaction::Query(std::string const& sql)
{
    ResultSet* rows = _connection.Query(sql.c_str());
    if (!rows || !rows->GetRowCount()) { delete rows; return QueryResult(nullptr); }
    rows->NextRow();
    return QueryResult(rows);
}
std::string CheckedTransaction::Quote(std::string const& value) { return _connection.QuoteString(value); }
bool CheckedTransaction::Execute(SQLTransaction const& statements)
{
    for (auto const& statement : statements->m_queries)
        if (statement.type == SQL_ELEMENT_PREPARED ? !_connection.Execute(statement.element.stmt) :
            !_connection.Execute(statement.element.query)) return false;
    return true;
}
