#include "DatabaseManager.h"
#include <iostream>
#include <iomanip>
#include <regex>
#include <fstream>
#include <sstream>
#include <algorithm>

// Конструктор
DatabaseManager::DatabaseManager() : hEnv(nullptr), hDbc(nullptr), hStmt(nullptr), connected(false) {
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &hEnv);
    if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
        SQLSetEnvAttr(hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    }
}

// Деструктор
DatabaseManager::~DatabaseManager() {
    try {
        disconnect();
        if (hStmt) {
            SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
            hStmt = nullptr;
        }
        if (hDbc) {
            SQLFreeHandle(SQL_HANDLE_DBC, hDbc);
            hDbc = nullptr;
        }
        if (hEnv) {
            SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
            hEnv = nullptr;
        }
    }
    catch (...) {
        // Игнорируем ошибки в деструкторе
    }
}

// Функция конвертации UTF-8 в Windows-1251
std::string DatabaseManager::utf8_to_cp1251(const std::string& utf8_str) {
    if (utf8_str.empty()) return "";

    // Конвертируем UTF-8 в UTF-16
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, NULL, 0);
    if (wide_len <= 0) return utf8_str;

    wchar_t* wide_str = new wchar_t[wide_len];
    MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, wide_str, wide_len);

    // Конвертируем UTF-16 в CP1251
    int cp1251_len = WideCharToMultiByte(1251, 0, wide_str, -1, NULL, 0, NULL, NULL);
    if (cp1251_len <= 0) {
        delete[] wide_str;
        return utf8_str;
    }

    char* cp1251_str = new char[cp1251_len];
    WideCharToMultiByte(1251, 0, wide_str, -1, cp1251_str, cp1251_len, NULL, NULL);

    std::string result(cp1251_str);
    delete[] wide_str;
    delete[] cp1251_str;

    return result;
}

// Подключение к базе данных
bool DatabaseManager::connect(const std::wstring& connectionString) {
    if (connected) {
        disconnect();
    }

    // Проверяем и создаем окружение
    if (!hEnv) {
        SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &hEnv);
        if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
            return false;
        }
        SQLSetEnvAttr(hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    }

    // Создаем дескриптор соединения
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_DBC, hEnv, &hDbc);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        return false;
    }

    if (!hDbc) {
        return false;
    }

    // Устанавливаем таймаут
    SQLSetConnectAttr(hDbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);

    // Подключаемся
    SQLWCHAR outConnStr[1024];
    SQLSMALLINT outConnStrLen;

    ret = SQLDriverConnect(hDbc,
        nullptr,
        (SQLWCHAR*)connectionString.c_str(),
        (SQLSMALLINT)connectionString.length(),
        outConnStr,
        sizeof(outConnStr),
        &outConnStrLen,
        SQL_DRIVER_COMPLETE);

    if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
        connected = true;

        // Создаем дескриптор запроса
        if (hStmt) {
            SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
            hStmt = nullptr;
        }

        ret = SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
        if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
            connected = false;
            return false;
        }

        return true;
    }

    // Получаем ошибку
    SQLWCHAR sqlState[6], message[SQL_MAX_MESSAGE_LENGTH];
    SQLINTEGER nativeError;
    SQLSMALLINT messageLen;
    SQLGetDiagRec(SQL_HANDLE_DBC, hDbc, 1, sqlState, &nativeError, message, sizeof(message), &messageLen);

    char messageChar[SQL_MAX_MESSAGE_LENGTH];
    WideCharToMultiByte(CP_UTF8, 0, message, -1, messageChar, SQL_MAX_MESSAGE_LENGTH, nullptr, nullptr);
    lastError = messageChar;

    return false;
}

// Отключение от БД
void DatabaseManager::disconnect() {
    if (hStmt) {
        SQLFreeStmt(hStmt, SQL_CLOSE);
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        hStmt = nullptr;
    }
    if (hDbc) {
        SQLDisconnect(hDbc);
        SQLFreeHandle(SQL_HANDLE_DBC, hDbc);
        hDbc = nullptr;
    }
    connected = false;
}

// Проверка соединения
bool DatabaseManager::checkConnection() {
    if (!connected || !hDbc) return false;
    return true;
}

// Смена базы данных
bool DatabaseManager::changeDatabase(const std::wstring& newDatabase) {
    if (!connected || !checkConnection() || !hStmt) return false;

    std::wstring query = L"USE " + newDatabase;
    SQLRETURN ret = SQLExecDirect(hStmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
    SQLFreeStmt(hStmt, SQL_CLOSE);

    if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
        std::string dbName(newDatabase.begin(), newDatabase.end());
        logAction("CHANGE_DB", "Смена базы данных на: " + dbName);
        return true;
    }

    return false;
}

// Добавление ресурса
bool DatabaseManager::addResource(const Resource& resource) {
    if (!connected || !checkConnection() || !hStmt) return false;

    if (!isValidExtension(resource.name)) {
        lastError = "Недопустимое расширение файла";
        return false;
    }

    if (isDuplicateName(resource.name)) {
        lastError = "Файл с таким именем уже существует";
        return false;
    }

    if (!isValidFilename(resource.name)) {
        lastError = "Имя файла содержит недопустимые символы";
        return false;
    }

    const wchar_t* query = L"INSERT INTO Resources (Name, Size, CategoryID, OwnerID) VALUES (?, ?, ?, ?)";

    SQLRETURN ret = SQLPrepare(hStmt, (SQLWCHAR*)query, SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        return false;
    }

    SQLWCHAR name[256];
    MultiByteToWideChar(CP_UTF8, 0, resource.name.c_str(), -1, name, 256);

    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0, name, 0, nullptr);
    SQLBindParameter(hStmt, 2, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0,
        (SQLPOINTER)&resource.size, 0, nullptr);
    SQLBindParameter(hStmt, 3, SQL_PARAM_INPUT, SQL_C_SHORT, SQL_INTEGER, 0, 0,
        (SQLPOINTER)&resource.categoryId, 0, nullptr);
    SQLBindParameter(hStmt, 4, SQL_PARAM_INPUT, SQL_C_SHORT, SQL_INTEGER, 0, 0,
        (SQLPOINTER)&resource.ownerId, 0, nullptr);

    ret = SQLExecute(hStmt);
    SQLFreeStmt(hStmt, SQL_RESET_PARAMS);
    SQLFreeStmt(hStmt, SQL_CLOSE);

    if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
        logAction("ADD", "Добавлен ресурс: " + resource.name);
        return true;
    }

    return false;
}

// Мягкое удаление (в корзину)
bool DatabaseManager::softDeleteResource(int id) {
    if (!connected || !checkConnection() || !hStmt) return false;

    const wchar_t* query = L"UPDATE Resources SET isDeleted = 1, DeletedDate = GETDATE() WHERE ResourceID = ?";

    SQLRETURN ret = SQLPrepare(hStmt, (SQLWCHAR*)query, SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) return false;

    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &id, 0, nullptr);

    ret = SQLExecute(hStmt);
    SQLFreeStmt(hStmt, SQL_RESET_PARAMS);
    SQLFreeStmt(hStmt, SQL_CLOSE);

    if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
        logAction("SOFT_DELETE", "Удален ресурс ID: " + std::to_string(id));
        return true;
    }

    return false;
}

// Восстановление из корзины
bool DatabaseManager::restoreResource(int id) {
    if (!connected || !checkConnection() || !hStmt) return false;

    const wchar_t* query = L"UPDATE Resources SET isDeleted = 0, DeletedDate = NULL WHERE ResourceID = ?";

    SQLRETURN ret = SQLPrepare(hStmt, (SQLWCHAR*)query, SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) return false;

    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &id, 0, nullptr);

    ret = SQLExecute(hStmt);
    SQLFreeStmt(hStmt, SQL_RESET_PARAMS);
    SQLFreeStmt(hStmt, SQL_CLOSE);

    if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
        logAction("RESTORE", "Восстановлен ресурс ID: " + std::to_string(id));
        return true;
    }

    return false;
}

// Полное удаление
bool DatabaseManager::hardDeleteResource(int id) {
    if (!connected || !checkConnection() || !hStmt) return false;

    const wchar_t* query = L"DELETE FROM Resources WHERE ResourceID = ?";

    SQLRETURN ret = SQLPrepare(hStmt, (SQLWCHAR*)query, SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) return false;

    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &id, 0, nullptr);

    ret = SQLExecute(hStmt);
    SQLFreeStmt(hStmt, SQL_RESET_PARAMS);
    SQLFreeStmt(hStmt, SQL_CLOSE);

    if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
        logAction("HARD_DELETE", "Полностью удален ресурс ID: " + std::to_string(id));
        return true;
    }

    return false;
}

// Поиск по маске
std::vector<Resource> DatabaseManager::searchByName(const std::string& mask) {
    std::vector<Resource> results;
    if (!connected || !checkConnection() || !hStmt) return results;

    std::wstring query = L"SELECT ResourceID, Name, Size, CategoryID, OwnerID, CreatedDate "
        L"FROM Resources WHERE Name LIKE ? AND isDeleted = 0 ORDER BY Name";

    SQLRETURN ret = SQLPrepare(hStmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) return results;

    std::wstring searchPattern = L"%" + std::wstring(mask.begin(), mask.end()) + L"%";
    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,
        (SQLPOINTER)searchPattern.c_str(), 0, nullptr);

    ret = SQLExecute(hStmt);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        SQLFreeStmt(hStmt, SQL_CLOSE);
        return results;
    }

    SQLINTEGER id;
    SQLWCHAR name[256];
    SQLBIGINT size;
    SQLSMALLINT categoryId, ownerId;
    SQL_TIMESTAMP_STRUCT createdDate;

    SQLBindCol(hStmt, 1, SQL_C_LONG, &id, 0, nullptr);
    SQLBindCol(hStmt, 2, SQL_C_WCHAR, name, sizeof(name), nullptr);
    SQLBindCol(hStmt, 3, SQL_C_SBIGINT, &size, 0, nullptr);
    SQLBindCol(hStmt, 4, SQL_C_SHORT, &categoryId, 0, nullptr);
    SQLBindCol(hStmt, 5, SQL_C_SHORT, &ownerId, 0, nullptr);
    SQLBindCol(hStmt, 6, SQL_C_TYPE_TIMESTAMP, &createdDate, 0, nullptr);

    while (true) {
        ret = SQLFetch(hStmt);
        if (ret == SQL_NO_DATA) break;
        if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) break;

        Resource r;
        r.id = id;

        char nameChar[256] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, name, -1, nameChar, 256, nullptr, nullptr);
        r.name = utf8_to_cp1251(nameChar);

        r.size = size;
        r.categoryId = categoryId;
        r.ownerId = ownerId;

        char dateStr[20] = { 0 };
        sprintf_s(dateStr, "%04d-%02d-%02d", createdDate.year, createdDate.month, createdDate.day);
        r.createdDate = dateStr;
        r.isDeleted = false;

        results.push_back(r);
    }

    SQLFreeStmt(hStmt, SQL_CLOSE);
    return results;
}

// Получение отсортированных ресурсов
std::vector<Resource> DatabaseManager::getResourcesSorted(const std::string& orderBy, bool ascending) {
    std::vector<Resource> results;
    if (!connected || !checkConnection() || !hStmt) return results;

    std::string orderField;
    if (orderBy == "Name") orderField = "Name";
    else if (orderBy == "Size") orderField = "Size";
    else if (orderBy == "CreatedDate") orderField = "CreatedDate";
    else orderField = "ResourceID";

    std::string orderDir = ascending ? "ASC" : "DESC";

    std::wstring query = L"SELECT ResourceID, Name, Size, CategoryID, OwnerID, CreatedDate "
        L"FROM Resources WHERE isDeleted = 0 "
        L"ORDER BY " + std::wstring(orderField.begin(), orderField.end()) +
        L" " + std::wstring(orderDir.begin(), orderDir.end());

    SQLRETURN ret = SQLExecDirect(hStmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) return results;

    SQLINTEGER id;
    SQLWCHAR name[256];
    SQLBIGINT size;
    SQLSMALLINT categoryId, ownerId;
    SQL_TIMESTAMP_STRUCT createdDate;

    SQLBindCol(hStmt, 1, SQL_C_LONG, &id, 0, nullptr);
    SQLBindCol(hStmt, 2, SQL_C_WCHAR, name, sizeof(name), nullptr);
    SQLBindCol(hStmt, 3, SQL_C_SBIGINT, &size, 0, nullptr);
    SQLBindCol(hStmt, 4, SQL_C_SHORT, &categoryId, 0, nullptr);
    SQLBindCol(hStmt, 5, SQL_C_SHORT, &ownerId, 0, nullptr);
    SQLBindCol(hStmt, 6, SQL_C_TYPE_TIMESTAMP, &createdDate, 0, nullptr);

    while (true) {
        ret = SQLFetch(hStmt);
        if (ret == SQL_NO_DATA) break;
        if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) break;

        Resource r;
        r.id = id;

        char nameChar[256] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, name, -1, nameChar, 256, nullptr, nullptr);
        r.name = utf8_to_cp1251(nameChar);

        r.size = size;
        r.categoryId = categoryId;
        r.ownerId = ownerId;

        char dateStr[20] = { 0 };
        sprintf_s(dateStr, "%04d-%02d-%02d", createdDate.year, createdDate.month, createdDate.day);
        r.createdDate = dateStr;

        results.push_back(r);
    }

    SQLFreeStmt(hStmt, SQL_CLOSE);
    return results;
}

// Постраничный вывод
std::vector<Resource> DatabaseManager::getResourcesPaginated(int offset, int rowsPerPage) {
    std::vector<Resource> results;

    if (!connected || !checkConnection() || !hStmt) {
        return results;
    }

    std::wstring query = L"SELECT ResourceID, Name, Size, CategoryID, OwnerID, CreatedDate "
        L"FROM Resources WHERE isDeleted = 0 "
        L"ORDER BY ResourceID OFFSET ? ROWS FETCH NEXT ? ROWS ONLY";

    SQLRETURN ret = SQLPrepare(hStmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        return results;
    }

    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &offset, 0, nullptr);
    SQLBindParameter(hStmt, 2, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &rowsPerPage, 0, nullptr);

    ret = SQLExecute(hStmt);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        SQLFreeStmt(hStmt, SQL_CLOSE);
        return results;
    }

    SQLINTEGER id;
    SQLWCHAR name[256];
    SQLBIGINT size;
    SQLSMALLINT categoryId, ownerId;
    SQL_TIMESTAMP_STRUCT createdDate;

    SQLBindCol(hStmt, 1, SQL_C_LONG, &id, 0, nullptr);
    SQLBindCol(hStmt, 2, SQL_C_WCHAR, name, sizeof(name), nullptr);
    SQLBindCol(hStmt, 3, SQL_C_SBIGINT, &size, 0, nullptr);
    SQLBindCol(hStmt, 4, SQL_C_SHORT, &categoryId, 0, nullptr);
    SQLBindCol(hStmt, 5, SQL_C_SHORT, &ownerId, 0, nullptr);
    SQLBindCol(hStmt, 6, SQL_C_TYPE_TIMESTAMP, &createdDate, 0, nullptr);

    while (true) {
        ret = SQLFetch(hStmt);
        if (ret == SQL_NO_DATA) break;
        if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) break;

        Resource r;
        r.id = id;

        char nameChar[256] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, name, -1, nameChar, 256, nullptr, nullptr);
        r.name = utf8_to_cp1251(nameChar);

        r.size = size;
        r.categoryId = categoryId;
        r.ownerId = ownerId;

        char dateStr[20] = { 0 };
        sprintf_s(dateStr, "%04d-%02d-%02d", createdDate.year, createdDate.month, createdDate.day);
        r.createdDate = dateStr;
        r.isDeleted = false;

        results.push_back(r);
    }

    SQLFreeStmt(hStmt, SQL_CLOSE);
    return results;
}

// Статистика
bool DatabaseManager::getStatistics(int& totalCount, long long& totalSize) {
    totalCount = 0;
    totalSize = 0;

    if (!connected || !checkConnection() || !hStmt) return false;

    const wchar_t* query = L"SELECT COUNT(*), ISNULL(SUM(Size), 0) FROM Resources WHERE isDeleted = 0";

    SQLRETURN ret = SQLExecDirect(hStmt, (SQLWCHAR*)query, SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        return false;
    }

    SQLBindCol(hStmt, 1, SQL_C_LONG, &totalCount, 0, nullptr);
    SQLBindCol(hStmt, 2, SQL_C_SBIGINT, &totalSize, 0, nullptr);

    SQLFetch(hStmt);
    SQLFreeStmt(hStmt, SQL_CLOSE);

    return true;
}

// Логирование
bool DatabaseManager::logAction(const std::string& actionType, const std::string& description) {
    if (!connected || !hStmt) return false;

    const wchar_t* query = L"INSERT INTO Logs (ActionType, Description) VALUES (?, ?)";

    SQLRETURN ret = SQLPrepare(hStmt, (SQLWCHAR*)query, SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) return false;

    SQLWCHAR type[50], desc[500];
    MultiByteToWideChar(CP_UTF8, 0, actionType.c_str(), -1, type, 50);
    MultiByteToWideChar(CP_UTF8, 0, description.c_str(), -1, desc, 500);

    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 50, 0, type, 0, nullptr);
    SQLBindParameter(hStmt, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 500, 0, desc, 0, nullptr);

    ret = SQLExecute(hStmt);
    SQLFreeStmt(hStmt, SQL_RESET_PARAMS);
    SQLFreeStmt(hStmt, SQL_CLOSE);

    return (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO);
}

// Получение логов
std::vector<LogEntry> DatabaseManager::getLogs(int limit) {
    std::vector<LogEntry> results;
    if (!connected || !checkConnection() || !hStmt) return results;

    std::wstring query = L"SELECT TOP " + std::to_wstring(limit) +
        L" LogID, ActionType, Description, ActionDate FROM Logs ORDER BY ActionDate DESC";

    SQLRETURN ret = SQLExecDirect(hStmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) return results;

    SQLINTEGER id;
    SQLWCHAR type[50], desc[500];
    SQL_TIMESTAMP_STRUCT actionDate;

    SQLBindCol(hStmt, 1, SQL_C_LONG, &id, 0, nullptr);
    SQLBindCol(hStmt, 2, SQL_C_WCHAR, type, sizeof(type), nullptr);
    SQLBindCol(hStmt, 3, SQL_C_WCHAR, desc, sizeof(desc), nullptr);
    SQLBindCol(hStmt, 4, SQL_C_TYPE_TIMESTAMP, &actionDate, 0, nullptr);

    while (true) {
        ret = SQLFetch(hStmt);
        if (ret == SQL_NO_DATA) break;
        if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) break;

        LogEntry log;
        log.id = id;

        char typeChar[50] = { 0 }, descChar[500] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, type, -1, typeChar, 50, nullptr, nullptr);
        WideCharToMultiByte(CP_UTF8, 0, desc, -1, descChar, 500, nullptr, nullptr);

        log.actionType = utf8_to_cp1251(typeChar);
        log.description = utf8_to_cp1251(descChar);

        char dateStr[30] = { 0 };
        sprintf_s(dateStr, "%04d-%02d-%02d %02d:%02d:%02d",
            actionDate.year, actionDate.month, actionDate.day,
            actionDate.hour, actionDate.minute, actionDate.second);
        log.actionDate = dateStr;

        results.push_back(log);
    }

    SQLFreeStmt(hStmt, SQL_CLOSE);
    return results;
}

// Проверка расширения файла
bool DatabaseManager::isValidExtension(const std::string& filename) {
    const std::vector<std::string> allowedExtensions = { ".exe", ".txt", ".pdf", ".doc", ".docx",
                                                         ".xls", ".xlsx", ".jpg", ".png", ".mp3",
                                                         ".mp4", ".zip", ".rar", ".cpp", ".h", ".c", ".cs" };

    size_t dotPos = filename.find_last_of('.');
    if (dotPos == std::string::npos) return false;

    std::string ext = filename.substr(dotPos);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    for (const auto& allowed : allowedExtensions) {
        if (ext == allowed) return true;
    }

    return false;
}

// Проверка дубликатов
bool DatabaseManager::isDuplicateName(const std::string& name) {
    if (!connected || !checkConnection() || !hStmt) return false;

    const wchar_t* query = L"SELECT COUNT(*) FROM Resources WHERE Name = ? AND isDeleted = 0";

    SQLRETURN ret = SQLPrepare(hStmt, (SQLWCHAR*)query, SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) return false;

    SQLWCHAR nameW[256];
    MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nameW, 256);
    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0, nameW, 0, nullptr);

    ret = SQLExecute(hStmt);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        SQLFreeStmt(hStmt, SQL_CLOSE);
        return false;
    }

    SQLINTEGER count;
    SQLBindCol(hStmt, 1, SQL_C_LONG, &count, 0, nullptr);
    SQLFetch(hStmt);

    SQLFreeStmt(hStmt, SQL_CLOSE);

    return count > 0;
}

// Валидация имени файла
bool DatabaseManager::isValidFilename(const std::string& filename) {
    std::regex invalidChars(R"([/\\:*?\"<>|])");
    return !std::regex_search(filename, invalidChars);
}

// Получение категорий
std::vector<Category> DatabaseManager::getCategories() {
    std::vector<Category> results;

    if (!connected || !checkConnection() || !hStmt) {
        return results;
    }

    const wchar_t* query = L"SELECT CategoryID, CategoryName FROM Categories ORDER BY CategoryName";

    SQLRETURN ret = SQLExecDirect(hStmt, (SQLWCHAR*)query, SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        SQLFreeStmt(hStmt, SQL_CLOSE);
        return results;
    }

    SQLSMALLINT id;
    SQLWCHAR name[256];

    SQLBindCol(hStmt, 1, SQL_C_SHORT, &id, 0, nullptr);
    SQLBindCol(hStmt, 2, SQL_C_WCHAR, name, sizeof(name), nullptr);

    while (true) {
        ret = SQLFetch(hStmt);
        if (ret == SQL_NO_DATA) break;
        if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) break;

        Category c;
        c.id = id;

        char nameChar[256] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, name, -1, nameChar, 256, nullptr, nullptr);
        c.name = utf8_to_cp1251(nameChar);

        results.push_back(c);
    }

    SQLFreeStmt(hStmt, SQL_CLOSE);
    return results;
}

// Получение пользователей
std::vector<User> DatabaseManager::getUsers() {
    std::vector<User> results;

    if (!connected || !checkConnection() || !hStmt) {
        return results;
    }

    const wchar_t* query = L"SELECT UserID, UserName FROM Users ORDER BY UserName";

    SQLRETURN ret = SQLExecDirect(hStmt, (SQLWCHAR*)query, SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        SQLFreeStmt(hStmt, SQL_CLOSE);
        return results;
    }

    SQLSMALLINT id;
    SQLWCHAR name[256];

    SQLBindCol(hStmt, 1, SQL_C_SHORT, &id, 0, nullptr);
    SQLBindCol(hStmt, 2, SQL_C_WCHAR, name, sizeof(name), nullptr);

    while (true) {
        ret = SQLFetch(hStmt);
        if (ret == SQL_NO_DATA) break;
        if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) break;

        User u;
        u.id = id;

        char nameChar[256] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, name, -1, nameChar, 256, nullptr, nullptr);
        u.name = utf8_to_cp1251(nameChar);

        results.push_back(u);
    }

    SQLFreeStmt(hStmt, SQL_CLOSE);
    return results;
}

// Количество ресурсов
int DatabaseManager::getResourceCount() {
    if (!connected || !checkConnection() || !hStmt) return 0;

    const wchar_t* query = L"SELECT COUNT(*) FROM Resources WHERE isDeleted = 0";

    SQLRETURN ret = SQLExecDirect(hStmt, (SQLWCHAR*)query, SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        return 0;
    }

    SQLINTEGER count;
    SQLBindCol(hStmt, 1, SQL_C_LONG, &count, 0, nullptr);
    SQLFetch(hStmt);

    SQLFreeStmt(hStmt, SQL_CLOSE);
    return count;
}

// Экспорт в CSV
bool DatabaseManager::exportToCSV(const std::string& filename) {
    if (!connected || !checkConnection() || !hStmt) return false;

    std::ofstream file(filename);
    if (!file.is_open()) return false;

    file << "ID,Name,Size,CategoryID,OwnerID,CreatedDate\n";

    const wchar_t* query = L"SELECT ResourceID, Name, Size, CategoryID, OwnerID, CreatedDate "
        L"FROM Resources WHERE isDeleted = 0";

    SQLRETURN ret = SQLExecDirect(hStmt, (SQLWCHAR*)query, SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        file.close();
        return false;
    }

    SQLINTEGER id;
    SQLWCHAR name[256];
    SQLBIGINT size;
    SQLSMALLINT categoryId, ownerId;
    SQL_TIMESTAMP_STRUCT createdDate;

    SQLBindCol(hStmt, 1, SQL_C_LONG, &id, 0, nullptr);
    SQLBindCol(hStmt, 2, SQL_C_WCHAR, name, sizeof(name), nullptr);
    SQLBindCol(hStmt, 3, SQL_C_SBIGINT, &size, 0, nullptr);
    SQLBindCol(hStmt, 4, SQL_C_SHORT, &categoryId, 0, nullptr);
    SQLBindCol(hStmt, 5, SQL_C_SHORT, &ownerId, 0, nullptr);
    SQLBindCol(hStmt, 6, SQL_C_TYPE_TIMESTAMP, &createdDate, 0, nullptr);

    while (true) {
        ret = SQLFetch(hStmt);
        if (ret == SQL_NO_DATA) break;
        if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) break;

        char nameChar[256] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, name, -1, nameChar, 256, nullptr, nullptr);
        std::string nameStr = utf8_to_cp1251(nameChar);

        file << id << ","
            << "\"" << nameStr << "\","
            << size << ","
            << categoryId << ","
            << ownerId << ","
            << createdDate.year << "-" << createdDate.month << "-" << createdDate.day << "\n";
    }

    SQLFreeStmt(hStmt, SQL_CLOSE);
    file.close();

    logAction("EXPORT", "Экспорт в CSV: " + filename);
    return true;
}

// Экспорт отчета
bool DatabaseManager::exportReport(const std::string& filename) {
    auto resources = getResourcesSorted("Name", true);

    std::ofstream file(filename);
    if (!file.is_open()) return false;

    file << "========================================" << std::endl;
    file << "ОТЧЕТ ПО РЕСУРСАМ" << std::endl;
    file << "Дата: " << __DATE__ << " " << __TIME__ << std::endl;
    file << "========================================" << std::endl << std::endl;

    for (const auto& r : resources) {
        file << "ID: " << r.id << std::endl;
        file << "Имя: " << r.name << std::endl;
        file << "Размер: " << r.size << " байт" << std::endl;
        file << "Дата создания: " << r.createdDate << std::endl;
        file << "----------------------------------------" << std::endl;
    }

    file.close();
    return true;
}

// Получение старых ресурсов
std::vector<Resource> DatabaseManager::getOldResources(int monthsOld) {
    std::vector<Resource> results;
    if (!connected || !checkConnection() || !hStmt) return results;

    std::wstring query = L"SELECT ResourceID, Name, Size, CategoryID, OwnerID, CreatedDate "
        L"FROM Resources WHERE isDeleted = 0 "
        L"AND DATEDIFF(MONTH, CreatedDate, GETDATE()) >= ?";

    SQLRETURN ret = SQLPrepare(hStmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) return results;

    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &monthsOld, 0, nullptr);

    ret = SQLExecute(hStmt);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        SQLFreeStmt(hStmt, SQL_CLOSE);
        return results;
    }

    SQLINTEGER id;
    SQLWCHAR name[256];
    SQLBIGINT size;
    SQLSMALLINT categoryId, ownerId;
    SQL_TIMESTAMP_STRUCT createdDate;

    SQLBindCol(hStmt, 1, SQL_C_LONG, &id, 0, nullptr);
    SQLBindCol(hStmt, 2, SQL_C_WCHAR, name, sizeof(name), nullptr);
    SQLBindCol(hStmt, 3, SQL_C_SBIGINT, &size, 0, nullptr);
    SQLBindCol(hStmt, 4, SQL_C_SHORT, &categoryId, 0, nullptr);
    SQLBindCol(hStmt, 5, SQL_C_SHORT, &ownerId, 0, nullptr);
    SQLBindCol(hStmt, 6, SQL_C_TYPE_TIMESTAMP, &createdDate, 0, nullptr);

    while (true) {
        ret = SQLFetch(hStmt);
        if (ret == SQL_NO_DATA) break;
        if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) break;

        Resource r;
        r.id = id;

        char nameChar[256] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, name, -1, nameChar, 256, nullptr, nullptr);
        r.name = utf8_to_cp1251(nameChar);

        r.size = size;
        r.categoryId = categoryId;
        r.ownerId = ownerId;

        char dateStr[20] = { 0 };
        sprintf_s(dateStr, "%04d-%02d-%02d", createdDate.year, createdDate.month, createdDate.day);
        r.createdDate = dateStr;

        results.push_back(r);
    }

    SQLFreeStmt(hStmt, SQL_CLOSE);
    return results;
}

// Удаление старых ресурсов
bool DatabaseManager::deleteOldResources(int monthsOld) {
    if (!connected || !checkConnection() || !hStmt) return false;

    const wchar_t* query = L"DELETE FROM Resources WHERE isDeleted = 0 "
        L"AND DATEDIFF(MONTH, CreatedDate, GETDATE()) >= ?";

    SQLRETURN ret = SQLPrepare(hStmt, (SQLWCHAR*)query, SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) return false;

    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &monthsOld, 0, nullptr);

    ret = SQLExecute(hStmt);
    SQLFreeStmt(hStmt, SQL_RESET_PARAMS);
    SQLFreeStmt(hStmt, SQL_CLOSE);

    if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
        logAction("CLEAN", "Удалены ресурсы старше " + std::to_string(monthsOld) + " месяцев");
        return true;
    }

    return false;
}

// Получение ошибки
std::string DatabaseManager::getLastError(SQLHANDLE handle, SQLSMALLINT handleType) {
    SQLWCHAR sqlState[6], message[SQL_MAX_MESSAGE_LENGTH];
    SQLINTEGER nativeError;
    SQLSMALLINT messageLen;

    SQLGetDiagRec(handleType, handle, 1, sqlState, &nativeError, message, sizeof(message), &messageLen);

    char sqlStateChar[6] = { 0 }, messageChar[SQL_MAX_MESSAGE_LENGTH] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, sqlState, -1, sqlStateChar, 6, nullptr, nullptr);
    WideCharToMultiByte(CP_UTF8, 0, message, -1, messageChar, SQL_MAX_MESSAGE_LENGTH, nullptr, nullptr);

    return std::string(sqlStateChar) + ": " + std::string(messageChar);
}