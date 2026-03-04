#pragma once
#include <string>
#include <filesystem>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

class ProjectCompiler {
public:
    static void Start(const std::filesystem::path& projectDir);
    static bool IsCompiling();
    static bool HasFinished();
    static bool GetResult();
    static std::vector<std::string> GetLogs();
    static void Reset();

private:
    static void CompileThread(std::filesystem::path projectDir);

    static inline std::thread s_CompilerThread;
    static inline std::atomic<bool> s_IsCompiling{false};
    static inline std::atomic<bool> s_HasFinished{false};
    static inline std::atomic<bool> s_Result{false};
    
    static inline std::mutex s_LogMutex;
    static inline std::vector<std::string> s_Logs;
};