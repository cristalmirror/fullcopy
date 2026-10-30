#include "Copier.hpp"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cwchar>

namespace fullcopy {

    namespace {
        bool isDirectory(const std::wstring& path) {
            const DWORD attributes = GetFileAttributesW(path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES
                && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        }

        class FindHandle {
        public:
            explicit FindHandle(HANDLE handle) : handle_(handle) {}

            ~FindHandle() {
            if (handle_ != INVALID_HANDLE_VALUE) {
                FindClose(handle_);
            }
        }

        FindHandle(const FindHandle&) = delete;
        FindHandle& operator=(const FindHandle&) = delete;

        private:
            HANDLE handle_;
        };
        
        // Joins two path parts with a single backslash.
        std::wstring joinPath(const std::wstring& base, const std::wstring& part) {
            if (part.empty()) {
                return base;
            }
            if (!base.empty() && base.back() == L'\\') {
                return base + part;
            }
            return base + L'\\' + part;
        }

        bool equalsIgnoreCase(const std::wstring& first, const std::wstring& second) {
            return CompareStringOrdinal(first.c_str(),
                                        static_cast<int>(first.size()),
                                        second.c_str(),
                                        static_cast<int>(second.size()),
                                        TRUE) == CSTR_EQUAL;
        }

        // Names that Windows reserves for devices, with or without extension.
        bool isReservedName(const std::wstring& name) {
            static const wchar_t* const RESERVED[] = {
                L"CON", L"PRN", L"AUX", L"NUL",
                L"COM1", L"COM2", L"COM3", L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",
                L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9"
            };

            const std::wstring baseName = name.substr(0, name.find(L'.'));
            for (const wchar_t* reserved : RESERVED) {
                if (equalsIgnoreCase(baseName, reserved)) {
                    return true;
                }
            }
            return false;
        }

    } // namespace

    Copier::Copier()
        : sizeApproved_(true),
        tempSize_(0) {
    }

    void Copier::setDirectories(const std::wstring& sourceRoot, const std::wstring& destinationRoot) {
        sourceRoot_ = normalizePath(sourceRoot);
        destinationRoot_ = normalizePath(destinationRoot);
        entries_.clear();
    }

    bool Copier::load() {
        entries_.clear();

        const DWORD attributes = GetFileAttributesW(getFullSourcePath(L"").c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            return false;
        }

        const int result = SHCreateDirectoryExW(nullptr, destinationRoot_.c_str(), nullptr);
        if (result != ERROR_SUCCESS && result != ERROR_ALREADY_EXISTS && result != ERROR_FILE_EXISTS) {
            return false;
        }

        if (!isDirectory(destinationRoot_)) {
            return false;
        }

        scanDirectory(L"");
        return true;
    }

    bool Copier::check(std::size_t index) {
        const CopyEntry& entry = entries_[index];

        tempSize_ = getFullDestinationPath(index).size();

        if (entry.skipped) {
            sizeApproved_ = true;
            return sizeApproved_;
        }

        const std::size_t limit = entry.isDirectory ? MAX_DIRECTORY_PATH_LENGTH : MAX_FILE_PATH_LENGTH;
        sizeApproved_ = tempSize_ <= limit && getDestinationName(index).size() <= MAX_NAME_LENGTH;
        return sizeApproved_;
    }

    bool Copier::copy(std::size_t index) {
        const CopyEntry& entry = entries_[index];
        if (entry.skipped) {
            return true;
        }

        const std::wstring destination = getFullDestinationPath(index);

        if (entry.isDirectory) {
            if (CreateDirectoryW(destination.c_str(), nullptr)) {
                return true;
            }
            const DWORD error = GetLastError();
            return error == ERROR_ALREADY_EXISTS && isDirectory(destination);
        }

        return CopyFileW(getFullSourcePath(entry.sourcePath).c_str(), destination.c_str(), TRUE) != FALSE;
    }

    RenameResult Copier::rename(std::size_t index, const std::wstring& newName) {
        if (newName.empty()) {
            return RenameResult::Empty;
        }
        if (!isValidName(newName)) {
            return RenameResult::InvalidName;
        }
        if (newName.size() > getMaxNameLength(index)) {
            return RenameResult::TooLong;
        }

        CopyEntry& entry = entries_[index];
        const std::wstring oldPath = entry.destinationPath;
        const std::size_t separator = oldPath.find_last_of(L'\\');
        const std::wstring parent = separator == std::wstring::npos ? L"" : oldPath.substr(0, separator + 1);
        const std::wstring newPath = parent + newName;

        // Two entries can not end up with the same destination.
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (i != index && !entries_[i].skipped && equalsIgnoreCase(entries_[i].destinationPath, newPath)) {
                return RenameResult::AlreadyExists;
            }
        }

        const std::wstring fullPath = joinPath(destinationRoot_, newPath);
        const DWORD attributes = GetFileAttributesW(fullPath.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            return RenameResult::AlreadyExists;
        }

        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
            return RenameResult::DestinationUnavailable;
        }

        entry.destinationPath = newPath;

        // Everything inside a renamed directory goes under the new name.
        // Entries are stored in pre-order, so the content always comes after the directory.
        if (entry.isDirectory) {
            const std::wstring oldPrefix = oldPath + L'\\';
            for (std::size_t i = index + 1; i < entries_.size(); ++i) {
                std::wstring& path = entries_[i].destinationPath;
                if (startsWith(path, oldPrefix)) {
                    path = newPath + L'\\' + path.substr(oldPrefix.size());
                }
            }
        }

        return RenameResult::Ok;
    }

    void Copier::skip(std::size_t index) {
        CopyEntry& entry = entries_[index];
        entry.skipped = true;

        if (entry.isDirectory) {
            const std::wstring prefix = entry.sourcePath + L'\\';
            for (std::size_t i = index + 1; i < entries_.size(); ++i) {
                if (startsWith(entries_[i].sourcePath, prefix)) {
                    entries_[i].skipped = true;
                }
            }
        }
    }

    std::size_t Copier::getEntryCount() const {
        return entries_.size();
    }

    const CopyEntry& Copier::getEntry(std::size_t index) const {
        return entries_[index];
    }

    std::wstring Copier::getDestinationName(std::size_t index) const {
        const std::wstring& path = entries_[index].destinationPath;
        const std::size_t separator = path.find_last_of(L'\\');
        return separator == std::wstring::npos ? path : path.substr(separator + 1);
    }

    std::wstring Copier::getFullDestinationPath(std::size_t index) const {
        return joinPath(destinationRoot_, entries_[index].destinationPath);
    }

    std::size_t Copier::getMaxNameLength(std::size_t index) const {
        const std::size_t fullLength = getFullDestinationPath(index).size();
        const std::size_t nameLength = getDestinationName(index).size();
        const std::size_t limit = entries_[index].isDirectory ? MAX_DIRECTORY_PATH_LENGTH : MAX_FILE_PATH_LENGTH;
        const std::size_t usedLength = fullLength - nameLength;

        if (usedLength >= limit) {
            return 0;
        }
        return std::min(limit - usedLength, MAX_NAME_LENGTH);
    }

    const std::wstring& Copier::getSourceRoot() const {
        return sourceRoot_;
    }

    const std::wstring& Copier::getDestinationRoot() const {
        return destinationRoot_;
    }

    bool Copier::isSizeApproved() const {
        return sizeApproved_;
    }

    std::size_t Copier::getTempSize() const {
        return tempSize_;
    }

    void Copier::scanDirectory(const std::wstring& relativePath) {
        const std::wstring pattern = joinPath(getFullSourcePath(relativePath), L"*");

        WIN32_FIND_DATAW data;
        HANDLE find = FindFirstFileExW( pattern.c_str(), 
                                        FindExInfoBasic, 
                                        &data,
                                        FindExSearchNameMatch, 
                                        nullptr, 
                                        FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE) {
            return;
        }

        do {
            const std::wstring name = data.cFileName;
            if (name == L"." || name == L"..") {
                continue;
            }

            const std::wstring childPath = relativePath.empty() ? name : relativePath + L'\\' + name;
            const bool isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

            CopyEntry entry;
            entry.sourcePath = childPath;
            entry.destinationPath = childPath;
            entry.isDirectory = isDirectory;
            entry.skipped = false;
            entries_.push_back(entry);

            // Links and junctions are created as empty directories to avoid infinite loops.
            if (isDirectory && (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                scanDirectory(childPath);
            }
        } while (FindNextFileW(find, &data));

        FindClose(find);
    }

    /*
     *The source is read with the "\\?\" prefix, so long names can be read even
     *though they can not be written to the destination.\
     */
    std::wstring Copier::getFullSourcePath(const std::wstring& relativePath) const {
    std::wstring root;
        if (startsWith(sourceRoot_, L"\\\\?\\")) {
            root = sourceRoot_;
        } else if (startsWith(sourceRoot_, L"\\\\")) {
            root = L"\\\\?\\UNC\\" + sourceRoot_.substr(2);
        } else {
            root = L"\\\\?\\" + sourceRoot_;
        }
        return joinPath(root, relativePath);
    }

    // Returns the absolute path without the trailing backslash (except for drive roots like "C:\").
    std::wstring Copier::normalizePath(const std::wstring& path) {
        std::wstring result = path;

        const DWORD length = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
        if (length > 0) {
            std::wstring buffer(length, L'\0');
            const DWORD written = GetFullPathNameW(path.c_str(), length, &buffer[0], nullptr);
            if (written > 0 && written < length) {
                buffer.resize(written);
                result = buffer;
            }
        }

        std::replace(result.begin(), result.end(), L'/', L'\\');
    
        
        while (result.size() > 1 && result.back() == L'\\' && result[result.size() - 2] != L':') {
            result.pop_back();
        }
        return result;
    }

    bool Copier::isValidName(const std::wstring& name) {
        if (name == L"." || name == L"..") {
            return false;
        }

        for (const wchar_t character : name) {
            if (character < 32 || std::wcschr(L"\\/:*?\"<>|", character) != nullptr) {
                return false;
            }
        }

        const wchar_t last = name.back();
        if (last == L' ' || last == L'.') {
            return false;
        }

        return !isReservedName(name);
    }

    bool Copier::startsWith(const std::wstring& text, const std::wstring& prefix) {
        return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
    }

} // namespace fullcopy
