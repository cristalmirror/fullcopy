#include "Copier.hpp"
#include "Common.hpp"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <iterator>
#include <utility>

namespace fullcopy {

    namespace {
        // Returns true if the path exists and is a directory.
        bool isDirectory(const std::wstring &path) {
            const DWORD attributes = GetFileAttributesW(path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES
                && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        }

        // Owns a directory handle while resolving aliases through the file system.
        class DirectoryHandle {
        public:
            explicit DirectoryHandle(HANDLE handle) : handle_(handle) {}
            ~DirectoryHandle() {
                if (handle_ != INVALID_HANDLE_VALUE) {
                    CloseHandle(handle_);
                }
            }
            DirectoryHandle(const DirectoryHandle&) = delete;
            DirectoryHandle &operator=(const DirectoryHandle&) = delete;
            HANDLE get() const { return handle_; }
        private:
            HANDLE handle_;
        };

        // Writes the final path of an open handle, with every junction and symbolic link resolved.
        // volumeFlag is VOLUME_NAME_DOS (C:\...) or VOLUME_NAME_GUID (\\?\Volume{...}\...).
        bool getFinalPath(HANDLE handle, DWORD volumeFlag, std::wstring &path) {
            const DWORD flags = FILE_NAME_NORMALIZED | volumeFlag;
            const DWORD required = GetFinalPathNameByHandleW(handle, nullptr, 0, flags);
            if (required == 0) {
                return false;
            }
            std::wstring buffer(static_cast<std::size_t>(required) + 1, L'\0');
            const DWORD written = GetFinalPathNameByHandleW(handle, buffer.data(),
                static_cast<DWORD>(buffer.size()), flags);
            if (written == 0 || written >= buffer.size()) {
                return false;
            }
            buffer.resize(written);
            path = buffer;
            return true;
        }

        // Resolve existing ancestors first, including junctions. Missing destination
        // components are appended only after resolving the nearest existing directory.
        bool resolveDirectoryPath(const std::wstring &path, bool allowMissing,
                                  std::wstring &resolved) {
            namespace fs = std::filesystem;
            fs::path current(path);
            std::vector<std::wstring> missingParts;
            for (;;) {
                DirectoryHandle handle(CreateFileW(current.c_str(), FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
                if (handle.get() != INVALID_HANDLE_VALUE) {
                    BY_HANDLE_FILE_INFORMATION info{};
                    if (!GetFileInformationByHandle(handle.get(), &info) ||
                        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                        return false;
                    }
                    std::wstring finalPath;
                    if (!getFinalPath(handle.get(), VOLUME_NAME_DOS, finalPath)) {
                        return false;
                    }
                    fs::path result(finalPath);
                    for (auto it = missingParts.rbegin(); it != missingParts.rend(); ++it) {
                        result /= *it;
                    }
                    resolved = result.wstring();
                    while (!resolved.empty() && resolved.back() == L'\\') {
                        resolved.pop_back();
                    }
                    return true;
                }
                const DWORD error = GetLastError();
                if (!allowMissing || (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)) {
                    return false;
                }
                const fs::path parent = current.parent_path();
                if (parent.empty() || parent == current || current.filename().empty()) {
                    return false;
                }
                missingParts.push_back(current.filename().wstring());
                current = parent;
            }
        }

        // Progress callback of CopyFileExW, called while a file is being copied. When the user
        // cancels, it stops the copy and Windows deletes the partial file.
        DWORD CALLBACK copyProgress(
            LARGE_INTEGER,
            LARGE_INTEGER,
            LARGE_INTEGER,
            LARGE_INTEGER,
            DWORD,
            DWORD,
            HANDLE,
            HANDLE,
            LPVOID context
        ) {
            const auto *cancellation = static_cast<const std::atomic_bool*>(context);
            if (cancellation != nullptr && cancellation->load()) {
                return PROGRESS_CANCEL;
            }
            return PROGRESS_CONTINUE;
        }

        // Closes a FindFirstFileExW search handle when it goes out of scope, even on an early
        // return or an exception (RAII).
        class FindHandle {
        public:
            // Takes ownership of the search handle.
            explicit FindHandle(HANDLE handle) : handle_(handle) {}

            // Closes the handle, if it is valid.
            ~FindHandle() {
            if (handle_ != INVALID_HANDLE_VALUE) {
                FindClose(handle_);
            }
        }

        FindHandle(const FindHandle&) = delete;
        FindHandle &operator=(const FindHandle&) = delete;

        private:
            HANDLE handle_;
        };
        
        // Returns true if Windows reserves the name for a device (CON, NUL, COM1, ...),
        // with or without an extension.
        bool isReservedName(const std::wstring &name) {
            static const wchar_t *const RESERVED[] = {
                L"CON", L"PRN", L"AUX", L"NUL",
                L"COM1", L"COM2", L"COM3", L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",
                L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9",
                L"COM\u00B9", L"COM\u00B2", L"COM\u00B3",
                L"LPT\u00B9", L"LPT\u00B2", L"LPT\u00B3"
            };

            // Only the part before the first dot matters: "nul.txt" is reserved too.
            const std::wstring baseName = name.substr(0, name.find(L'.'));
            for (const wchar_t *reserved : RESERVED) {
                if (equalsIgnoreCase(baseName, reserved)) {
                    return true;
                }
            }
            return false;
        }

        // Returns true if the text starts with the prefix, ignoring case.
        bool startsWithIgnoreCase(const std::wstring &text, const std::wstring &prefix) {
            return text.size() >= prefix.size() && equalsIgnoreCase(text.substr(0, prefix.size()), prefix);
        }

        // Returns true if the path is the directory itself or something inside it, ignoring case.
        bool isSameOrInside(const std::wstring &path, const std::wstring &directory) {
            return equalsIgnoreCase(path, directory) || startsWithIgnoreCase(path, directory + L'\\');
        }

        // Opens a file or directory following its junctions and symbolic links, and writes the
        // real path of the target with its volume GUID (\\?\Volume{...}\...) and without the
        // trailing backslash. Fails if the target is missing, can not be opened, or has no volume
        // GUID, like a network share.
        bool getRealPath(const std::wstring &path, std::wstring &realPath) {
            DirectoryHandle handle(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
            if (handle.get() == INVALID_HANDLE_VALUE || !getFinalPath(handle.get(), VOLUME_NAME_GUID, realPath)) {
                return false;
            }
            while (!realPath.empty() && realPath.back() == L'\\') {
                realPath.pop_back();
            }
            return true;
        }

        // Volume part of a real path written by getRealPath(): "\\?\Volume{...}".
        std::wstring getVolume(const std::wstring &realPath) {
            const std::size_t end = realPath.find(L'\\', 4);
            return end == std::wstring::npos ? realPath : realPath.substr(0, end);
        }

        // Returns true for the errors of OneDrive (and other cloud) files that can not be read
        // now: the provider is not running, there is no network, the request failed, ...
        bool isCloudError(DWORD error) {
            static const DWORD CLOUD_ERRORS[] = {
                ERROR_CLOUD_FILE_SYNC_ROOT_METADATA_CORRUPT, ERROR_CLOUD_FILE_PROVIDER_NOT_RUNNING,
                ERROR_CLOUD_FILE_METADATA_CORRUPT, ERROR_CLOUD_FILE_NOT_IN_SYNC,
                ERROR_CLOUD_FILE_VALIDATION_FAILED, ERROR_CLOUD_FILE_AUTHENTICATION_FAILED,
                ERROR_CLOUD_FILE_INSUFFICIENT_RESOURCES, ERROR_CLOUD_FILE_NETWORK_UNAVAILABLE,
                ERROR_CLOUD_FILE_UNSUCCESSFUL, ERROR_CLOUD_FILE_IN_USE,
                ERROR_CLOUD_FILE_REQUEST_ABORTED, ERROR_CLOUD_FILE_PROPERTY_CORRUPT,
                ERROR_CLOUD_FILE_ACCESS_DENIED, ERROR_CLOUD_FILE_REQUEST_CANCELED,
                ERROR_CLOUD_FILE_PROVIDER_TERMINATED, ERROR_CLOUD_FILE_REQUEST_TIMEOUT
            };
            return std::find(std::begin(CLOUD_ERRORS), std::end(CLOUD_ERRORS), error) != std::end(CLOUD_ERRORS);
        }

    } // namespace

    // Starts with no directories and no entries.
    Copier::Copier()
        : sizeApproved_(true),
        tempSize_(0) {
    }

    // Stores both directories as absolute paths and discards the entries of a previous scan.
    void Copier::setDirectories(const std::wstring &sourceRoot, const std::wstring &destinationRoot) {
        sourceRoot_ = normalizePath(sourceRoot);
        destinationRoot_ = normalizePath(destinationRoot);
        entries_.clear();
        inaccessible_.clear();
    }

    // Checks the directories before a copy. The checks run in order and the first
    // failure is returned.
    DirectoryResult Copier::checkDirectories() const {
        if (sourceRoot_.empty() || destinationRoot_.empty()) {
            return DirectoryResult::Empty;
        }

        std::wstring source;
        std::wstring destination;
        if (!resolveDirectoryPath(sourceRoot_, false, source)) {
            return DirectoryResult::SourceNotFound;
        }
        if (!resolveDirectoryPath(destinationRoot_, true, destination)) {
            return DirectoryResult::DestinationUnavailable;
        }
        if (equalsIgnoreCase(source, destination)) {
            return DirectoryResult::SameDirectory;
        }
        if (startsWithIgnoreCase(destination, source + L'\\')) {
            return DirectoryResult::DestinationInsideSource;
        }

        return DirectoryResult::Ok;
    }

    // Makes sure the destination exists (it is created if needed) and scans the source directory.
    bool Copier::load(std::atomic_bool *cancellation, const RecursionCallback &onRecursion) {
        entries_.clear();
        inaccessible_.clear();
        if (checkDirectories() != DirectoryResult::Ok) {
            return false;
        }

        // The source must be an existing directory.
        const DWORD attributes = GetFileAttributesW(getFullSourcePath(L"").c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            return false;
        }

        // Create the destination with all its missing parent directories.
        const int result = SHCreateDirectoryExW(nullptr, destinationRoot_.c_str(), nullptr);
        if (result != ERROR_SUCCESS && result != ERROR_ALREADY_EXISTS && result != ERROR_FILE_EXISTS) {
            return false;
        }

        // The destination may already exist as a file, but it must be a directory.
        if (!isDirectory(destinationRoot_)) {
            return false;
        }

        // Scan from the root. On failure, do not keep a partial list.
        if (!scanDirectory(L"", cancellation, onRecursion)) {
            entries_.clear();
            inaccessible_.clear();
            return false;
        }
        return true;
    }

    // Checks that the destination path of an entry fits the Windows limits. Skipped entries are
    // always approved. The path length is saved in tempSize_ so it can be shown to the user.
    bool Copier::check(std::size_t index) {
        const CopyEntry &entry = entries_[index];

        tempSize_ = getFullDestinationPath(index).size();

        if (entry.skipped) {
            sizeApproved_ = true;
            return sizeApproved_;
        }

        // Both the full path and the name alone must fit.
        const std::size_t limit = getPathLengthLimit(index);
        sizeApproved_ = tempSize_ <= limit && getDestinationName(index).size() <= MAX_NAME_LENGTH;
        return sizeApproved_;
    }

    // Copies one entry: creates the directory, or copies the file without overwriting an
    // existing one. Returns Copied for a skipped entry, because there is nothing to do.
    CopyResult Copier::copy(std::size_t index, std::atomic_bool *cancellation) {
        // Do not start a new entry after a cancellation.
        if (cancellation != nullptr && cancellation->load()) {
            return CopyResult::Failed;
        }

        const CopyEntry &entry = entries_.at(index);
        if (entry.skipped) {
            return CopyResult::Copied;
        }

        const std::wstring destination = getFullDestinationPath(index);

        if (entry.isDirectory) {
            // A directory that already exists is fine, but a file with the same name is not.
            if (CreateDirectoryW(destination.c_str(), nullptr)) {
                return CopyResult::Copied;
            }
            const DWORD error = GetLastError();
            return error == ERROR_ALREADY_EXISTS && isDirectory(destination) ? CopyResult::Copied
                                                                             : CopyResult::Failed;
        }

        // Read the source with the long path prefix. COPY_FILE_FAIL_IF_EXISTS never overwrites a file,
        // and copyProgress stops the copy if the user cancels.
        const std::wstring source = getFullSourcePath(entry.sourcePath);
        if (CopyFileExW(
            source.c_str(),
            destination.c_str(),
            copyProgress,
            cancellation,
            nullptr,
            COPY_FILE_FAIL_IF_EXISTS
        ) != FALSE) {
            return CopyResult::Copied;
        }

        // A OneDrive file that can not be downloaded now is reported apart from the other errors.
        return isCloudError(GetLastError()) ? CopyResult::Inaccessible : CopyResult::Failed;
    }

    // Gives a new name to the destination of an entry. It checks the name, its length and the
    // collisions first, and changes nothing if any check fails.
    RenameResult Copier::rename(std::size_t index, const std::wstring &newName) {
        // Check the name itself.
        if (newName.empty()) {
            return RenameResult::Empty;
        }
        if (!isValidName(newName)) {
            return RenameResult::InvalidName;
        }
        if (newName.size() > getMaxNameLength(index)) {
            return RenameResult::TooLong;
        }

        // Build the new relative path: same parent directory, new name.
        CopyEntry &entry = entries_[index];
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

        // The new name can not exist in the destination either.
        const std::wstring fullPath = joinPath(destinationRoot_, newPath);
        const DWORD attributes = GetFileAttributesW(fullPath.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            return RenameResult::AlreadyExists;
        }

        const DWORD error = GetLastError();
        // "Not found" is the expected answer. Any other error means the destination could not be checked.
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
            return RenameResult::DestinationUnavailable;
        }

        // All the checks passed: apply the new name.
        entry.destinationPath = newPath;

        // Everything inside a renamed directory goes under the new name.
        // Entries are stored in pre-order, so the content always comes after the directory.
        if (entry.isDirectory) {
            const std::wstring oldPrefix = oldPath + L'\\';
            for (std::size_t i = index + 1; i < entries_.size(); ++i) {
                std::wstring &path = entries_[i].destinationPath;
                if (startsWith(path, oldPrefix)) {
                    path = newPath + L'\\' + path.substr(oldPrefix.size());
                }
            }
        }

        return RenameResult::Ok;
    }

    // Marks an entry as not to be copied. For a directory, its whole content is skipped too.
    void Copier::skip(std::size_t index) {
        CopyEntry &entry = entries_[index];
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

    // Number of entries found by the last scan.
    std::size_t Copier::getEntryCount() const {
        return entries_.size();
    }

    // Returns one entry. The index must be valid: it is not checked.
    const CopyEntry &Copier::getEntry(std::size_t index) const {
        return entries_[index];
    }

    // Returns the last part of the destination path: the file or directory name.
    std::wstring Copier::getDestinationName(std::size_t index) const {
        const std::wstring &path = entries_[index].destinationPath;
        const std::size_t separator = path.find_last_of(L'\\');
        return separator == std::wstring::npos ? path : path.substr(separator + 1);
    }

    // Returns the absolute destination path of an entry.
    std::wstring Copier::getFullDestinationPath(std::size_t index) const {
        return joinPath(destinationRoot_, entries_[index].destinationPath);
    }

    // Longest full path allowed for the entry. A directory needs room for the names inside it.
    std::size_t Copier::getPathLengthLimit(std::size_t index) const {
        return entries_[index].isDirectory ? MAX_DIRECTORY_PATH_LENGTH : MAX_FILE_PATH_LENGTH;
    }

    // Longest name the entry can have so its full destination path still fits.
    std::size_t Copier::getMaxNameLength(std::size_t index) const {
        const std::size_t fullLength = getFullDestinationPath(index).size();
        const std::size_t nameLength = getDestinationName(index).size();
        const std::size_t limit = getPathLengthLimit(index);
        // Part of the path that the name does not control: the root and the parent directories.
        const std::size_t usedLength = fullLength - nameLength;

        // The parent directories already use all the room: no name fits.
        if (usedLength >= limit) {
            return 0;
        }
        // A single name can not be longer than MAX_NAME_LENGTH either.
        return std::min(limit - usedLength, MAX_NAME_LENGTH);
    }

    // Source directory, as an absolute path.
    const std::wstring &Copier::getSourceRoot() const {
        return sourceRoot_;
    }

    // Destination directory, as an absolute path.
    const std::wstring &Copier::getDestinationRoot() const {
        return destinationRoot_;
    }

    // Result of the last call to check().
    bool Copier::isSizeApproved() const {
        return sizeApproved_;
    }

    // Length of the full destination path measured by the last call to check().
    std::size_t Copier::getTempSize() const {
        return tempSize_;
    }

    // Relative source paths that the last scan could not read.
    const std::vector<std::wstring> &Copier::getInaccessiblePaths() const {
        return inaccessible_;
    }

    // Iterative depth-first scan: directories precede their contents, and only
    // one search handle is open at a time regardless of tree depth.
    // References (junctions and symbolic links) are followed only when they point to the
    // source volume and not to a directory that contains them.
    bool Copier::scanDirectory(const std::wstring &relativePath, std::atomic_bool *cancellation,
                               const RecursionCallback &onRecursion) {
        // A scanned directory and the one that contains it. realPath has every reference
        // resolved, so a reference that points back to a scanned directory can be found.
        struct Ancestor {
            std::wstring realPath;
            std::size_t parent;
        };
        struct Pending {
            std::wstring path;
            std::wstring realPath;
            std::size_t parent;  // Index in ancestors of the directory that contains it.
            bool isDirectory;
            bool descend;
            bool addEntry;
            bool isReference;    // Reached through a reference: a failed read is not fatal.
        };
        // A file or directory found in a directory, before deciding what to do with it.
        struct Found {
            std::wstring name;
            DWORD attributes;
            DWORD reparseTag;
        };
        const std::size_t NO_PARENT = static_cast<std::size_t>(-1);

        // Only references to the volume of the source are followed. A network source has no
        // volume GUID, so none of its references is followed.
        std::wstring rootRealPath;
        const std::wstring sourceVolume = getRealPath(getFullSourcePath(relativePath), rootRealPath)
                                          ? getVolume(rootRealPath) : std::wstring();

        std::vector<Ancestor> ancestors;
        std::vector<Pending> pending{{relativePath, rootRealPath, NO_PARENT, true, true, false, false}};
        const auto cancelled = [cancellation]() {
            return cancellation != nullptr && cancellation->load();
        };
        while (!pending.empty()) {
            if (cancelled()) {
                return false;
            }
            Pending item = std::move(pending.back());
            pending.pop_back();
            if (item.addEntry) {
                entries_.push_back({item.path, item.path, item.isDirectory, false});
            }
            if (!item.descend) {
                continue;
            }

            // A directory reached through a reference, or a OneDrive directory that can not be
            // read now, is not copied and the scan goes on. Its entry is the last one added.
            const auto skipUnreadable = [this, &item](DWORD error) {
                if (!item.addEntry || !(item.isReference || isCloudError(error))) {
                    return false;
                }
                entries_.pop_back();
                inaccessible_.push_back(item.path);
                return true;
            };

            std::vector<Found> found;
            {
                const std::wstring directory = getFullSourcePath(item.path);
                const std::wstring pattern = joinPath(directory, L"*");
                WIN32_FIND_DATAW data{};
                const HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic,
                    &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
                if (find == INVALID_HANDLE_VALUE) {
                    const DWORD error = GetLastError();
                    if (error == ERROR_FILE_NOT_FOUND && isDirectory(directory)) {
                        continue;
                    }
                    if (skipUnreadable(error)) {
                        continue;
                    }
                    return false;
                }
                const FindHandle guard(find);
                do {
                    if (cancelled()) {
                        return false;
                    }
                    const std::wstring name = data.cFileName;
                    if (name == L"." || name == L"..") {
                        continue;
                    }
                    // dwReserved0 holds the reparse tag when the entry is a reparse point.
                    found.push_back({name, data.dwFileAttributes, data.dwReserved0});
                } while (FindNextFileW(find, &data));
                const DWORD error = GetLastError();
                if (error != ERROR_NO_MORE_FILES) {
                    if (skipUnreadable(error)) {
                        continue;
                    }
                    return false;
                }
            }

            // Remember this directory, so the references inside it can be compared with it.
            const std::size_t self = ancestors.size();
            ancestors.push_back({item.realPath, item.parent});

            std::vector<Pending> children;
            for (const Found &child : found) {
                const std::wstring path = item.path.empty() ? child.name : item.path + L'\\' + child.name;
                const bool directoryEntry = (child.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

                // Junctions and symbolic links point to another path ("name surrogates"). Other
                // reparse points, like OneDrive files, are the real file or directory.
                const bool reference = (child.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0
                                       && IsReparseTagNameSurrogate(child.reparseTag);
                if (!reference) {
                    children.push_back({path, item.realPath + L'\\' + child.name, self,
                                        directoryEntry, directoryEntry, true, false});
                    continue;
                }

                // A reference is followed only if its target exists on the source volume.
                std::wstring target;
                if (sourceVolume.empty() || !getRealPath(getFullSourcePath(path), target)
                    || !equalsIgnoreCase(getVolume(target), sourceVolume)) {
                    inaccessible_.push_back(path);
                    continue;
                }

                // A reference to this directory or to one that contains it would be scanned forever.
                bool recursive = false;
                for (std::size_t ancestor = self; directoryEntry && !recursive && ancestor != NO_PARENT;
                     ancestor = ancestors[ancestor].parent) {
                    recursive = isSameOrInside(ancestors[ancestor].realPath, target);
                }
                if (recursive) {
                    if (onRecursion) {
                        onRecursion(path);
                    }
                    continue;
                }

                children.push_back({path, target, self, directoryEntry, directoryEntry, true, true});
            }
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                pending.push_back(std::move(*it));
            }
        }
        return true;
    }

    // Returns the absolute source path of a relative path, with the "\\?\" prefix.
    // With it, long names can be read even though they can not be written to the destination.
    std::wstring Copier::getFullSourcePath(const std::wstring &relativePath) const {
    std::wstring root;
        // Add the prefix unless it is already there. Network paths like \\server\share use \\?\UNC instead.
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
    // An empty path stays empty, so checkDirectories() can report it.
    std::wstring Copier::normalizePath(const std::wstring &path) {
        if (path.empty()) {
            return path;
        }

        std::wstring result = path;

        // The first call returns the size of the buffer, and the second one writes the absolute path.
        // If anything fails, the path is kept as it was written.
        const DWORD length = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
        if (length > 0) {
            std::wstring buffer(length, L'\0');
            const DWORD written = GetFullPathNameW(path.c_str(), length, &buffer[0], nullptr);
            if (written > 0 && written < length) {
                buffer.resize(written);
                result = buffer;
            }
        }

        // Use backslashes only.
        std::replace(result.begin(), result.end(), L'/', L'\\');
    
        
        // Remove the trailing backslashes, except the one after a drive letter ("C:\").
        while (result.size() > 1 && result.back() == L'\\' && result[result.size() - 2] != L':') {
            result.pop_back();
        }
        return result;
    }

    // Returns true if Windows accepts the name for a file or a directory.
    // The name must not be empty: rename() checks that first.
    bool Copier::isValidName(const std::wstring &name) {
        // "." and ".." mean the current and the parent directory.
        if (name == L"." || name == L"..") {
            return false;
        }

        // No control characters, and none of the characters that Windows forbids in names.
        for (const wchar_t character : name) {
            if (character < 32 || std::wcschr(L"\\/:*?\"<>|", character) != nullptr) {
                return false;
            }
        }

        // Windows does not allow a name to end with a space or a dot.
        const wchar_t last = name.back();
        if (last == L' ' || last == L'.') {
            return false;
        }

        // Device names (CON, NUL, ...) are not allowed either.
        return !isReservedName(name);
    }

    // Returns true if the text starts with the prefix (case-sensitive).
    bool Copier::startsWith(const std::wstring &text, const std::wstring &prefix) {
        return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
    }

} // namespace fullcopy
