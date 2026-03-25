#pragma once
#include <string>

struct Resource {
    int id;
    std::string name;
    long long size;
    int categoryId;
    int ownerId;
    std::string createdDate;
    bool isDeleted;

    Resource() : id(0), size(0), categoryId(0), ownerId(0), isDeleted(false) {}
};

struct Category {
    int id;
    std::string name;
};

struct User {
    int id;
    std::string name;
};

struct LogEntry {
    int id;
    std::string actionType;
    std::string description;
    std::string actionDate;
};