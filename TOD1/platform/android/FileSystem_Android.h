/*
 * FileSystem_Android.h
 *
 * Android file system layer replacing Windows file I/O.
 * Reads game assets from AAssetManager (APK) and external storage.
 */
#pragma once

#include <android/asset_manager.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <mutex>

// ---------------------------------------------------------------------------
// FileHandle_Android - replaces HANDLE-based file ops
// ---------------------------------------------------------------------------
class FileHandle_Android {
public:
    enum OpenMode {
        READ_ONLY,
        WRITE_ONLY,
        READ_WRITE,
    };

    FileHandle_Android();
    ~FileHandle_Android();

    // Open from AAssetManager (read-only, for bundled game data)
    bool    OpenAsset(AAssetManager* mgr, const char* path);

    // Open from external storage (read/write, for saves/configs)
    bool    OpenFile(const char* path, OpenMode mode);

    void    Close();
    bool    IsOpen() const;

    // Read/Write
    int     Read(void* buffer, int bytesToRead);
    int     Write(const void* buffer, int bytesToWrite);

    // Seek/Position
    int     Seek(int position);
    int     SeekRelative(int offset);
    int     SeekEnd(int offset);
    int     GetPosition() const;
    int     GetSize() const;

    // Utility
    bool    ReadAll(std::vector<uint8_t>& outData);

private:
    AAsset*     m_Asset;        // For APK-based assets
    FILE*       m_File;         // For external storage files
    std::string m_Path;
    bool        m_IsAsset;
};

// ---------------------------------------------------------------------------
// FileSystem_Android - replaces FileBufferImpl static methods
// ---------------------------------------------------------------------------
class FileSystem_Android {
public:
    FileSystem_Android();
    ~FileSystem_Android();

    bool    Initialize(AAssetManager* assetManager, const char* internalPath, const char* externalPath);
    void    Shutdown();

    // Path management
    void    SetGameDataPath(const char* path);
    const char* GetGameDataPath() const { return m_GameDataPath.c_str(); }
    const char* GetSavePath() const { return m_SavePath.c_str(); }
    const char* GetInternalPath() const { return m_InternalPath.c_str(); }

    // File operations
    FileHandle_Android* OpenFile(const char* path, FileHandle_Android::OpenMode mode = FileHandle_Android::READ_ONLY);
    bool    FileExists(const char* path);
    bool    DirectoryExists(const char* path);
    void    CreateDirectory(const char* path);
    bool    DeleteFile(const char* path);

    // Asset enumeration
    bool    ListAssetDirectory(const char* dir, std::vector<std::string>& outFiles);
    bool    ListExternalDirectory(const char* dir, std::vector<std::string>& outFiles);

    // Path resolution: tries asset manager first, then external storage
    std::string ResolvePath(const char* relativePath);

    // ZIP archive support (for game's .zip data files)
    bool    OpenZipArchive(const char* zipPath);

    // Storage info
    uint64_t    GetFreeSpace() const;
    bool        IsStorageFull() const;

    // Singleton
    static FileSystem_Android* GetInstance() { return s_Instance; }

private:
    AAssetManager*  m_AssetManager;
    std::string     m_InternalPath;
    std::string     m_ExternalPath;
    std::string     m_GameDataPath;
    std::string     m_SavePath;

    std::mutex      m_Mutex;
    static FileSystem_Android* s_Instance;
};
