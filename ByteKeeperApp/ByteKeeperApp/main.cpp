#include <iostream>
#include <iomanip>
#include <windows.h>
#include <string>
#include <locale>
#include "DatabaseManager.h"

using namespace std;

// Функция для установки кодировки для русского языка
void setRussianConsole() {
    SetConsoleOutputCP(1251);
    SetConsoleCP(1251);
    setlocale(LC_ALL, "Russian");
    system("chcp 1251 > nul");
}

// Функция для установки цвета консоли
void setConsoleColor(WORD color_value) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color_value);
}

// Функция для обрезки длинного имени
string truncateString(const string& str, size_t maxLen = 15) {
    if (str.length() <= maxLen) return str;
    return str.substr(0, maxLen - 3) + "...";
}

// Функция для вывода ресурсов
void displayResources(const vector<Resource>& resources_list, DatabaseManager& db_manager) {
    if (resources_list.empty()) {
        setConsoleColor(14);
        cout << "Нет ресурсов для отображения." << endl;
        setConsoleColor(7);
        return;
    }

    int maxNameLen = 4;
    for (const auto& res : resources_list) {
        int len = res.name.length();
        if (len > maxNameLen && len < 50) maxNameLen = len;
    }
    if (maxNameLen > 30) maxNameLen = 30;

    setConsoleColor(11);
    cout << left
        << setw(5) << "ID"
        << setw(maxNameLen + 2) << "Имя"
        << setw(12) << "Размер"
        << setw(12) << "Категория"
        << setw(15) << "Владелец"
        << setw(12) << "Дата"
        << endl;
    cout << string(5 + maxNameLen + 2 + 12 + 12 + 15 + 12, '-') << endl;

    setConsoleColor(7);

    auto categories_list = db_manager.getCategories();
    auto users_list = db_manager.getUsers();

    for (const auto& res : resources_list) {
        string categoryName = "Неизвестно";
        for (const auto& cat : categories_list) {
            if (cat.id == res.categoryId) {
                categoryName = cat.name;
                break;
            }
        }

        string userName = "Неизвестно";
        for (const auto& usr : users_list) {
            if (usr.id == res.ownerId) {
                userName = usr.name;
                break;
            }
        }

        cout << left
            << setw(5) << res.id
            << setw(maxNameLen + 2) << truncateString(res.name, maxNameLen)
            << setw(12) << res.size
            << setw(12) << truncateString(categoryName, 10)
            << setw(15) << truncateString(userName, 13)
            << setw(12) << res.createdDate
            << endl;
    }
}

// Функция для ввода ID с валидацией
int inputInt(const string& prompt_text) {
    string input_str;
    while (true) {
        cout << prompt_text;
        getline(cin, input_str);

        bool isValid = true;
        for (char c : input_str) {
            if (!isdigit(c)) {
                isValid = false;
                break;
            }
        }

        if (isValid && !input_str.empty()) {
            return stoi(input_str);
        }

        setConsoleColor(12);
        cout << "Ошибка: Введите число!" << endl;
        setConsoleColor(7);
    }
}

// Функция вывода меню
void printMenu() {
    setConsoleColor(7);
    cout << "\n========================================" << endl;
    cout << "    УПРАВЛЕНИЕ ЦИФРОВЫМИ АКТИВАМИ" << endl;
    cout << "========================================" << endl;
    cout << "1. Добавить ресурс" << endl;
    cout << "2. Показать все ресурсы" << endl;
    cout << "3. Поиск по имени" << endl;
    cout << "4. Сортировка ресурсов" << endl;
    cout << "5. Статистика" << endl;
    cout << "6. Корзина (восстановление)" << endl;
    cout << "7. Удалить ресурс (в корзину)" << endl;
    cout << "8. Очистка старых данных" << endl;
    cout << "9. Экспорт в CSV" << endl;
    cout << "10. Экспорт отчета" << endl;
    cout << "11. Показать логи" << endl;
    cout << "12. Смена базы данных" << endl;
    cout << "13. Проверка соединения" << endl;
    cout << "0. Выход" << endl;
    cout << "========================================" << endl;
    cout << "Выберите действие: ";
}

int main() {
    // Устанавливаем русскую кодировку
    setRussianConsole();
    setConsoleColor(7);

    cout << "========================================" << endl;
    cout << "   BYTEKEEPER - СИСТЕМА УПРАВЛЕНИЯ" << endl;
    cout << "========================================" << endl << endl;

    DatabaseManager db_manager;

    // Строка подключения к вашей базе данных
    wstring connStr = L"DRIVER={SQL Server};SERVER=localhost\\SQLEXPRESS;DATABASE=PlotnikovUP0101;Trusted_Connection=yes;";

    cout << "Подключение к базе данных PlotnikovUP0101..." << endl;

    if (!db_manager.connect(connStr)) {
        setConsoleColor(12);
        cout << "Ошибка подключения к базе данных!" << endl;
        setConsoleColor(7);
        system("pause");
        return 1;
    }

    setConsoleColor(10);
    cout << "Подключение успешно установлено!" << endl;
    setConsoleColor(7);

    // Проверяем наличие данных
    auto categories = db_manager.getCategories();
    auto users = db_manager.getUsers();
    int resourceCount = db_manager.getResourceCount();

    cout << "\nКатегории в базе: " << categories.size() << endl;
    cout << "Пользователи в базе: " << users.size() << endl;
    cout << "Ресурсы в базе: " << resourceCount << endl;

    int choice;
    string input_str;

    while (true) {
        printMenu();
        getline(cin, input_str);

        if (input_str.empty() || !isdigit(input_str[0])) {
            setConsoleColor(12);
            cout << "Ошибка: Введите номер пункта меню!" << endl;
            setConsoleColor(7);
            continue;
        }

        choice = stoi(input_str);

        switch (choice) {
        case 0: {
            cout << "До свидания!" << endl;
            return 0;
        }

        case 1: {
            Resource newRes;

            cout << "Имя файла: ";
            getline(cin, newRes.name);

            if (!db_manager.isValidFilename(newRes.name)) {
                setConsoleColor(12);
                cout << "Ошибка: Имя содержит недопустимые символы!" << endl;
                setConsoleColor(7);
                break;
            }

            if (!db_manager.isValidExtension(newRes.name)) {
                setConsoleColor(12);
                cout << "Ошибка: Недопустимое расширение файла!" << endl;
                cout << "Разрешенные расширения: .exe, .txt, .pdf, .doc, .docx, .jpg, .png, .mp3, .mp4, .zip, .rar, .cpp, .h" << endl;
                setConsoleColor(7);
                break;
            }

            if (db_manager.isDuplicateName(newRes.name)) {
                setConsoleColor(12);
                cout << "Ошибка: Файл с таким именем уже существует!" << endl;
                setConsoleColor(7);
                break;
            }

            cout << "Размер (байт): ";
            string sizeStr;
            getline(cin, sizeStr);
            newRes.size = stoll(sizeStr);

            auto categories_list = db_manager.getCategories();
            if (categories_list.empty()) {
                setConsoleColor(12);
                cout << "Ошибка: Нет доступных категорий!" << endl;
                setConsoleColor(7);
                break;
            }

            cout << "\nДоступные категории:" << endl;
            for (const auto& cat : categories_list) {
                cout << "  " << cat.id << ". " << cat.name << endl;
            }
            newRes.categoryId = inputInt("Выберите ID категории: ");

            auto users_list = db_manager.getUsers();
            if (users_list.empty()) {
                setConsoleColor(12);
                cout << "Ошибка: Нет доступных пользователей!" << endl;
                setConsoleColor(7);
                break;
            }

            cout << "\nДоступные пользователи:" << endl;
            for (const auto& usr : users_list) {
                cout << "  " << usr.id << ". " << usr.name << endl;
            }
            newRes.ownerId = inputInt("Выберите ID владельца: ");

            if (db_manager.addResource(newRes)) {
                setConsoleColor(10);
                cout << "Ресурс успешно добавлен!" << endl;
                setConsoleColor(7);
            }
            else {
                setConsoleColor(12);
                cout << "Ошибка при добавлении ресурса!" << endl;
                setConsoleColor(7);
            }
            break;
        }

        case 2: {
            int page = 0;
            const int rowsPerPage = 10;

            while (true) {
                auto resources_list = db_manager.getResourcesPaginated(page * rowsPerPage, rowsPerPage);

                if (resources_list.empty() && page == 0) {
                    setConsoleColor(14);
                    cout << "Нет ресурсов для отображения." << endl;
                    setConsoleColor(7);
                    break;
                }

                cout << "\n--- Страница " << (page + 1) << " ---" << endl;
                displayResources(resources_list, db_manager);

                if (resources_list.size() < rowsPerPage) {
                    cout << "\n--- Конец списка ---" << endl;
                    break;
                }

                cout << "\n[N] - следующая страница, [P] - предыдущая, [Q] - выход: ";
                string nav;
                getline(cin, nav);

                if (nav == "N" || nav == "n") {
                    page++;
                }
                else if (nav == "P" || nav == "p") {
                    if (page > 0) page--;
                }
                else if (nav == "Q" || nav == "q") {
                    break;
                }
            }
            break;
        }

        case 3: {
            cout << "Введите часть имени для поиска: ";
            string mask;
            getline(cin, mask);

            auto results = db_manager.searchByName(mask);
            cout << "\nРезультаты поиска (найдено: " << results.size() << "):" << endl;
            displayResources(results, db_manager);
            break;
        }

        case 4: {
            cout << "Сортировать по (Name/Size/CreatedDate): ";
            string orderBy;
            getline(cin, orderBy);

            cout << "Порядок (ASC/DESC): ";
            string orderDir;
            getline(cin, orderDir);

            bool ascending = (orderDir == "ASC" || orderDir == "asc");
            auto results = db_manager.getResourcesSorted(orderBy, ascending);
            cout << "\nОтсортированные ресурсы:" << endl;
            displayResources(results, db_manager);
            break;
        }

        case 5: {
            int totalCount;
            long long totalSize;

            if (db_manager.getStatistics(totalCount, totalSize)) {
                setConsoleColor(11);
                cout << "\n=== СТАТИСТИКА ===" << endl;
                cout << "Всего файлов: " << totalCount << endl;
                cout << "Общий размер: " << totalSize << " байт (";

                if (totalSize > 1024 * 1024) {
                    cout << fixed << setprecision(2) << (totalSize / (1024.0 * 1024.0)) << " MB";
                }
                else if (totalSize > 1024) {
                    cout << fixed << setprecision(2) << (totalSize / 1024.0) << " KB";
                }
                else {
                    cout << totalSize << " B";
                }
                cout << ")" << endl;
                setConsoleColor(7);
            }
            break;
        }

        case 6: {
            int id = inputInt("Введите ID ресурса для восстановления: ");
            if (db_manager.restoreResource(id)) {
                setConsoleColor(10);
                cout << "Ресурс восстановлен из корзины!" << endl;
                setConsoleColor(7);
            }
            else {
                setConsoleColor(12);
                cout << "Ошибка при восстановлении!" << endl;
                setConsoleColor(7);
            }
            break;
        }

        case 7: {
            int id = inputInt("Введите ID ресурса для удаления: ");
            if (db_manager.softDeleteResource(id)) {
                setConsoleColor(10);
                cout << "Ресурс перемещен в корзину!" << endl;
                setConsoleColor(7);
            }
            else {
                setConsoleColor(12);
                cout << "Ошибка при удалении!" << endl;
                setConsoleColor(7);
            }
            break;
        }

        case 8: {
            int months = inputInt("Удалить ресурсы старше (месяцев): ");

            auto oldResources = db_manager.getOldResources(months);

            if (oldResources.empty()) {
                setConsoleColor(14);
                cout << "Нет ресурсов старше " << months << " месяцев." << endl;
                setConsoleColor(7);
            }
            else {
                setConsoleColor(12);
                cout << "Найдено " << oldResources.size() << " старых ресурсов:" << endl;
                displayResources(oldResources, db_manager);

                cout << "\nУдалить их безвозвратно? (y/n): ";
                string confirm;
                getline(cin, confirm);

                if (confirm == "y" || confirm == "Y") {
                    if (db_manager.deleteOldResources(months)) {
                        setConsoleColor(10);
                        cout << "Старые ресурсы удалены!" << endl;
                        setConsoleColor(7);
                    }
                    else {
                        setConsoleColor(12);
                        cout << "Ошибка при удалении!" << endl;
                        setConsoleColor(7);
                    }
                }
            }
            break;
        }

        case 9: {
            cout << "Имя файла для экспорта (например, resources.csv): ";
            string filename;
            getline(cin, filename);

            if (db_manager.exportToCSV(filename)) {
                setConsoleColor(10);
                cout << "Экспорт выполнен в файл: " << filename << endl;
                setConsoleColor(7);
            }
            else {
                setConsoleColor(12);
                cout << "Ошибка при экспорте!" << endl;
                setConsoleColor(7);
            }
            break;
        }

        case 10: {
            cout << "Имя файла для отчета (например, report.txt): ";
            string filename;
            getline(cin, filename);

            if (db_manager.exportReport(filename)) {
                setConsoleColor(10);
                cout << "Отчет сохранен в файл: " << filename << endl;
                setConsoleColor(7);
            }
            else {
                setConsoleColor(12);
                cout << "Ошибка при создании отчета!" << endl;
                setConsoleColor(7);
            }
            break;
        }

        case 11: {
            auto logs_list = db_manager.getLogs(50);

            if (logs_list.empty()) {
                setConsoleColor(14);
                cout << "Нет записей в логах." << endl;
            }
            else {
                setConsoleColor(11);
                cout << left
                    << setw(5) << "ID"
                    << setw(20) << "Тип"
                    << setw(25) << "Дата"
                    << "Описание" << endl;
                cout << string(80, '-') << endl;

                setConsoleColor(7);
                for (const auto& log_item : logs_list) {
                    cout << left
                        << setw(5) << log_item.id
                        << setw(20) << log_item.actionType
                        << setw(25) << log_item.actionDate
                        << log_item.description << endl;
                }
            }
            setConsoleColor(7);
            break;
        }

        case 12: {
            cout << "Введите имя базы данных: ";
            string dbName;
            getline(cin, dbName);

            wstring wDbName(dbName.begin(), dbName.end());
            if (db_manager.changeDatabase(wDbName)) {
                setConsoleColor(10);
                cout << "База данных изменена на: " << dbName << endl;
                setConsoleColor(7);
            }
            else {
                setConsoleColor(12);
                cout << "Ошибка при смене базы данных!" << endl;
                setConsoleColor(7);
            }
            break;
        }

        case 13: {
            if (db_manager.isConnected()) {
                setConsoleColor(10);
                cout << "Соединение активно!" << endl;
            }
            else {
                setConsoleColor(12);
                cout << "Соединение потеряно!" << endl;
            }
            setConsoleColor(7);
            break;
        }

        default:
            setConsoleColor(12);
            cout << "Неверный выбор! Выберите пункт от 0 до 13." << endl;
            setConsoleColor(7);
            break;
        }

        cout << "\nНажмите Enter для продолжения...";
        cin.get();
        system("cls");
    }

    return 0;
}