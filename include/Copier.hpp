#ifndef FULLCOPY_COPIER_HPP
#define FULLCOPY_COPIER_HPP

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace fullcopy {
    // One file or directory found while scanning the source directory.
    struct CopyEntry {
        std::wstring sourcePath;      // Relative to the source root.
        std::wstring destinationPath; // Relative to the destination root (changes after a rename).
        bool isDirectory;
        bool skipped;
    };

    // Result of a rename attempt, so the caller can explain what went wrong.
    enum class RenameResult {
        Ok,
        Empty,
        InvalidName,
        TooLong,
        AlreadyExists,
        DestinationUnavailable
    };

    // Result of copying one entry.
    enum class CopyResult {
        Copied,       // Copied, or nothing to do because the entry is skipped.
        Failed,
        Inaccessible  // The source is a OneDrive file that can not be read now.
    };

    // Result of checking the source and destination directories before a copy.
    enum class DirectoryResult {
        Ok,
        Empty,
        SourceNotFound,
        SameDirectory,
        DestinationInsideSource,
        DestinationUnavailable
    };

    // Model: scans the source directory, checks and renames the entries, and copies them.
    class Copier {
    public:
        // Windows limits (without the "\\?\" prefix):
        //  - a file path must be shorter than MAX_PATH (260),
        //  - a directory path must leave room for an 8.3 file name (248),
        //  - a single name can not be longer than 255 characters.
        static const std::size_t MAX_FILE_PATH_LENGTH      = 259;
        static const std::size_t MAX_DIRECTORY_PATH_LENGTH = 247;
        static const std::size_t MAX_NAME_LENGTH           = 255;

        Copier();

        void setDirectories(const std::wstring &sourceRoot, const std::wstring &destinationRoot);

        // Checks the directories given to setDirectories(): both are set, the source exists,
        // and the destination is neither the source nor inside it.
        DirectoryResult checkDirectories() const;

        // Called during the scan with the relative source path of a reference that points to a
        // directory containing it. That reference is not copied.
        using RecursionCallback = std::function<void(const std::wstring &relativePath)>;

        // Scans the source directory and all its subdirectories.
        // A reference (junction or symbolic link) to the source volume is scanned like a real
        // directory or file. A reference to another volume, a reference that can not be followed
        // and a OneDrive directory that can not be read are not copied: they are listed by
        // getInaccessiblePaths(). A reference to a directory that contains it is passed to
        // onRecursion and not copied.
        // Returns false on cancellation, any other scan error, or an invalid destination.
        // A failed scan discards all partial entries.
        bool load(std::atomic_bool *cancellation = nullptr, const RecursionCallback &onRecursion = nullptr);

        // Checks that the destination path of an entry is short enough to be copied.
        // Updates sizeApproved_ and tempSize_ and returns sizeApproved_.
        bool check(std::size_t index);

        // Copies one entry without overwriting existing files. Cancellation removes
        // the partial file created by the current copy operation. Returns Inaccessible for a
        // OneDrive file that can not be read now.
        CopyResult copy(std::size_t index, std::atomic_bool *cancellation = nullptr);

        // Gives a new destination name to an entry. When the entry is a directory,
        // everything inside it is moved under the new name too.
        RenameResult rename(std::size_t index, const std::wstring &newName);

        // Marks an entry (and its content, if it is a directory) as not to be copied.
        void skip(std::size_t index);

        std::size_t getEntryCount() const;
        const CopyEntry &getEntry(std::size_t index) const;
        std::wstring getDestinationName(std::size_t index) const;
        std::wstring getFullDestinationPath(std::size_t index) const;

        // Longest name the entry can have so its destination path fits (0 if none fits).
        std::size_t getMaxNameLength(std::size_t index) const;

        const std::wstring &getSourceRoot() const;
        const std::wstring &getDestinationRoot() const;
        bool isSizeApproved() const;
        std::size_t getTempSize() const;

        // Relative source paths that the last scan could not read (see load()).
        const std::vector<std::wstring> &getInaccessiblePaths() const;

    private:
        std::size_t getPathLengthLimit(std::size_t index) const;
        bool scanDirectory(const std::wstring &relativePath, std::atomic_bool *cancellation,
                           const RecursionCallback &onRecursion);
        std::wstring getFullSourcePath(const std::wstring &relativePath) const;
        static std::wstring normalizePath(const std::wstring &path);
        static bool isValidName(const std::wstring &name);
        static bool startsWith(const std::wstring &text, const std::wstring &prefix);

        bool sizeApproved_;
        std::size_t tempSize_;
        std::wstring sourceRoot_;
        std::wstring destinationRoot_;
        std::vector<CopyEntry> entries_;
        std::vector<std::wstring> inaccessible_;
    };
} // namespace fullcopy

#endif // FULLCOPY_COPIER_HPP
