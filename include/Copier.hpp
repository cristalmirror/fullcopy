#ifndef FULLCOPY_COPIER_HPP
#define FULLCOPY_COPIER_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace fullcopy
{

// One file or directory found while scanning the source directory.
struct CopyEntry
{
    std::wstring sourcePath;      // Relative to the source root.
    std::wstring destinationPath; // Relative to the destination root (changes after a rename).
    bool isDirectory;
    bool skipped;
};

// Result of a rename attempt, so the caller can explain what went wrong.
enum class RenameResult
{
    Ok,
    Empty,
    InvalidName,
    TooLong,
    AlreadyExists,
    DestinationUnavailable
};

class Copier
{
public:
    // Windows limits (without the "\\?\" prefix):
    //  - a file path must be shorter than MAX_PATH (260),
    //  - a directory path must leave room for an 8.3 file name (248),
    //  - a single name can not be longer than 255 characters.
    static const std::size_t MAX_FILE_PATH_LENGTH      = 259;
    static const std::size_t MAX_DIRECTORY_PATH_LENGTH = 247;
    static const std::size_t MAX_NAME_LENGTH           = 255;

    Copier();

    void setDirectories(const std::wstring& sourceRoot, const std::wstring& destinationRoot);

    // Scans the source directory and all its subdirectories.
    // Returns false on any scan error or an invalid destination.
    // A failed scan discards all partial entries.
    bool load();

    // Checks that the destination path of an entry is short enough to be copied.
    // Updates sizeApproved_ and tempSize_ and returns sizeApproved_.
    bool check(std::size_t index);

    // Copies one entry: creates the directory or copies the file.
    bool copy(std::size_t index);

    // Gives a new destination name to an entry. When the entry is a directory,
    // everything inside it is moved under the new name too.
    RenameResult rename(std::size_t index, const std::wstring& newName);

    // Marks an entry (and its content, if it is a directory) as not to be copied.
    void skip(std::size_t index);

    std::size_t getEntryCount() const;
    const CopyEntry& getEntry(std::size_t index) const;
    std::wstring getDestinationName(std::size_t index) const;
    std::wstring getFullDestinationPath(std::size_t index) const;

    // Longest name the entry can have so its destination path fits (0 if none fits).
    std::size_t getMaxNameLength(std::size_t index) const;

    const std::wstring& getSourceRoot() const;
    const std::wstring& getDestinationRoot() const;
    bool isSizeApproved() const;
    std::size_t getTempSize() const;

private:
    bool scanDirectory(const std::wstring& relativePath);
    std::wstring getFullSourcePath(const std::wstring& relativePath) const;
    static std::wstring normalizePath(const std::wstring& path);
    static bool isValidName(const std::wstring& name);
    static bool startsWith(const std::wstring& text, const std::wstring& prefix);

    bool sizeApproved_;
    std::size_t tempSize_;
    std::wstring sourceRoot_;
    std::wstring destinationRoot_;
    std::vector<CopyEntry> entries_;
};

} // namespace fullcopy

#endif // FULLCOPY_COPIER_HPP
