#include "Copier.hpp"
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using fullcopy::Copier;
using fullcopy::DirectoryResult;
using fullcopy::RenameResult;

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

struct Fixture {
    fs::path root;
    fs::path source;
    fs::path destination;

    Fixture() {
        wchar_t temporary[MAX_PATH + 1] = {};
        const DWORD length = GetTempPathW(MAX_PATH + 1, temporary);
        require(length > 0 && length <= MAX_PATH, "GetTempPathW failed");
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            root = fs::path(temporary) / (L"fullcopy-test-" + std::to_wstring(GetCurrentProcessId())
                   + L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt));
            if (fs::create_directory(root)) {
                source = root / L"source";
                destination = root / L"destination";
                fs::create_directory(source);
                return;
            }
        }
        throw std::runtime_error("Could not create isolated fixture");
    }

    ~Fixture() {
        std::error_code error;
        fs::remove_all(root, error);
    }
};

void write(const fs::path &path, const std::string &content) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    require(static_cast<bool>(stream), "Fixture write failed");
}

std::string read(const fs::path &path) {
    std::ifstream stream(path, std::ios::binary);
    require(static_cast<bool>(stream), "Copied file missing");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::size_t find(const Copier &copier, const std::wstring &relative) {
    for (std::size_t i = 0; i < copier.getEntryCount(); ++i) {
        if (copier.getEntry(i).sourcePath == relative) return i;
    }
    throw std::runtime_error("Expected entry missing");
}

std::wstring upper(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towupper(character));
    });
    return text;
}

std::wstring forwardSlashes(std::wstring text) {
    std::replace(text.begin(), text.end(), L'\\', L'/');
    return text;
}

void copyAll(Copier &copier) {
    for (std::size_t i = 0; i < copier.getEntryCount(); ++i) {
        require(copier.check(i), "Unexpected path rejection");
        require(copier.copy(i), "Copy failed");
    }
}

void copyAndRelease() {
    Fixture f;
    fs::create_directories(f.source / L"nested" / L"empty");
    std::string payload;
    for (unsigned i = 0; i < 65536; ++i) payload += static_cast<char>(i % 256);
    const fs::path name = L"\u00e1rbol-\u6587.bin";
    write(f.source / L"nested" / name, payload);
    write(f.source / L"zero", "");
    Copier copier;
    copier.setDirectories(f.source.wstring(), f.destination.wstring());
    require(copier.load(), "Load failed");
    require(copier.getEntryCount() == 4, "Unexpected scan count");
    copyAll(copier);
    require(read(f.destination / L"nested" / name) == payload, "Binary content changed");
    require(read(f.destination / L"zero").empty(), "Empty file changed");
    require(fs::is_directory(f.destination / L"nested" / L"empty"), "Empty directory missing");
    // Keep Copier alive: deletion must not depend on its destructor or process exit.
    require(fs::remove_all(f.destination) == 5, "Destination could not be deleted");
    require(fs::remove_all(f.source) == 5, "Source search handles still open");
}

void renameAndSkip() {
    Fixture f;
    fs::create_directory(f.source / L"original");
    write(f.source / L"original" / L"child", "child");
    write(f.source / L"other", "other");
    Copier copier;
    copier.setDirectories(f.source.wstring(), f.destination.wstring());
    require(copier.load(), "Load failed");
    const auto directory = find(copier, L"original");
    const auto child = find(copier, L"original\\child");
    require(copier.rename(directory, L"OTHER") == RenameResult::AlreadyExists, "Case collision accepted");
    require(copier.rename(directory, L"renamed") == RenameResult::Ok, "Rename failed");
    require(copier.getEntry(child).destinationPath == L"renamed\\child", "Child path not renamed");
    for (const auto *name : {L"CON", L"nul.txt", L"bad/name", L"tail.", L"tail ", L".."}) {
        require(copier.rename(directory, name) == RenameResult::InvalidName, "Invalid name accepted");
    }
    require(copier.rename(directory, L"") == RenameResult::Empty, "Empty name accepted");
    copier.skip(directory);
    require(copier.getEntry(child).skipped, "Child not skipped");
    copyAll(copier);
    require(!fs::exists(f.destination / L"renamed"), "Skipped directory copied");
    require(read(f.destination / L"other") == "other", "Sibling lost");
    // A fresh scan must discard the previous skip/rename state.
    // Start from an empty destination: existing files are never overwritten.
    fs::remove_all(f.destination);
    require(copier.load(), "Reload failed");
    copyAll(copier);
    require(read(f.destination / L"original" / L"child") == "child", "Reload retained stale state");
}

void boundaries() {
    Fixture f;
    write(f.source / L"file", "data");
    fs::create_directory(f.source / L"folder");
    Copier copier;
    copier.setDirectories(f.source.wstring(), f.destination.wstring());
    require(copier.load(), "Load failed");
    for (const auto *relative : {L"file", L"folder"}) {
        const auto index = find(copier, relative);
        const auto maximum = copier.getMaxNameLength(index);
        require(maximum > 0, "Fixture leaves no name space");
        require(copier.rename(index, std::wstring(maximum + 1, L'x')) == RenameResult::TooLong,
                "Overlong name accepted");
        require(copier.rename(index, std::wstring(maximum, L'x')) == RenameResult::Ok,
                "Boundary name rejected");
        require(copier.check(index), "Boundary path rejected");
        require(copier.copy(index), "Boundary copy failed");
    }
}

void failuresAndEmptySource() {
    Fixture f;
    Copier copier;
    copier.setDirectories((f.root / L"missing").wstring(), f.destination.wstring());
    require(!copier.load(), "Missing source accepted");
    write(f.root / L"file", "not a directory");
    copier.setDirectories(f.source.wstring(), (f.root / L"file" / L"child").wstring());
    require(!copier.load(), "Destination under file accepted");
    copier.setDirectories(f.source.wstring(), f.destination.wstring());
    require(copier.load(), "Empty source rejected");
    require(copier.getEntryCount() == 0, "Empty source contains entries");
    require(fs::is_directory(f.destination), "Empty destination not created");
    write(f.source / L"vanishing", "data");
    require(copier.load(), "Reload failed");
    fs::remove(f.source / L"vanishing");
    require(!copier.copy(find(copier, L"vanishing")), "Missing file copy reported success");
}

void directoryChecks() {
    Fixture f;
    write(f.root / L"file", "not a directory");
    Copier copier;
    const auto check = [&copier](const std::wstring &source, const std::wstring &destination) {
        copier.setDirectories(source, destination);
        return copier.checkDirectories();
    };
    const std::wstring source = f.source.wstring();
    const std::wstring destination = f.destination.wstring();
    require(check(L"", destination) == DirectoryResult::Empty, "Empty source accepted");
    require(check(source, L"") == DirectoryResult::Empty, "Empty destination accepted");
    require(check((f.root / L"missing").wstring(), destination) == DirectoryResult::SourceNotFound,
            "Missing source accepted");
    require(check((f.root / L"file").wstring(), destination) == DirectoryResult::SourceNotFound,
            "File accepted as source");
    require(check(source, source) == DirectoryResult::SameDirectory, "Same directory accepted");
    require(check(source, upper(source) + L"\\") == DirectoryResult::SameDirectory,
            "Same directory with other case and a trailing backslash accepted");
    require(check(forwardSlashes(source), source) == DirectoryResult::SameDirectory,
            "Same directory with forward slashes accepted");
    require(check(source, (f.source / L"inner").wstring()) == DirectoryResult::DestinationInsideSource,
            "Destination inside source accepted");
    require(check(source, (f.source / L"a" / L"b").wstring()) == DirectoryResult::DestinationInsideSource,
            "Destination deep inside source accepted");
    // "source2" starts like "source" but is not inside it.
    require(check(source, (f.root / L"source2").wstring()) == DirectoryResult::Ok,
            "Sibling with the same prefix rejected");
    require(check(source + L"\\", destination) == DirectoryResult::Ok, "Valid directories rejected");
    require(!fs::exists(f.destination) && !fs::exists(f.source / L"inner") && !fs::exists(f.root / L"source2"),
            "Checking the directories created one");
}

int main() {
    unsigned failures = 0;
    const struct { const char *name; void (*run)(); } tests[] = {
        {"binary/Unicode copy and folder release", copyAndRelease},
        {"rename, validation, skip and reload", renameAndSkip},
        {"file and directory path boundaries", boundaries},
        {"failures and empty source", failuresAndEmptySource},
        {"directory checks before a copy", directoryChecks}
    };
    for (const auto &test : tests) {
        try {
            test.run();
            std::cout << "PASS: " << test.name << '\n';
        } catch (const std::exception &error) {
            ++failures;
            std::cerr << "FAIL: " << test.name << ": " << error.what() << '\n';
        }
    }
    return failures == 0 ? 0 : 1;
}
