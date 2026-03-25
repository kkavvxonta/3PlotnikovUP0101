#pragma once

#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <string>
#include <vector>
#include "Resource.h"

class DatabaseManager {
private:
    SQLHENV hEnv;
    SQLHDBC hDbc;
    SQLHSTMT hStmt;
    bool connected;
    std::string lastError;

    std::string getLastError(SQLHANDLE handle, SQLSMALLINT handleType);
    bool checkConnection();
    std::string utf8_to_cp1251(const std::string& utf8_str);

public:
    DatabaseManager();
    ~DatabaseManager();

    bool connect(const std::wstring& connectionString);
    void disconnect();
    bool isConnected() const { return connected; }
    bool changeDatabase(const std::wstring& newDatabase);

    bool addResource(const Resource& resource);
    bool softDeleteResource(int id);
    bool restoreResource(int id);
    bool hardDeleteResource(int id);

    std::vector<Resource> searchByName(const std::string& mask);
    std::vector<Resource> getResourcesSorted(const std::string& orderBy, bool ascending = true);
    std::vector<Resource> getResourcesPaginated(int offset, int rowsPerPage);

    bool getStatistics(int& totalCount, long long& totalSize);
    bool logAction(const std::string& actionType, const std::string& description);
    std::vector<LogEntry> getLogs(int limit = 50);

    bool isValidExtension(const std::string& filename);
    bool isDuplicateName(const std::string& name);
    bool isValidFilename(const std::string& filename);

    std::vector<Category> getCategories();
    std::vector<User> getUsers();
    int getResourceCount();

    bool exportToCSV(const std::string& filename);
    bool exportReport(const std::string& filename);
    std::vector<Resource> getOldResources(int monthsOld);
    bool deleteOldResources(int monthsOld);
};