#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <openssl/sha.h>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

// Структура для хранения информации о файле
struct FileInfo {
  fs::path path;
  string hash;
  ino_t inode;  // Номер inode
  dev_t device; // Номер устройства
};

// Класс для работы с хардлинками
class HardLinksManager {
private:
  unordered_map<string, vector<FileInfo>> hashMap;
  int filesProcessed = 0;
  long long bytesSaved = 0;

public:
  // Вычисление SHA1 хэша файла
  string calculateSHA1(const fs::path &filePath) {
    ifstream file(filePath, ios::binary);
    if (!file) {
      throw runtime_error("Cannot open file: " + filePath.string());
    }

    unsigned char buffer[4096];
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA_CTX sha1_context;
    SHA1_Init(&sha1_context);

    while (file.read(reinterpret_cast<char *>(buffer), sizeof(buffer))) {
      SHA1_Update(&sha1_context, buffer, file.gcount());
    }
    SHA1_Final(hash, &sha1_context);

    // Преобразование хэша в hex строку
    char hex[SHA_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
      sprintf(hex + (i * 2), "%02x", hash[i]);
    }
    hex[SHA_DIGEST_LENGTH * 2] = '\0';
    return string(hex);
  }

  // Рекурсивный обход каталога
  void scanDirectory(const fs::path &dirPath) {
    try {
      for (const auto &entry : fs::recursive_directory_iterator(dirPath)) {
        if (fs::is_regular_file(entry)) {
          try {
            string hash = calculateSHA1(entry.path());

            struct stat statbuf;
            stat(entry.path().c_str(), &statbuf);

            FileInfo info{entry.path(), hash, statbuf.st_ino, statbuf.st_dev};

            hashMap[hash].push_back(info);
            filesProcessed++;

            cout << "Обработан: " << entry.path() << endl;
          } catch (const exception &e) {
            cerr << "Ошибка при обработке " << entry.path() << ": " << e.what()
                 << endl;
          }
        }
      }
    } catch (const exception &e) {
      cerr << "Ошибка при сканировании: " << e.what() << endl;
    }
  }

  // Замена дубликатов на жёсткие ссылки
  void createHardLinks() {
    int totalDuplicates = 0;

    for (auto &entry : hashMap) {
      const string &hash = entry.first;
      vector<FileInfo> &files = entry.second;

      // Если есть дубликаты
      if (files.size() > 1) {
        cout << "\nОбнаружены дубликаты (SHA1: " << hash.substr(0, 8)
             << "...):" << endl;

        // Сортируем по времени изменения (старший файл будет оригиналом)
        sort(files.begin(), files.end(),
             [](const FileInfo &a, const FileInfo &b) {
               return fs::last_write_time(a.path) < fs::last_write_time(b.path);
             });

        // Первый файл остаётся оригиналом
        const FileInfo &original = files[0];
        cout << "  Оригинал: " << original.path << endl;

        // Остальные заменяются на жёсткие ссылки
        for (size_t i = 1; i < files.size(); ++i) {
          FileInfo &duplicate = files[i];

          // Сохраняем размер дубликата перед удалением
          off_t fileSize = fs::file_size(duplicate.path);
          bytesSaved += fileSize;

          try {
            // Удаляем дубликат
            fs::remove(duplicate.path);

            // Создаём жёсткую ссылку
            fs::create_hard_link(original.path, duplicate.path);

            cout << "  Ссылка создана: " << duplicate.path << endl;
            totalDuplicates++;
          } catch (const exception &e) {
            cerr << "  Ошибка при создании ссылки: " << e.what() << endl;
          }
        }
      }
    }

    cout << "\n"
         << totalDuplicates << " дубликатов заменено на жёсткие ссылки."
         << endl;
  }

  // Вывод статистики
  void printStatistics() {
    cout << "\n=== СТАТИСТИКА ===" << endl;
    cout << "Всего файлов обработано: " << filesProcessed << endl;
    cout << "Уникальных хэшей найдено: " << hashMap.size() << endl;

    int totalDuplicates = 0;
    int groupsWithDuplicates = 0;

    for (const auto &entry : hashMap) {
      if (entry.second.size() > 1) {
        groupsWithDuplicates++;
        totalDuplicates += entry.second.size() - 1;
      }
    }

    cout << "Групп с дубликатами: " << groupsWithDuplicates << endl;
    cout << "Дубликатов найдено: " << totalDuplicates << endl;
    cout << "Экономия памяти: " << bytesSaved << " байт ("
         << (bytesSaved / 1024.0) << " КБ, " << (bytesSaved / 1024.0 / 1024.0)
         << " МБ)" << endl;
  }
};

int main(int argc, char *argv[]) {
  if (argc != 2) {
    cerr << "Использование: " << argv[0] << " <путь_к_каталогу>" << endl;
    cerr << "Пример: " << argv[0] << " /home/user/documents" << endl;
    return 1;
  }

  fs::path targetDir = argv[1];

  if (!fs::exists(targetDir) || !fs::is_directory(targetDir)) {
    cerr << "Ошибка: путь не существует или не является каталогом" << endl;
    return 1;
  }

  cout << "Сканирование каталога: " << targetDir << endl;

  HardLinksManager manager;
  manager.scanDirectory(targetDir);
  manager.createHardLinks();
  manager.printStatistics();

  return 0;
}