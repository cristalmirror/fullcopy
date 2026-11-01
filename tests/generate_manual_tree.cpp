// Command line tool that creates a source tree to test FullCopy by hand (see docs/testing.md).
//
// By default it creates 10 directories with 1000 files each. Some directories and files, chosen
// at random, get a long name of 245 to 255 characters. With it the destination path goes over
// the Windows limits (259 characters for a file path, 247 for a directory path), so FullCopy
// shows the Name too long window. A single name can not be longer than 255 characters, so it is
// the full path that goes over 260 characters. Every file contains a short message of 100
// characters at most.
//
// On Windows the tree is written with the "\\?\" prefix, so its paths can be longer than 260
// characters. The tool also builds on Linux, which is useful to check it without Windows.
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

const std::size_t MIN_LONG_NAME = 245;
const std::size_t MAX_LONG_NAME = 255; // Longest name Windows allows.
const std::size_t MAX_MESSAGE = 100;
const std::size_t MAX_DIRECTORIES = 1000;
const std::size_t MAX_FILES = 10000;
const char LETTERS[] = "abcdefghijklmnopqrstuvwxyz0123456789";

// ---------------------------------------------------------------------------------------------
// File system: the only part that depends on the operating system.
// ---------------------------------------------------------------------------------------------

#ifdef _WIN32

const char SEPARATOR = '\\';
const char *const DELETE_NOTE = "To delete the tree (Explorer may fail with paths longer than 260 characters):";

// Converts UTF-8 text to UTF-16 for the Windows API.
std::wstring widen(const std::string &text) {
    if (text.empty()) {
        return std::wstring();
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &result[0], size);
    return result;
}

// Converts UTF-16 text from the Windows API to UTF-8.
std::string narrow(const std::wstring &text) {
    if (text.empty()) {
        return std::string();
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &result[0], size, nullptr, nullptr);
    return result;
}

// Adds the "\\?\" prefix, so Windows accepts paths longer than 260 characters.
// Network paths like \\server\share use \\?\UNC instead.
std::string longPath(const std::string &path) {
    if (path.rfind("\\\\?\\", 0) == 0) {
        return path;
    }
    if (path.rfind("\\\\", 0) == 0) {
        return "\\\\?\\UNC\\" + path.substr(2);
    }
    return "\\\\?\\" + path;
}

void createDirectory(const std::string &path) {
    if (!CreateDirectoryW(widen(path).c_str(), nullptr)) {
        const DWORD error = GetLastError();
        throw std::runtime_error("Could not create the directory " + path + " (error " + std::to_string(error) + ")");
    }
}

// Creates a new file with the content. It fails if the file already exists.
void writeNewFile(const std::string &path, const std::string &content) {
    const HANDLE file = CreateFileW(widen(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        throw std::runtime_error("Could not create the file " + path + " (error " + std::to_string(error) + ")");
    }

    DWORD written = 0;
    const BOOL ok = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
    const DWORD error = GetLastError();
    CloseHandle(file);
    if (!ok || written != content.size()) {
        throw std::runtime_error("Could not write the file " + path + " (error " + std::to_string(error) + ")");
    }
}

std::string deleteCommand(const std::string &root) {
    return "cmd /c rd /s /q \"" + longPath(root) + "\"";
}

#else

const char SEPARATOR = '/';
const char *const DELETE_NOTE = "To delete the tree:";

// Linux has no 260 character limit, so paths need no prefix.
std::string longPath(const std::string &path) {
    return path;
}

void createDirectory(const std::string &path) {
    if (mkdir(path.c_str(), 0755) != 0) {
        const int error = errno;
        throw std::runtime_error("Could not create the directory " + path + ": " + std::strerror(error));
    }
}

// Creates a new file with the content. It fails if the file already exists.
void writeNewFile(const std::string &path, const std::string &content) {
    const int file = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (file < 0) {
        const int error = errno;
        throw std::runtime_error("Could not create the file " + path + ": " + std::strerror(error));
    }

    const ssize_t written = write(file, content.data(), content.size());
    const int error = errno;
    close(file);
    if (written < 0 || static_cast<std::size_t>(written) != content.size()) {
        throw std::runtime_error("Could not write the file " + path + ": " + std::strerror(error));
    }
}

std::string deleteCommand(const std::string &root) {
    return "rm -rf '" + root + "'";
}

#endif

// ---------------------------------------------------------------------------------------------
// Command line.
// ---------------------------------------------------------------------------------------------

// Options given on the command line.
struct Options {
    std::string path;
    std::size_t directories = 10;
    std::size_t filesPerDirectory = 1000;
    double longDirectoryPercent = 20.0;
    double longFilePercent = 0.2;
    bool hasSeed = false;
    std::uint32_t seed = 0;
    bool help = false;
};

// Thrown for a wrong command line, so the usage is shown with the error.
class UsageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void printUsage(std::ostream &out) {
    out << "Usage: generate_manual_tree <path> [options]\n"
           "\n"
           "Creates a directory tree to test FullCopy by hand. <path> must not exist yet.\n"
           "Some directories and files get a name of 245 to 255 characters, so their destination\n"
           "paths go over the Windows limits and FullCopy shows the Name too long window.\n"
           "\n"
           "Options:\n"
           "  --directories N        Number of directories, 1 to 1000 (default 10).\n"
           "  --files N              Files in each directory, 1 to 10000 (default 1000).\n"
           "  --long-directories P   Chance, in percent, that a directory gets a long name (default 20).\n"
           "  --long-files P         Chance, in percent, that a file gets a long name (default 0.2).\n"
           "  --seed N               Seed of the random generator. The same seed creates the same tree.\n"
           "  --help                 Shows this help.\n"
           "\n"
           "When a chance is above 0, at least one directory and one file in a directory with a\n"
           "normal name get a long name.\n";
}

// Reads a whole number from minimum to maximum. Signs, spaces and other text are rejected.
unsigned long long parseNumber(const std::string &option, const std::string &text,
                               unsigned long long minimum, unsigned long long maximum) {
    const std::string error = option + " needs a number from " + std::to_string(minimum) + " to "
                            + std::to_string(maximum);
    if (text.empty() || !std::isdigit(static_cast<unsigned char>(text[0]))) {
        throw UsageError(error);
    }

    std::size_t used = 0;
    unsigned long long value = 0;
    try {
        value = std::stoull(text, &used);
    } catch (const std::exception &) {
        throw UsageError(error);
    }
    if (used != text.size() || value < minimum || value > maximum) {
        throw UsageError(error);
    }
    return value;
}

// Reads a percentage from 0 to 100.
double parsePercent(const std::string &option, const std::string &text) {
    const std::string error = option + " needs a percentage from 0 to 100";
    if (text.empty() || !(std::isdigit(static_cast<unsigned char>(text[0])) || text[0] == '.')) {
        throw UsageError(error);
    }

    std::size_t used = 0;
    double value = 0.0;
    try {
        value = std::stod(text, &used);
    } catch (const std::exception &) {
        throw UsageError(error);
    }
    if (used != text.size() || !(value >= 0.0 && value <= 100.0)) {
        throw UsageError(error);
    }
    return value;
}

Options parseOptions(const std::vector<std::string> &arguments) {
    Options options;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const std::string &argument = arguments[i];
        if (argument == "--help" || argument == "-h") {
            options.help = true;
            return options;
        }

        if (argument.rfind("--", 0) != 0) {
            if (!options.path.empty()) {
                throw UsageError("Give only one path");
            }
            options.path = argument;
            continue;
        }

        if (i + 1 >= arguments.size()) {
            throw UsageError(argument + " needs a value");
        }
        const std::string &value = arguments[++i];
        if (argument == "--directories") {
            options.directories = static_cast<std::size_t>(parseNumber(argument, value, 1, MAX_DIRECTORIES));
        } else if (argument == "--files") {
            options.filesPerDirectory = static_cast<std::size_t>(parseNumber(argument, value, 1, MAX_FILES));
        } else if (argument == "--long-directories") {
            options.longDirectoryPercent = parsePercent(argument, value);
        } else if (argument == "--long-files") {
            options.longFilePercent = parsePercent(argument, value);
        } else if (argument == "--seed") {
            options.seed = static_cast<std::uint32_t>(parseNumber(argument, value, 0, std::numeric_limits<std::uint32_t>::max()));
            options.hasSeed = true;
        } else {
            throw UsageError("Unknown option " + argument);
        }
    }

    if (options.path.empty()) {
        throw UsageError("Give the path of the directory to create");
    }
    return options;
}

// ---------------------------------------------------------------------------------------------
// Tree.
// ---------------------------------------------------------------------------------------------

// Random numbers. The same seed always gives the same tree.
class Random {
public:
    explicit Random(std::uint32_t seed) : engine_(seed) {}

    // Returns a number from 0 to count - 1.
    std::size_t below(std::size_t count) {
        return std::uniform_int_distribution<std::size_t>(0, count - 1)(engine_);
    }

    // Returns a number from first to last, both included.
    std::size_t between(std::size_t first, std::size_t last) {
        return std::uniform_int_distribution<std::size_t>(first, last)(engine_);
    }

    // Returns true with the given chance, in percent.
    bool chance(double percent) {
        return std::uniform_real_distribution<double>(0.0, 100.0)(engine_) < percent;
    }

private:
    std::mt19937 engine_;
};

// Which directories and files get a long name.
struct Plan {
    std::vector<bool> longDirectory;
    std::vector<bool> longFile; // Index: directory * filesPerDirectory + file.
};

Plan makePlan(const Options &options, Random &random) {
    Plan plan;

    // At least one long directory when the chance is above 0.
    plan.longDirectory.resize(options.directories);
    for (std::size_t d = 0; d < options.directories; ++d) {
        plan.longDirectory[d] = random.chance(options.longDirectoryPercent);
    }
    const bool anyLongDirectory = std::find(plan.longDirectory.begin(), plan.longDirectory.end(), true)
                                != plan.longDirectory.end();
    if (options.longDirectoryPercent > 0.0 && !anyLongDirectory) {
        plan.longDirectory[random.below(options.directories)] = true;
    }

    // At least one long file in a directory with a normal name, so the Name too long window
    // appears for a file even when the long directories are skipped.
    plan.longFile.resize(options.directories * options.filesPerDirectory);
    std::vector<std::size_t> normalDirectories;
    std::size_t longFilesInNormalDirectories = 0;
    for (std::size_t d = 0; d < options.directories; ++d) {
        if (!plan.longDirectory[d]) {
            normalDirectories.push_back(d);
        }
        for (std::size_t f = 0; f < options.filesPerDirectory; ++f) {
            const bool isLong = random.chance(options.longFilePercent);
            plan.longFile[d * options.filesPerDirectory + f] = isLong;
            if (isLong && !plan.longDirectory[d]) {
                ++longFilesInNormalDirectories;
            }
        }
    }
    if (options.longFilePercent > 0.0 && longFilesInNormalDirectories == 0) {
        const std::size_t d = normalDirectories.empty() ? random.below(options.directories)
                                                        : normalDirectories[random.below(normalDirectories.size())];
        plan.longFile[d * options.filesPerDirectory + random.below(options.filesPerDirectory)] = true;
    }
    return plan;
}

// Writes the number with zeros in front, so the names sort in order: 7 of 1000 gives "0007".
std::string padNumber(std::size_t number, std::size_t last) {
    const std::string text = std::to_string(number);
    const std::size_t width = std::to_string(last).size();
    return std::string(width - text.size(), '0') + text;
}

// Returns a long name that starts with the normal name, so it stays unique and easy to find.
std::string longName(Random &random, const std::string &baseName, const std::string &extension) {
    const std::size_t length = random.between(MIN_LONG_NAME, MAX_LONG_NAME);
    std::string name = baseName + '-';
    while (name.size() + extension.size() < length) {
        name += LETTERS[random.below(sizeof(LETTERS) - 1)];
    }
    return name + extension;
}

// Shortens a long name for the summary.
std::string shortName(const std::string &name) {
    if (name.size() <= 40) {
        return name;
    }
    return name.substr(0, 30) + "... (" + std::to_string(name.size()) + " characters)";
}

// Counts and lines shown in the summary.
struct Result {
    std::size_t longDirectories = 0;
    std::size_t longFiles = 0;
    std::size_t expectedWindows = 0;
    std::vector<std::string> lines;
};

Result createTree(const Options &options, const std::string &root, const Plan &plan, Random &random) {
    Result result;
    const std::string base = longPath(root);

    for (std::size_t d = 0; d < options.directories; ++d) {
        std::cerr << "\rCreating directory " << (d + 1) << " of " << options.directories << "..." << std::flush;

        std::string directoryName = "dir-" + padNumber(d + 1, options.directories);
        if (plan.longDirectory[d]) {
            directoryName = longName(random, directoryName, "");
            ++result.longDirectories;
            ++result.expectedWindows;
        }
        const std::string directoryPath = base + SEPARATOR + directoryName;
        createDirectory(directoryPath);

        std::size_t longFilesHere = 0;
        for (std::size_t f = 0; f < options.filesPerDirectory; ++f) {
            std::string fileName = "file-" + padNumber(f + 1, options.filesPerDirectory);
            std::string message = "FullCopy manual test. Directory " + std::to_string(d + 1) + " of "
                                + std::to_string(options.directories) + ", file " + std::to_string(f + 1)
                                + " of " + std::to_string(options.filesPerDirectory) + ".";
            if (plan.longFile[d * options.filesPerDirectory + f]) {
                fileName = longName(random, fileName, ".txt");
                message += " Long name.";
                ++longFilesHere;
            } else {
                fileName += ".txt";
            }
            if (message.size() > MAX_MESSAGE) {
                throw std::logic_error("The message is longer than " + std::to_string(MAX_MESSAGE) + " characters: " + message);
            }
            writeNewFile(directoryPath + SEPARATOR + fileName, message);
        }

        // A long file inside a long directory only asks if the directory is renamed.
        result.longFiles += longFilesHere;
        if (!plan.longDirectory[d]) {
            result.expectedWindows += longFilesHere;
        }

        std::ostringstream line;
        line << "  " << std::left << std::setw(51) << shortName(directoryName) << ' '
             << options.filesPerDirectory << " files, " << longFilesHere << " with a long name";
        result.lines.push_back(line.str());
    }

    std::cerr << '\r' << std::string(40, ' ') << '\r' << std::flush;
    return result;
}

void printSummary(const Options &options, const std::string &root, const Result &result, double seconds) {
    std::cout << "Created " << root << " in " << std::fixed << std::setprecision(1) << seconds
              << " s (seed " << options.seed << ").\n"
              << "  Directories: " << options.directories << ", " << result.longDirectories << " with a long name.\n"
              << "  Files: " << options.directories * options.filesPerDirectory << ", " << result.longFiles
              << " with a long name.\n";
    for (const std::string &line : result.lines) {
        std::cout << line << '\n';
    }
    std::cout << "\n"
              << "FullCopy shows the Name too long window at least " << result.expectedWindows
              << " times: once for each long\n"
              << "directory and once for each long file in a directory with a normal name. Renaming a long\n"
              << "directory instead of skipping it also asks for the long files inside it.\n"
              << "\n"
              << "Run it again with --seed " << options.seed << " to create the same tree.\n"
              << DELETE_NOTE << "\n"
              << "  " << deleteCommand(root) << '\n';
}

int run(const std::vector<std::string> &arguments) {
    Options options;
    try {
        options = parseOptions(arguments);
    } catch (const UsageError &error) {
        std::cerr << "Error: " << error.what() << "\n\n";
        printUsage(std::cerr);
        return 2;
    }
    if (options.help) {
        printUsage(std::cout);
        return 0;
    }

    try {
        // The tree must be new, so nothing is overwritten or mixed with older files.
        fs::path rootPath = fs::absolute(fs::u8path(options.path)).lexically_normal();
        if (rootPath.filename().empty()) {
            rootPath = rootPath.parent_path();
        }
        const std::string root = rootPath.u8string();
        if (fs::exists(rootPath)) {
            throw std::runtime_error(root + " already exists. Choose a new path, or delete it with: " + deleteCommand(root));
        }

        // Without a seed, choose one and print it, so the same tree can be created again.
        if (!options.hasSeed) {
            options.seed = std::random_device()();
        }
        Random random(options.seed);
        const Plan plan = makePlan(options, random);

        const auto start = std::chrono::steady_clock::now();
        fs::create_directories(rootPath);
        const Result result = createTree(options, root, plan, random);
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;

        printSummary(options, root, result, elapsed.count());
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "\nError: " << error.what() << '\n';
        return 1;
    }
}

#ifdef _WIN32

// wmain receives the arguments in UTF-16, so paths with any character work.
int wmain(int argc, wchar_t *argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i) {
        arguments.push_back(narrow(argv[i]));
    }
    return run(arguments);
}

#else

int main(int argc, char *argv[]) {
    return run(std::vector<std::string>(argv + 1, argv + argc));
}

#endif
