[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$VcVars)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vc = (Resolve-Path -LiteralPath $VcVars).Path
$output = Join-Path $repo ('build\diagnostic-tests-' + [Guid]::NewGuid().ToString('N'))
$shaderRoot = Join-Path $output 'Shaders'
New-Item -ItemType Directory -Path (Join-Path $shaderRoot 'PIXL\Modules') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $shaderRoot 'LegacyModule') | Out-Null
$outside = Join-Path $output 'Outside'
New-Item -ItemType Directory -Path $outside | Out-Null
# A junction within this unique fixture tests redirected targets without touching its contents.
New-Item -ItemType Junction -Path (Join-Path $shaderRoot 'Redirected') -Target $outside | Out-Null
$impl = Get-Content -LiteralPath (Join-Path $repo 'engine\PipelineHealth.cpp') -Raw
$header = Get-Content -LiteralPath (Join-Path $repo 'engine\PipelineHealth.h') -Raw
$fileSystem = Get-Content -LiteralPath (Join-Path $repo 'engine\Utils\FileSystem.cpp') -Raw
function Extract([string]$Source, [string]$Pattern) {
    $match = [regex]::Match($Source, $Pattern)
    if (-not $match.Success) { throw "Diagnostic safety extraction changed: $Pattern" }
    return $match.Value
}
$helpers = @('IsModuleIdentifier', 'IsExpectedModulePath') | ForEach-Object {
    Extract $impl ('(?s)static bool ' + $_ + '\(.*?\r?\n\t\}')
}
$deletion = Extract $impl '(?s)bool DeleteFeatureFiles\(.*?\r?\n\t\}'
$restore = Extract $impl '(?s)bool RestoreOriginalState\(.*?\r?\n\t\t\}'
$sanitize = Extract $fileSystem '(?s)std::string SanitizeFileName\(.*?\r?\n\t\t\}'
$fileInfo = Extract $header '(?s)struct FeatureFileInfo.*?\r?\n\t\};'
$issueInfo = Extract $header '(?s)struct FeatureIssueInfo.*?\r?\n\t\};'
$testInfo = Extract $header '(?s)struct TestIniInfo.*?\r?\n\t\t\};'
$draw = Extract $impl '(?s)static void DrawFeatureIssue\([^;\r\n]*\r?\n\t\{.*?\r?\n\t\}'
if ($draw -match 'issues.erase|std::erase_if|remove_if' -or $draw -notmatch 'deletedIssue = issue.shortName') { throw 'Issue drawing must defer vector mutation.' }
$erase = Extract $impl 'std::erase_if\(s_featureIssues, \[&deletedIssue\].*?; \}\);'
$prefix = @'
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
namespace REL { struct Version {}; }
namespace logger {
template<class... T> void warn(const char*, T&&...) {}
template<class... T> void error(const char*, T&&...) {}
template<class... T> void info(const char*, T&&...) {}
template<class... T> void debug(const char*, T&&...) {}
}
namespace Util {
bool IEquals(const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) == 0; }
std::string WStringToString(const std::filesystem::path& path) { return path.string(); }
namespace PathHelpers {
std::filesystem::path root;
std::filesystem::path GetShadersPath() { return root; }
std::filesystem::path GetOverridesPath() { return root.parent_path() / "Overrides"; }
std::filesystem::path GetModuleDescriptorPath(const std::string& name) { return root / "PIXL" / "Modules" / (name + ".ini"); }
std::filesystem::path GetModuleKernelPath(const std::string& name) { return root / name; }
}
namespace FileHelpers {
struct DeletionResult { bool success; std::string deletedDescription, errorMessage; };
std::vector<std::string> requests;
DeletionResult SafeDelete(const std::string& path, const std::string&) {
    requests.push_back(path); return {true, path, {}}; // Record only. Never delete payloads.
}
}
}
using SI_Error = int;
struct CSimpleIniA {
    static inline bool failLoad = false;
    static inline std::string deletedSection, restoredVersion;
    void SetUnicode() {}
    int LoadFile(const char*) { return failLoad ? -1 : 0; }
    int SaveFile(const char*) { return 0; }
    void Delete(const char* section, const char*) { deletedSection = section; }
    void SetValue(const char*, const char*, const char* version) { restoredVersion = version; }
};
void ClearPipelineHealth() {}
void ScanForOrphanedFeatureINIs(bool) {}
'@
$test = @'
int main(int argc, char** argv) {
    assert(argc == 2);
    Util::PathHelpers::root = std::filesystem::absolute(argv[1]);
    const auto root = Util::PathHelpers::root;
    assert(SanitizeFileName("CON.txt") == "CON_.txt");
    assert(SanitizeFileName("nul.ini") == "nul_.ini");
    assert(SanitizeFileName("COM1.esp") == "COM1_.esp");
    assert(SanitizeFileName("LPT9") == "LPT9_");
    assert(SanitizeFileName("Weather Mod.esp") == "Weather Mod.esp");
    assert(SanitizeFileName(" test/invalid:*? ") == "test_invalid___");
    assert(SanitizeFileName("  ").empty());
    for (const auto* valid : {"LegacyModule", "WaterOptics", "Test_Module-2"}) assert(IsModuleIdentifier(valid));
    for (const auto* invalid : {"", ".", "..", "../Shaders", "C:\\data", "A/B", "A:B", "A ", "A."}) assert(!IsModuleIdentifier(invalid));
    assert(!IsModuleIdentifier(std::string("A\0B", 3)));
    assert(std::filesystem::path("..ini").stem() == "."); // Real malformed INI case.
    FeatureIssueInfo issue;
    issue.shortName = "LegacyModule";
    issue.fileInfo.hasINI = true;
    issue.fileInfo.iniPath = Util::PathHelpers::GetModuleDescriptorPath(issue.shortName).string();
    issue.fileInfo.hasDeployedFolder = true;
    issue.fileInfo.deployedFolderPath = Util::PathHelpers::GetModuleKernelPath(issue.shortName).string();
    assert(DeleteFeatureFiles(issue));
    assert(Util::FileHelpers::requests.size() == 2);
    Util::FileHelpers::requests.clear();
    issue.fileInfo.deployedFolderPath = root.string();
    assert(!DeleteFeatureFiles(issue) && Util::FileHelpers::requests.empty());
    FeatureIssueInfo overrideIssue;
    overrideIssue.issueType = FeatureIssueInfo::IssueType::OVERRIDE_FAILED;
    overrideIssue.shortName = "Weather Mod.esp_Global";
    overrideIssue.fileInfo.hasINI = true;
    overrideIssue.fileInfo.iniPath = (Util::PathHelpers::GetOverridesPath() / (overrideIssue.shortName + ".json")).string();
    assert(DeleteFeatureFiles(overrideIssue));
    assert(Util::FileHelpers::requests.size() == 1);
    Util::FileHelpers::requests.clear();
    overrideIssue.fileInfo.hasDeployedFolder = true;
    assert(!DeleteFeatureFiles(overrideIssue));
    overrideIssue.fileInfo.hasDeployedFolder = false;
    overrideIssue.fileInfo.iniPath = (root.parent_path() / "Outside" / "unrelated.json").string();
    assert(!DeleteFeatureFiles(overrideIssue) && Util::FileHelpers::requests.empty());
    issue.fileInfo.deployedFolderPath = (root.parent_path() / "Outside").string();
    assert(!DeleteFeatureFiles(issue) && Util::FileHelpers::requests.empty());
    issue.shortName = ".";
    assert(!DeleteFeatureFiles(issue));
    issue.shortName = "Redirected";
    issue.fileInfo.iniPath = Util::PathHelpers::GetModuleDescriptorPath(issue.shortName).string();
    issue.fileInfo.deployedFolderPath = (root / "Redirected").string();
    assert(!DeleteFeatureFiles(issue) && Util::FileHelpers::requests.empty());
    TestIniInfo record;
    record.featureName = "LegacyModule";
    record.testIniPath = Util::PathHelpers::GetModuleDescriptorPath(record.featureName).string();
    record.isNewFile = false; // Mock INI API only; no fixture file write/removal needed.
    record.originalVersion = "none";
    s_activeTestInis = {record};
    assert(RestoreOriginalState(s_activeTestInis));
    assert(CSimpleIniA::deletedSection == "PIXL Module");
    assert(s_activeTestInis.empty());
    record.originalVersion = "1-0-0";
    s_activeTestInis = {record};
    CSimpleIniA::failLoad = true;
    assert(!RestoreOriginalState(s_activeTestInis));
    assert(s_activeTestInis.size() == 1 && s_activeTestInis[0].originalVersion == "1-0-0");
    CSimpleIniA::failLoad = false;
    assert(RestoreOriginalState(s_activeTestInis));
    assert(CSimpleIniA::restoredVersion == "1-0-0");
    record.testIniPath = (root.parent_path() / "Outside" / "unrelated.ini").string();
    s_activeTestInis = {record};
    assert(!RestoreOriginalState(s_activeTestInis) && s_activeTestInis.size() == 1);
    std::vector<FeatureIssueInfo> s_featureIssues(3);
    s_featureIssues[0].shortName = "First";
    s_featureIssues[1].shortName = "Second";
    s_featureIssues[2].shortName = "Third";
    std::vector<const FeatureIssueInfo*> pointers;
    for (const auto& item : s_featureIssues) pointers.push_back(&item);
    std::string deletedIssue;
    for (const auto* item : pointers) {
        assert(!item->shortName.empty());
        if (item->shortName == "First") deletedIssue = item->shortName;
    }
'@
$source = $prefix + "`r`n" + $sanitize + "`r`n" + $fileInfo + "`r`n" + $issueInfo + "`r`n" + ($helpers -join "`r`n") + "`r`n" + $deletion + "`r`n" + $testInfo + @'

std::vector<TestIniInfo> s_activeTestInis;
std::filesystem::path GetTestStateFilePath() { return Util::PathHelpers::root / "not-created.test"; }
'@ + "`r`n" + $restore + "`r`n" + $test + "`r`n" + $erase + @'

    assert(s_featureIssues.size() == 2 && s_featureIssues[0].shortName == "Second" && s_featureIssues[1].shortName == "Third");
    std::cout << "PASS exact diagnostic target gates, no root/outside/junction deletion requests, deferred issue erase, correct version restoration and retained failed recovery\n";
}
'@
$sourcePath = Join-Path $output 'diagnostic-tests.cpp'
$source | Set-Content -LiteralPath $sourcePath -Encoding UTF8
Push-Location $output
try {
    $command = 'call "{0}" >nul && cl.exe /nologo /std:c++latest /EHsc /W4 /WX /MD /O2 "{1}" /Fe:diagnostic-tests.exe && diagnostic-tests.exe "{2}"' -f $vc, $sourcePath, $shaderRoot
    $ErrorActionPreference = 'Continue'
    & cmd.exe /d /c $command 2>&1 | Tee-Object -FilePath (Join-Path $output 'test.log')
    $result = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($result -ne 0) { throw "Diagnostic safety tests failed ($result)." }
} finally { $ErrorActionPreference = 'Stop'; Pop-Location }
Write-Host "Diagnostic test artifacts (retained, including fixture junction): $output"
